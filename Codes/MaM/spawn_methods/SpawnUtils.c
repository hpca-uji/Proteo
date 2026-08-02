#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "SpawnUtils.h"

/**
 * @file SpawnUtils.c
 * @brief Implementation of basic spawn and command helpers.
 */

/**
 * @brief Spawn one ::Spawn_set and return the parent–children intercommunicator.
 *
 * Creates processes according to the configuration prepared in ProcessDist.
 * On success, updates MaM main structures on the new intercommunicator.
 *
 * @param[in]  i_spawn_set Spawn set (command, count, mapping).
 * @param[in]  i_comm      Intracommunicator of spawners (sources / parents).
 * @param[out] o_child     Receives the intercommunicator to the children.
 */
void mam_spawn(Spawn_set i_spawn_set, MPI_Comm i_comm, MPI_Comm *o_child) {
  int rootBcast = MPI_PROC_NULL;
  int comm_size;
  MPI_Comm_size(i_comm, &comm_size);
  if (mall->myId == mall->root || comm_size == 1) rootBcast = MPI_ROOT;

  int spawn_err = MPI_Comm_spawn(i_spawn_set.cmd, MPI_ARGV_NULL, i_spawn_set.spawn_qty, i_spawn_set.mapping, MAM_ROOT, i_comm, o_child, MPI_ERRCODES_IGNORE);

  if (spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", i_spawn_set.spawn_qty);
    MPI_Abort(MPI_COMM_WORLD, -1);
  }
  MAM_Comm_main_structures(*o_child, rootBcast);
}


/**
 * @brief Resolve the executable/wrapper command used for all spawn sets.
 * @return Pointer to the command string (Valgrind/Extrae script or @c mall->name_exec).
 */
char *get_spawn_cmd(void) {
  char *cmd_aux;
  switch (mall_conf->external_usage) {
    case MAM_USE_VALGRIND:
      cmd_aux = MAM_VALGRIND_SCRIPT;
      break;
    case MAM_USE_EXTRAE:
      cmd_aux = MAM_EXTRAE_SCRIPT;
      break;
    default:
      cmd_aux = mall->name_exec;
      break;
  }

  return cmd_aux;
}
