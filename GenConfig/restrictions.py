"""Cross-field group restrictions and sanitization."""

from typing import List

from schema import (
    RMA_ALLOWED_RED_STRATEGIES,
    RMA_REDISTRIBUTION_METHODS,
    SPAWN_EXCLUSIVE_WITH_PARALLEL,
    SPAWN_PARALLEL,
)


def _is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def sanitize_groups(config: dict) -> None:
    """Normalize group strategy arrays in-place (minimal cleanup)."""
    groups = config.get("groups")
    if not isinstance(groups, list):
        return

    for group in groups:
        if not isinstance(group, dict):
            continue
        for key in ("Redistribution_Strategy", "Spawn_Strategy"):
            value = group.get(key)
            if value is None or (isinstance(value, list) and len(value) == 0):
                group[key] = [0]


def validate_group_restrictions(config: dict) -> List[str]:
    """Return list of cross-field group restriction errors."""
    errors: List[str] = []

    if not isinstance(config, dict):
        return errors

    groups = config.get("groups")
    if not isinstance(groups, list):
        return errors

    for gi, group in enumerate(groups):
        gp = f"groups[{gi}]"
        if not isinstance(group, dict):
            continue

        for key in ("Iters", "Procs"):
            if key not in group:
                continue
            value = group[key]
            if not isinstance(value, int) or isinstance(value, bool):
                errors.append(f"{gp}.{key}: must be an integer")
            elif value < 1:
                errors.append(f"{gp}.{key}: must be a positive integer")

        if "FactorS" in group:
            value = group["FactorS"]
            if not _is_number(value):
                errors.append(f"{gp}.FactorS: must be a number")
            elif value < 0:
                errors.append(f"{gp}.FactorS: must be >= 0")

        red_strategy = group.get("Redistribution_Strategy")
        if isinstance(red_strategy, list):
            if len(red_strategy) != 1:
                errors.append(f"{gp}.Redistribution_Strategy: must contain exactly one value")
            elif len(red_strategy) == 1:
                red_method = group.get("Redistribution_Method")
                strategy_value = red_strategy[0]
                if (
                    isinstance(red_method, int)
                    and not isinstance(red_method, bool)
                    and red_method in RMA_REDISTRIBUTION_METHODS
                    and isinstance(strategy_value, int)
                    and not isinstance(strategy_value, bool)
                    and strategy_value not in RMA_ALLOWED_RED_STRATEGIES
                ):
                    errors.append(
                        f"{gp}.Redistribution_Strategy: RMA methods require "
                        f"strategy 1 (Pthread) or 3 (Wait targets)"
                    )

        spawn_strategy = group.get("Spawn_Strategy")
        if isinstance(spawn_strategy, list) and len(spawn_strategy) > 0:
            if SPAWN_PARALLEL in spawn_strategy and any(
                s in spawn_strategy for s in SPAWN_EXCLUSIVE_WITH_PARALLEL
            ):
                errors.append(
                    f"{gp}.Spawn_Strategy: strategies 2 (Single) and 4 (Multiple) "
                    f"cannot be combined with 5 (Parallel)"
                )

    return errors
