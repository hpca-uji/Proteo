#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "results.h"

/**
 * @file results.c
 * @brief Implementation of SAM timing capture, MPI reduction, and printing.
 */

/** @brief Extra capacity reserved when initialising per-phase iter buffers. */
#define RESULTS_EXTRA_SIZE 100

void def_results_type(results_data *i_results, int i_resizes, MPI_Datatype *o_results_type);

void compute_phase(results_phase *io_phase, int i_myId, int i_numP, int i_root,
                   int i_capture_method, MPI_Comm i_comm);
void compute_max(results_phase *i_phase, double *io_computed_array, int i_myId, int i_root,
                 MPI_Comm i_comm);
void compute_mean(results_phase *i_phase, double *io_computed_array, int i_myId, int i_numP,
                  int i_root, MPI_Comm i_comm);
void compute_median(results_phase *i_phase, double *io_computed_array, size_t *o_used_ids,
                    int i_myId, int i_numP, int i_root, MPI_Comm i_comm);
void match_median(results_phase *i_phase, double *io_computed_array, size_t *i_used_ids,
                  int i_myId, int i_numP, int i_root, MPI_Comm i_comm);

void init_phase_data(results_phase *io_phase, size_t i_stages, size_t i_iters_size);
void free_results_phase(results_phase *io_phase);

//======================================================||
//======================================================||
//==================CAPTURE  FUNCTIONS==================||
//======================================================||
//======================================================||

void capture_iteration(results_data *io_results, size_t i_phase_ind, int i_is_async,
                       double i_time, double *i_times_stages_aux) {
  size_t i;
  results_phase *phase = io_results->phases_times + i_phase_ind;

  if (i_is_async) {
    // TODO: Differentiate between types of asynchronous parts?
    phase->iters_async += 1;
  }

  if (phase->iter_index == phase->iters_size) { // Grow both result vectors
    realloc_results_iters(io_results, i_phase_ind, phase->iters_size + 100);
  }
  phase->iters_time[phase->iter_index] = i_time;
  for (i = 0; i < phase->qty_stages; i++) {
    phase->stage_times[i][phase->iter_index] = i_times_stages_aux[i];
  }
  phase->iter_index = phase->iter_index + 1;
}

void capture_m_iterations(results_data *io_results, size_t i_phase_ind, size_t i_qty_iters,
                          double *i_times, double **i_times_stages_aux) {
  size_t iter;
  int is_async = 0;

  for (iter = 0; iter < i_qty_iters; iter++) {
    capture_iteration(io_results, i_phase_ind, is_async, i_times[iter], i_times_stages_aux[iter]);
  }
}

//======================================================||
//======================================================||
//================MPI RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||

void results_comm(results_data *io_results, int i_root, size_t i_resizes, MPI_Comm i_comm) {
  MPI_Datatype results_type;

  // Build a derived type so all scalar and vector fields travel in one message
  def_results_type(io_results, (int)i_resizes, &results_type);
  MPI_Bcast(io_results, 1, results_type, i_root, i_comm);

  // Free derived type
  MPI_Type_free(&results_type);
}

/**
 * @brief Build an MPI derived type for broadcasting ::results_data timing fields.
 *
 * Packs @c exec_start, @c wasted_time, and the five per-resize arrays
 * (@c sync_time, @c async_time, @c user_time, @c spawn_time, @c malleability_time)
 * each of length @p i_resizes.
 *
 * @param[in]  i_results       Results structure whose addresses define the type.
 * @param[in]  i_resizes       Length of each per-resize timing array.
 * @param[out] o_results_type  Committed MPI datatype (caller must free).
 */
