#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "PortService.h"
#include "Strategy_Multiple.h"

/**
 * @file Strategy_Multiple.c
 * @brief Implementation of the Multiple spawn strategy (active PortService path).
 *
 * Spawns one group per target node (via Baseline), merges those children into a
 * single intracomm, then reconnects parents and children through an intercomm.
 * Deprecated alternate implementations later in this file are intentionally
 * left undocumented.
 */

/**
 * @brief Parents side: coordinate child-group merge order, then connect to merged children.
 *
 * Broadcasts group id / total_spawns on each spawn intercomm so children can
 * merge in order; root waits for readiness tokens; then all sources
 * @c MPI_Comm_connect to the children's published port.
 *
 * @param[in]     i_spawn_data  Spawn configuration (@c total_spawns).
 * @param[in,out] io_spawn_port Ports for discovering the children's published service.
 * @param[in]     i_comm        Intracommunicator among sources used for the final connect.
 * @param[in,out] io_intercomms Per-spawn intercomms from ::mam_spawn (disconnected here).
 * @param[out]    o_child       Receives the final parent–children intercommunicator.
 */
void multiple_strat_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm i_comm,
                            MPI_Comm *io_intercomms, MPI_Comm *o_child) {
  int i, rootBcast;
  int buffer[2];
  char aux;

  i = 0;
  rootBcast = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;

  buffer[0] = i;
  buffer[1] = i_spawn_data.total_spawns;
  MPI_Bcast(buffer, 2, MPI_INT, rootBcast, io_intercomms[i]);
  if (mall->myId == mall->root) {
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_MULTIPLE, io_intercomms[0], MPI_STATUS_IGNORE);
  }

  for (i = 1; i < i_spawn_data.total_spawns; i++) {
    buffer[0] = i;
    MPI_Bcast(buffer, 2, MPI_INT, rootBcast, io_intercomms[i]);
    if (mall->myId == mall->root) {
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_MULTIPLE, io_intercomms[0], MPI_STATUS_IGNORE);
    }
  }

  // Reconnect with new children communicator
  if (mall->myId == mall->root) { discover_remote_port(0, io_spawn_port); }
  else { discover_remote_port(MAM_SERVICE_UNNEEDED, io_spawn_port); }
  MPI_Comm_connect(io_spawn_port->remote_port, MPI_INFO_NULL, mall->root, i_comm, o_child);

  // Free unneeded spawn communicators
  for (i = 0; i < i_spawn_data.total_spawns; i++) { MPI_Comm_disconnect(&io_intercomms[i]); }

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Multiple PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/**
 * @brief Children side: merge all child groups into one intracomm, then accept parents.
 *
 * Groups with nonzero group id connect/merge into the root children's group;
 * the root group accepts remaining groups. Finally accept the sources and
 * update MaM communicators.
 *
 * @param[in,out] io_parents    Parents intercomm on entry; final intercomm on exit.
 * @param[in,out] io_spawn_port Ports for publish/lookup during the merge.
 */
