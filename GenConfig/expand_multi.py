"""Combinatorial expansion of complex JSON configs (colon-separated variants)."""

import copy
import json
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

from stage_rules import sanitize_config
from validator import validate_for_expansion

DIFFERENT_VALUE_DELIMITER = ":"
LIST_VALUE_DELIMITER = ","

STRATEGY_KEYS = frozenset({"Spawn_Strategy", "Redistribution_Strategy"})

STRUCTURAL_PATHS = frozenset({
    "general.Total_Phases",
    "general.Total_Resizes",
})

STRUCTURAL_SUFFIXES = (".Total_Stages",)

FACTORS_KEY = "FactorS"
PROCS_KEY = "Procs"
SDR_KEY = "SDR"
ADR_KEY = "ADR"


@dataclass
class VariantAxis:
    path: Tuple[Union[str, int], ...]
    path_str: str
    alternatives: List[Any]
    is_strategy: bool
    is_adr_percentage: bool = False


def convert_to_number(text: str) -> Any:
    text = text.strip()
    try:
        value = float(text)
    except ValueError:
        return text
    if value.is_integer():
        try:
            return int(text)
        except ValueError:
            pass
    return value


def is_variant_string(value: Any) -> bool:
    return isinstance(value, str) and DIFFERENT_VALUE_DELIMITER in value


def parse_variant_segment(segment: str, is_strategy: bool) -> Any:
    segment = segment.strip()
    if is_strategy:
        parts = [p.strip() for p in segment.split(LIST_VALUE_DELIMITER) if p.strip() != ""]
        if not parts:
            return [0]
        return [convert_to_number(p) for p in parts]
    return convert_to_number(segment)


def parse_variant_string(value: str, is_strategy: bool = False) -> List[Any]:
    segments = [s for s in value.split(DIFFERENT_VALUE_DELIMITER)]
    if any(s.strip() == "" for s in segments):
        raise ValueError(f"empty segment in variant string '{value}'")
    return [parse_variant_segment(s, is_strategy) for s in segments]


def path_to_str(path: Tuple[Union[str, int], ...]) -> str:
    parts = []
    for item in path:
        if isinstance(item, int):
            parts.append(f"[{item}]")
        else:
            if parts:
                parts.append(".")
            parts.append(str(item))
    return "".join(parts)


def get_at_path(obj: Any, path: Tuple[Union[str, int], ...]) -> Any:
    current = obj
    for key in path:
        current = current[key]
    return current


def set_at_path(obj: Any, path: Tuple[Union[str, int], ...], value: Any) -> None:
    current = obj
    for key in path[:-1]:
        current = current[key]
    current[path[-1]] = value


def _is_structural_path(path_str: str, key: str) -> bool:
    if path_str in STRUCTURAL_PATHS:
        return True
    if key == FACTORS_KEY:
        return False
    return any(path_str.endswith(suffix) for suffix in STRUCTURAL_SUFFIXES)


def _walk_variants(
    obj: Any,
    path: Tuple[Union[str, int], ...],
    axes: List[VariantAxis],
    warnings: List[str],
) -> None:
    if isinstance(obj, dict):
        for key, value in obj.items():
            child_path = path + (key,)
            path_str = path_to_str(child_path)
            if key == FACTORS_KEY:
                if is_variant_string(value):
                    continue
            if _is_structural_path(path_str, key):
                if is_variant_string(value):
                    warnings.append(f"{path_str}: structural field cannot use variant syntax")
                continue
            if key in STRATEGY_KEYS:
                if is_variant_string(value):
                    alts = parse_variant_string(value, is_strategy=True)
                    axes.append(VariantAxis(child_path, path_str, alts, True))
                continue
            if is_variant_string(value):
                is_strategy = False
                is_adr = key == ADR_KEY
                alts = parse_variant_string(value, is_strategy=is_strategy)
                adr_pct = False
                # Complex-file rule: ADR variants are interpreted as percentages when in [0, 100].
                if is_adr and all(isinstance(a, (int, float)) and 0 <= a <= 100 for a in alts):
                    adr_pct = True
                axes.append(VariantAxis(child_path, path_str, alts, is_strategy, adr_pct))
            elif isinstance(value, (dict, list)):
                _walk_variants(value, child_path, axes, warnings)
    elif isinstance(obj, list):
        for index, item in enumerate(obj):
            _walk_variants(item, path + (index,), axes, warnings)


