#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../malleabilityStates.h"
#include "../malleabilityDataStructures.h"
#include "Baseline.h"
#include "Spawn_state.h"

//--------------PRIVATE DECLARATIONS---------------//
int baseline_spawn(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *child);
int single_strat_parents(Spawn_data spawn_data, MPI_Comm *child);
void single_strat_children(MPI_Comm *parents);


//--------------PUBLIC FUNCTIONS---------------//
/*
 * Metodo basico para la creacion de procesos. Crea en total
 * spawn_data.spawn_qty procesos.
 */
int baseline(Spawn_data spawn_data, MPI_Comm *child) { //TODO Tratamiento de errores
  MPI_Comm intercomm;
  MPI_Comm_get_parent(&intercomm);

  if (intercomm == MPI_COMM_NULL) { // Parents path
    if (spawn_data.spawn_is_single) {
      single_strat_parents(spawn_data, child);
    } else {
      baseline_spawn(spawn_data, spawn_data.comm, child);
    }
  } else if(spawn_data.spawn_is_single) { // Children path
    single_strat_children(child);
  }

  return MALL_SPAWN_COMPLETED;
}

//--------------PRIVATE FUNCTIONS---------------//
/*
 * Crea un grupo de procesos segun la configuracion indicada por la funcion
 * "processes_dist()".
 */
int baseline_spawn(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *child) {
  int rootBcast = MPI_PROC_NULL;
  if(mall->myId == mall->root) rootBcast = MPI_ROOT;

  int spawn_err = MPI_Comm_spawn(mall->name_exec, MPI_ARGV_NULL, spawn_data.spawn_qty, spawn_data.mapping, mall->root, comm, child, MPI_ERRCODES_IGNORE); 
  MPI_Comm_set_name(*child, "MPI_COMM_MALL_RESIZE");

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", spawn_data.spawn_qty);
  }
  MAM_Comm_main_structures(rootBcast);

  return spawn_err;
}

/* 
 * Si la variable "type" es 1, la creación es con la participación de todo el grupo de padres
 * Si el valor es diferente, la creación es solo con la participación del proceso root
 */
int single_strat_parents(Spawn_data spawn_data, MPI_Comm *child) {
  int spawn_err;
  char *port_name;
  MPI_Comm newintercomm;

  if (mall->myId == mall->root) {
    spawn_err = baseline_spawn(spawn_data, MPI_COMM_SELF, child);

    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, 130, *child, MPI_STATUS_IGNORE);

    set_spawn_state(MALL_SPAWN_SINGLE_COMPLETED, spawn_data.spawn_is_async); // Indicate other processes to join root to end spawn procedure
    wakeup_completion();
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, spawn_data.comm, &newintercomm);

  if(mall->myId == mall->root)
    MPI_Comm_free(child);
  free(port_name);
  *child = newintercomm;

  return spawn_err;
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
    MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, mall->root_parents, 130, *parents);
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_accept(port_name, MPI_INFO_NULL, mall->root, MPI_COMM_WORLD, &newintercomm);

  if(mall->myId == mall->root) {
    MPI_Close_port(port_name);
  }
  free(port_name);
  MPI_Comm_free(parents);
  *parents = newintercomm;
}
