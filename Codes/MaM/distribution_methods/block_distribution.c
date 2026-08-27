#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "block_distribution.h"

/**
 * @file block_distribution.c
 * @brief Implementation of MaM block-distribution layout helpers.
 */

void set_interblock_counts(int i_id, int i_numP, struct Dist_data i_data_dist, int i_offset_ids,
                           MPI_Count *o_sendcounts);
void get_util_ids(struct Dist_data i_dist_data, int i_numP_other, int **o_idS);

void prepare_comm_alltoall(int i_myId, int i_numP, int i_numP_other, size_t i_n, int i_offset_ids,
                           struct Counts *io_counts) {
  int i, *idS, first_id = 0;
  struct Dist_data dist_data, dist_target;

  if (io_counts == NULL) {
    fprintf(stderr, "Counts is NULL for rank %d/%d ", i_myId, i_numP);
    MPI_Abort(MPI_COMM_WORLD, -3);
  }

  get_block_dist(i_n, i_myId, i_numP, &dist_data);
  get_util_ids(dist_data, i_numP_other, &idS);

  io_counts->idI = idS[0] + i_offset_ids;
  io_counts->idE = idS[1] + i_offset_ids;
  get_block_dist(i_n, idS[0], i_numP_other, &dist_target); // RMA Specific operation -- uses idS[0], not idI
  io_counts->first_target_displs = dist_data.ini - dist_target.ini; // RMA Specific operation

  if (idS[0] == 0) { // Uses idS[0], not idI
    set_interblock_counts(io_counts->idI, i_numP_other, dist_data, i_offset_ids, io_counts->counts);
    first_id++;
  }
  for (i = io_counts->idI + first_id; i < io_counts->idE; i++) {
    set_interblock_counts(i, i_numP_other, dist_data, i_offset_ids, io_counts->counts);
    io_counts->displs[i] = io_counts->displs[i - 1] + io_counts->counts[i - 1];
  }
  free(idS);

  for (i = 0; i < i_numP_other; i++) {
    if (io_counts->counts[i] < 0) {
      fprintf(stderr, "Counts value [i=%d/%d] is negative for rank %d/%d ", i, i_numP_other, i_myId, i_numP);
      MPI_Abort(MPI_COMM_WORLD, -3);
    }
    if (io_counts->displs[i] < 0) {
      fprintf(stderr, "Displs value [i=%d/%d] is negative for rank %d/%d ", i, i_numP_other, i_myId, i_numP);
      MPI_Abort(MPI_COMM_WORLD, -3);
    }
  }
}

void prepare_comm_allgatherv(int i_numP, int i_n, struct Counts *o_counts) {
  int i;
  struct Dist_data dist_data;

  mallocCounts(o_counts, i_numP);
  get_block_dist(i_n, 0, i_numP, &dist_data);

  o_counts->counts[0] = dist_data.tamBl;
  for (i = 1; i < i_numP; i++) {
    get_block_dist(i_n, i, i_numP, &dist_data);
    o_counts->counts[i] = dist_data.tamBl;
    o_counts->displs[i] = o_counts->displs[i - 1] + o_counts->counts[i - 1];
  }
}

/*
 * ========================================================================================
 * ========================================================================================
 * ================================DISTRIBUTION FUNCTIONS==================================
 * ========================================================================================
 * ========================================================================================
*/

void get_block_dist(size_t i_qty, int i_id, int i_numP, struct Dist_data *o_dist_data) {
  size_t rem;

  o_dist_data->myId = i_id;
  o_dist_data->numP = i_numP;
  o_dist_data->qty = i_qty;
  o_dist_data->tamBl = i_qty / i_numP;
  rem = i_qty % i_numP;

  if (((size_t)i_id) < rem) { // First subgroup
    o_dist_data->ini = i_id * o_dist_data->tamBl + i_id;
    o_dist_data->fin = (i_id + 1) * o_dist_data->tamBl + (i_id + 1);
  } else { // Second subgroup
    o_dist_data->ini = i_id * o_dist_data->tamBl + rem;
    o_dist_data->fin = (i_id + 1) * o_dist_data->tamBl + rem;
  }

  if (o_dist_data->fin > i_qty) { o_dist_data->fin = i_qty; }
  if (o_dist_data->ini > o_dist_data->fin) { o_dist_data->ini = o_dist_data->fin; }

  o_dist_data->tamBl = o_dist_data->fin - o_dist_data->ini;
}