def collect_variants(config: dict) -> Tuple[List[VariantAxis], List[str]]:
    warnings: List[str] = []
    axes: List[VariantAxis] = []
    if not isinstance(config, dict):
        return axes, ["Root value must be a JSON object"]
    _walk_variants(config, (), axes, warnings)
    _apply_procs_factors_rules(config, axes, warnings)
    # Deterministic axis order (also ensures general.SDR typically precedes general.ADR).
    axes.sort(key=lambda a: a.path_str)
    return axes, warnings


def _apply_procs_factors_rules(config: dict, axes: List[VariantAxis], warnings: List[str]) -> None:
    """FactorS is not an independent axis when Procs varies in the same group.

    Additionally, if Procs uses variants, FactorS must also use variants with the same count.
    """
    procs_axes = {}
    factors_axes = {}
    for axis in axes:
        if len(axis.path) >= 3 and axis.path[-1] == PROCS_KEY and axis.path[-3] == "groups":
            gi = axis.path[-2]
            if isinstance(gi, int):
                procs_axes[gi] = axis
        if len(axis.path) >= 3 and axis.path[-1] == FACTORS_KEY and axis.path[-3] == "groups":
            gi = axis.path[-2]
            if isinstance(gi, int):
                factors_axes[gi] = axis

    for gi, procs_axis in procs_axes.items():
        factors_axis = factors_axes.get(gi)
        if factors_axis:
            # FactorS is parallel to Procs; do not treat as independent axis.
            axes.remove(factors_axis)
        # If FactorS is present only in the template (and not in axes), it is still applied
        # during Procs assignment; hard requirements are enforced in validate_multi_syntax().


def correct_adr(sdr: float, adr_percentage: float, general: dict) -> None:
    if adr_percentage != 0:
        general[ADR_KEY] = sdr * (adr_percentage / 100.0)
    general[SDR_KEY] = sdr * ((100.0 - adr_percentage) / 100.0)


def _resolve_current_sdr(config: dict) -> float:
    """Resolve SDR after materialization/coercion, not from the template."""
    sdr = config.get("general", {}).get(SDR_KEY, 0)
    if is_variant_string(sdr):
        # If called before SDR is assigned (unexpected), fall back to first alternative.
        return float(parse_variant_string(sdr)[0])
    return float(sdr)


def passes_procs_filter(config: dict) -> bool:
    groups = config.get("groups")
    if not isinstance(groups, list) or len(groups) < 2:
        return True
    for i in range(len(groups) - 1):
        procs_a = groups[i].get(PROCS_KEY)
        procs_b = groups[i + 1].get(PROCS_KEY)
        if procs_a == procs_b:
            return False
    return True


def _assign_concrete_value(config: dict, axis: VariantAxis, index: int, template: dict, axes: List[VariantAxis]) -> None:
    value = copy.deepcopy(axis.alternatives[index])
    set_at_path(config, axis.path, value)

    if axis.path[-1] == PROCS_KEY and len(axis.path) >= 3 and axis.path[-3] == "groups":
        gi = axis.path[-2]
        if isinstance(gi, int):
            factors_path = ("groups", gi, FACTORS_KEY)
            factors_raw = get_at_path(template, factors_path)
            if is_variant_string(factors_raw):
                factors_alts = parse_variant_string(factors_raw)
                if index < len(factors_alts):
                    set_at_path(config, factors_path, factors_alts[index])

    # ADR percentage handling is applied in materialize_config() after all assignments,
    # so ordering between SDR and ADR axes cannot corrupt results.


def _generate_assignments(axes: List[VariantAxis]) -> List[List[int]]:
    if not axes:
        return [[]]
    lengths = [len(a.alternatives) for a in axes]
    indices = [0] * len(axes)
    assignments = []
    while True:
        assignments.append(list(indices))
        pos = len(axes) - 1
        while pos >= 0:
            indices[pos] += 1
            if indices[pos] < lengths[pos]:
                break
            indices[pos] = 0
            pos -= 1
        if pos < 0:
            break
    return assignments


def materialize_config(template: dict, axes: List[VariantAxis], assignment: List[int]) -> dict:
    concrete = copy.deepcopy(template)
    for axis, idx in zip(axes, assignment):
        _assign_concrete_value(concrete, axis, idx, template, axes)

    general = concrete.get("general", {})
    adr = general.get(ADR_KEY)
    # Complex-file behavior: if any variants exist, ADR is always treated as percentage in [0,100].
    if axes and not is_variant_string(adr) and isinstance(adr, (int, float)) and not isinstance(adr, bool):
        correct_adr(_resolve_current_sdr(concrete), float(adr), general)

    _normalize_strategies(concrete)
    return concrete


