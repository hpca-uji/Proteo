#ifndef MAM_SPAWN_PROCESS_DIST_H
#define MAM_SPAWN_PROCESS_DIST_H

#include "Spawn_DataStructure.h"

void processes_dist(Spawn_data *spawn_data);
extern void remove_dist(Spawn_data spawn_data);
void set_hostfile_name(char **file_name, int *n, int jid, int index);
int read_hostfile_procs(char *file_name, int *qty);

#endif
