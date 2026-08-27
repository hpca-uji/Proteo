#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "PortService.h"
#include "Spawn_state.h"
#include "Strategy_Single.h"

/**
 * @file Strategy_Single.c
 * @brief Implementation of the Single-root spawn strategy.
 */

/**
 * @brief Parents side of Single: root receives children's port; all sources connect.
 *
 * Root receives the port name over the spawn intercomm, signals
 * @c MAM_I_SPAWN_SINGLE_COMPLETED (and wakes async waiters), then all sources
 * @c MPI_Comm_connect on @c spawn_data.comm to obtain a shared parent–children
 * intercommunicator.
 *
 * @param[in]     i_spawn_data Spawn configuration (async flag for state updates).
 * @param[in,out] io_child     Root: spawn intercomm then new parent–children intercomm.
 *                             Non-root: receives the connected intercomm.
 */
void single_strat_parents(Spawn_data i_spawn_data, MPI_Comm *io_child) {
  char *port_name;
  MPI_Comm newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single PA started", mall->myId, mall->numP); fflush(stdout);
  #endif

  if (mall->myId == mall->root) {
    port_name = (char *)malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_SINGLE, *io_child, MPI_STATUS_IGNORE);

    set_spawn_state(MAM_I_SPAWN_SINGLE_COMPLETED, i_spawn_data.spawn_is_async); // Indicate other processes to join root to end spawn procedure
    wakeup_completion();
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, i_spawn_data.comm, &newintercomm);

  if (mall->myId == mall->root)
    MPI_Comm_disconnect(io_child);
  free(port_name);
  *io_child = newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/**
 * @brief Children side of Single: root opens a port, sends it to parents, all accept.
 *
 * Used when children were created by a single parent process (Single strategy).
 *
 * @param[in,out] io_parents    Parents intercomm; replaced by the new intercomm after accept.
 * @param[in,out] io_spawn_port Ports structure used to open the children's port.
 */
void single_strat_children(MPI_Comm *io_parents, Spawn_ports *io_spawn_port) {
  MPI_Comm newintercomm;
  int is_root = mall->myId == mall->root ? 1 : 0;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single CH started", mall->myId, mall->numP); fflush(stdout);
  #endif

  open_port(io_spawn_port, is_root, MAM_SERVICE_UNNEEDED);
  if (mall->myId == mall->root) {
    MPI_Send(io_spawn_port->port_name, MPI_MAX_PORT_NAME, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_SINGLE, *io_parents);
  }

  MPI_Comm_accept(io_spawn_port->port_name, MPI_INFO_NULL, mall->root, mall->comm, &newintercomm);
  MPI_Comm_disconnect(io_parents);
  *io_parents = newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single CH completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}
