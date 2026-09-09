#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <mpi.h>
#include <math.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "io_func.h"
#include "Main_datatypes.h"
#include "process_stage.h"
#include "configuration.h"

/**
 * @file process_stage.c
 * @brief Implementation of SAM stage init and execution dispatch.
 */

double init_emulation_comm_time(group_data i_group, stage_t *io_stage, MPI_Comm i_comm);
double init_emulation_icomm_time(group_data i_group, stage_t *io_stage, MPI_Comm i_comm);

double init_matrix_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_pi_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);

double init_comm_ptop_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_comm_iptop_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_comm_bcast_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_comm_allgatherv_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_comm_reduce_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);
double init_comm_wait_pt(stage_t *io_stage, phase_t *i_phase);

double init_io_write_pt(group_data i_group, stage_t *io_stage, phase_t *i_phase, MPI_Comm i_comm,
                        int i_compute);
double init_io_read_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute);

void prepare_comm_allgatherv(int i_numP, int i_n, struct Counts *o_counts);
void get_sam_block_dist(int i_qty, int i_id, int i_numP, int *o_tamBl);

double init_stage(stage_t *io_stage, phase_t *i_phase, group_data i_group, MPI_Comm i_comm,
                  int i_compute) {
  double result = 0;
  int qty = 5000;

  io_stage->operations = qty;

  switch (io_stage->pt) {
    // Compute
    case COMP_MATRIX:
      result = init_matrix_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_PI:
      result = init_pi_pt(i_group, io_stage, i_comm, i_compute);
      break;

    // Communication
    case COMP_POINT:
      result = init_comm_ptop_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_IPOINT:
      result = init_comm_iptop_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_BCAST:
      result = init_comm_bcast_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_ALLGATHER:
      result = init_comm_allgatherv_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_REDUCE:
    case COMP_ALLREDUCE:
      result = init_comm_reduce_pt(i_group, io_stage, i_comm, i_compute);
      break;
    case COMP_WAIT:
      result = init_comm_wait_pt(io_stage, i_phase);
      break;

    // I/O
    case COMP_IOWRITE:
      result = init_io_write_pt(i_group, io_stage, i_phase, i_comm, i_compute);
      break;
    case COMP_IOREAD:
      result = init_io_read_pt(i_group, io_stage, i_comm, i_compute);
      break;
  }
  return result;
}

