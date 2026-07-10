"""Structural validation for Proteo JSON configs (mirrors read_json.c)."""

from restrictions import validate_group_restrictions
from stage_rules import validate_stage_semantics

GENERAL_REQUIRED = (
    "Total_Resizes",
    "Total_Phases",
    "SDR",
    "ADR",
    "Datasize",
    "Rigid",
    "Capture_Method",
)

STAGE_REQUIRED = ("Stage_Type", "Stage_Bytes", "Stage_Time")

GROUP_REQUIRED = (
    "Iters",
    "Procs",
    "FactorS",
    "Dist",
    "Redistribution_Method",
    "Redistribution_Strategy",
    "Spawn_Method",
    "Spawn_Strategy",
)


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _validate_int_array(value, path):
    errors = []
    if not isinstance(value, list) or len(value) == 0:
        errors.append(f"{path}: must be a non-empty array of integers")
        return errors
    for i, item in enumerate(value):
        if not isinstance(item, int) or isinstance(item, bool):
            errors.append(f"{path}[{i}]: must be an integer")
    return errors


def validate_config(config):
    """Return list of human-readable error strings. Empty if valid."""
    errors = []

    if not isinstance(config, dict):
        return ["Root value must be a JSON object"]

    general = config.get("general")
    phases = config.get("phases")
    groups = config.get("groups")

    if not isinstance(general, dict):
        errors.append("general: must be an object")
        general = {}
    if not isinstance(phases, list):
        errors.append("phases: must be an array")
        phases = []
    if not isinstance(groups, list):
        errors.append("groups: must be an array")
        groups = []

    for key in GENERAL_REQUIRED:
        if key not in general:
            errors.append(f"general.{key}: missing required field")
        elif key in ("Rigid", "Capture_Method", "Total_Resizes", "Total_Phases", "Datasize"):
            if not isinstance(general[key], int) or isinstance(general[key], bool):
                errors.append(f"general.{key}: must be an integer")
        elif not _is_number(general[key]):
            errors.append(f"general.{key}: must be a number")

    total_phases = general.get("Total_Phases")
    total_resizes = general.get("Total_Resizes")

    if isinstance(total_phases, int) and len(phases) != total_phases:
        errors.append(
            f"phases: expected {total_phases} entries, found {len(phases)}"
        )

    if isinstance(total_resizes, int) and len(groups) != total_resizes + 1:
        errors.append(
            f"groups: expected {total_resizes + 1} entries, found {len(groups)}"
        )

    for pi, phase in enumerate(phases):
        prefix = f"phases[{pi}]"
        if not isinstance(phase, dict):
            errors.append(f"{prefix}: must be an object")
            continue
        for key in ("Total_Iters", "Total_Stages"):
            if key not in phase:
                errors.append(f"{prefix}.{key}: missing required field")
            elif not isinstance(phase[key], int) or isinstance(phase[key], bool):
                errors.append(f"{prefix}.{key}: must be an integer")

        stages = phase.get("stages")
        if not isinstance(stages, list):
            errors.append(f"{prefix}.stages: must be an array")
            continue

        qty_stages = phase.get("Total_Stages")
        if isinstance(qty_stages, int) and len(stages) != qty_stages:
            errors.append(
                f"{prefix}.stages: expected {qty_stages} entries, found {len(stages)}"
            )

        for si, stage in enumerate(stages):
            sp = f"{prefix}.stages[{si}]"
            if not isinstance(stage, dict):
                errors.append(f"{sp}: must be an object")
                continue
            for key in STAGE_REQUIRED:
                if key not in stage:
                    errors.append(f"{sp}.{key}: missing required field")
                elif key == "Stage_Type" or key == "Stage_Bytes":
                    if not isinstance(stage[key], int) or isinstance(stage[key], bool):
                        errors.append(f"{sp}.{key}: must be an integer")
                elif not _is_number(stage[key]):
                    errors.append(f"{sp}.{key}: must be a number")

            for opt in ("Granularity", "Stage_Time_Capped", "Stage_Identifier", "Stage_Involved_Procs"):
                if opt in stage and (not isinstance(stage[opt], int) or isinstance(stage[opt], bool)):
                    errors.append(f"{sp}.{opt}: must be an integer")

    for gi, group in enumerate(groups):
        gp = f"groups[{gi}]"
        if not isinstance(group, dict):
            errors.append(f"{gp}: must be an object")
            continue
        for key in GROUP_REQUIRED:
            if key not in group:
                errors.append(f"{gp}.{key}: missing required field")

        if "Dist" in group and group["Dist"] not in ("compact", "spread"):
            errors.append(f"{gp}.Dist: must be 'compact' or 'spread'")

        for key in ("Iters", "Procs", "Redistribution_Method", "Spawn_Method"):
            if key in group and (not isinstance(group[key], int) or isinstance(group[key], bool)):
                errors.append(f"{gp}.{key}: must be an integer")

        if "FactorS" in group and not _is_number(group["FactorS"]):
            errors.append(f"{gp}.FactorS: must be a number")

        if "Redistribution_Strategy" in group:
            errors.extend(_validate_int_array(group["Redistribution_Strategy"], f"{gp}.Redistribution_Strategy"))
        if "Spawn_Strategy" in group:
            errors.extend(_validate_int_array(group["Spawn_Strategy"], f"{gp}.Spawn_Strategy"))

    # Group cross-field rules: see restrictions.py (v2)
    errors.extend(validate_group_restrictions(config))

    return errors


def validate_config_full(config):
    """Return structural errors, semantic warnings, and combined validity."""
    errors = validate_config(config)
    semantic_errors, warnings = validate_stage_semantics(config)
    errors.extend(semantic_errors)
    return {
        "errors": errors,
        "warnings": warnings,
        "valid": len(errors) == 0,
    }
