# SAM

SAM is Proteo’s application / emulator: it loads a run configuration, executes
compute / communication / I/O phases, triggers process-group resizes through
MaM, and writes timing results.

## Running

End users are **not** expected to launch SAM’s binary directly. For how to
build, configure, and execute Proteo jobs, consult the repository **Manual**
(`Manual.pdf` / `Manual_Latex/`). This Doxygen site is source and API
orientation only.

## Source entry point

Developers reading the code should start at @ref Main.c.

## Modules

- **Main** — process-group lifecycle, MaM data registration, and the user
  redistribution callback (@ref Main.c, @ref Main_datatypes.h,
  @ref configuration.h).
- **Emulation** — phases and stages (compute, communication, I/O).
- **IOcodes** — configuration parsers and results writers. Vendored `ini` /
  `cJSON` sources are excluded from this documentation set.

## Relation to MaM

SAM links against the MaM library. MaM’s public API is documented in the
separate MaM Doxygen site (`Codes/MaM/docs/html/`), not duplicated here.