double process_stage(stage_t i_stage, group_data i_group, MPI_Comm i_comm) {
  int i = 0;
  double result, t_start, t_total;
  t_start = MPI_Wtime();
  t_total = 0;
  result = 1;

  switch (i_stage.pt) {
    // Compute
    case COMP_PI:
      for (i = 0; i < i_stage.operations; i++) {
        result += computePiSerial(i_stage.granularity);
      }
      break;
    case COMP_MATRIX:
      for (i = 0; i < i_stage.operations; i++) {
        result += computeMatrix(i_stage.double_array, i_stage.granularity);
      }
      break;
    // Communications
    case COMP_POINT:
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          point_to_point_inter(i_group.myId, i_group.numP, i_comm, i_stage.array, i_stage.full_array, i_stage.real_bytes);
          t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          point_to_point_inter(i_group.myId, i_group.numP, i_comm, i_stage.array, i_stage.full_array, i_stage.real_bytes);
        }
      }
      break;
    case COMP_IPOINT:
      for (i = 0; i < i_stage.operations; i++) {
        point_to_point_asynch_inter(i_group.myId, i_group.numP, i_comm, i_stage.array, i_stage.full_array, i_stage.real_bytes, &(i_stage.reqs[i * 2])); // FIXME: Magical number (2 = Isend + Irecv)
      }
      break;

    case COMP_BCAST:
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          MPI_Bcast(i_stage.array, i_stage.real_bytes, MPI_CHAR, ROOT, i_comm);
          t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          MPI_Bcast(i_stage.array, i_stage.real_bytes, MPI_CHAR, ROOT, i_comm);
        }
      }
      break;
    case COMP_ALLGATHER:
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          MPI_Allgatherv(i_stage.array, i_stage.my_bytes, MPI_CHAR, i_stage.full_array, i_stage.counts.counts, i_stage.counts.displs, MPI_CHAR, i_comm);
          t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          MPI_Allgatherv(i_stage.array, i_stage.my_bytes, MPI_CHAR, i_stage.full_array, i_stage.counts.counts, i_stage.counts.displs, MPI_CHAR, i_comm);
        }
      }
      break;
    case COMP_REDUCE:
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          MPI_Reduce(i_stage.array, i_stage.full_array, i_stage.real_bytes, MPI_CHAR, MPI_MAX, ROOT, i_comm);
          t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          MPI_Reduce(i_stage.array, i_stage.full_array, i_stage.real_bytes, MPI_CHAR, MPI_MAX, ROOT, i_comm);
        }
      }
      break;
    case COMP_ALLREDUCE:
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          MPI_Allreduce(i_stage.array, i_stage.full_array, i_stage.real_bytes, MPI_CHAR, MPI_MAX, i_comm);
          t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          MPI_Allreduce(i_stage.array, i_stage.full_array, i_stage.real_bytes, MPI_CHAR, MPI_MAX, i_comm);
        }
      }
      break;
    case COMP_WAIT:
      if (i_stage.t_capped) { // FIXME: Right now, COMP_WAIT with t_capped only works for P2P comms
        int remaining;
        i = 0;

        // Wait until t_stage time has passed
        while (t_total < i_stage.t_stage) {
          MPI_Waitall(2, &(i_stage.reqs[i * 2]), MPI_STATUSES_IGNORE); // FIXME: Magical number (2 = Isend + Irecv)
          t_total = MPI_Wtime() - t_start;
          i++;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, i_comm);
        }
        remaining = i_stage.operations - i;

        // If there are operations remaining, terminate them
        if (remaining) {
          for (; i < i_stage.operations; i++) {
            MPI_Cancel(&(i_stage.reqs[i * 2])); // FIXME: Magical number
            MPI_Cancel(&(i_stage.reqs[i * 2 + 1])); // FIXME: Magical number
          }
          MPI_Waitall(remaining * 2, &(i_stage.reqs[(i_stage.operations - remaining) * 2]), MPI_STATUSES_IGNORE); // FIXME: Magical number (2 = Isend + Irecv)
        }
      } else {
        MPI_Waitall(i_stage.req_count, i_stage.reqs, MPI_STATUSES_IGNORE);
      }
      break;

    // IO functions
    case COMP_IOWRITE:
      if (!(i_group.myId < i_stage.involved_procs || !i_stage.involved_procs)) { break; }
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          write_n_bytes(i_stage.fd, i_stage.array, i_stage.real_bytes);
          t_total = MPI_Wtime() - t_start;
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          write_n_bytes(i_stage.fd, i_stage.array, i_stage.real_bytes);
        }
      }
      break;
    case COMP_IOREAD:
      if (!(i_group.myId < i_stage.involved_procs || !i_stage.involved_procs)) { break; }
      if (i_stage.t_capped) {
        while (t_total < i_stage.t_stage) {
          read_n_bytes(i_stage.fd, i_stage.array, i_stage.real_bytes);
          t_total = MPI_Wtime() - t_start;
        }
      } else {
        for (i = 0; i < i_stage.operations; i++) {
          read_n_bytes(i_stage.fd, i_stage.array, i_stage.real_bytes);
        }
      }
      break;

  }
  return result;
}

/*
 * ========================================================================================
 * ========================================================================================
 * =================================INIT STAGE FUNCTIONS===================================
 * ========================================================================================
 * ========================================================================================
*/

/**
 * @brief Calibrate how many blocking communication operations fit in @c t_stage.
 *
 * Times one ::process_stage call under barriers, derives @c t_op, then sets
 * @c operations = ceil(t_stage * factor / t_op) and broadcasts it.
 *
 * @param[in]     i_group  Current process-group snapshot.
 * @param[in,out] io_stage Stage being calibrated.
 * @param[in]     i_comm   MPI communicator.
 * @return Always 0 (placeholder; calibration updates @p io_stage in place).
 */