void def_results_type(results_data *i_results, int i_resizes, MPI_Datatype *o_results_type) {
  int i, counts = 7;
  int blocklengths[] = {1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Fill types vector
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = types[6] = MPI_DOUBLE;
  blocklengths[2] = blocklengths[3] = blocklengths[4] = blocklengths[5] = blocklengths[6] = i_resizes;

  // Fill displs vector
  MPI_Get_address(i_results, &dir);

  MPI_Get_address(&(i_results->exec_start), &displs[0]);
  MPI_Get_address(&(i_results->wasted_time), &displs[1]);
  MPI_Get_address(i_results->sync_time, &displs[2]);
  MPI_Get_address(i_results->async_time, &displs[3]);
  MPI_Get_address(i_results->user_time, &displs[4]);
  MPI_Get_address(i_results->spawn_time, &displs[5]);
  MPI_Get_address(i_results->malleability_time, &displs[6]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, o_results_type);
  MPI_Type_commit(o_results_type);
}

//======================================================||
//======================================================||
//================SET RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||

void reset_results_index(results_data *io_results, size_t i_phase_ind) {
  results_phase *phase = io_results->phases_times + i_phase_ind;
  phase->iter_index = 0;
  phase->iters_async = 0;
}

void compute_results_iter(results_data *io_results, int i_myId, int i_numP, int i_root,
                          size_t i_phases, size_t i_actual_phase, int i_capture_method,
                          MPI_Comm i_comm) {
  size_t i;
  results_phase *phase;

  for (i = i_actual_phase; i < i_phases; i++) {
    phase = io_results->phases_times + i;
    compute_phase(phase, i_myId, i_numP, i_root, i_capture_method, i_comm);
  }
}

/**
 * @brief Reduce iteration and stage times for one phase according to @p i_capture_method.
 *
 * @param[in,out] io_phase           Phase results to reduce in place.
 * @param[in]     i_myId             Local MPI rank.
 * @param[in]     i_numP             Communicator size.
 * @param[in]     i_root             Reduction root.
 * @param[in]     i_capture_method   One of ::capture_methods.
 * @param[in]     i_comm             MPI communicator.
 */
void compute_phase(results_phase *io_phase, int i_myId, int i_numP, int i_root,
                   int i_capture_method, MPI_Comm i_comm) {
  size_t i, *used_ids;
  switch (i_capture_method) {
    case RESULTS_MAX:
      compute_max(io_phase, io_phase->iters_time, i_myId, i_root, i_comm);
      for (i = 0; i < io_phase->qty_stages; i++) {
        compute_max(io_phase, io_phase->stage_times[i], i_myId, i_root, i_comm);
      }
      break;
    case RESULTS_MEAN:
      compute_mean(io_phase, io_phase->iters_time, i_myId, i_numP, i_root, i_comm);
      for (i = 0; i < io_phase->qty_stages; i++) {
        compute_mean(io_phase, io_phase->stage_times[i], i_myId, i_numP, i_root, i_comm);
      }
      break;
    case RESULTS_MEDIAN:
      used_ids = malloc(io_phase->iter_index * sizeof(size_t));
      compute_median(io_phase, io_phase->iters_time, used_ids, i_myId, i_numP, i_root, i_comm);
      for (i = 0; i < io_phase->qty_stages; i++) {
        match_median(io_phase, io_phase->stage_times[i], used_ids, i_myId, i_numP, i_root, i_comm);
      }
      free(used_ids);
      break;
  }
}

/**
 * @brief In-place MPI_MAX reduction of @p io_computed_array over @c iter_index elements.
 *
 * @param[in]     i_phase            Phase (provides @c iter_index).
 * @param[in,out] io_computed_array  Local times; root receives the max vector.
 * @param[in]     i_myId             Local MPI rank.
 * @param[in]     i_root             Reduction root.
 * @param[in]     i_comm             MPI communicator.
 */
void compute_max(results_phase *i_phase, double *io_computed_array, int i_myId, int i_root,
                 MPI_Comm i_comm) {
  if (i_myId == i_root) {
    MPI_Reduce(MPI_IN_PLACE, io_computed_array, i_phase->iter_index, MPI_DOUBLE, MPI_MAX, i_root, i_comm);
  } else {
    MPI_Reduce(io_computed_array, NULL, i_phase->iter_index, MPI_DOUBLE, MPI_MAX, i_root, i_comm);
  }
}

/**
 * @brief In-place mean reduction of @p io_computed_array across @p i_numP ranks.
 *
 * Root sums with @c MPI_SUM then divides each element by @p i_numP.
 *
 * @param[in]     i_phase            Phase (provides @c iter_index).
 * @param[in,out] io_computed_array  Local times; root receives the mean vector.
 * @param[in]     i_myId             Local MPI rank.
 * @param[in]     i_numP             Communicator size.
 * @param[in]     i_root             Reduction root.
 * @param[in]     i_comm             MPI communicator.
 */
void compute_mean(results_phase *i_phase, double *io_computed_array, int i_myId, int i_numP,
                  int i_root, MPI_Comm i_comm) {
  if (i_myId == i_root) {
    MPI_Reduce(MPI_IN_PLACE, io_computed_array, i_phase->iter_index, MPI_DOUBLE, MPI_SUM, i_root, i_comm);
    for (size_t i = 0; i < i_phase->iter_index; i++) {
      io_computed_array[i] = i_phase->iters_time[i] / i_numP;
    }
  } else {
    MPI_Reduce(io_computed_array, NULL, i_phase->iter_index, MPI_DOUBLE, MPI_SUM, i_root, i_comm);
  }
}

/**
 * @brief Pair of a timing value and the rank that produced it (median helper).
 */
struct TimeWithIndex {
    double time;   /**< Timing sample. */
    size_t index;  /**< Rank index that contributed @c time. */
};

/**
 * @brief qsort comparator ordering ::TimeWithIndex by ascending @c time.
 * @param[in] i_a First element.
 * @param[in] i_b Second element.
 * @return Negative, zero, or positive as for @c qsort.
 */
int compare(const void *i_a, const void *i_b) {
  return ((struct TimeWithIndex *)i_a)->time - ((struct TimeWithIndex *)i_b)->time;
}

/**
 * @brief Compute the per-element median of a replicated timing vector across ranks.
 *
 * On the root, also fills @p o_used_ids with the rank index chosen for each
 * element (upper-middle sample when @p i_numP is even; see FIXME: in code).
 *
 * @param[in]     i_phase            Phase (provides @c iter_index).
 * @param[in,out] io_computed_array  Local times; root receives medians.
 * @param[out]    o_used_ids         Per-element source rank indices (root only).
 * @param[in]     i_myId             Local MPI rank.
 * @param[in]     i_numP             Communicator size.
 * @param[in]     i_root             Gather root.
 * @param[in]     i_comm             MPI communicator.
 */
void compute_median(results_phase *i_phase, double *io_computed_array, size_t *o_used_ids,
                    int i_myId, int i_numP, int i_root, MPI_Comm i_comm) {
  double *aux_all_iters, median;
  struct TimeWithIndex *aux_id_iters;

  aux_all_iters = NULL;
  aux_id_iters = NULL;
  if (i_myId == i_root) {
    aux_all_iters = malloc(i_numP * i_phase->iter_index * sizeof(double));
    aux_id_iters = malloc(i_numP * sizeof(struct TimeWithIndex));
  }
  MPI_Gather(io_computed_array, i_phase->iter_index, MPI_DOUBLE, aux_all_iters, i_phase->iter_index, MPI_DOUBLE, i_root, i_comm);
  if (i_myId == i_root) {
    for (size_t i = 0; i < i_phase->iter_index; i++) {
      for (int j = 0; j < i_numP; j++) {
        aux_id_iters[j].time = aux_all_iters[i + (i_phase->iter_index * j)];
        aux_id_iters[j].index = (size_t)j;
      }
      // Get Median
      qsort(aux_id_iters, i_numP, sizeof(struct TimeWithIndex), &compare);
      median = aux_id_iters[i_numP / 2].time;
      if (i_numP % 2 == 0) median = (aux_id_iters[i_numP / 2 - 1].time + aux_id_iters[i_numP / 2].time) / 2;
      io_computed_array[i] = median;
      o_used_ids[i] = aux_id_iters[i_numP / 2].index; // FIXME: What should be the index when numP is even?
    }
    free(aux_all_iters);
    free(aux_id_iters);
  }
}

/**
 * @brief Pick per-element values from the ranks recorded in @p i_used_ids.
 *
 * Used after ::compute_median so stage-time vectors follow the same source
 * ranks as the iteration-time medians.
 *
 * @param[in]     i_phase            Phase (provides @c iter_index).
 * @param[in,out] io_computed_array  Local times; root receives matched values.
 * @param[in]     i_used_ids         Per-element source rank indices.
 * @param[in]     i_myId             Local MPI rank.
 * @param[in]     i_numP             Communicator size.
 * @param[in]     i_root             Gather root.
 * @param[in]     i_comm             MPI communicator.
 */
void match_median(results_phase *i_phase, double *io_computed_array, size_t *i_used_ids,
                  int i_myId, int i_numP, int i_root, MPI_Comm i_comm) {
  double *aux_all_iters = NULL;
  size_t matched_id;
  if (i_myId == i_root) {
    aux_all_iters = malloc(i_numP * i_phase->iter_index * sizeof(double));
  }
  MPI_Gather(io_computed_array, i_phase->iter_index, MPI_DOUBLE, aux_all_iters, i_phase->iter_index, MPI_DOUBLE, i_root, i_comm);
  if (i_myId == i_root) {
    for (size_t i = 0; i < i_phase->iter_index; i++) {
      matched_id = i_used_ids[i];
      io_computed_array[i] = aux_all_iters[i + (i_phase->iter_index * matched_id)];
    }
    free(aux_all_iters);
  }
}

//======================================================||
//======================================================||
//===============PRINT RESULTS FUNCTIONS================||
//======================================================||
//======================================================||

void print_iter_results(results_data i_results, size_t i_phase_ind) {
  size_t i;
  results_phase phase = i_results.phases_times[i_phase_ind];

  printf("R_Phase %zu\n", i_phase_ind);
  printf("\tIters: %ld\n", phase.iter_index);
  printf("\tAsync_Iters: %ld\n", phase.iters_async);
  printf("\tT_iter: ");
  for (i = 0; i < phase.iter_index; i++) {
    printf("%lf ", phase.iters_time[i]);
  }
  printf("\n");
}

void print_stage_results(results_data i_results, size_t i_phase_ind) {
  size_t i, j;
  results_phase phase = i_results.phases_times[i_phase_ind];

  for (i = 0; i < phase.qty_stages; i++) {
    printf("\tT_stage %ld: ", i);
    for (j = 0; j < phase.iter_index; j++) {
      printf("%lf ", phase.stage_times[i][j]);
    }
    printf("\n");
  }
}

void print_global_results(results_data i_results, size_t i_resizes) {
  size_t i;

  printf("T_spawn: ");
  for (i = 0; i < i_resizes; i++) {
    printf("%lf ", i_results.spawn_time[i]);
  }

  printf("\nT_SR: ");
  for (i = 0; i < i_resizes; i++) {
    printf("%lf ", i_results.sync_time[i]);
  }

  printf("\nT_AR: ");
  for (i = 0; i < i_resizes; i++) {
    printf("%lf ", i_results.async_time[i]);
  }

  printf("\nT_US: ");
  for (i = 0; i < i_resizes; i++) {
    printf("%lf ", i_results.user_time[i]);
  }

  printf("\nT_Malleability: ");
  for (i = 0; i < i_resizes; i++) {
    printf("%lf ", i_results.malleability_time[i]);
  }

  printf("\nT_total: %lf\n", i_results.exec_time);
}

//======================================================||
//======================================================||
//=============INIT/FREE RESULTS FUNCTIONS==============||
//======================================================||
//======================================================||

void init_results_data(results_data *io_results, size_t i_resizes, size_t i_phases,
                       size_t *i_stages, size_t *i_iters_size) {
  size_t i;

  io_results->spawn_time = calloc(i_resizes, sizeof(double));
  io_results->sync_time = calloc(i_resizes, sizeof(double));
  io_results->async_time = calloc(i_resizes, sizeof(double));
  io_results->user_time = calloc(i_resizes, sizeof(double));
  io_results->malleability_time = calloc(i_resizes, sizeof(double));
  io_results->wasted_time = 0;

  io_results->phases_times = malloc(i_phases * sizeof *(io_results->phases_times));
  for (i = 0; i < i_phases; i++) {
    init_phase_data(io_results->phases_times + i, i_stages[i], i_iters_size[i]);
  }
}

/**
 * @brief Allocate iteration and stage timing buffers for one phase.
 *
 * @param[in,out] io_phase     Phase results to initialise.
 * @param[in]     i_stages     Number of stages.
 * @param[in]     i_iters_size Base iteration capacity (plus ::RESULTS_EXTRA_SIZE).
 */
void init_phase_data(results_phase *io_phase, size_t i_stages, size_t i_iters_size) {
  size_t i;

  io_phase->qty_stages = i_stages;
  io_phase->iters_size = i_iters_size + RESULTS_EXTRA_SIZE;
  io_phase->iters_time = calloc(io_phase->iters_size, sizeof(double));
  io_phase->stage_times = malloc(i_stages * sizeof(double *));
  for (i = 0; i < i_stages; i++) {
    io_phase->stage_times[i] = calloc(io_phase->iters_size, sizeof(double));
  }
  io_phase->iters_async = 0;
  io_phase->iter_index = 0;
}

void realloc_results_iters(results_data *io_results, size_t i_phase_ind, size_t i_needed) {
  int error = 0;
  double *time_aux;
  size_t i;
  results_phase *phase;

  phase = io_results->phases_times + i_phase_ind;
  if (phase->iters_size >= i_needed) return;

  time_aux = (double *)realloc(phase->iters_time, i_needed * sizeof(double));
  if (time_aux == NULL) error = 1;

  for (i = 0; i < phase->qty_stages; i++) {
    phase->stage_times[i] = (double *)realloc(phase->stage_times[i], i_needed * sizeof(double));
    if (phase->stage_times[i] == NULL) error = 1;
  }

  if (error) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria de resultados\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  phase->iters_time = time_aux;
  phase->iters_size = i_needed;
}

void free_results_data(results_data *io_results, size_t i_phases) {
  size_t i;
  results_phase *phase;

  if (io_results != NULL) {
    if (io_results->spawn_time != NULL) {
      free(io_results->spawn_time);
      io_results->spawn_time = NULL;
    }
    if (io_results->sync_time != NULL) {
      free(io_results->sync_time);
      io_results->sync_time = NULL;
    }
    if (io_results->async_time != NULL) {
      free(io_results->async_time);
      io_results->async_time = NULL;
    }
    if (io_results->user_time != NULL) {
      free(io_results->user_time);
      io_results->user_time = NULL;
    }
    if (io_results->malleability_time != NULL) {
      free(io_results->malleability_time);
      io_results->malleability_time = NULL;
    }

    if (io_results->phases_times != NULL) {
      for (i = 0; i < i_phases; i++) {
        phase = io_results->phases_times + i;
        free_results_phase(phase);
      }
      free(io_results->phases_times);
      io_results->phases_times = NULL;
    }

  }
}

/**
 * @brief Free heap memory owned by one ::results_phase.
 * @param[in,out] io_phase Phase results to free (may be @c NULL).
 */
void free_results_phase(results_phase *io_phase) {
  size_t i;

  if (io_phase != NULL) {
    if (io_phase->iters_time != NULL) {
      free(io_phase->iters_time);
      io_phase->iters_time = NULL;
    }
    for (i = 0; i < io_phase->qty_stages; i++) {
      if (io_phase->stage_times[i] != NULL) {
        free(io_phase->stage_times[i]);
        io_phase->stage_times[i] = NULL;
      }
    }
    if (io_phase->stage_times != NULL) {
      free(io_phase->stage_times);
      io_phase->stage_times = NULL;
    }
  }
}
