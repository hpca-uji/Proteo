#include "MAM_Times.h"
#include "MAM_DataStructures.h"

void def_malleability_times(MPI_Datatype *new_type);

void init_malleability_times() {
  #if MAM_DEBUG
    DEBUG_FUNC("Initializing recording structure", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif

  mall_conf->times = (malleability_times_t *) malloc(sizeof(malleability_times_t));
  if(mall_conf->times == NULL) {
    perror("Error al crear la estructura de tiempos interna para maleabilidad\n");
    MPI_Abort(MPI_COMM_WORLD, -5);
  }

  reset_malleability_times();
  def_malleability_times(&mall_conf->times->times_type);

  #if MAM_DEBUG
    DEBUG_FUNC("Initialized recording structure", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif
}


void reset_malleability_times() {
  malleability_times_t *times = mall_conf->times;
  times->spawn_start = 0; times->sync_start = 0; times->async_start = 0; times->user_start = 0; times->malleability_start = 0;
  times->sync_end = 0; times->async_end = 0; times->user_end = 0; times->malleability_end = 0;
  times->spawn_time = 0;
}

void free_malleability_times() {
  #if MAM_DEBUG
    DEBUG_FUNC("Freeing recording structure", mall->myId, mall->numP); fflush(stdout);
  #endif
  if(mall_conf->times != NULL) {
    if(mall_conf->times->times_type != MPI_DATATYPE_NULL) {
      MPI_Type_free(&mall_conf->times->times_type);
      mall_conf->times->times_type = MPI_DATATYPE_NULL;
    }
    free(mall_conf->times);
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Freed recording structure", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/*
 * @brief Returns the times used for the different steps of last reconfiguration.
 *
 * This function is intended to be called when a reconfiguration has ended. 
 * It is designed to provide the necessary information for the user to perform data redistribution.
 *
 * Parameters:
 *  - double *sp_time:   A pointer where the spawn time will be saved.
 *  - double *sy_time:   A pointer where the sychronous data redistribution time will be saved.
 *  - double *asy_time:  A pointer where the asychronous data redistribution time will be saved.
 *  - double *user_time: A pointer where the user data redistribution time will be saved.
 *  - double *mall_time: A pointer where the malleability time will be saved.
 */
void MAM_Retrieve_times(double *sp_time, double *sy_time, double *asy_time, double *user_time, double *mall_time) {
  malleability_times_t *times = mall_conf->times;
  *sp_time = times->spawn_time;
  *sy_time = times->sync_end - times->sync_start;
  *asy_time = times->async_end - times->async_start;
  *user_time = times->user_end - times->user_start;
  *mall_time = times->malleability_end - times->malleability_start;
}

void malleability_times_broadcast(int root) {
  MPI_Bcast(mall_conf->times, 1, mall_conf->times->times_type, root, mall->intercomm);
}

void def_malleability_times(MPI_Datatype *new_type) {
  int i, counts = 5;
  int blocklengths[counts];
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  blocklengths[0] = blocklengths[1] = blocklengths[2] = blocklengths[3] = blocklengths[4] = 1;
  types[0] = types[1] =  types[2] = types[3] = types[4] = MPI_DOUBLE;

  // Se pasa el vector a traves de la direccion de "mall_conf"
  // Rellenar vector displs
  MPI_Get_address(mall_conf->times, &dir);

  // Obtener direccion base
  MPI_Get_address(&(mall_conf->times->spawn_time), &displs[0]);
  MPI_Get_address(&(mall_conf->times->sync_start), &displs[1]);
  MPI_Get_address(&(mall_conf->times->async_start), &displs[2]);
  MPI_Get_address(&(mall_conf->times->user_start), &displs[3]);
  MPI_Get_address(&(mall_conf->times->malleability_start), &displs[4]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, new_type);
  MPI_Type_commit(new_type);
}
