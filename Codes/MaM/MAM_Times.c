#include "MAM_Times.h"
#include "MAM_DataStructures.h"

/**
 * @file MAM_Times.c
 * @brief Implementation of MaM reconfiguration timing capture and retrieval.
 */

void def_malleability_times(MPI_Datatype *o_new_type);

/**
 * @brief Allocate ::mall_conf->times, reset fields, and commit the MPI datatype.
 */
void init_malleability_times(void) {
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Initializing recording structure", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif

  mall_conf->times = (malleability_times_t *)malloc(sizeof(malleability_times_t));
  if (mall_conf->times == NULL) {
    perror("Error al crear la estructura de tiempos interna para maleabilidad\n");
    MPI_Abort(MPI_COMM_WORLD, -5);
  }

  reset_malleability_times();
  def_malleability_times(&mall_conf->times->times_type);

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Initialized recording structure", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif
}

/**
 * @brief Zero timing scalars before a reconfiguration.
 */
void reset_malleability_times(void) {
  malleability_times_t *times = mall_conf->times;
  times->rms_start = 0; times->spawn_start = 0; times->sync_start = 0; times->async_start = 0; times->user_start = 0; times->malleability_start = 0;
  times->sync_end = 0; times->async_end = 0; times->user_end = 0; times->malleability_end = 0;
  times->rms_time = 0; times->spawn_time = 0;
}

/**
 * @brief Free the timing structure and its MPI datatype.
 */
void free_malleability_times(void) {
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Freeing recording structure", mall->myId, mall->numP); fflush(stdout);
  #endif
  if (mall_conf->times != NULL) {
    if (mall_conf->times->times_type != MPI_DATATYPE_NULL) {
      MPI_Type_free(&mall_conf->times->times_type);
      mall_conf->times->times_type = MPI_DATATYPE_NULL;
    }
    free(mall_conf->times);
  }
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Freed recording structure", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/**
 * @brief Return durations (seconds) from the last completed reconfiguration.
 *
 * @param[out] o_rms_time  RMS duration (or @c NULL).
 * @param[out] o_sp_time   Spawn duration (or @c NULL).
 * @param[out] o_sy_time   Synchronous redistribution duration (or @c NULL).
 * @param[out] o_asy_time  Asynchronous redistribution duration (or @c NULL).
 * @param[out] o_user_time User-callback phase duration (or @c NULL).
 * @param[out] o_mall_time Whole malleability operation duration (or @c NULL).
 */
void MAM_Retrieve_times(double *o_rms_time, double *o_sp_time, double *o_sy_time, double *o_asy_time, double *o_user_time, double *o_mall_time) {
  malleability_times_t *times = mall_conf->times;
  if (o_rms_time != NULL)   *o_rms_time = times->rms_time;
  if (o_sp_time != NULL)   *o_sp_time = times->spawn_time;
  if (o_sy_time != NULL)   *o_sy_time = times->sync_end - times->sync_start;
  if (o_asy_time != NULL)  *o_asy_time = times->async_end - times->async_start;
  if (o_user_time != NULL) *o_user_time = times->user_end - times->user_start;
  if (o_mall_time != NULL) *o_mall_time = times->malleability_end - times->malleability_start;
}

/**
 * @brief Broadcast packed timing fields over @c mall->intercomm.
 *
 * Useful when Baseline sources will not survive and targets must retrieve times
 * via ::MAM_Retrieve_times.
 *
 * @param[in] i_root Bcast root (@c MPI_ROOT / @c MPI_PROC_NULL on intercomm).
 */
void malleability_times_broadcast(int i_root) {
  MPI_Bcast(mall_conf->times, 1, mall_conf->times->times_type, i_root, mall->intercomm);
}

/**
 * @brief Create the derived MPI type packing the five broadcast timing fields.
 *
 * Packs @c spawn_time plus @c sync_start, @c async_start, @c user_start, and
 * @c malleability_start (not every start/end scalar).
 *
 * @param[out] o_new_type Receives the committed datatype.
 */
void def_malleability_times(MPI_Datatype *o_new_type) {
  int i, counts = 6;
  int blocklengths[counts];
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  blocklengths[0] = blocklengths[1] = blocklengths[2] = blocklengths[3] = blocklengths[4] = blocklengths[5] = 1;
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = MPI_DOUBLE;

  MPI_Get_address(mall_conf->times, &dir);

  MPI_Get_address(&(mall_conf->times->rms_time), &displs[0]);
  MPI_Get_address(&(mall_conf->times->spawn_time), &displs[1]);
  MPI_Get_address(&(mall_conf->times->sync_start), &displs[2]);
  MPI_Get_address(&(mall_conf->times->async_start), &displs[3]);
  MPI_Get_address(&(mall_conf->times->user_start), &displs[4]);
  MPI_Get_address(&(mall_conf->times->malleability_start), &displs[5]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, o_new_type);
  MPI_Type_commit(o_new_type);
}
