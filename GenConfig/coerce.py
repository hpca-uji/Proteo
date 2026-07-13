"""Type coercion helpers for configs loaded/edited as strings.

Multi mode intentionally stores scalar fields as strings so users can enter
variant syntax like "10:20". For expansion/validation we need to coerce
single-value strings (no ':') back to numbers of the correct type.
"""

from __future__ import annotations

from typing import Any, Optional


def _is_variant_string(value: Any) -> bool:
    return isinstance(value, str) and ":" in value


def _to_number(value: str) -> Any:
    text = value.strip()
    try:
        f = float(text)
    except ValueError:
        return value
    if f.is_integer():
        try:
            return int(text)
        except ValueError:
            return f
    return f


def coerce_scalar(value: Any, *, kind: str) -> Any:
    """Coerce a scalar field if it's a single-value string.

    - If the value contains ':' we leave it as-is (variant syntax).
    - kind in {'int','float','number'} controls target type.
    """

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
    # number
    return num


def coerce_config_scalars_inplace(config: Any) -> None:
    """In-place coercion for known numeric scalar fields.

    Safe to call repeatedly; variant strings are preserved.
    """

    if not isinstance(config, dict):
        return

    general = config.get("general")
    if isinstance(general, dict):
        for k in ("Total_Resizes", "Total_Phases", "Datasize", "Rigid", "Capture_Method"):
            if k in general:
                general[k] = coerce_scalar(general[k], kind="int")
        for k in ("SDR", "ADR"):
            if k in general:
                general[k] = coerce_scalar(general[k], kind="float")

    phases = config.get("phases")
    if isinstance(phases, list):
        for phase in phases:
            if not isinstance(phase, dict):
                continue
            for k in ("Total_Iters", "Total_Stages"):
                if k in phase:
                    phase[k] = coerce_scalar(phase[k], kind="int")
            stages = phase.get("stages")
            if not isinstance(stages, list):
                continue
            for stage in stages:
                if not isinstance(stage, dict):
                    continue
                for k in ("Stage_Type", "Stage_Bytes"):
                    if k in stage:
                        stage[k] = coerce_scalar(stage[k], kind="int")
                if "Stage_Time" in stage:
                    stage["Stage_Time"] = coerce_scalar(stage["Stage_Time"], kind="float")
                for k in ("Granularity", "Stage_Time_Capped", "Stage_Identifier", "Stage_Involved_Procs"):
                    if k in stage:
                        stage[k] = coerce_scalar(stage[k], kind="int")

    groups = config.get("groups")
    if isinstance(groups, list):
        for group in groups:
            if not isinstance(group, dict):
                continue
            for k in ("Iters", "Procs", "Redistribution_Method", "Spawn_Method"):
                if k in group:
                    group[k] = coerce_scalar(group[k], kind="int")
            if "FactorS" in group:
                group["FactorS"] = coerce_scalar(group["FactorS"], kind="float")