def _normalize_strategies(obj: Any) -> None:
    if isinstance(obj, dict):
        for key, value in list(obj.items()):
            if key in STRATEGY_KEYS and isinstance(value, str):
                if is_variant_string(value):
                    obj[key] = parse_variant_string(value, is_strategy=True)[0]
                elif LIST_VALUE_DELIMITER in value:
                    obj[key] = parse_variant_segment(value, True)
                else:
                    obj[key] = parse_variant_segment(value, True)
            else:
                _normalize_strategies(value)
    elif isinstance(obj, list):
        for item in obj:
            _normalize_strategies(item)


def validate_multi_syntax(config: dict) -> Dict[str, Any]:
    errors: List[str] = []
    warnings: List[str] = []

    if not isinstance(config, dict):
        return {"valid": False, "errors": ["Root value must be a JSON object"], "warnings": []}

    general = config.get("general")
    phases = config.get("phases")
    groups = config.get("groups")

    if not isinstance(general, dict):
        errors.append("general: must be an object")
    if not isinstance(phases, list):
        errors.append("phases: must be an array")
    if not isinstance(groups, list):
        errors.append("groups: must be an array")

    if isinstance(general, dict) and isinstance(phases, list):
        total_phases = general.get("Total_Phases")
        if isinstance(total_phases, int) and len(phases) != total_phases:
            errors.append(f"phases: expected {total_phases} entries, found {len(phases)}")

    if isinstance(general, dict) and isinstance(groups, list):
        total_resizes = general.get("Total_Resizes")
        if isinstance(total_resizes, int) and len(groups) != total_resizes + 1:
            errors.append(f"groups: expected {total_resizes + 1} entries, found {len(groups)}")

    try:
        axes, walk_warnings = collect_variants(config)
        warnings.extend(walk_warnings)
    except ValueError as exc:
        errors.append(str(exc))
        axes = []

    # Complex-only FactorS rule: when Procs varies in a group, FactorS must provide a value
    # for each Procs alternative (parallel assignment), and FactorS does not add combinations.
    if axes and isinstance(groups, list):
        for gi, group in enumerate(groups):
            if not isinstance(group, dict):
                continue
            procs_raw = group.get(PROCS_KEY)
            if not is_variant_string(procs_raw):
                continue
            factors_raw = group.get(FACTORS_KEY)
            if not is_variant_string(factors_raw):
                errors.append(f"groups[{gi}].FactorS: required as variant when groups[{gi}].Procs uses variants")
                continue
            try:
                procs_alts = parse_variant_string(procs_raw, is_strategy=False)
                factors_alts = parse_variant_string(factors_raw, is_strategy=False)
            except ValueError as exc:
                errors.append(f"groups[{gi}]: {exc}")
                continue
            if len(procs_alts) != len(factors_alts):
                errors.append(
                    f"groups[{gi}].FactorS: must have same number of alternatives as groups[{gi}].Procs "
                    f"({len(factors_alts)} vs {len(procs_alts)})"
                )

    # Complex-only ADR rule: ADR is always a percentage in [0, 100].
    # We treat "complex" as "has any variant axis".
    if axes and isinstance(general, dict):
        adr_val = general.get(ADR_KEY)
        if is_variant_string(adr_val):
            try:
                adr_alts = parse_variant_string(adr_val, is_strategy=False)
            except ValueError as exc:
                errors.append(f"general.ADR: {exc}")
                adr_alts = []
            for i, alt in enumerate(adr_alts):
                if not isinstance(alt, (int, float)) or isinstance(alt, bool):
                    errors.append(f"general.ADR[{i}]: must be a number in [0, 100]")
                elif alt < 0 or alt > 100:
                    errors.append(f"general.ADR[{i}]: must be in [0, 100]")
        else:
            # Allow strings (from UI) as long as they coerce cleanly to a number.
            if isinstance(adr_val, str) and adr_val.strip() != "":
                coerced = convert_to_number(adr_val)
                adr_val = coerced
            if not isinstance(adr_val, (int, float)) or isinstance(adr_val, bool):
                errors.append("general.ADR: must be a number in [0, 100] for complex configs")
            elif adr_val < 0 or adr_val > 100:
                errors.append("general.ADR: must be in [0, 100] for complex configs")

        # Complex-only SDR rule (implicit): SDR is required as a base for ADR%.
        sdr_val = general.get(SDR_KEY)
        if sdr_val is None or (isinstance(sdr_val, str) and sdr_val.strip() == ""):
            errors.append("general.SDR: required for complex configs (ADR is a percentage)")

    for axis in axes:
        if not axis.alternatives:
            errors.append(f"{axis.path_str}: variant must have at least one alternative")

    return {
        "valid": len(errors) == 0,
        "errors": errors,
        "warnings": warnings,
        "variant_count": len(axes),
    }


