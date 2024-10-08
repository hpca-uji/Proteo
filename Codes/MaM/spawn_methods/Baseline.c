#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "Baseline.h"
#include "Strategy_Single.h"
#include "Strategy_Multiple.h"
#include "PortService.h"

//--------------PRIVATE DECLARATIONS---------------//
int baseline_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child);
void baseline_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child);

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
    if(spawn_data.spawn_is_multiple) { multiple_strat_children(child, &spawn_port); }
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
  if(spawn_data.spawn_is_multiple) { multiple_strat_parents(spawn_data, spawn_port, comm, intercomms, child); }
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