double init_emulation_comm_time(group_data i_group, stage_t *io_stage, MPI_Comm i_comm) {
  double start_time, end_time, time = 0;
  double t_stage;

  MPI_Barrier(i_comm);
  start_time = MPI_Wtime();
  process_stage(*io_stage, i_group, i_comm);
  MPI_Barrier(i_comm);
  end_time = MPI_Wtime();
  io_stage->t_op = (end_time - start_time) / io_stage->operations; // Time of one operation
  t_stage = io_stage->t_stage * i_group.grp_config.factor;
  io_stage->operations = ceil(t_stage / io_stage->t_op);
  MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);

  return time;
}

/**
 * @brief Calibrate how many non-blocking communication operations fit in @c t_stage.
 *
 * Times Isend/Irecv posting plus a matching ::COMP_WAIT, then updates
 * @c operations like ::init_emulation_comm_time.
 *
 * @param[in]     i_group  Current process-group snapshot.
 * @param[in,out] io_stage Stage being calibrated.
 * @param[in]     i_comm   MPI communicator.
 * @return Always 0 (placeholder; calibration updates @p io_stage in place).
 */
double init_emulation_icomm_time(group_data i_group, stage_t *io_stage, MPI_Comm i_comm) {
  double start_time, end_time, time = 0;
  double t_stage;
  stage_t wait_stage;
  wait_stage.pt = COMP_WAIT;
  wait_stage.id = io_stage->id;
  wait_stage.operations = io_stage->operations;
  wait_stage.req_count = io_stage->req_count;
  wait_stage.reqs = io_stage->reqs;

  MPI_Barrier(i_comm);
  start_time = MPI_Wtime();
  process_stage(*io_stage, i_group, i_comm);
  process_stage(wait_stage, i_group, i_comm);
  MPI_Barrier(i_comm);
  end_time = MPI_Wtime();

  io_stage->t_op = (end_time - start_time) / io_stage->operations; // Time of one operation
  t_stage = io_stage->t_stage * i_group.grp_config.factor;
  io_stage->operations = ceil(t_stage / io_stage->t_op);
  MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);

  return time;
}

/**
 * @brief Initialise a ::COMP_MATRIX stage (allocate matrix, optional calibrate).
 */
double init_matrix_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double result, t_stage, start_time;

  result = 0;
  t_stage = io_stage->t_stage * i_group.grp_config.factor;
  initMatrix(&(io_stage->double_array), io_stage->granularity);

  if (i_compute) {
    if (i_group.myId == ROOT) {
      start_time = MPI_Wtime();
      result += process_stage(*io_stage, i_group, i_comm);
      io_stage->t_op = (MPI_Wtime() - start_time) / io_stage->operations; // Time of one operation
      io_stage->operations = ceil(t_stage / io_stage->t_op);
    }
    MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);
    MPI_Bcast(&(io_stage->t_op), 1, MPI_DOUBLE, ROOT, i_comm);
  } else {
    io_stage->operations = ceil(t_stage / io_stage->t_op);
  }

  return result;
}

/**
 * @brief Initialise a ::COMP_PI stage (optional calibrate on root).
 */
double init_pi_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double result, t_stage, start_time;

  result = 0;
  t_stage = io_stage->t_stage * i_group.grp_config.factor;
  if (i_compute) {
    if (i_group.myId == ROOT) {
      start_time = MPI_Wtime();
      result += process_stage(*io_stage, i_group, i_comm);
      io_stage->t_op = (MPI_Wtime() - start_time) / io_stage->operations; // Time of one operation
      io_stage->operations = ceil(t_stage / io_stage->t_op);
    }
    MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);
    MPI_Bcast(&(io_stage->t_op), 1, MPI_DOUBLE, ROOT, i_comm);
  } else {
    io_stage->operations = ceil(t_stage / io_stage->t_op);
  }

  return result;
}

/**
 * @brief Initialise a ::COMP_POINT stage (buffers + optional communication calibrate).
 */
