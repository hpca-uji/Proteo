# Proteo Config-Gen

Minimal web UI to create and edit Proteo **JSON** configuration files for the Phases_Feature schema (used by SAM and `read_json.c`).

## Requirements

- Python 3.8+
- Flask (only external dependency)

## Install and run

```bash
cd Config-Gen
pip install -r requirements.txt
python app.py
```

Open [http://127.0.0.1:5000](http://127.0.0.1:5000).

Optional environment variables:

- `CONFIG_GEN_HOST` (default `127.0.0.1`)
- `CONFIG_GEN_PORT` (default `5000`)

## Usage

1. On open, the editor starts with a **minimal blank** configuration (one phase, one stage, one group).
2. **New** — loads a template (from `Codes/test.json` if present, otherwise the same minimal default).
3. Edit **General**, **Phases** (with nested stages), and **Groups** sections.
4. **Validate** — checks structure, stage rules, and group rules; applies field cleanup (sanitization).
5. **Download JSON** — validates/sanitizes, then saves locally (download is never blocked; errors are advisory).
6. **Open file…** — load a JSON file from disk; the config file name updates to match.

Set **Config file name** to the output filename used when downloading (default `config.json`). It updates automatically when you open a local file.

Counts sync automatically:

- `Total_Phases` = number of phases
- `Total_Resizes` = number of groups − 1
- Each phase's `Total_Stages` = number of stages in that phase

Phases and groups are collapsible: click the summary to expand/collapse. When multiple phases or groups exist, only the first is expanded by default.

## Stage types (`Stage_Type`)

Matches `enum compute_methods` in SAM:

| Value | Type |
|-------|------|
| 0 | Compute: Monte Carlo (pi) |
| 1 | Compute: matrix-vector |
| 2 | Comm: point-to-point |
| 3 | Comm: IPoint-to-point (async P2P) |
| 4 | Comm: MPI_Wait |
| 5 | Comm: Bcast |
| 6 | Comm: Allgather |
| 7 | Comm: Reduce |
| 8 | Comm: Allreduce |
| 9 | I/O: write |
| 10 | I/O: read |

## Stage validation rules

On **Validate** or **Download**, the server sanitizes and checks:

- **Compute (0–1):** `Stage_Time > 0` and `Granularity > 0` required. `Stage_Bytes` is not used (forced to 0). `Stage_Identifier` and `Stage_Involved_Procs` are removed.
- **Comm (2–8):** `Stage_Bytes > 0` or `Stage_Time > 0` required. `Stage_Involved_Procs` is removed. If both bytes and time are set, a warning is shown (bytes are used). `Granularity > 0` required when using bytes without time cap (except Wait).
- **IPoint (3) / Wait (4):** `Stage_Identifier > 0` required. Every IPoint must have a matching Wait with the same identifier; duplicate Wait identifiers are errors.
- **I/O (9–10):** `Stage_Bytes > 0` or `Stage_Time > 0`; `Stage_Involved_Procs` required (`>= 0`; `0` = all processes). `Granularity > 0` required when using bytes without time cap.
- **Stage_Time_Capped:** if set, must be `0` (operation count) or `1` (time cap).

## Group validation rules

- **Iters / Procs:** positive integers (`> 0`).
- **FactorS:** float, `>= 0`.
- **Redistribution strategy:** exactly one value in the array (empty field = clear/`[0]`).
- **Spawn strategy:** comma-separated indices allowed; strategies **2 (Single)** or **4 (Multiple)** cannot be combined with **5 (Parallel)**.
- **RMA redistribution** (methods 2 or 3): strategy must be **1 (Pthread)** or **3 (Wait targets)**.

Strategy index **0** means clear/empty and is not listed in the UI legend. Leave the strategy field empty for the default.

## Spawn and redistribution strategies

Available indices are shown in the UI legend (excluding 0):

**Spawn:** 1=Pthread, 2=Single, 3=Intercomm, 4=Multiple, 5=Parallel

**Redistribution:** 1=Pthread, 2=Wait sources, 3=Wait targets

## Launch a generated config

```bash
bash Exec/singleRunCostum.sh Codes/my_config.json 0 1 0
```

## Notes

- This tool outputs **JSON only** (not INI). For combinatorial INI generation, use `Exec/multipleRuns.sh` / `read_multiple.py`.

## Files

| File | Purpose |
|------|---------|
| `app.py` | Flask server |
| `schema.py` | Field metadata, enums, defaults |
| `validator.py` | Structural + semantic validation |
| `stage_rules.py` | Stage sanitization and semantic rules |
| `restrictions.py` | Group cross-field rules |
| `static/app.js` | Form UI logic |
| `templates/index.html` | Page layout |
