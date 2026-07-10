"""Proteo JSON config schema metadata and default template."""

# Group cross-field rules live in restrictions.py

STAGE_TYPES = {
    0: "Compute: Monte Carlo (pi)",
    1: "Compute: matrix-vector",
    2: "Comm: point-to-point",
    3: "Comm: IPoint-to-point (async P2P)",
    4: "Comm: MPI_Wait",
    5: "Comm: Bcast",
    6: "Comm: Allgather",
    7: "Comm: Reduce",
    8: "Comm: Allreduce",
    9: "I/O: write",
    10: "I/O: read",
}

STAGE_COMPUTE = {0, 1}
STAGE_COMM = {2, 3, 4, 5, 6, 7, 8}
STAGE_IO = {9, 10}
STAGE_IPOINT = 3
STAGE_WAIT = 4

STAGE_TYPE_HINTS = {
    0: "Compute: requires Stage_Time > 0 and Granularity > 0; Stage_Bytes not used.",
    1: "Compute: requires Stage_Time > 0 and Granularity > 0; Stage_Bytes not used.",
    2: "Comm: requires Stage_Bytes > 0 or Stage_Time > 0. Granularity required when using bytes without time cap.",
    3: "IPoint: requires Stage_Identifier > 0 and a matching Wait stage. Granularity required when using bytes without time cap.",
    4: "Wait: requires Stage_Identifier > 0 matching an IPoint stage.",
    5: "Comm: requires Stage_Bytes > 0 or Stage_Time > 0. Granularity required when using bytes without time cap.",
    6: "Comm: requires Stage_Bytes > 0 or Stage_Time > 0. Granularity required when using bytes without time cap.",
    7: "Comm: requires Stage_Bytes > 0 or Stage_Time > 0. Granularity required when using bytes without time cap.",
    8: "Comm: requires Stage_Bytes > 0 or Stage_Time > 0. Granularity required when using bytes without time cap.",
    9: "I/O write: requires Stage_Bytes > 0 or Stage_Time > 0; Stage_Involved_Procs required (0 = all processes). Granularity required when using bytes without time cap.",
    10: "I/O read: requires Stage_Bytes > 0 or Stage_Time > 0; Stage_Involved_Procs required (0 = all processes). Granularity required when using bytes without time cap.",
}

STAGE_TIME_CAPPED_OPTIONS = {
    0: "Operation count (0)",
    1: "Time cap (1)",
}

SPAWN_STRATEGIES = {
    0: "Clear (empty)",
    1: "Pthread (async)",
    2: "Single",
    3: "Intercomm",
    4: "Multiple",
    5: "Parallel",
}

REDISTRIBUTION_STRATEGIES = {
    0: "Clear (empty)",
    1: "Pthread (async)",
    2: "Wait sources",
    3: "Wait targets",
}

SPAWN_STRATEGY_OPTIONS = {k: v for k, v in SPAWN_STRATEGIES.items() if k != 0}
REDISTRIBUTION_STRATEGY_OPTIONS = {k: v for k, v in REDISTRIBUTION_STRATEGIES.items() if k != 0}

RMA_REDISTRIBUTION_METHODS = {2, 3}
RMA_ALLOWED_RED_STRATEGIES = {1, 3}
SPAWN_PARALLEL = 5
SPAWN_EXCLUSIVE_WITH_PARALLEL = {2, 4}

SPAWN_METHODS = {
    0: "Baseline",
    1: "Merge",
}

REDISTRIBUTION_METHODS = {
    0: "Baseline / All-to-all (parents)",
    1: "Point to point",
    2: "RMA Lock",
    3: "RMA LockAll",
}

DIST_OPTIONS = ("compact", "spread")

RIGID_OPTIONS = {
    0: "No — no MPI_Barrier between iterations",
    1: "Yes — MPI_Barrier between iterations (precise timing)",
}

CAPTURE_METHOD_OPTIONS = {
    0: "Max",
    1: "Mean",
    2: "Median",
}

GENERAL_FIELDS = [
    ("Total_Resizes", "Number of resizes during execution. Groups count = this + 1."),
    ("Total_Phases", "Number of phases (must match phases array length)."),
    ("SDR", "Total bytes redistributed synchronously on each resize."),
    ("ADR", "Total bytes redistributed asynchronously on each resize."),
    ("Datasize", "Size of each redistributed data element (bytes)."),
    ("Rigid", "If 1, MPI_Barrier is used between iterations for precise time recording; if 0, no barriers between iterations."),
    ("Capture_Method", "How stage times are captured in results (0=max, 1=mean, 2=median)."),
]

STAGE_OPTIONAL_FIELDS = [
    ("Granularity", "Problem size; mandatory for compute. Required for other stages when using bytes without time cap (except Wait)."),
    ("Stage_Time_Capped", "0 = operation count, 1 = time cap."),
    ("Stage_Identifier", "Optional stage identifier for MPI request reuse."),
    ("Stage_Involved_Procs", "I/O only: processes involved (0 = all processes participate)."),
]

GROUP_FIELDS = [
    ("Iters", "Iterations to run in this process group before resize or end."),
    ("Procs", "Number of MPI processes in this group."),
    ("FactorS", "Scalability factor for compute stages (float, >= 0)."),
    ("Dist", "Physical process mapping: compact or spread."),
    ("Redistribution_Method", "MaM data redistribution method (0-3)."),
    ("Redistribution_Strategy", "Single strategy index (empty = clear); see legend below."),
    ("Spawn_Method", "Process spawn method: 0=Baseline, 1=Merge."),
    ("Spawn_Strategy", "Comma-separated indices; 2/4 cannot combine with 5; see legend."),
]


def default_stage():
    return {
        "Stage_Type": 0,
        "Stage_Bytes": 0,
        "Stage_Time": 0.0,
    }


def default_phase():
    return {
        "Total_Iters": 1,
        "Total_Stages": 1,
        "stages": [default_stage()],
    }


def default_group():
    return {
        "Iters": 1,
        "Procs": 2,
        "FactorS": 1.0,
        "Dist": "compact",
        "Redistribution_Method": 0,
        "Redistribution_Strategy": [0],
        "Spawn_Method": 0,
        "Spawn_Strategy": [0],
    }


def default_config():
    return {
        "general": {
            "Total_Resizes": 0,
            "Total_Phases": 1,
            "SDR": 0.0,
            "ADR": 0.0,
            "Datasize": 1,
            "Rigid": 0,
            "Capture_Method": 0,
        },
        "phases": [default_phase()],
        "groups": [default_group()],
    }
