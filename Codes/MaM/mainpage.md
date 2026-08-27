# MaM

MaM is Proteo’s MPI malleability library: it grows or shrinks a process group
(spawn) and redistributes application data across the new layout.

## Public include

Applications include @ref MAM.h. That umbrella exposes the application-facing
API from @ref MAM_Constants.h, @ref MAM_Manager.h, @ref MAM_Configuration.h,
and @ref MAM_Times_retrieve.h.

```c
#include "MAM.h"
```

## Typical call flow

- @ref MAM_Init — start MaM on sources, or complete a children’s join
- Configure spawn/redistribution (@ref MAM_Set_configuration / key setters)
- @ref MAM_Data_add — register buffers for redistribution
- Loop @ref MAM_Checkpoint until the public state reaches completion
- Optional user phase / @ref MAM_Resume_redistribution
- @ref MAM_Get_Reconf_Info — role and size snapshot for the application
- @ref MAM_Retrieve_times — durations from the last reconfiguration
- @ref MAM_Finalize — tear down and wake Merge zombies if any

## Roles

- **Sources** — ranks in the pre-reconfiguration group (also called parents).
- **Children** — ranks newly spawned for this reconfiguration.
- **Targets** — ranks that continue after reconfiguration.
  - Baseline: targets are the children only.
  - Merge: targets are the children plus reused sources.

## Where to browse

Start with the public headers above. The `spawn_methods/` and
`distribution_methods/` trees are internal implementation (documented in this
site, but not part of the @ref MAM.h umbrella).

## Further reading

For build, run, and benchmark context see the repository `Manual.pdf` (and
`Manual_Latex/`) and the top-level `README.md`. This site is API documentation
only.
