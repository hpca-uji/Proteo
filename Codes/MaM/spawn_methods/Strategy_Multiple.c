#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "PortService.h"
#include "Strategy_Multiple.h"


void multiple_strat_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child) {
  int i, rootBcast;
  int buffer[2];
  char aux;

  i = 0; 
  rootBcast = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;

  buffer[0] = i;
  buffer[1] = spawn_data.total_spawns; 
  MPI_Bcast(buffer, 2, MPI_INT, rootBcast, intercomms[i]);
  if(mall->myId == mall->root) { 
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_MULTIPLE, intercomms[0], MPI_STATUS_IGNORE);
  }

  for(i=1; i<spawn_data.total_spawns; i++) {
    buffer[0] = i;
    MPI_Bcast(buffer, 2, MPI_INT, rootBcast, intercomms[i]);
    if(mall->myId == mall->root) { 
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_MULTIPLE, intercomms[0], MPI_STATUS_IGNORE);
    }
  }

  // Reconnect with new children communicator
  if(mall->myId == mall->root) { discover_remote_port(0, spawn_port); }
  else { discover_remote_port(MAM_SERVICE_UNNEEDED, spawn_port); }
  MPI_Comm_connect(spawn_port->remote_port, MPI_INFO_NULL, mall->root, comm, child);

  // Free unneeded spawn communicators
  for(i=0; i<spawn_data.total_spawns; i++) { MPI_Comm_disconnect(&intercomms[i]); }

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Multiple PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

void multiple_strat_children(MPI_Comm *parents, Spawn_ports *spawn_port) {
  int i, group_id, total_spawns, new_root;
  int buffer[2];
  char aux;
  MPI_Comm newintracomm, intercomm, parents_comm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Multiple CH started", mall->myId, mall->numP); fflush(stdout);
  #endif

  new_root = 0;
  parents_comm = *parents;

  MPI_Bcast(buffer, 2, MPI_INT, mall->root_parents, parents_comm);
  group_id = buffer[0];
  total_spawns = buffer[1];
  if(mall->myId == mall->root && !group_id) { new_root = 1;  }
  open_port(spawn_port, new_root, group_id);

  if(group_id) {
    if(mall->myId == mall->root) { discover_remote_port(0, spawn_port); }
    else { discover_remote_port(MAM_SERVICE_UNNEEDED, spawn_port); }

    MPI_Comm_connect(spawn_port->remote_port, MPI_INFO_NULL, mall->root, mall->comm, &intercomm);
    MPI_Intercomm_merge(intercomm, 1, &newintracomm); // Get last ranks
    MPI_Comm_disconnect(&intercomm);
    group_id++;
  } else { // Root group of targets
    group_id = 1; 
    MPI_Comm_dup(mall->comm, &newintracomm);

    if(new_root) {
      MPI_Send(&aux, 1, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_MULTIPLE, parents_comm); // Ensures order in the created intracommunicator
    }
  }
  
  for(i=group_id; i<total_spawns; i++) {
    MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
    if(newintracomm != MPI_COMM_WORLD) MPI_Comm_disconnect(&newintracomm);
    MPI_Intercomm_merge(intercomm, 0, &newintracomm); // Get first ranks
    MPI_Comm_disconnect(&intercomm);

    if(new_root) {
      MPI_Send(&aux, 1, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_MULTIPLE, parents_comm); // Ensures order in the created intracommunicator
    }
  }

  // Connect with sources
  MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
  // Update communicator to expected one
  MAM_comms_update(newintracomm);
  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);

  MPI_Comm_disconnect(&newintracomm);
  MPI_Comm_disconnect(parents);
  *parents = intercomm;

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
    MPI_Bcast(&total_spawns, 1, MPI_INT, mall->root, intercomm); // FIXME Seems inneficient - Should be performed by parent root?
    MPI_Intercomm_merge(intercomm, 1, &newintracomm); // Get last ranks
    MPI_Comm_disconnect(&intercomm);
  } else { 
    start = 1; 
    MPI_Comm_dup(mall->comm, &newintracomm);
    MPI_Bcast(&total_spawns, 1, MPI_INT, mall->root, mall->comm); // FIXME Seems inneficient - Should be performed by parent root?
  }

  for(i=start; i<total_spawns; i++) {
    MPI_Comm_accept(port_name, MPI_INFO_NULL, mall->root, newintracomm, &intercomm);
    MPI_Bcast(&total_spawns, 1, MPI_INT, rootBcast, intercomm); // FIXME Seems inneficient - Should be performed by parent root?
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