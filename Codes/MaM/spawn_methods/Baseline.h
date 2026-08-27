#ifndef MAM_SPAWN_BASELINE_H
#define MAM_SPAWN_BASELINE_H

/**
 * @file Baseline.h
 * @brief Baseline spawn method: create all required children via MPI_Comm_spawn strategies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "Spawn_DataStructure.h"

/**
 * @brief Run the Baseline spawn path for sources (parents) or children.
 *
 * Sources call the parents path; newly created ranks (children) call the
 * children path. Dispatches Single / Multiple / Parallel strategies as configured.
 *
 * @param[in]     i_spawn_data Spawn configuration.
 * @param[in,out] io_child     Parents: receives intercomm to children.
 *                             Children: used as parents intercomm on entry
 *                             (@c MPI_Comm_get_parent result passed through).
 * @return @c MAM_I_SPAWN_COMPLETED.
 */
int baseline(Spawn_data i_spawn_data, MPI_Comm *io_child);
#endif
