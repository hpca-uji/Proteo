"""Combinatorial expansion for complex JSON configs (Exec-side, no GenConfig dependency).

Supports the same colon variant syntax as the legacy INI expander:
- ':' separates alternative values (Cartesian product)
- ',' is only meaningful inside strategy fields (values kept together)

Complex JSON rules implemented here (per project requirements):
- ADR is always a percentage in [0, 100] and applied to the chosen SDR alternative.
- When a group's Procs uses variants, FactorS must also be a variant with the same count.
  FactorS is parallel-assigned by index and does not add combinations.
- Skip combinations where consecutive groups have the same Procs.

This module intentionally focuses on expansion + basic normalization/coercion.
Validation of deeper semantics is handled elsewhere (e.g. GenConfig server-side).
"""

from __future__ import annotations

import copy
import json
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple, Union

DIFFERENT_VALUE_DELIMITER = ":"
LIST_VALUE_DELIMITER = ","

STRATEGY_KEYS = frozenset({"Spawn_Strategy", "Redistribution_Strategy"})

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


def _is_variant_string(value: Any) -> bool:
    return isinstance(value, str) and DIFFERENT_VALUE_DELIMITER in value


def _to_number(text: str) -> Any:
    raw = text.strip()
    try:
        f = float(raw)
    except ValueError:
        return text
    if f.is_integer():
        try:
            return int(raw)
        except ValueError:
            return f
    return f


def _parse_variant_segment(segment: str, is_strategy: bool) -> Any:
    segment = segment.strip()
    if is_strategy:
        parts = [p.strip() for p in segment.split(LIST_VALUE_DELIMITER) if p.strip() != ""]
        if not parts:
            return [0]
        return [int(_to_number(p)) if isinstance(_to_number(p), int) else _to_number(p) for p in parts]
    return _to_number(segment)


def parse_variant_string(value: str, *, is_strategy: bool = False) -> List[Any]:
    segments = [s for s in value.split(DIFFERENT_VALUE_DELIMITER)]
    if any(s.strip() == "" for s in segments):
        raise ValueError(f"empty segment in variant string '{value}'")
    return [_parse_variant_segment(s, is_strategy) for s in segments]


def path_to_str(path: Tuple[Union[str, int], ...]) -> str:
    parts: List[str] = []
    for item in path:
        if isinstance(item, int):
            parts.append(f"[{item}]")
        else:
            if parts:
                parts.append(".")
            parts.append(str(item))
    return "".join(parts)


def get_at_path(obj: Any, path: Tuple[Union[str, int], ...]) -> Any:
    cur = obj
    for k in path:
        cur = cur[k]
    return cur


def set_at_path(obj: Any, path: Tuple[Union[str, int], ...], value: Any) -> None:
    cur = obj
    for k in path[:-1]:
        cur = cur[k]
    cur[path[-1]] = value


def correct_adr(sdr: float, adr_percentage: float, general: dict) -> None:
    if adr_percentage != 0:
        general[ADR_KEY] = sdr * (adr_percentage / 100.0)
    general[SDR_KEY] = sdr * ((100.0 - adr_percentage) / 100.0)


def _resolve_current_sdr(config: dict) -> float:
    sdr = config.get("general", {}).get(SDR_KEY, 0)
    if _is_variant_string(sdr):
        return float(parse_variant_string(sdr)[0])
    return float(sdr)


def _normalize_strategies(obj: Any) -> None:
    if isinstance(obj, dict):
        for key, value in list(obj.items()):
            if key in STRATEGY_KEYS:
                if isinstance(value, list):
                    continue
                if value is None:
                    obj[key] = [0]
                    continue
                if isinstance(value, str):
                    trimmed = value.strip()
                    if trimmed == "":
                        obj[key] = [0]
                        continue
                    if _is_variant_string(trimmed):
                        # shouldn't happen post-materialize, but be resilient
                        obj[key] = parse_variant_string(trimmed, is_strategy=True)[0]
                        continue
                    # comma list
                    obj[key] = [int(p.strip() or 0) for p in trimmed.split(LIST_VALUE_DELIMITER)]
                    continue
            _normalize_strategies(value)
    elif isinstance(obj, list):
        for item in obj:
            _normalize_strategies(item)


def _coerce_scalar(value: Any, *, kind: str) -> Any:
    if not isinstance(value, str) or _is_variant_string(value):
        return value
    num = _to_number(value)
    if kind == "int":
        if isinstance(num, int) and not isinstance(num, bool):
            return num
        if isinstance(num, float) and not isinstance(num, bool):
            return int(num)
        return value
    if kind == "float":
        if isinstance(num, (int, float)) and not isinstance(num, bool):
            return float(num)
        return value
    return num


