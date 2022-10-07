#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../malleabilityStates.h"
#include "Merge.h"
#include "Baseline.h"

//--------------PRIVATE DECLARATIONS---------------//
void merge_adapt_expand(MPI_Comm *child, int is_children_group);
void merge_adapt_shrink(int numC, MPI_Comm *child, MPI_Comm comm, int myId);

//--------------PUBLIC FUNCTIONS---------------//
int merge(Spawn_data spawn_data, MPI_Comm *child, int data_state) {
  MPI_Comm intercomm;
  int local_state;
  int is_children_group = 1;

  if(spawn_data.initial_qty > spawn_data.target_qty) { //Shrink
    if(data_state == MALL_DIST_COMPLETED) {
      merge_adapt_shrink(spawn_data.target_qty, child, spawn_data.comm, spawn_data.myId);
      local_state = MALL_DIST_ADAPTED;
    } else {
      local_state = MALL_SPAWN_ADAPT_POSTPONE;
    }
  } else { //Expand
    MPI_Comm_get_parent(&intercomm);
    is_children_group = intercomm == MPI_COMM_NULL ? 0:1;

    baseline(spawn_data, child);
    merge_adapt_expand(child, is_children_group);
    local_state = MALL_SPAWN_COMPLETED;
  }

  return local_state;
}

//--------------PRIVATE MERGE TYPE FUNCTIONS---------------//

/*
 * Se encarga de que el grupo de procesos resultante se 
 * encuentren todos en un intra comunicador, uniendo a
 * padres e hijos en un solo comunicador.
 *
 * Se llama antes de la redistribución de datos.
 *
 * TODO REFACTOR
 */
void merge_adapt_expand(MPI_Comm *child, int is_children_group) {
  MPI_Comm new_comm = MPI_COMM_NULL;

  MPI_Intercomm_merge(*child, is_children_group, &new_comm); //El que pone 0 va primero

  MPI_Comm_free(child); //POSIBLE ERROR?
  *child = new_comm;

  //*numP = numC; //TODO REFACTOR Llevar a otra parte -- Hacer solo si MALL_SPAWN_ADAPTED
  //if(*comm != MPI_COMM_WORLD && *comm != MPI_COMM_NULL) {
  //  MPI_Comm_free(comm);
  //}
}


/*
 * Se encarga de que el grupo de procesos resultante se
 * eliminen aquellos procesos que ya no son necesarios.
 * Los procesos eliminados se quedaran como zombies.
 *
 * Se llama una vez ha terminado la redistribución de datos.
 */
void merge_adapt_shrink(int numC, MPI_Comm *child, MPI_Comm comm, int myId) {
  int color = MPI_UNDEFINED;

  if(myId < numC) {
      color = 1;  
  }
  MPI_Comm_split(comm, color, myId, child);

  //TODO REFACTOR Llevar a otra parte -- Hacer solo si MALL_SPAWN_ADAPTED
  //if(*comm != MPI_COMM_WORLD && *comm != MPI_COMM_NULL)
  //  MPI_Comm_free(comm); //POSIBLE ERROR?
}