def expand_config(
    config: dict,
    *,
    validate_outputs: bool = True,
    apply_sanitize: bool = True,
) -> Tuple[List[dict], Dict[str, int]]:
    axes, _ = collect_variants(config)
    stats = {
        "total_combinations": 0,
        "skipped_procs_filter": 0,
        "skipped_invalid": 0,
        "written": 0,
    }

    if not axes:
        concrete = materialize_config(config, [], [])
        if not passes_procs_filter(concrete):
            return [], {**stats, "skipped_procs_filter": 1}
        if apply_sanitize:
            concrete = sanitize_config(concrete)
        if validate_outputs:
            result = validate_for_expansion(concrete)
            if not result["valid"]:
                return [], {**stats, "skipped_invalid": 1}
        return [concrete], {**stats, "total_combinations": 1, "written": 1}

    outputs: List[dict] = []
    assignments = _generate_assignments(axes)
    stats["total_combinations"] = len(assignments)

    for assignment in assignments:
        concrete = materialize_config(config, axes, assignment)
        if not passes_procs_filter(concrete):
            stats["skipped_procs_filter"] += 1
            continue
        if apply_sanitize:
            concrete = sanitize_config(concrete)
        if validate_outputs:
            result = validate_for_expansion(concrete)
            if not result["valid"]:
                stats["skipped_invalid"] += 1
                continue
        outputs.append(concrete)
        stats["written"] += 1

    return outputs, stats


def count_expansions(config: dict) -> int:
    axes, _ = collect_variants(config)
    if not axes:
        concrete = materialize_config(config, [], [])
        if not passes_procs_filter(concrete):
            return 0
        return 1
    count = 1
    for axis in axes:
        count *= len(axis.alternatives)
    return count


def preview_expansions(config: dict, validate_outputs: bool = True) -> Dict[str, Any]:
    syntax = validate_multi_syntax(config)
    axes, _ = collect_variants(config)
    theoretical = count_expansions(config)
    invalid_reasons: Dict[str, int] = {}
    if validate_outputs:
        assignments = _generate_assignments(axes)
        for assignment in assignments:
            concrete = materialize_config(config, axes, assignment)
            if not passes_procs_filter(concrete):
                continue
            concrete = sanitize_config(concrete)
            res = validate_for_expansion(concrete)
            if not res["valid"]:
                key = res["errors"][0] if res.get("errors") else "Unknown validation failure"
                invalid_reasons[key] = invalid_reasons.get(key, 0) + 1

    _, stats = expand_config(config, validate_outputs=validate_outputs)
    return {
        "syntax_valid": syntax["valid"],
        "syntax_errors": syntax["errors"],
        "syntax_warnings": syntax["warnings"],
        "variant_axes": len(axes),
        "theoretical_combinations": theoretical,
        "valid_outputs": stats["written"],
        "skipped_procs_filter": stats["skipped_procs_filter"],
        "skipped_invalid": stats["skipped_invalid"],
        "invalid_reasons": invalid_reasons,
    }


def expand_from_file(
    input_path: Path,
    output_prefix: str,
    output_dir: Optional[Path] = None,
) -> int:
    with open(input_path, encoding="utf-8") as handle:
        config = json.load(handle)

    if output_dir is None:
        directory_name = f"Desglosed-{date.today()}"
        output_dir = Path.cwd() / directory_name
    output_dir.mkdir(parents=True, exist_ok=True)

    outputs, stats = expand_config(config, validate_outputs=True, apply_sanitize=True)
    for index, concrete in enumerate(outputs):
        out_path = output_dir / f"{output_prefix}{index}.json"
        with open(out_path, "w", encoding="utf-8") as handle:
            json.dump(concrete, handle, indent=2)
            handle.write("\n")

    print(f"Wrote {len(outputs)} JSON file(s) to {output_dir}")
    if stats["skipped_procs_filter"]:
        print(f"Skipped {stats['skipped_procs_filter']} combination(s) (duplicate consecutive Procs)")
    if stats["skipped_invalid"]:
        print(f"Skipped {stats['skipped_invalid']} combination(s) (validation failed)")
    return len(outputs)


def config_has_variants(config: dict) -> bool:
    axes, _ = collect_variants(config)
    return len(axes) > 0