def coerce_config_scalars_inplace(config: Any) -> None:
    if not isinstance(config, dict):
        return
    general = config.get("general")
    if isinstance(general, dict):
        for k in ("Total_Resizes", "Total_Phases", "Datasize", "Rigid", "Capture_Method"):
            if k in general:
                general[k] = _coerce_scalar(general[k], kind="int")
        for k in ("SDR", "ADR"):
            if k in general:
                general[k] = _coerce_scalar(general[k], kind="float")
    phases = config.get("phases")
    if isinstance(phases, list):
        for phase in phases:
            if not isinstance(phase, dict):
                continue
            for k in ("Total_Iters", "Total_Stages"):
                if k in phase:
                    phase[k] = _coerce_scalar(phase[k], kind="int")
            stages = phase.get("stages")
            if isinstance(stages, list):
                for stage in stages:
                    if not isinstance(stage, dict):
                        continue
                    for k in ("Stage_Type", "Stage_Bytes"):
                        if k in stage:
                            stage[k] = _coerce_scalar(stage[k], kind="int")
                    if "Stage_Time" in stage:
                        stage["Stage_Time"] = _coerce_scalar(stage["Stage_Time"], kind="float")
                    for k in ("Granularity", "Stage_Time_Capped", "Stage_Identifier", "Stage_Involved_Procs"):
                        if k in stage:
                            stage[k] = _coerce_scalar(stage[k], kind="int")
    groups = config.get("groups")
    if isinstance(groups, list):
        for group in groups:
            if not isinstance(group, dict):
                continue
            for k in ("Iters", "Procs", "Redistribution_Method", "Spawn_Method"):
                if k in group:
                    group[k] = _coerce_scalar(group[k], kind="int")
            if "FactorS" in group:
                group["FactorS"] = _coerce_scalar(group["FactorS"], kind="float")


def collect_variants(config: dict) -> Tuple[List[VariantAxis], List[str]]:
    warnings: List[str] = []
    axes: List[VariantAxis] = []

    def walk(obj: Any, path: Tuple[Union[str, int], ...]) -> None:
        if isinstance(obj, dict):
            for key, value in obj.items():
                child = path + (key,)
                path_str = path_to_str(child)

                if key in STRATEGY_KEYS and _is_variant_string(value):
                    axes.append(VariantAxis(child, path_str, parse_variant_string(value, is_strategy=True), True))
                    continue

                if _is_variant_string(value):
                    is_adr = key == ADR_KEY
                    alts = parse_variant_string(value, is_strategy=False)
                    adr_pct = False
                    if is_adr and all(isinstance(a, (int, float)) and 0 <= a <= 100 for a in alts):
                        adr_pct = True
                    axes.append(VariantAxis(child, path_str, alts, False, adr_pct))
                    continue

                if isinstance(value, (dict, list)):
                    walk(value, child)
        elif isinstance(obj, list):
            for i, item in enumerate(obj):
                walk(item, path + (i,))

    if not isinstance(config, dict):
        return [], ["Root value must be a JSON object"]

    walk(config, ())

    # FactorS is parallel to Procs when Procs varies; remove FactorS axis and warn on mismatch.
    procs_axes: Dict[int, VariantAxis] = {}
    factors_axes: Dict[int, VariantAxis] = {}
    for axis in list(axes):
        if len(axis.path) >= 3 and axis.path[-1] == PROCS_KEY and axis.path[-3] == "groups" and isinstance(axis.path[-2], int):
            procs_axes[axis.path[-2]] = axis
        if len(axis.path) >= 3 and axis.path[-1] == FACTORS_KEY and axis.path[-3] == "groups" and isinstance(axis.path[-2], int):
            factors_axes[axis.path[-2]] = axis

    for gi, p_axis in procs_axes.items():
        f_axis = factors_axes.get(gi)
        if not f_axis:
            warnings.append(f"groups[{gi}].FactorS: required as variant when groups[{gi}].Procs uses variants")
            continue
        if len(f_axis.alternatives) != len(p_axis.alternatives):
            warnings.append(
                f"groups[{gi}].FactorS: variant count must match groups[{gi}].Procs "
                f"({len(f_axis.alternatives)} vs {len(p_axis.alternatives)})"
            )
        if f_axis in axes:
            axes.remove(f_axis)

    axes.sort(key=lambda a: a.path_str)
    return axes, warnings


def passes_procs_filter(config: dict) -> bool:
    groups = config.get("groups")
    if not isinstance(groups, list) or len(groups) < 2:
        return True
    for i in range(len(groups) - 1):
        if groups[i].get(PROCS_KEY) == groups[i + 1].get(PROCS_KEY):
            return False
    return True


def _assign_concrete_value(config: dict, axis: VariantAxis, index: int, template: dict) -> None:
    value = copy.deepcopy(axis.alternatives[index])
    set_at_path(config, axis.path, value)

    if axis.path[-1] == PROCS_KEY and len(axis.path) >= 3 and axis.path[-3] == "groups":
        gi = axis.path[-2]
        if isinstance(gi, int):
            factors_path = ("groups", gi, FACTORS_KEY)
            factors_raw = get_at_path(template, factors_path)
            if _is_variant_string(factors_raw):
                factors_alts = parse_variant_string(factors_raw, is_strategy=False)
                if index < len(factors_alts):
                    set_at_path(config, factors_path, factors_alts[index])

    if axis.is_adr_percentage and axis.path[-1] == ADR_KEY:
        # ADR% is applied after all assignments in materialize_config()
        # to avoid dependence on SDR/ADR axis order.
        return


