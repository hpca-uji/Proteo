#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "SpawnUtils.h"

/*
 * Funcion basica encargada de la creacion de procesos.
 * Crea un set de procesos segun la configuracion obtenida
 * en ProcessDist.c
 * Devuelve en "child" el intercomunicador que se conecta a los hijos.
 */
void mam_spawn(Spawn_set spawn_set, MPI_Comm comm, MPI_Comm *child) {
  int rootBcast = MPI_PROC_NULL;
  int comm_size;
  MPI_Comm_size(comm, &comm_size);
  if(mall->myId == mall->root || comm_size == 1) rootBcast = MPI_ROOT;

  int spawn_err = MPI_Comm_spawn(spawn_set.cmd, MPI_ARGV_NULL, spawn_set.spawn_qty, spawn_set.mapping, MAM_ROOT, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", spawn_set.spawn_qty);
    MPI_Abort(MPI_COMM_WORLD, -1);
  }
  MAM_Comm_main_structures(*child, rootBcast);
}


/*
 * Comprueba que comando hay que llamar al realizar
 * el spawn. Todos los sets tienen que hacer el mismo
 * comando.
 */
char* get_spawn_cmd() {
  char *cmd_aux;
  switch(mall_conf->external_usage) {
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