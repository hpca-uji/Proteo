#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "Baseline.h"
#include "Strategy_Single.h"
#include "PortService.h"

//--------------PRIVATE DECLARATIONS---------------//
int baseline_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child);
void baseline_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child);

void multiple_strat_parents(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child);
void multiple_strat_parents2(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child);
void multiple_strat_children(MPI_Comm *parents);
void multiple_strat_children2(MPI_Comm *parents, Spawn_ports *spawn_port);
//void single_strat_parents(Spawn_data spawn_data, MPI_Comm *child);
//void single_strat_children(MPI_Comm *parents, Spawn_ports *spawn_port);


//--------------PUBLIC FUNCTIONS---------------//
/*
 * Metodo basico para la creacion de procesos. Crea en total
 * spawn_data.spawn_qty procesos.
 */
int baseline(Spawn_data spawn_data, MPI_Comm *child) { //TODO Tratamiento de errores
  Spawn_ports spawn_port;
  MPI_Comm intercomm;
  MPI_Comm_get_parent(&intercomm); //FIXME May be a problem for third reconf or more with only expansions
  init_ports(&spawn_port);

  if (intercomm == MPI_COMM_NULL) { // Parents path
    baseline_parents(spawn_data, &spawn_port, child);
  } else { // Children path
    if(spawn_data.spawn_is_multiple) { multiple_strat_children2(child, &spawn_port); }
    if(spawn_data.spawn_is_single) { single_strat_children(child, &spawn_port); }
  }

  free_ports(&spawn_port);
  return MAM_I_SPAWN_COMPLETED;
}

//--------------PRIVATE FUNCTIONS---------------//

/*
 * Funcion utilizada por los padres para realizar la
 * creación de procesos.
 *
 */
void baseline_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child) {
  int i;
  MPI_Comm comm, *intercomms;

  if (spawn_data.spawn_is_single && mall->myId != mall->root) {
    single_strat_parents(spawn_data, child);
    return;
  }

  comm = spawn_data.spawn_is_single ? MPI_COMM_SELF : spawn_data.comm;
  MPI_Bcast(&spawn_data.total_spawns, 1, MPI_INT, mall->root, comm);
  intercomms = (MPI_Comm*) malloc(spawn_data.total_spawns * sizeof(MPI_Comm)); 
  if(mall->myId != mall->root) {
    spawn_data.sets = (Spawn_set *) malloc(spawn_data.total_spawns * sizeof(Spawn_set));
  }

  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Starting spawning of processes", mall->myId, mall->numP); fflush(stdout);
  #endif
  for(i=0; i<spawn_data.total_spawns; i++) {
    baseline_spawn(spawn_data.sets[i], comm, &intercomms[i]);
  }
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Sources have created the new processes. Performing additional actions if required.", mall->myId, mall->numP); fflush(stdout);
  #endif

  // TODO Improvement - Deactivate Multiple spawn before spawning if total_spawns == 1
  if(spawn_data.spawn_is_multiple) { multiple_strat_parents2(spawn_data, spawn_port, comm, intercomms, child); }
  else { *child = intercomms[0]; } 

  if(spawn_data.spawn_is_single) { single_strat_parents(spawn_data, child); }

  free(intercomms);
  if(mall->myId != mall->root) { free(spawn_data.sets); }
}

/*
 * Funcion basica encargada de la creacion de procesos.
 * Crea un set de procesos segun la configuracion obtenida
 * en ProcessDist.c
 * Devuelve en "child" el intercomunicador que se conecta a los hijos.
 */
int baseline_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child) {
  int rootBcast = MPI_PROC_NULL;
  if(mall->myId == mall->root) rootBcast = MPI_ROOT;

  int spawn_err = MPI_Comm_spawn(spawn_set.cmd, MPI_ARGV_NULL, spawn_set.spawn_qty, spawn_set.mapping, mall->root, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", spawn_set.spawn_qty);
  }
  MAM_Comm_main_structures(*child, rootBcast);

  return spawn_err;
}


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

void multiple_strat_parents2(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child) {
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

void multiple_strat_children2(MPI_Comm *parents, Spawn_ports *spawn_port) {
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