def _generate_assignments(axes: List[VariantAxis]) -> List[List[int]]:
    if not axes:
        return [[]]
    lengths = [len(a.alternatives) for a in axes]
    idx = [0] * len(axes)
    out: List[List[int]] = []
    while True:
        out.append(list(idx))
        pos = len(axes) - 1
        while pos >= 0:
            idx[pos] += 1
            if idx[pos] < lengths[pos]:
                break
            idx[pos] = 0
            pos -= 1
        if pos < 0:
            break
    return out


def validate_complex_rules(config: dict, axes: List[VariantAxis]) -> List[str]:
    """Exec-side strict checks for complex rules (fail fast)."""
    errors: List[str] = []
    if not axes:
        return errors
    general = config.get("general", {})
    adr = general.get(ADR_KEY)
    if _is_variant_string(adr):
        alts = parse_variant_string(adr, is_strategy=False)
        for i, a in enumerate(alts):
            if not isinstance(a, (int, float)) or isinstance(a, bool) or a < 0 or a > 100:
                errors.append(f"general.ADR[{i}]: must be a number in [0, 100]")
    else:
        if isinstance(adr, str) and adr.strip() != "":
            adr = _to_number(adr)
        if not isinstance(adr, (int, float)) or isinstance(adr, bool) or adr < 0 or adr > 100:
            errors.append("general.ADR: must be a number in [0, 100] for complex configs")

    groups = config.get("groups", [])
    if isinstance(groups, list):
        for gi, g in enumerate(groups):
            if not isinstance(g, dict):
                continue
            if _is_variant_string(g.get(PROCS_KEY)):
                if not _is_variant_string(g.get(FACTORS_KEY)):
                    errors.append(f"groups[{gi}].FactorS: required as variant when groups[{gi}].Procs uses variants")
                else:
                    p = parse_variant_string(g[PROCS_KEY], is_strategy=False)
                    f = parse_variant_string(g[FACTORS_KEY], is_strategy=False)
                    if len(p) != len(f):
                        errors.append(
                            f"groups[{gi}].FactorS: must have same number of alternatives as groups[{gi}].Procs "
                            f"({len(f)} vs {len(p)})"
                        )
    return errors


def materialize_config(template: dict, axes: List[VariantAxis], assignment: List[int]) -> dict:
    concrete = copy.deepcopy(template)
    for axis, idx in zip(axes, assignment):
        _assign_concrete_value(concrete, axis, idx, template)
    # Coerce scalar strings to numbers and normalize strategy arrays.
    coerce_config_scalars_inplace(concrete)
    _normalize_strategies(concrete)
    # Complex ADR%: ADR is always a percentage in [0,100] for complex configs.
    # Apply once after all assignments so axis ordering cannot corrupt SDR/ADR.
    if axes:
        general = concrete.get("general", {})
        adr = general.get(ADR_KEY)
        if isinstance(adr, (int, float)) and not isinstance(adr, bool):
            correct_adr(_resolve_current_sdr(concrete), float(adr), general)
    return concrete


def expand_config(config: dict) -> Tuple[List[dict], Dict[str, int]]:
    axes, _ = collect_variants(config)
    errors = validate_complex_rules(config, axes)
    if errors:
        raise ValueError("; ".join(errors[:5]) + ("" if len(errors) <= 5 else " ..."))

    stats = {"total_combinations": 0, "skipped_procs_filter": 0, "written": 0}
    if not axes:
        concrete = materialize_config(config, [], [])
        if not passes_procs_filter(concrete):
            return [], {**stats, "skipped_procs_filter": 1, "total_combinations": 1}
        return [concrete], {**stats, "written": 1, "total_combinations": 1}

    outputs: List[dict] = []
    assignments = _generate_assignments(axes)
    stats["total_combinations"] = len(assignments)
    for assignment in assignments:
        concrete = materialize_config(config, axes, assignment)
        if not passes_procs_filter(concrete):
            stats["skipped_procs_filter"] += 1
            continue
        outputs.append(concrete)
        stats["written"] += 1
    return outputs, stats


def expand_from_file(input_path: Path, output_prefix: str, output_dir: Optional[Path] = None) -> int:
    with open(input_path, encoding="utf-8") as handle:
        config = json.load(handle)

    if output_dir is None:
        output_dir = Path.cwd() / f"Desglosed-{date.today()}"
    output_dir.mkdir(parents=True, exist_ok=True)

    outputs, stats = expand_config(config)
    for i, concrete in enumerate(outputs):
        out_path = output_dir / f"{output_prefix}{i}.json"
        with open(out_path, "w", encoding="utf-8") as handle:
            json.dump(concrete, handle, indent=2)
            handle.write("\n")

    print(f"Wrote {len(outputs)} JSON file(s) to {output_dir}")
    if stats["skipped_procs_filter"]:
        print(f"Skipped {stats['skipped_procs_filter']} combination(s) (duplicate consecutive Procs)")
    return len(outputs)