double init_comm_ptop_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double time = 0;
  if (io_stage->array != NULL)
    free(io_stage->array);
  if (io_stage->full_array != NULL)
    free(io_stage->full_array);

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  io_stage->array = calloc(io_stage->real_bytes, sizeof(char));
  io_stage->full_array = calloc(io_stage->real_bytes, sizeof(char));

  if (i_compute && !io_stage->bytes && !io_stage->t_capped) {
    time = init_emulation_comm_time(i_group, io_stage, i_comm);
  } else {
    io_stage->operations = 1;
  }
  return time;
}

/**
 * @brief Initialise a ::COMP_IPOINT stage (buffers, requests, optional calibrate).
 */
double init_comm_iptop_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  int i;
  double time = 0;
  if (io_stage->array != NULL)
    free(io_stage->array);
  if (io_stage->full_array != NULL)
    free(io_stage->full_array);
  if (io_stage->reqs != NULL) // FIXME: May be erroneous if requests are active...
    free(io_stage->reqs);

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  io_stage->array = calloc(io_stage->real_bytes, sizeof(char));
  io_stage->full_array = calloc(io_stage->real_bytes, sizeof(char));

  if (i_compute && !io_stage->bytes) { // t_capped is not considered in this case
    io_stage->req_count = 2 * io_stage->operations; // FIXME: Magical number (2 = Isend + Irecv)
    io_stage->reqs = (MPI_Request *)malloc(io_stage->req_count * sizeof(MPI_Request));
    time = init_emulation_icomm_time(i_group, io_stage, i_comm);
    free(io_stage->reqs);
  } else {
    io_stage->operations = 1;
  }
  io_stage->req_count = 2 * io_stage->operations; // FIXME: Magical number (2 = Isend + Irecv)
  io_stage->reqs = (MPI_Request *)malloc(io_stage->req_count * sizeof(MPI_Request));
  for (i = 0; i < io_stage->req_count; i++) {
    io_stage->reqs[i] = MPI_REQUEST_NULL;
  }

  return time;
}

// TODO: Compute should be always 1 if the number of processes is different
/**
 * @brief Initialise a ::COMP_BCAST stage.
 */
double init_comm_bcast_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double time = 0;
  if (io_stage->array != NULL)
    free(io_stage->array);

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  io_stage->array = calloc(io_stage->real_bytes, sizeof(char)); // FIXME: Valgrind reports uninitialised

  if (i_compute && !io_stage->bytes && !io_stage->t_capped) {
    time = init_emulation_comm_time(i_group, io_stage, i_comm);
  } else {
    io_stage->operations = 1;
  }
  return time;
}

// TODO: Compute should be always 1 if the number of processes is different
/**
 * @brief Initialise a ::COMP_ALLGATHER stage (block distribution + buffers).
 */
double init_comm_allgatherv_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double time = 0;

  if (io_stage->array != NULL)
    free(io_stage->array);
  if (io_stage->counts.counts != NULL)
    free_counts(&(io_stage->counts));
  if (io_stage->full_array != NULL)
    free(io_stage->full_array);

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;

  prepare_comm_allgatherv(i_group.numP, io_stage->real_bytes, &(io_stage->counts));

  get_sam_block_dist(io_stage->real_bytes, i_group.myId, i_group.numP, &(io_stage->my_bytes));

  io_stage->array = calloc(io_stage->my_bytes, sizeof(char));
  io_stage->full_array = calloc(io_stage->real_bytes, sizeof(char));

  if (i_compute && !io_stage->bytes && !io_stage->t_capped) {
    time = init_emulation_comm_time(i_group, io_stage, i_comm);
  } else {
    io_stage->operations = 1;
  }

  return time;
}

// TODO: Compute should be always 1 if the number of processes is different
/**
 * @brief Initialise a ::COMP_REDUCE / ::COMP_ALLREDUCE stage.
 */
double init_comm_reduce_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  double time = 0;
  if (io_stage->array != NULL)
    free(io_stage->array);
  if (io_stage->full_array != NULL)
    free(io_stage->full_array);

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  io_stage->array = calloc(io_stage->real_bytes, sizeof(char));
  // Full array for reduce needs the same size
  io_stage->full_array = calloc(io_stage->real_bytes, sizeof(char));

  if (i_compute && !io_stage->bytes && !io_stage->t_capped) {
    time = init_emulation_comm_time(i_group, io_stage, i_comm);
  } else {
    io_stage->operations = 1;
  }

  return time;
}

