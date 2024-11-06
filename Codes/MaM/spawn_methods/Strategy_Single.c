#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "PortService.h"
#include "Spawn_state.h"
#include "Strategy_Single.h"


/* 
 * Si la variable "type" es 1, la creación es con la participación de todo el grupo de padres
 * Si el valor es diferente, la creación es solo con la participación del proceso root
 */
void single_strat_parents(Spawn_data spawn_data, MPI_Comm *child) {
  char *port_name;
  MPI_Comm newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single PA started", mall->myId, mall->numP); fflush(stdout);
  #endif

  if (mall->myId == mall->root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, MPI_ANY_SOURCE, MAM_MPITAG_STRAT_SINGLE, *child, MPI_STATUS_IGNORE);

    set_spawn_state(MAM_I_SPAWN_SINGLE_COMPLETED, spawn_data.spawn_is_async); // Indicate other processes to join root to end spawn procedure
    wakeup_completion();
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, mall->root, spawn_data.comm, &newintercomm);

  if(mall->myId == mall->root)
    MPI_Comm_disconnect(child);
  free(port_name);
  *child = newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/*
 * Conectar grupo de hijos con grupo de padres
 * Devuelve un intercomunicador para hablar con los padres
 *
 * Solo se utiliza cuando la creación de los procesos ha sido
 * realizada por un solo proceso padre
 */
void single_strat_children(MPI_Comm *parents, Spawn_ports *spawn_port) {
  MPI_Comm newintercomm;
  int is_root = mall->myId == mall->root ? 1 : 0;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single CH started", mall->myId, mall->numP); fflush(stdout);
  #endif

  open_port(spawn_port, is_root, MAM_SERVICE_UNNEEDED);
  if(mall->myId == mall->root) {
    MPI_Send(spawn_port->port_name, MPI_MAX_PORT_NAME, MPI_CHAR, mall->root_parents, MAM_MPITAG_STRAT_SINGLE, *parents);
  }

  MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, mall->root, mall->comm, &newintercomm);
  MPI_Comm_disconnect(parents);
  *parents = newintercomm;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Single CH completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}
