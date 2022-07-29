#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <pthread.h>
#include "../malleabilityStates.h"
#include "Baseline.h"

//--------------PRIVATE DECLARATIONS---------------//
int baseline_spawn(Spawn_data spawn_data, MPI_Comm comm, MPI_Comm *child);
int baseline_single_spawn(Spawn_data spawn_data, MPI_Comm *child);
void baseline_establish_connection(int myId, int root, MPI_Comm *parents);

//--------------PUBLIC FUNCTIONS---------------//
/*
 * Metodo basico para la creacion de procesos. Crea en total
 * spawn_data.spawn_qty procesos.
 *
 * Tiene incorporada la estrategia Single para permitir que
 * un solo proceso padre cree a los hijos.
 *
 * Si la funcion es llamada por los hijos se comprobara si
 * se esta utilizando la estrategia Single para terminar
 * la creacion de procesos. En caso contrario no realizan
 * nada los hijos.
 */
int baseline(Spawn_data spawn_data, MPI_Comm *child) { //TODO Tratamiento de errores
  int numRanks;
  MPI_Comm_size(spawn_data.comm, &numRanks);

  if (spawn_data.initial_qty == numRanks) { // Parents path
    if(spawn_data.spawn_is_single) {  
      baseline_single_spawn(spawn_data, child);
    } else {
      baseline_spawn(spawn_data, spawn_data.comm, child);
    }
  } else if(spawn_data.spawn_is_single) { // Children path
    baseline_establish_connection(spawn_data.myId, spawn_data.root, child);
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
  if(spawn_data.myId == spawn_data.root) rootBcast = MPI_ROOT;

  // WORK
  int spawn_err = MPI_Comm_spawn(spawn_data.cmd, MPI_ARGV_NULL, spawn_data.spawn_qty, spawn_data.mapping, spawn_data.root, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", spawn_data.spawn_qty);
  }
  // END WORK

  MPI_Bcast(&spawn_data, 1, spawn_data.dtype, rootBcast, *child);

  return spawn_err;
}


/* 
 * Si la variable "type" es 1, la creación es con la participación de todo el grupo de padres
 * Si el valor es diferente, la creación es solo con la participación del proceso root
 */
int baseline_single_spawn(Spawn_data spawn_data, MPI_Comm *child) {
  int spawn_err;
  char *port_name;
  MPI_Comm newintercomm;

  if (spawn_data.myId == spawn_data.root) {
    spawn_err = baseline_spawn(spawn_data, MPI_COMM_SELF, child);

    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, spawn_data.root, 130, *child, MPI_STATUS_IGNORE);

    if(spawn_data.spawn_is_async) {
      pthread_mutex_lock(&(spawn_data.spawn_mutex));
      commState = MALL_SPAWN_SINGLE_COMPLETED; // Indicate other processes to join root to end spawn procedure
      pthread_mutex_unlock(&(spawn_data.spawn_mutex));
    }
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, spawn_data.root, spawn_data.comm, &newintercomm);

  if(spawn_data.myId == spawn_data.root)
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
void baseline_establish_connection(int myId, int root, MPI_Comm *parents) {
  char *port_name;
  MPI_Comm newintercomm;

  if(myId == root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Open_port(MPI_INFO_NULL, port_name);
    MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, root, 130, *parents);
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_accept(port_name, MPI_INFO_NULL, root, MPI_COMM_WORLD, &newintercomm);

  if(myId == root) {
    MPI_Close_port(port_name);
  }
  free(port_name);
  MPI_Comm_free(parents);
  *parents = newintercomm;
}