/**
 * @brief Initialise a ::COMP_WAIT stage by linking to the matching async stage @c id.
 *
 * Copies @c req_count / @c reqs from the stage in @p i_phase with the same @c id.
 *
 * @param[in,out] io_stage Wait stage to initialise.
 * @param[in]     i_phase  Parent phase containing the matching async stage.
 * @return 0 on success; aborts on missing/negative id.
 */
double init_comm_wait_pt(stage_t *io_stage, phase_t *i_phase) {
  size_t i;
  double time = 0;
  stage_t aux_stage;

  if (io_stage->id < 0) {
    printf("Error when initializing wait stage. Id is negative\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  for (i = 0; i < i_phase->qty_stages; i++) {
    aux_stage = i_phase->stages[i];
    if (aux_stage.id == io_stage->id) { break; }
  }
  if (i >= i_phase->qty_stages) {
    printf("Error when initializing wait stage. Not found a corresponding id\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }

  io_stage->req_count = aux_stage.req_count;
  io_stage->reqs = aux_stage.reqs;

  return time;
}

/**
 * @brief Initialise a ::COMP_IOWRITE stage (open file, buffers, optional calibrate).
 */
double init_io_write_pt(group_data i_group, stage_t *io_stage, phase_t *i_phase, MPI_Comm i_comm,
                        int i_compute) {
  int min_operations;
  size_t stid;
  double result = 0, start_time = 0;
  char *filename = NULL;
  if (io_stage->array != NULL) { free(io_stage->array); }
  if (io_stage->fd > -1) { close(io_stage->fd); }

  if (i_group.myId < io_stage->involved_procs || !io_stage->involved_procs) {
    for (stid = 0; stid < i_phase->qty_stages; stid++) {
      if (i_phase->stages + stid == io_stage) { break; }
    }

    generate_name_file(&filename, SAM_FILE_WRITE, stid, i_group.myId);
    io_stage->fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (io_stage->fd < 0) {
      perror("SAM: Open write file");
      return -1;
    }
    free(filename);
  }

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  min_operations = ceil(io_stage->real_bytes / SAM_IO_MAX_BYTES);
  io_stage->real_bytes = min_operations > 1 ? SAM_IO_MAX_BYTES : io_stage->real_bytes;
  io_stage->array = malloc(io_stage->real_bytes * sizeof *io_stage->array);
  io_stage->array[io_stage->real_bytes - 1] = '\0';

  if (io_stage->bytes || io_stage->t_capped) {
    io_stage->operations = min_operations;
    return result;
  }

  if (!i_compute) {
    io_stage->operations = ceil(io_stage->t_stage / io_stage->t_op);
    return result;
  }

  MPI_Barrier(i_comm);
  if (i_group.myId < io_stage->involved_procs || !io_stage->involved_procs) {
    start_time = MPI_Wtime();
    result += process_stage(*io_stage, i_group, i_comm);
  }
  MPI_Barrier(i_comm);
  if (i_group.myId == ROOT) {
    io_stage->t_op = (MPI_Wtime() - start_time) / io_stage->operations; // Time of one operation
    io_stage->operations = ceil(io_stage->t_stage / io_stage->t_op);
  }
  MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);
  MPI_Bcast(&(io_stage->t_op), 1, MPI_DOUBLE, ROOT, i_comm);

  return result;
}

/**
 * @brief Initialise a ::COMP_IOREAD stage (open shared file, buffers, optional calibrate).
 */
double init_io_read_pt(group_data i_group, stage_t *io_stage, MPI_Comm i_comm, int i_compute) {
  int min_operations;
  double result = 0, start_time = 0;
  if (io_stage->array != NULL) { free(io_stage->array); }
  if (io_stage->fd > -1) { close(io_stage->fd); }

  io_stage->fd = open(SAM_FILE_RNAME, O_RDONLY);
  if (io_stage->fd < 0) {
    perror("SAM: Open write file");
    return -1;
  }

  io_stage->real_bytes = (io_stage->bytes && !io_stage->t_capped) ? io_stage->bytes : io_stage->granularity;
  min_operations = ceil(io_stage->real_bytes / SAM_IO_MAX_BYTES);
  io_stage->real_bytes = min_operations > 1 ? SAM_IO_MAX_BYTES : io_stage->real_bytes;
  io_stage->array = malloc(io_stage->real_bytes * sizeof *io_stage->array);
  io_stage->array[io_stage->real_bytes - 1] = '\0';

  if (i_group.myId < io_stage->involved_procs || !io_stage->involved_procs) {
    off_t starting_pos = i_group.myId * SAM_IO_MAX_BYTES * 100 / i_group.numP;
    lseek(io_stage->fd, starting_pos, SEEK_SET);
  }

  if (io_stage->bytes || io_stage->t_capped) {
    io_stage->operations = min_operations;
    return result;
  }

  if (!i_compute) {
    io_stage->operations = ceil(io_stage->t_stage / io_stage->t_op);
    return result;
  }

  MPI_Barrier(i_comm);
  if (i_group.myId < io_stage->involved_procs || !io_stage->involved_procs) {
    start_time = MPI_Wtime();
    result += process_stage(*io_stage, i_group, i_comm);
  }
  MPI_Barrier(i_comm);
  if (i_group.myId == ROOT) {
    io_stage->t_op = (MPI_Wtime() - start_time) / io_stage->operations; // Time of one operation
    io_stage->operations = ceil(io_stage->t_stage / io_stage->t_op);
  }
  MPI_Bcast(&(io_stage->operations), 1, MPI_INT, ROOT, i_comm);
  MPI_Bcast(&(io_stage->t_op), 1, MPI_DOUBLE, ROOT, i_comm);

  return result;
}

/*
 * ========================================================================================
 * ========================================================================================
 * ==================================INIT/FREE FUNCTIONS===================================
 * ========================================================================================
 * ========================================================================================
*/

/**
 * @brief Fill @c counts / @c displs for an Allgatherv of @p i_n bytes across @p i_numP ranks.
 *
 * Allocates the ::Counts arrays via @c malloc_counts; free with @c free_counts.
 *
 * @param[in]  i_numP    Communicator size.
 * @param[in]  i_n       Total number of elements to distribute.
 * @param[out] o_counts  Counts structure to fill.
 */
void prepare_comm_allgatherv(int i_numP, int i_n, struct Counts *o_counts) {
  int i;
  int tamBl;

  malloc_counts(o_counts, i_numP);
  get_sam_block_dist(i_n, 0, i_numP, &tamBl);
  o_counts->counts[0] = tamBl;

  for (i = 1; i < i_numP; i++) {
    get_sam_block_dist(i_n, i, i_numP, &tamBl);
    o_counts->counts[i] = tamBl;
    o_counts->displs[i] = o_counts->displs[i - 1] + o_counts->counts[i - 1];
  }

}

/**
 * @brief Compute how many of @p i_qty elements belong to rank @p i_id in a block distribution.
 *
 * @param[in]  i_qty   Total element count.
 * @param[in]  i_id    Rank index.
 * @param[in]  i_numP  Number of ranks.
 * @param[out] o_tamBl Receives the local block size for @p i_id.
 */
void get_sam_block_dist(int i_qty, int i_id, int i_numP, int *o_tamBl) {
  int rem, ini, end;
  int exp_tamBl = i_qty / i_numP;
  rem = i_qty % i_numP;

  if (i_id < rem) { // First subgroup
    ini = i_id * exp_tamBl + i_id;
    end = (i_id + 1) * exp_tamBl + (i_id + 1);
  } else { // Second subgroup
    ini = i_id * exp_tamBl + rem;
    end = (i_id + 1) * exp_tamBl + rem;
  }

  if (end > i_qty) { end = i_qty; }
  if (ini > end) { ini = end; }
  *o_tamBl = end - ini;
}