void multiple_strat_children(MPI_Comm *io_parents, Spawn_ports *io_spawn_port) {
  int i, group_id, total_spawns, new_root;
  int buffer[2];
  char aux;
  MPI_Comm newintracomm, intercomm, parents_comm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Multiple CH started", mall->myId, mall->numP); fflush(stdout);
  #endif

  new_root = 0;
  parents_comm = *io_parents;

  MPI_Bcast(buffer, 2, MPI_INT, mall->root_parents, parents_comm);
  group_id = buffer[0];
  total_spawns = buffer[1];
  if (mall->myId == mall->root && !group_id) { new_root = 1; }
  open_port(io_spawn_port, new_root, group_id);

  if (group_id) {
    if (mall->myId == mall->root) { discover_remote_port(0, io_spawn_port); }
    else { discover_remote_port(MAM_SERVICE_UNNEEDED, io_spawn_port); }

    MPI_Comm_connect(io_spawn_port->remote_port, MPI_INFO_NULL, mall->root, mall->comm, &intercomm);
    MPI_Intercomm_merge(intercomm, 1, &newintracomm); // Get last ranks
    MPI_Comm_disconnect(&intercomm);
    group_id++;
  } else { // Root group of children (targets created by spawn)
    group_id = 1;
    MPI_Comm_dup(mall->comm, &newintracomm);

    if (new_root) {
      MPI_Send(&aux, 1, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_MULTIPLE, parents_comm); // Ensures order in the created intracomm
    }
  }

  for (i = group_id; i < total_spawns; i++) {
    MPI_Comm_accept(io_spawn_port->port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
    if (newintracomm != MPI_COMM_WORLD) MPI_Comm_disconnect(&newintracomm);
    MPI_Intercomm_merge(intercomm, 0, &newintracomm); // Get first ranks
    MPI_Comm_disconnect(&intercomm);

    if (new_root) {
      MPI_Send(&aux, 1, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_MULTIPLE, parents_comm); // Ensures order in the created intracomm
    }
  }

  // Connect with sources (parents)
  MPI_Comm_accept(io_spawn_port->port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
  // Update communicator to expected one
  MAM_comms_update(newintracomm);
  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);

  MPI_Comm_disconnect(&newintracomm);
  MPI_Comm_disconnect(io_parents);
  *io_parents = intercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Multiple CH completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/* @deprecated functions -- Basic algorithm to try out if it the strategy could work
void multiple_strat_parents(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child) {
  int i, tag;
  char *port_name, aux;

  if(mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    tag = MAM_MPITAG_STRAT_MULTIPLE;
    MPI_Send(&spawn_data.total_spawns, 1, MPI_INT, MAM_ROOT, tag, intercomms[0]); 
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, tag, intercomms[0], MPI_STATUS_IGNORE);
    for(i=1; i<spawn_data.total_spawns; i++) {
      MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MAM_ROOT, tag+i, intercomms[i]);
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_MULTIPLE, intercomms[0], MPI_STATUS_IGNORE);
    }
  } else { port_name = malloc(1); }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, comm, child);
  for(i=0; i<spawn_data.total_spawns; i++) {
    MPI_Comm_disconnect(&intercomms[i]);
  }
  free(port_name);
}
*/

/*
void multiple_strat_children(MPI_Comm *parents) {
  int i, start, total_spawns, new_root;
  int rootBcast = MPI_PROC_NULL;
  char *port_name, aux;
  MPI_Status stat;
  MPI_Comm newintracomm, intercomm, parents_comm;

  new_root = 0;
  parents_comm = *parents;

  if(mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, parents_comm, &stat);
    if(stat.MPI_TAG == MAM_MPITAG_STRAT_MULTIPLE) {
      MPI_Recv(&total_spawns, 1, MPI_INT, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm, MPI_STATUS_IGNORE);
      MPI_Open_port(MPI_INFO_NULL, port_name);
      MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm);
      start = 0;
      new_root = 1;
      rootBcast = MPI_ROOT;
    } else {
      MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm, &stat);
      // The "+1" is because the first iteration is done before the loop
      start = stat.MPI_TAG - MAM_MPITAG_STRAT_MULTIPLE + 1;
    }
  } else { port_name = malloc(1); }

  MPI_Bcast(&start, 1, MPI_INT, mall->root, mall->comm);
  if(start) {
    MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, mall->comm, &intercomm);
    MPI_Bcast(&total_spawns, 1, MPI_INT, mall->root, intercomm); // FIXME: Seems inefficient - Should be performed by parent root?
    MPI_Intercomm_merge(intercomm, 1, &newintracomm); // Get last ranks
    MPI_Comm_disconnect(&intercomm);
  } else { 
    start = 1; 
    MPI_Comm_dup(mall->comm, &newintracomm);
    MPI_Bcast(&total_spawns, 1, MPI_INT, mall->root, mall->comm); // FIXME: Seems inefficient - Should be performed by parent root?
  }

  for(i=start; i<total_spawns; i++) {
    MPI_Comm_accept(port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
    MPI_Bcast(&total_spawns, 1, MPI_INT, rootBcast, intercomm); // FIXME: Seems inefficient - Should be performed by parent root?
    if(newintracomm != MPI_COMM_WORLD) MPI_Comm_disconnect(&newintracomm);
    MPI_Intercomm_merge(intercomm, 0, &newintracomm); // Get first ranks
    MPI_Comm_disconnect(&intercomm);

    if(new_root) {
      MPI_Send(&aux, 1, MPI_CHAR, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm); // Ensures order in the created intracommunicator
    }
  }
  
  // Connect with parents  
  MPI_Comm_accept(port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
  // Update communicator to expected one
  MAM_comms_update(newintracomm);
  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);

  if(new_root) MPI_Close_port(port_name);
  free(port_name);
  MPI_Comm_disconnect(&newintracomm);
  MPI_Comm_disconnect(parents);
  *parents = intercomm;
}
*/