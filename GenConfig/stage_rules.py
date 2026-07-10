"""Stage semantic rules: sanitization and cross-field validation."""

import copy
from typing import Any, Dict, List, Tuple

from schema import STAGE_COMM, STAGE_COMPUTE, STAGE_IO, STAGE_IPOINT, STAGE_WAIT
from restrictions import sanitize_groups

COMPUTE_STRIP_FIELDS = ("Stage_Identifier", "Stage_Involved_Procs")
COMM_STRIP_FIELDS = ("Stage_Involved_Procs",)


def _is_positive_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0


def _is_positive_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def _sanitize_stage(stage: dict) -> None:
    stage_type = stage.get("Stage_Type")
    if stage_type in STAGE_COMPUTE:
        stage["Stage_Bytes"] = 0
        for key in COMPUTE_STRIP_FIELDS:
            stage.pop(key, None)
    elif stage_type in STAGE_COMM:
        for key in COMM_STRIP_FIELDS:
            stage.pop(key, None)


def sanitize_config(config: dict) -> dict:
    """Return a deep copy with stage fields removed per semantic rules."""
    result = copy.deepcopy(config)
    if not isinstance(result, dict):
        return result

    phases = result.get("phases")
    if not isinstance(phases, list):
        return result

    for phase in phases:
        if not isinstance(phase, dict):
            continue
        stages = phase.get("stages")
        if not isinstance(stages, list):
            continue
        for stage in stages:
            if isinstance(stage, dict):
                _sanitize_stage(stage)

    sanitize_groups(result)

    return result


def _has_granularity_at_least_one(stage: dict) -> bool:
    value = stage.get("Granularity")
    return isinstance(value, int) and not isinstance(value, bool) and value >= 1


def _is_time_capped(stage: dict) -> bool:
    value = stage.get("Stage_Time_Capped", 0)
    return isinstance(value, int) and not isinstance(value, bool) and value == 1


def _has_positive_bytes(stage: dict) -> bool:
    value = stage.get("Stage_Bytes", 0)
    return _is_positive_int(value)


def _compute_granularity_required(stage_type: int) -> bool:
    return stage_type in STAGE_COMPUTE


def _non_compute_granularity_required(stage: dict, stage_type: int) -> bool:
    """Granularity rules for comm/I/O stages (not compute)."""
    if stage_type in STAGE_COMPUTE:
        return False
    if not _has_positive_bytes(stage):
        return True
    if _is_time_capped(stage):
        return True
    return False


def _check_positive_values(stage: dict, path: str, errors: List[str]) -> None:
    for key in ("Stage_Bytes", "Stage_Time", "Granularity", "Stage_Time_Capped",
                "Stage_Identifier", "Stage_Involved_Procs"):
        if key not in stage:
            continue
        value = stage[key]
        if key == "Stage_Time":
            if isinstance(value, (int, float)) and not isinstance(value, bool) and value < 0:
                errors.append(f"{path}.{key}: must be non-negative")
        elif key == "Stage_Time_Capped":
            if not isinstance(value, int) or isinstance(value, bool) or value not in (0, 1):
                errors.append(f"{path}.{key}: must be 0 or 1")
        elif isinstance(value, int) and not isinstance(value, bool) and value < 0:
            errors.append(f"{path}.{key}: must be non-negative")
        elif isinstance(value, (int, float)) and not isinstance(value, bool) and value < 0:
            errors.append(f"{path}.{key}: must be non-negative")


