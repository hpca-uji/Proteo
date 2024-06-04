#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../malleabilityStates.h"
#include "../malleabilityDataStructures.h"
#include "Baseline.h"
#include "Spawn_state.h"

#define MAM_TAG_STRAT_SINGLE 130
#define MAM_TAG_STRAT_MULTIPLE_FIRST 131
#define MAM_TAG_STRAT_MULTIPLE_OTHER 132

//--------------PRIVATE DECLARATIONS---------------//
int baseline_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child);
void baseline_parents(Spawn_data spawn_data, MPI_Comm *child);

void multiple_strat_parents(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child);
void multiple_strat_children(MPI_Comm *parents);
void single_strat_parents(Spawn_data spawn_data, MPI_Comm *child);
void single_strat_children(MPI_Comm *parents);


//--------------PUBLIC FUNCTIONS---------------//
/*
 * Metodo basico para la creacion de procesos. Crea en total
 * spawn_data.spawn_qty procesos.
 */
int baseline(Spawn_data spawn_data, MPI_Comm *child) { //TODO Tratamiento de errores
  MPI_Comm intercomm;
  MPI_Comm_get_parent(&intercomm); //FIXME May be a problem for third reconf or more with only expansions

  if (intercomm == MPI_COMM_NULL) { // Parents path
    baseline_parents(spawn_data, child);
  } else { // Children path
    if(spawn_data.spawn_is_multiple) { multiple_strat_children(child); }
    if(spawn_data.spawn_is_single) { single_strat_children(child); }
  }

  return MALL_SPAWN_COMPLETED;
}

//--------------PRIVATE FUNCTIONS---------------//

/*
 * Funcion utilizada por los padres para realizar la
 * creación de procesos.
 *
 */
void baseline_parents(Spawn_data spawn_data, MPI_Comm *child) {
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

  for(i=0; i<spawn_data.total_spawns; i++) {
    baseline_spawn(spawn_data.sets[i], comm, &intercomms[i]);
  }

  // TODO Improvement - Deactivate Multiple spawn before spawning if total_spawns == 1
  if(spawn_data.spawn_is_multiple) { multiple_strat_parents(spawn_data, comm, intercomms, child); }
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

  int spawn_err = MPI_Comm_spawn(mall->name_exec, MPI_ARGV_NULL, spawn_set.spawn_qty, spawn_set.mapping, mall->root, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", spawn_set.spawn_qty);
  }
  MAM_Comm_main_structures(*child, rootBcast);

  return spawn_err;
}


void multiple_strat_parents(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *intercomms, MPI_Comm *child) {
  int i, tag;
  char *port_name, aux;

  //MPI_Barrier(MPI_COMM_WORLD);
  //printf("P%d TEST END - set[%d] spw=%d\n", mall->myId, i, spawn_data.sets[i].spawn_qty); fflush(stdout);
  if(mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    tag = MAM_TAG_STRAT_MULTIPLE_FIRST;
    MPI_Send(&spawn_data.total_spawns, 1, MPI_INT, MALLEABILITY_ROOT, tag, intercomms[0]); 
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, tag, intercomms[0], MPI_STATUS_IGNORE);
    for(i=1; i<spawn_data.total_spawns; i++) {
      MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MALLEABILITY_ROOT, tag+i, intercomms[i]);
      MPI_Recv(&aux, 1, MPI_CHAR, MPI_ANY_SOURCE, MAM_TAG_STRAT_MULTIPLE_FIRST, intercomms[0], MPI_STATUS_IGNORE);
    }
  } else { port_name = malloc(1); }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, comm, child);
  for(i=0; i<spawn_data.total_spawns; i++) {
    MPI_Comm_disconnect(&intercomms[i]);
  }
  free(port_name);
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
    if(stat.MPI_TAG == MAM_TAG_STRAT_MULTIPLE_FIRST) {
      MPI_Recv(&total_spawns, 1, MPI_INT, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm, MPI_STATUS_IGNORE);
      MPI_Open_port(MPI_INFO_NULL, port_name);
      MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm);
      start = 0;
      new_root = 1;
      rootBcast = MPI_ROOT;
    } else {
      MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, stat.MPI_SOURCE, stat.MPI_TAG, parents_comm, &stat);
      // The "+1" is because the first iteration is done before the loop
      start = stat.MPI_TAG - MAM_TAG_STRAT_MULTIPLE_FIRST + 1;
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

/* 
 * Si la variable "type" es 1, la creación es con la participación de todo el grupo de padres
 * Si el valor es diferente, la creación es solo con la participación del proceso root
 */
void single_strat_parents(Spawn_data spawn_data, MPI_Comm *child) {
  char *port_name;
  MPI_Comm newintercomm;

  if (mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, MAM_TAG_STRAT_SINGLE, *child, MPI_STATUS_IGNORE);

    set_spawn_state(MALL_SPAWN_SINGLE_COMPLETED, spawn_data.spawn_is_async); // Indicate other processes to join root to end spawn procedure
    wakeup_completion();
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, spawn_data.comm, &newintercomm);

  if(mall->myId == mall->root)
    MPI_Comm_disconnect(child);
  free(port_name);
  *child = newintercomm;
}

/*
 * Conectar grupo de hijos con grupo de padres
 * Devuelve un intercomunicador para hablar con los padres
 *
 * Solo se utiliza cuando la creación de los procesos ha sido
 * realizada por un solo proceso padre
 */
void single_strat_children(MPI_Comm *parents) {
  char *port_name;
  MPI_Comm newintercomm;

  if(mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Open_port(MPI_INFO_NULL, port_name);
    MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, mall->root_parents, MAM_TAG_STRAT_SINGLE, *parents);
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_accept(port_name, MPI_INFO_NULL, mall->root, mall->comm, &newintercomm);

  if(mall->myId == mall->root) {
    MPI_Close_port(port_name);
  }
  free(port_name);
  MPI_Comm_disconnect(parents);
  *parents = newintercomm;
}