/**
 * @brief Set how many elements this rank exchanges with remote rank @p i_id.
 *
 * Overlap of the local block (@p i_data_dist) with the remote block of @p i_id
 * determines @p o_sendcounts[@p i_id].
 *
 * @param[in]  i_id          Remote rank index (may include @p i_offset_ids).
 * @param[in]  i_numP        Size of the remote group.
 * @param[in]  i_data_dist   Local ownership range.
 * @param[in]  i_offset_ids  Rank-index offset for the remote group.
 * @param[out] o_sendcounts  Counts array to update at index @p i_id.
 */
void set_interblock_counts(int i_id, int i_numP, struct Dist_data i_data_dist, int i_offset_ids,
                           MPI_Count *o_sendcounts) {
  struct Dist_data other;
  size_t biggest_ini, smallest_end;

  get_block_dist(i_data_dist.qty, i_id - i_offset_ids, i_numP, &other);

  // If the ranges do not overlap, skip this peer
  if (i_data_dist.ini >= other.fin || i_data_dist.fin <= other.ini) {
    return;
  }

  // Larger of the two start indices
  biggest_ini = (i_data_dist.ini > other.ini) ? i_data_dist.ini : other.ini;
  // Smaller of the two end indices
  smallest_end = (i_data_dist.fin < other.fin) ? i_data_dist.fin : other.fin;

  o_sendcounts[i_id] = smallest_end - biggest_ini; // Elements exchanged with rank i_id
}

/**
 * @brief Compute the remote peer rank range this process must communicate with.
 *
 * @param[in]  i_dist_data   Local ownership range.
 * @param[in]  i_numP_other  Size of the remote group.
 * @param[out] o_idS         Allocated pair [idI, idE) (caller frees).
 */
void get_util_ids(struct Dist_data i_dist_data, int i_numP_other, int **o_idS) {
    int idI, idE;
    size_t tamOther = i_dist_data.qty / i_numP_other;
    size_t remOther = i_dist_data.qty % i_numP_other;
    // Cut between remote ranks that own tamOther+1 vs tamOther elements
    size_t middle = (tamOther + 1) * remOther;

    // Compute idI depending on whether the peer uses tamOther or tamOther+1
    if (middle > i_dist_data.ini) { // First subgroup (tamOther+1)
      idI = i_dist_data.ini / (tamOther + 1);
    } else { // Second subgroup (tamOther)
      idI = ((i_dist_data.ini - middle) / tamOther) + remOther;
    }

    // Compute idE depending on whether the peer uses tamOther or tamOther+1
    if (middle >= i_dist_data.fin) { // First subgroup (tamOther+1)
      idE = i_dist_data.fin / (tamOther + 1);
      idE = (i_dist_data.fin % (tamOther + 1) > 0 && idE + 1 <= i_numP_other) ? idE + 1 : idE;
    } else { // Second subgroup (tamOther)
      idE = ((i_dist_data.fin - middle) / tamOther) + remOther;
      idE = ((i_dist_data.fin - middle) % tamOther > 0 && idE + 1 <= i_numP_other) ? idE + 1 : idE;
    }

    *o_idS = malloc(2 * sizeof(int));
    (*o_idS)[0] = idI;
    (*o_idS)[1] = idE;
}

/*
 * ========================================================================================
 * ========================================================================================
 * ==============================INIT/FREE/PRINT FUNCTIONS=================================
 * ========================================================================================
 * ========================================================================================
*/

void mallocCounts(struct Counts *io_counts, size_t i_numP) {

    io_counts->counts = calloc(i_numP, sizeof(MPI_Count));
    if (io_counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2); }

    io_counts->displs = calloc(i_numP, sizeof(MPI_Aint));
    if (io_counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2); }

    io_counts->len = i_numP;
    io_counts->idI = -1;
    io_counts->idE = -1;
    io_counts->first_target_displs = 0;
}

void freeCounts(struct Counts *io_counts) {
    if (io_counts == NULL) {
      return;
    }

    if (io_counts->counts != NULL) {
      free(io_counts->counts);
      io_counts->counts = NULL;
    }
    if (io_counts->displs != NULL) {
      free(io_counts->displs);
      io_counts->displs = NULL;
    }
}

void print_counts(struct Dist_data i_data_dist, MPI_Count *i_xcounts, MPI_Aint *i_xdispls, int i_size,
                  int i_include_zero, const char *i_name) {
  int i;

  for (i = 0; i < i_size; i++) {
    if (i_xcounts[i] != 0 || i_include_zero) {
      printf("P%d of %d | %scounts[%d]=%lld disp=%ld\n", i_data_dist.myId, i_data_dist.numP, i_name, i, i_xcounts[i], i_xdispls[i]);
    }
  }
}
