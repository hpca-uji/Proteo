#ifndef MAM_SPAWN_UTILS_H
#define MAM_SPAWN_UTILS_H

/**
 * @file SpawnUtils.h
 * @brief Thin wrappers around MPI_Comm_spawn and spawn command selection.
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Spawn one ::Spawn_set and return the parent–children intercommunicator.
 *
 * @param[in]  i_spawn_set Spawn set (command, count, mapping).
 * @param[in]  i_comm      Intracommunicator of spawners (sources / parents).
 * @param[out] o_child     Receives the intercommunicator to the children.
 */
void mam_spawn(Spawn_set i_spawn_set, MPI_Comm i_comm, MPI_Comm *o_child);

/**
 * @brief Resolve the executable/wrapper command used for all spawn sets.
 * @return Pointer to the command string (Valgrind/Extrae script or @c mall->name_exec).
 */
char *get_spawn_cmd(void);

#endif