def _validate_single_stage(
    stage: dict,
    path: str,
    errors: List[str],
    warnings: List[str],
    *,
    wait_paired: bool = False,
) -> None:
    if not isinstance(stage, dict):
        errors.append(f"{path}: must be an object")
        return

    stage_type = stage.get("Stage_Type")
    if not isinstance(stage_type, int) or isinstance(stage_type, bool):
        return

    _check_positive_values(stage, path, errors)

    stage_time = stage.get("Stage_Time", 0)
    has_bytes = _has_positive_bytes(stage)
    has_time = _is_positive_number(stage_time)

    if _compute_granularity_required(stage_type):
        if not _has_granularity_at_least_one(stage):
            errors.append(f"{path}: compute stage requires Granularity >= 1")
    elif _non_compute_granularity_required(stage, stage_type):
        if not _has_granularity_at_least_one(stage):
            errors.append(f"{path}: Granularity >= 1 is required")

    if stage_type not in STAGE_COMPUTE and _is_time_capped(stage) and not has_time:
        errors.append(f"{path}: time-capped stage requires Stage_Time > 0")

    if stage_type in STAGE_COMPUTE:
        if not has_time:
            errors.append(f"{path}: compute stage requires Stage_Time > 0")
        if has_bytes:
            errors.append(f"{path}: compute stages cannot use Stage_Bytes")

    elif stage_type in STAGE_COMM:
        if not wait_paired and not has_bytes and not has_time:
            errors.append(f"{path}: comm stage requires Stage_Bytes > 0 or Stage_Time > 0")
        if has_bytes and has_time:
            warnings.append(
                f"{path}: both Stage_Bytes and Stage_Time are set; bytes will be used, time ignored"
            )
        if stage_type in (STAGE_IPOINT, STAGE_WAIT):
            stage_id = stage.get("Stage_Identifier")
            if stage_id is None:
                errors.append(f"{path}: requires Stage_Identifier > 0")
            elif not _is_positive_int(stage_id):
                errors.append(f"{path}.Stage_Identifier: must be a positive integer")

    elif stage_type in STAGE_IO:
        if not has_bytes and not has_time:
            errors.append(f"{path}: I/O stage requires Stage_Bytes > 0 or Stage_Time > 0")
        involved = stage.get("Stage_Involved_Procs")
        if involved is None:
            errors.append(f"{path}: I/O stage requires Stage_Involved_Procs (use 0 for all processes)")
        elif not isinstance(involved, int) or isinstance(involved, bool) or involved < 0:
            errors.append(f"{path}.Stage_Involved_Procs: must be an integer >= 0 (0 = all processes)")


def _validate_ipoint_wait_pairing(
    stages: list,
    phase_path: str,
    errors: List[str],
    warnings: List[str],
) -> Dict[int, set]:
    """Validate IPoint/Wait pairing; return wait indices paired with an IPoint."""
    ipoint_ids: Dict[int, List[int]] = {}
    wait_ids: Dict[int, List[int]] = {}
    paired_wait_indices: set = set()

    for si, stage in enumerate(stages):
        if not isinstance(stage, dict):
            continue
        stage_type = stage.get("Stage_Type")
        stage_id = stage.get("Stage_Identifier")
        if not isinstance(stage_id, int) or isinstance(stage_id, bool) or stage_id <= 0:
            continue

        if stage_type == STAGE_IPOINT:
            ipoint_ids.setdefault(stage_id, []).append(si)
        elif stage_type == STAGE_WAIT:
            wait_ids.setdefault(stage_id, []).append(si)

    for stage_id, indices in wait_ids.items():
        if len(indices) > 1:
            stages_str = ", ".join(str(i) for i in indices)
            errors.append(
                f"{phase_path}: multiple Wait stages share Stage_Identifier {stage_id} "
                f"(stages {stages_str})"
            )

    for stage_id, ipoint_indices in ipoint_ids.items():
        if stage_id not in wait_ids:
            stages_str = ", ".join(str(i) for i in ipoint_indices)
            errors.append(
                f"{phase_path}: IPoint stage(s) {stages_str} with Stage_Identifier {stage_id} "
                f"have no matching Wait stage"
            )
        else:
            paired_wait_indices.add(wait_ids[stage_id][0])

    for stage_id, wait_indices in wait_ids.items():
        if stage_id not in ipoint_ids:
            stages_str = ", ".join(str(i) for i in wait_indices)
            warnings.append(
                f"{phase_path}: Wait stage(s) {stages_str} with Stage_Identifier {stage_id} "
                f"have no matching IPoint stage"
            )

    return paired_wait_indices


def validate_stage_semantics(config: dict) -> Tuple[List[str], List[str]]:
    """Return (errors, warnings) for stage semantic rules."""
    errors: List[str] = []
    warnings: List[str] = []

    if not isinstance(config, dict):
        return errors, warnings

    phases = config.get("phases")
    if not isinstance(phases, list):
        return errors, warnings

    for pi, phase in enumerate(phases):
        prefix = f"phases[{pi}]"
        if not isinstance(phase, dict):
            continue

        for key in ("Total_Iters", "Total_Stages"):
            if key in phase:
                value = phase[key]
                if isinstance(value, int) and not isinstance(value, bool) and value < 1:
                    errors.append(f"{prefix}.{key}: must be a positive integer")

        stages = phase.get("stages")
        if not isinstance(stages, list):
            continue

        paired_waits = _validate_ipoint_wait_pairing(stages, prefix, errors, warnings)

        for si, stage in enumerate(stages):
            wait_paired = (
                isinstance(stage, dict)
                and stage.get("Stage_Type") == STAGE_WAIT
                and si in paired_waits
            )
            _validate_single_stage(
                stage,
                f"{prefix}.stages[{si}]",
                errors,
                warnings,
                wait_paired=wait_paired,
            )

    return errors, warnings
