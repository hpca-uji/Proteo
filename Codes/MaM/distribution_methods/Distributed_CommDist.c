#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "block_distribution.h"
#include "Distributed_CommDist.h"
#include "MAM_Constants.h"
#include "MAM_Configuration.h"
#include "MAM_DataStructures.h"

/**
 * @file Distributed_CommDist.c
 * @brief Implementation of MaM sync/async block redistribution.
 */

void prepare_redistribution(size_t i_qty, size_t i_prev_qty, MPI_Datatype i_datatype, int i_numP, int i_numO,
                            int i_is_children_group, void **o_recv, struct Counts *o_s_counts,
                            struct Counts *o_r_counts, MPI_Aint *o_win_size);
void check_requests(struct Counts i_s_counts, struct Counts i_r_counts, MPI_Request **io_requests,
                    size_t *io_request_qty);

void sync_communication(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                        struct Counts i_r_counts, MPI_Aint i_win_size, MPI_Comm i_comm, MPI_Win *io_win);
void sync_communication_unlock(MPI_Win i_win, int *i_idS);
void sync_communication_end(MPI_Win *io_win);
void send_sync_data(int i_numP_children, malleability_data_t *io_data_struct);
void send_async_data(int i_numP_children, malleability_data_t *io_data_struct);
void recv_sync_data(int i_numP_parents, malleability_data_t *io_data_struct);
void recv_async_data(int i_numP_parents, malleability_data_t *io_data_struct);

void sync_point2point(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                      struct Counts i_r_counts, MPI_Comm i_comm);
void sync_rma(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, size_t i_tamBl,
              MPI_Comm i_comm, MPI_Win *o_win);
void sync_rma_lock(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win);
void sync_rma_lockall(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win);

void async_communication_start(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                               struct Counts i_r_counts, MPI_Aint i_win_size, MPI_Comm i_comm,
                               MPI_Request *o_requests, MPI_Win *io_win);

void async_point2point(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                       struct Counts i_r_counts, MPI_Comm i_comm, MPI_Request *o_requests);
void async_rma(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, size_t i_tamBl,
               MPI_Comm i_comm, MPI_Request *o_requests, MPI_Win *o_win);
void async_rma_lock(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win,
                    MPI_Request *o_requests);
void async_rma_lockall(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win,
                       MPI_Request *o_requests);

void fill_char_range(char *o_array, size_t i_local_size, size_t i_ini, size_t i_fin);

/**
 * @brief Allocate a local block of a distributed array of @p i_qty elements.
 *
 * @param[out] o_array    Receives the allocated buffer.
 * @param[in]  i_qty      Global element count.
 * @param[in]  i_datasize Bytes per element.
 * @param[in]  i_myId     Local rank.
 * @param[in]  i_numP     Number of ranks in the distribution.
 * @param[in]  i_init     Non-zero to fill the buffer via ::fill_char_range for checks.
 */
void malloc_comm_array(void **o_array, size_t i_qty, size_t i_datasize, int i_myId, int i_numP, int i_init) {
  struct Dist_data dist_data;

  get_block_dist(i_qty, i_myId, i_numP, &dist_data);
  if ((*o_array = malloc(dist_data.tamBl * i_datasize)) == NULL) {
    printf("Memory Error (Malloc Arrays(%ld)) Qty=%ld\n", dist_data.tamBl, i_datasize);
    exit(1);
  }

  if (i_init) { fill_char_range((char *)*o_array, dist_data.tamBl, dist_data.ini, dist_data.fin); }
}

/**
 * @brief Fill a local block with a deterministic character pattern for redistribution checks.
 *
 * Characters cycle from @c 'A' to @c 'z' based on the global index.
 *
 * @param[out] o_array      Local buffer to fill.
 * @param[in]  i_local_size Number of local elements.
 * @param[in]  i_ini        Global start index of this block.
 * @param[in]  i_fin        Global end index (exclusive); no-op if @p i_fin < @p i_ini.
 */
void fill_char_range(char *o_array, size_t i_local_size, size_t i_ini, size_t i_fin) {
  const char start = 'A';
  const char end   = 'z';
  const int range  = (int)(end - start + 1);

  if (i_fin < i_ini) return;

  for (size_t i = 0; i < i_local_size; i++) {
    size_t global_idx = i_ini + i;
    o_array[i] = (char)(start + (global_idx % range));
  }
}

/**
 * @brief Verify that a buffer filled by ::fill_char_range matches the expected pattern.
 *
 * Ranks print OK/ERROR in turn, synchronised with @c MPI_Barrier on @c mall->comm.
 *
 * @param[in] i_array Local block to check.
 * @param[in] i_qty   Global element count used for the distribution.
 */
void check_ordered(const char *i_array, size_t i_qty) {
  const char start = 'A';
  const char end   = 'z';
  const int range  = (int)(end - start + 1);

  struct Dist_data dist_data;
  get_block_dist(i_qty, mall->myId, mall->numP, &dist_data);
  for (int turn = 0; turn < mall->numP; turn++) {
    if (mall->myId == turn) {
      int error_found = 0;
      for (size_t i = 0; i < dist_data.tamBl; i++) {
        size_t global_idx = dist_data.ini + i;
        char expected = (char)(start + (global_idx % range));
        char got = i_array[i];
        if (got != expected) {
          if (!error_found) {
            printf("Rank %d ERROR(s):\n", mall->myId);
            error_found = 1;
          }
          printf("idx=%zu got='%c' expected='%c'\n", global_idx, got, expected);
        }
      }

      if (!error_found) {
        printf("Rank %d OK [%zu - %zu]\n", mall->myId, dist_data.ini, dist_data.fin);
      }
      fflush(stdout);
    }
    MPI_Barrier(mall->comm);
  }
}


//================================================================================
//================================================================================
//========================PUBLIC BASIC FUNCTIONS==================================
//================================================================================
//================================================================================

/**
 * @brief Send registered data entries toward the children / target group.
 *
 * Dispatches to ::send_sync_data or ::send_async_data.
 *
 * @param[in]     i_numP_children    Size of the remote (children) group.
 * @param[in,out] io_data_struct     MaM data structure to redistribute.
 * @param[in]     i_is_asynchronous  Non-zero for non-blocking redistribution.
 */
void send_data(int i_numP_children, malleability_data_t *io_data_struct, int i_is_asynchronous) {
  if (i_is_asynchronous) {
    send_async_data(i_numP_children, io_data_struct);
  } else {
    send_sync_data(i_numP_children, io_data_struct);
  }
}

/**
 * @brief Receive registered data entries from the parents / source group.
 *
 * Dispatches to ::recv_sync_data or ::recv_async_data.
 *
 * @param[in]     i_numP_parents     Size of the remote (parents) group.
 * @param[in,out] io_data_struct     MaM data structure to fill.
 * @param[in]     i_is_asynchronous  Non-zero for non-blocking redistribution.
 */
void recv_data(int i_numP_parents, malleability_data_t *io_data_struct, int i_is_asynchronous) {
  if (i_is_asynchronous) {
    recv_async_data(i_numP_parents, io_data_struct);
  } else {
    recv_sync_data(i_numP_parents, io_data_struct);
  }
}

//================================================================================
//================================================================================
//========================PRIVATE BASIC FUNCTIONS=================================
//============================ SEND FUNCTIONS ====================================
//================================================================================
//================================================================================

/**
 * @brief Prepare and perform a synchronous data redistribution (sources send).
 *
 * Sources that are also targets may receive into a new buffer. Method/strategy
 * follow the current MaM redistribution configuration. After all entries, RMA
 * paths call ::sync_communication_unlock then ::sync_communication_end.
 *
 * @param[in]     i_numP_children Size of the children / target group.
 * @param[in,out] io_data_struct  Data entries to redistribute.
 */
void send_sync_data(int i_numP_children, malleability_data_t *io_data_struct) {
  size_t qty_prev = 0;
  size_t i;
  void *aux_send, *aux_recv;
  struct Counts s_counts, r_counts;
  MPI_Aint win_size;
  io_data_struct->idS = (int *)malloc(io_data_struct->entries * 2 * sizeof(int));

  for (i = 0; i < io_data_struct->entries; i++) {
    aux_send = io_data_struct->arrays[i];
    aux_recv = NULL;

    /* PREPARE COMMUNICATION */
    prepare_redistribution(io_data_struct->qty[i], qty_prev, io_data_struct->types[i], mall->numP, i_numP_children, MAM_SOURCES, &aux_recv, &s_counts, &r_counts, &win_size);
    qty_prev = io_data_struct->qty[i];
    io_data_struct->idS[i * 2] = r_counts.idI;
    io_data_struct->idS[i * 2 + 1] = r_counts.idE;

    /* COMMUNICATION */
    sync_communication(aux_send, aux_recv, io_data_struct->types[i], s_counts, r_counts, win_size, mall->intercomm, &io_data_struct->windows[i]);
    if (aux_recv != NULL) io_data_struct->arrays[i] = aux_recv;
  }

  // RMA Specific operations
  for (i = 0; i < io_data_struct->entries; i++) { sync_communication_unlock(io_data_struct->windows[i], &io_data_struct->idS[i * 2]); }
  for (i = 0; i < io_data_struct->entries; i++) { sync_communication_end(&io_data_struct->windows[i]); }
  free(io_data_struct->idS);

  freeCounts(&s_counts);
  freeCounts(&r_counts);
}

/**
 * @brief Prepare and start an asynchronous data redistribution (sources send).
 *
 * Communication may still be in progress when this returns.
 *
 * @param[in]     i_numP_children Size of the children / target group.
 * @param[in,out] io_data_struct  Data entries to redistribute.
 */
void send_async_data(int i_numP_children, malleability_data_t *io_data_struct) {
  int qty_prev = 0;
  size_t i;
  void *aux_send, *aux_recv;
  struct Counts s_counts, r_counts;
  MPI_Aint win_size;
  io_data_struct->idS = (int *)malloc(io_data_struct->entries * 2 * sizeof(int));

  for (i = 0; i < io_data_struct->entries; i++) {
    aux_send = io_data_struct->arrays[i];
    aux_recv = NULL;

    /* PREPARE COMMUNICATION */
    prepare_redistribution(io_data_struct->qty[i], qty_prev, io_data_struct->types[i], mall->numP, i_numP_children, MAM_SOURCES, &aux_recv, &s_counts, &r_counts, &win_size);
    check_requests(s_counts, r_counts, &io_data_struct->requests[i], &io_data_struct->request_qty[i]); // FIXME: Error related to second reconf if Merge Shrink + P2P --> Invalid requests
    qty_prev = io_data_struct->qty[i];
    io_data_struct->idS[i * 2] = r_counts.idI;
    io_data_struct->idS[i * 2 + 1] = r_counts.idE;

    /* COMMUNICATION */
    async_communication_start(aux_send, aux_recv, io_data_struct->types[i], s_counts, r_counts, win_size, mall->intercomm, io_data_struct->requests[i], &io_data_struct->windows[i]);
    if (aux_recv != NULL) io_data_struct->arrays[i] = aux_recv;
  }

  freeCounts(&s_counts);
  freeCounts(&r_counts);
}

//================================================================================
//================================================================================
//========================PRIVATE BASIC FUNCTIONS=================================
//============================ RECV FUNCTIONS ====================================
//================================================================================
//================================================================================

/**
 * @brief Prepare and perform a synchronous data redistribution (targets receive).
 *
 * After all entries, RMA paths call ::sync_communication_unlock then
 * ::sync_communication_end.
 *
 * @param[in]     i_numP_parents Size of the parents / source group.
 * @param[in,out] io_data_struct Data entries to fill.
 */
void recv_sync_data(int i_numP_parents, malleability_data_t *io_data_struct) {
  int qty_prev = 0;
  size_t i;
  void *aux_recv, *aux_send = NULL;
  struct Counts s_counts, r_counts;
  MPI_Aint win_size;
  io_data_struct->idS = (int *)malloc(io_data_struct->entries * 2 * sizeof(int));

  for (i = 0; i < io_data_struct->entries; i++) {
    aux_recv = io_data_struct->arrays[i];

    /* PREPARE COMMUNICATION */
    prepare_redistribution(io_data_struct->qty[i], qty_prev, io_data_struct->types[i], mall->numP, i_numP_parents, MAM_TARGETS, &aux_recv, &s_counts, &r_counts, &win_size);
    qty_prev = io_data_struct->qty[i];
    io_data_struct->idS[i * 2] = r_counts.idI;
    io_data_struct->idS[i * 2 + 1] = r_counts.idE;

    /* COMMUNICATION */
    sync_communication(aux_send, aux_recv, io_data_struct->types[i], s_counts, r_counts, win_size, mall->intercomm, &io_data_struct->windows[i]);
    io_data_struct->arrays[i] = aux_recv;
  }

  // RMA Specific operations
  for (i = 0; i < io_data_struct->entries; i++) { sync_communication_unlock(io_data_struct->windows[i], &io_data_struct->idS[i * 2]); }
  for (i = 0; i < io_data_struct->entries; i++) { sync_communication_end(&io_data_struct->windows[i]); }
  free(io_data_struct->idS);

  freeCounts(&s_counts);
  freeCounts(&r_counts);
}

/**
 * @brief Prepare and start an asynchronous data redistribution (targets receive).
 * @param[in]     i_numP_parents Size of the parents / source group.
 * @param[in,out] io_data_struct Data entries to fill.
 */
void recv_async_data(int i_numP_parents, malleability_data_t *io_data_struct) {
  int qty_prev = 0;
  size_t i;
  void *aux_recv, *aux_send = NULL;
  struct Counts s_counts, r_counts;
  MPI_Aint win_size;
  io_data_struct->idS = (int *)malloc(io_data_struct->entries * 2 * sizeof(int));

  for (i = 0; i < io_data_struct->entries; i++) {
    aux_recv = io_data_struct->arrays[i];

    /* PREPARE COMMUNICATION */
    prepare_redistribution(io_data_struct->qty[i], qty_prev, io_data_struct->types[i], mall->numP, i_numP_parents, MAM_TARGETS, &aux_recv, &s_counts, &r_counts, &win_size);
    check_requests(s_counts, r_counts, &io_data_struct->requests[i], &io_data_struct->request_qty[i]); // FIXME: Error related to second reconf if Merge Shrink + P2P --> Invalid requests
    qty_prev = io_data_struct->qty[i];
    io_data_struct->idS[i * 2] = r_counts.idI;
    io_data_struct->idS[i * 2 + 1] = r_counts.idE;

    /* COMMUNICATION */
    async_communication_start(aux_send, aux_recv, io_data_struct->types[i], s_counts, r_counts, win_size, mall->intercomm, io_data_struct->requests[i], &io_data_struct->windows[i]);
    io_data_struct->arrays[i] = aux_recv;
  }

  freeCounts(&s_counts);
  freeCounts(&r_counts);
}

//================================================================================
//================================================================================
//========================SYNCHRONOUS FUNCTIONS===================================
//================================================================================
//================================================================================

/**
 * @brief Redistribute one array synchronously (Alltoallv, P2P, or RMA).
 *
 * Parent and children groups may pass different layouts. Dispatches on
 * @c mall_conf->red_method: @c MAM_RED_BASELINE → @c MPI_Alltoallv_c;
 * @c MAM_RED_POINT → ::sync_point2point; @c MAM_RED_RMA_LOCK / @c LOCKALL → ::sync_rma.
 *
 * @param[in]     i_send      Send buffer (required for sources/parents).
 * @param[out]    o_recv      Receive buffer (NULL only if this rank receives nothing).
 * @param[in]     i_datatype  MPI datatype of each element.
 * @param[in]     i_s_counts  Send counts/displs toward the remote group.
 * @param[in]     i_r_counts  Receive counts/displs from the remote group.
 * @param[in]     i_win_size  Elements exposed in the RMA window (RMA methods).
 * @param[in]     i_comm      Redistribution communicator (RMA requires intracomm).
 * @param[in,out] io_win      RMA window (created for RMA; may stay @c MPI_WIN_NULL).
 */
void sync_communication(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                        struct Counts i_r_counts, MPI_Aint i_win_size, MPI_Comm i_comm, MPI_Win *io_win) {

  /* PERFORM COMMUNICATION */
  switch (mall_conf->red_method) {
  case MAM_RED_RMA_LOCKALL:
  case MAM_RED_RMA_LOCK:
    sync_rma(i_send, o_recv, i_datatype, i_r_counts, i_win_size, i_comm, io_win);
    break;

  case MAM_RED_POINT:
    sync_point2point(i_send, o_recv, i_datatype, i_s_counts, i_r_counts, i_comm);
    break;
  case MAM_RED_BASELINE:
  default:
    MPI_Alltoallv_c(i_send, i_s_counts.counts, i_s_counts.displs, i_datatype, o_recv, i_r_counts.counts, i_r_counts.displs, i_datatype, i_comm);
    break;
  }
}

/**
 * @brief Blocking P2P redistribution using the prepared ::Counts layouts.
 *
 * Posts non-blocking sends (@c MPI_Isend_c) then blocking receives (@c MPI_Recv_c),
 * and waits on outstanding sends. Under @c MAM_SPAWN_MERGE, the self-rank slice is
 * copied with @c memcpy and skipped in the MPI loops.
 *
 * @param[in]  i_send      Send buffer.
 * @param[out] o_recv      Receive buffer.
 * @param[in]  i_datatype  Element MPI datatype.
 * @param[in]  i_s_counts  Send layout / peer range [@c idI, @c idE).
 * @param[in]  i_r_counts  Receive layout / peer range.
 * @param[in]  i_comm      Communicator for the point-to-point exchanges.
 */
void sync_point2point(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                      struct Counts i_r_counts, MPI_Comm i_comm) {
    int i, j, init, end, total_sends, datasize;
    size_t offset, offset2;
    MPI_Request *sends = NULL;

    MPI_Type_size(i_datatype, &datasize);
    init = i_s_counts.idI;
    end = i_s_counts.idE;
    if (mall_conf->spawn_method == MAM_SPAWN_MERGE && (i_s_counts.idI == mall->myId || i_s_counts.idE == mall->myId + 1)) {
      offset = i_s_counts.displs[mall->myId] * datasize;
      offset2 = i_r_counts.displs[mall->myId] * datasize;
      memcpy(o_recv + offset2, i_send + offset, i_s_counts.counts[mall->myId]);

      if (i_s_counts.idI == mall->myId) init = i_s_counts.idI + 1;
      else end = i_s_counts.idE - 1;
    }

    total_sends = end - init;
    j = 0;
    if (total_sends > 0) {
      sends = (MPI_Request *)malloc(total_sends * sizeof(MPI_Request));
    }
    for (i = init; i < end; i++) {
      sends[j] = MPI_REQUEST_NULL;
      offset = i_s_counts.displs[i] * datasize;
      MPI_Isend_c(i_send + offset, i_s_counts.counts[i], i_datatype, i, 99, i_comm, &(sends[j]));
      j++;
    }

    init = i_r_counts.idI;
    end = i_r_counts.idE;
    if (mall_conf->spawn_method == MAM_SPAWN_MERGE) {
      if (i_r_counts.idI == mall->myId) init = i_r_counts.idI + 1;
      else if (i_r_counts.idE == mall->myId + 1) end = i_r_counts.idE - 1;
    }

    for (i = init; i < end; i++) {
      offset = i_r_counts.displs[i] * datasize;
      MPI_Recv_c(o_recv + offset, i_r_counts.counts[i], i_datatype, i, 99, i_comm, MPI_STATUS_IGNORE);
    }

    if (total_sends > 0) {
      MPI_Waitall(total_sends, sends, MPI_STATUSES_IGNORE);
      free(sends);
    }
}

/**
 * @brief Synchronous RMA redistribution (creates window, then Lock or Lockall Gets).
 *
 * Creates an @c MPI_Win on @p i_send (@p i_tamBl elements), then calls
 * ::sync_rma_lock or ::sync_rma_lockall according to @c mall_conf->red_method.
 * The window is unlocked/freed later by ::sync_communication_unlock / ::sync_communication_end.
 *
 * @param[in]  i_send      Window base (NULL allowed for children with empty window).
 * @param[out] o_recv      Receive buffer.
 * @param[in]  i_datatype  Element datatype.
 * @param[in]  i_r_counts  Receive layout / peer range.
 * @param[in]  i_tamBl     Elements exposed in @p i_send.
 * @param[in]  i_comm      Intracommunicator (MPI-RMA requirement).
 * @param[out] o_win       Created window (freed later via unlock/end helpers).
 */
void sync_rma(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, size_t i_tamBl,
              MPI_Comm i_comm, MPI_Win *o_win) {
  int datasize;
  MPI_Type_size(i_datatype, &datasize);
  MPI_Win_create(i_send, (MPI_Aint)i_tamBl * datasize, datasize, MPI_INFO_NULL, i_comm, o_win);

  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Created Window for synchronous RMA communication", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(i_comm);
  #endif
  switch (mall_conf->red_method) {
    case MAM_RED_RMA_LOCKALL:
      sync_rma_lockall(o_recv, i_datatype, i_r_counts, *o_win);
      break;
    case MAM_RED_RMA_LOCK:
      sync_rma_lock(o_recv, i_datatype, i_r_counts, *o_win);
      break;
  }
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Completed synchronous RMA communication", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(i_comm);
  #endif
}

/**
 * @brief Passive RMA Get with per-target Lock/Unlock epochs.
 *
 * For each peer in [@c idI, @c idE): @c MPI_Win_lock then @c MPI_Get_c.
 * The first Get uses @c first_target_displs; later targets use displacement 0.
 * Unlocks are performed later by ::sync_communication_unlock.
 *
 * @param[out] o_recv      Receive buffer.
 * @param[in]  i_datatype  Element MPI datatype.
 * @param[in]  i_r_counts  Receive layout / peer range and first-target displacement.
 * @param[in]  i_win       Window previously created by ::sync_rma.
 */
void sync_rma_lock(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win) {
  int i, datasize;
  size_t offset, target_displs;

  MPI_Type_size(i_datatype, &datasize);
  target_displs = i_r_counts.first_target_displs;

  for (i = i_r_counts.idI; i < i_r_counts.idE; i++) {
    offset = i_r_counts.displs[i] * datasize;
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, i_win);
    MPI_Get_c(o_recv + offset, i_r_counts.counts[i], i_datatype, i, target_displs, i_r_counts.counts[i], i_datatype, i_win);
    target_displs = 0;
  }
}

/**
 * @brief Passive RMA Get with a single Lockall/Unlockall epoch.
 *
 * Issues @c MPI_Win_lock_all once, then @c MPI_Get_c for each peer in
 * [@c idI, @c idE). First Get uses @c first_target_displs; later targets use 0.
 * Unlockall is performed later by ::sync_communication_unlock.
 *
 * @param[out] o_recv      Receive buffer.
 * @param[in]  i_datatype  Element MPI datatype.
 * @param[in]  i_r_counts  Receive layout / peer range and first-target displacement.
 * @param[in]  i_win       Window previously created by ::sync_rma.
 */
void sync_rma_lockall(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win) {
  int i, datasize;
  size_t offset, target_displs;

  MPI_Type_size(i_datatype, &datasize);
  target_displs = i_r_counts.first_target_displs;

  MPI_Win_lock_all(MPI_MODE_NOCHECK, i_win);
  for (i = i_r_counts.idI; i < i_r_counts.idE; i++) {
    offset = i_r_counts.displs[i] * datasize;
    MPI_Get_c(o_recv + offset, i_r_counts.counts[i], i_datatype, i, target_displs, i_r_counts.counts[i], i_datatype, i_win);
    target_displs = 0;
  }
}

/**
 * @brief Complete pending RMA Gets by unlocking the window.
 * @param[in] i_win  Window to unlock (@c MPI_WIN_NULL is a no-op).
 * @param[in] i_idS  Pair [idI, idE) of targets for Lock-mode unlock.
 */
void sync_communication_unlock(MPI_Win i_win, int *i_idS) {
  if (i_win == MPI_WIN_NULL) { return; }

  if (mall_conf->red_method == MAM_RED_RMA_LOCKALL) {
    MPI_Win_unlock_all(i_win);
  } else if (mall_conf->red_method == MAM_RED_RMA_LOCK) {
    for (int i = i_idS[0]; i < i_idS[1]; i++) {
      MPI_Win_unlock(i, i_win);
    }
  }
}

/**
 * @brief Free an RMA window created for synchronous redistribution.
 * @param[in,out] io_win Window pointer (set freed by @c MPI_Win_free).
 */
void sync_communication_end(MPI_Win *io_win) {
  if ((mall_conf->red_method == MAM_RED_RMA_LOCK || mall_conf->red_method == MAM_RED_RMA_LOCKALL)
  && *io_win != MPI_WIN_NULL) { MPI_Win_free(io_win); }
}

//================================================================================
//================================================================================
//========================ASYNCHRONOUS FUNCTIONS==================================
//================================================================================
//================================================================================

/**
 * @brief Start a non-blocking redistribution (Ialltoallv, Isend/Irecv, or Rget).
 *
 * Dispatches on @c mall_conf->red_method: @c MAM_RED_BASELINE → @c MPI_Ialltoallv_c;
 * @c MAM_RED_POINT → ::async_point2point; @c MAM_RED_RMA_LOCK / @c LOCKALL → ::async_rma.
 * Communication may still be in progress when this returns.
 *
 * @param[in]     i_send       Send buffer (required for sources/parents).
 * @param[out]    o_recv       Receive buffer (NULL only if this rank receives nothing).
 * @param[in]     i_datatype   MPI datatype of each element.
 * @param[in]     i_s_counts   Send counts/displs toward the remote group.
 * @param[in]     i_r_counts   Receive counts/displs from the remote group.
 * @param[in]     i_win_size   Elements exposed in the RMA window (RMA methods).
 * @param[in]     i_comm       Redistribution communicator (RMA requires intracomm).
 * @param[out]    o_requests   Request array (sized by ::check_requests).
 * @param[in,out] io_win       RMA window (created for RMA; may stay @c MPI_WIN_NULL).
 */
void async_communication_start(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                               struct Counts i_r_counts, MPI_Aint i_win_size, MPI_Comm i_comm,
                               MPI_Request *o_requests, MPI_Win *io_win) {
  /* PERFORM COMMUNICATION */
  switch (mall_conf->red_method) {

    case MAM_RED_RMA_LOCKALL:
    case MAM_RED_RMA_LOCK:
      async_rma(i_send, o_recv, i_datatype, i_r_counts, i_win_size, i_comm, o_requests, io_win);
      break;
    case MAM_RED_POINT:
      async_point2point(i_send, o_recv, i_datatype, i_s_counts, i_r_counts, i_comm, o_requests);
      break;
    case MAM_RED_BASELINE:
    default:
      MPI_Ialltoallv_c(i_send, i_s_counts.counts, i_s_counts.displs, i_datatype, o_recv, i_r_counts.counts, i_r_counts.displs, i_datatype, i_comm, &o_requests[0]);
      break;
  }
}

/**
 * @brief Test whether a set of async redistribution requests has completed.
 *
 * Children currently return 1 immediately (@c FIXME: should return a negative code).
 * Otherwise loops @c MPI_Test over each request (preferred over @c MPI_Testall due to
 * a known MPICH issue — @c FIXME:). Aborts on unexpected MPI errors.
 *
 * @param[in] i_is_children_group Non-zero if this rank is in the children group.
 * @param[in] i_requests          Request array to test.
 * @param[in] i_request_qty       Number of requests.
 * @return Non-zero if all tested requests completed, 0 otherwise.
 */
int async_communication_check(int i_is_children_group, MPI_Request *i_requests, size_t i_request_qty) {
  int completed, req_completed, test_err;
  size_t i;
  completed = 1;
  test_err = MPI_SUCCESS;

  if (i_is_children_group) return 1; // FIXME: Should return a negative code

  for (i = 0; i < i_request_qty; i++) {
    test_err = MPI_Test(&(i_requests[i]), &req_completed, MPI_STATUS_IGNORE);
    completed = completed && req_completed;
  }
  //test_err = MPI_Testall(i_request_qty, i_requests, &completed, MPI_STATUSES_IGNORE); // FIXME: Some kind of bug with MPICH.

  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", mall->myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  return completed;
}

/**
 * @brief Wait until all async redistribution requests complete.
 * @param[in,out] io_requests    Request array.
 * @param[in]     i_request_qty  Number of requests.
 */
void async_communication_wait(MPI_Request *io_requests, size_t i_request_qty) {
  MPI_Waitall(i_request_qty, io_requests, MPI_STATUSES_IGNORE);
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Processes Waitall completed", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

/**
 * @brief Finalise one async redistribution (wait if needed, unlock/free RMA window).
 *
 * If strategy @c MAM_STRAT_RED_WAIT_TARGETS is set, issues @c MPI_Waitall (needed when
 * disconnecting process groups even after completion). Then unlocks the RMA window
 * (Lockall vs per-target Lock using @p i_idS) and frees it when non-null.
 *
 * @param[in,out] io_requests    Request array from ::async_communication_start.
 * @param[in]     i_request_qty  Number of requests.
 * @param[in,out] io_win         RMA window (may be @c MPI_WIN_NULL).
 * @param[in]     i_idS          Pair [idI, idE) of ranks to unlock for Lock RMA.
 */
void async_communication_end(MPI_Request *io_requests, size_t i_request_qty, MPI_Win *io_win, int *i_idS) {

  // Disconnecting both process groups requires telling MPI this communication
  // has finished, even though this path is only reached after completion
  if (MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) { MPI_Waitall(i_request_qty, io_requests, MPI_STATUSES_IGNORE); }

  if (*io_win != MPI_WIN_NULL) {
    if (mall_conf->red_method == MAM_RED_RMA_LOCKALL) {
      MPI_Win_unlock_all(*io_win);
    } else if (mall_conf->red_method == MAM_RED_RMA_LOCK) {
      for (int i = i_idS[0]; i < i_idS[1]; i++) {
        MPI_Win_unlock(i, *io_win);
      }
    }
    MPI_Win_free(io_win);
  }
}

/**
 * @brief Non-blocking P2P redistribution using the prepared ::Counts layouts.
 *
 * Posts @c MPI_Isend_c for each send peer, then @c MPI_Irecv_c for each recv peer,
 * storing requests in @p o_requests in that order (sends then recvs).
 *
 * @param[in]  i_send       Send buffer.
 * @param[out] o_recv       Receive buffer.
 * @param[in]  i_datatype   Element MPI datatype.
 * @param[in]  i_s_counts   Send layout / peer range [@c idI, @c idE).
 * @param[in]  i_r_counts   Receive layout / peer range.
 * @param[in]  i_comm       Communicator for the point-to-point exchanges.
 * @param[out] o_requests   Request array (one entry per Isend/Irecv).
 */
void async_point2point(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_s_counts,
                       struct Counts i_r_counts, MPI_Comm i_comm, MPI_Request *o_requests) {
    int i, j = 0, datasize;
    size_t offset;
    MPI_Type_size(i_datatype, &datasize);

    for (i = i_s_counts.idI; i < i_s_counts.idE; i++) {
      offset = i_s_counts.displs[i] * datasize;
      MPI_Isend_c(i_send + offset, i_s_counts.counts[i], i_datatype, i, 99, i_comm, &(o_requests[j]));
      j++;
    }

    for (i = i_r_counts.idI; i < i_r_counts.idE; i++) {
      offset = i_r_counts.displs[i] * datasize;
      MPI_Irecv_c(o_recv + offset, i_r_counts.counts[i], i_datatype, i, 99, i_comm, &(o_requests[j]));
      j++;
    }
}

/**
 * @brief Asynchronous RMA redistribution (window create + Lock/Lockall Rget).
 *
 * Creates an @c MPI_Win on @p i_send (@p i_tamBl elements), then calls
 * ::async_rma_lock or ::async_rma_lockall according to @c mall_conf->red_method.
 * Window unlock/free is deferred to ::async_communication_end.
 *
 * @param[in]  i_send       Window base (NULL allowed for children with empty window).
 * @param[out] o_recv       Receive buffer.
 * @param[in]  i_datatype   Element datatype.
 * @param[in]  i_r_counts   Receive layout / peer range.
 * @param[in]  i_tamBl      Elements exposed in @p i_send.
 * @param[in]  i_comm       Intracommunicator (MPI-RMA requirement).
 * @param[out] o_requests   Request array for outstanding @c MPI_Rget / @c MPI_Rget_c.
 * @param[out] o_win        Created window (freed later via ::async_communication_end).
 */
void async_rma(void *i_send, void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, size_t i_tamBl,
               MPI_Comm i_comm, MPI_Request *o_requests, MPI_Win *o_win) {
  int datasize;

  MPI_Type_size(i_datatype, &datasize);
  MPI_Win_create(i_send, (MPI_Aint)i_tamBl * datasize, datasize, MPI_INFO_NULL, i_comm, o_win);
  switch (mall_conf->red_method) {
    case MAM_RED_RMA_LOCKALL:
      async_rma_lockall(o_recv, i_datatype, i_r_counts, *o_win, o_requests);
      break;
    case MAM_RED_RMA_LOCK:
      async_rma_lock(o_recv, i_datatype, i_r_counts, *o_win, o_requests);
      break;
  }
}

/**
 * @brief Asynchronous passive RMA with per-target Lock and @c MPI_Rget_c.
 *
 * For each peer in [@c idI, @c idE): @c MPI_Win_lock then @c MPI_Rget_c into
 * @p o_requests. The first Get uses @c first_target_displs; later targets use 0.
 * Unlocks are performed later by ::async_communication_end.
 *
 * @param[out] o_recv       Receive buffer.
 * @param[in]  i_datatype   Element MPI datatype.
 * @param[in]  i_r_counts   Receive layout / peer range and first-target displacement.
 * @param[in]  i_win        Window previously created by ::async_rma.
 * @param[out] o_requests   One request per Rget.
 */
void async_rma_lock(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win,
                    MPI_Request *o_requests) {
  int i, j = 0, datasize;
  size_t offset, target_displs;

  MPI_Type_size(i_datatype, &datasize);
  target_displs = i_r_counts.first_target_displs;

  for (i = i_r_counts.idI; i < i_r_counts.idE; i++) {
    offset = i_r_counts.displs[i] * datasize;
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, i_win);
    MPI_Rget_c(o_recv + offset, i_r_counts.counts[i], i_datatype, i, target_displs, i_r_counts.counts[i], i_datatype, i_win, &(o_requests[j]));
    target_displs = 0;
    j++;
  }
}

/**
 * @brief Asynchronous passive RMA with Lockall and @c MPI_Rget.
 *
 * Issues @c MPI_Win_lock_all once, then @c MPI_Rget for each peer in
 * [@c idI, @c idE) into @p o_requests. First Get uses @c first_target_displs;
 * later targets use 0. Unlockall is performed later by ::async_communication_end.
 *
 * @param[out] o_recv       Receive buffer.
 * @param[in]  i_datatype   Element MPI datatype.
 * @param[in]  i_r_counts   Receive layout / peer range and first-target displacement.
 * @param[in]  i_win        Window previously created by ::async_rma.
 * @param[out] o_requests   One request per Rget.
 */
void async_rma_lockall(void *o_recv, MPI_Datatype i_datatype, struct Counts i_r_counts, MPI_Win i_win,
                       MPI_Request *o_requests) {
  int i, j = 0, datasize;
  size_t offset, target_displs;

  MPI_Type_size(i_datatype, &datasize);
  target_displs = i_r_counts.first_target_displs;

  MPI_Win_lock_all(MPI_MODE_NOCHECK, i_win);
  for (i = i_r_counts.idI; i < i_r_counts.idE; i++) {
    offset = i_r_counts.displs[i] * datasize;
    MPI_Rget(o_recv + offset, i_r_counts.counts[i], i_datatype, i, target_displs, i_r_counts.counts[i], i_datatype, i_win, &(o_requests[j]));
    target_displs = 0;
    j++;
  }
}

/*
 * ========================================================================================
 * ========================================================================================
 * ================================DISTRIBUTION FUNCTIONS==================================
 * ========================================================================================
 * ========================================================================================
*/

/**
 * @brief Compute send/recv layouts and allocate the local receive buffer for one entry.
 *
 * If @p i_prev_qty equals @p i_qty (and is non-zero), skip rebuilding counts and only
 * allocate a new receive buffer when needed.
 *
 * @param[in]  i_qty               Global element count for this entry.
 * @param[in]  i_prev_qty          Element count from the previous entry (0 = first).
 * @param[in]  i_datatype          Element MPI datatype.
 * @param[in]  i_numP              Local group size.
 * @param[in]  i_numO              Remote group size.
 * @param[in]  i_is_children_group Non-zero for targets/children.
 * @param[out] o_recv              Receives allocated receive buffer when applicable.
 * @param[out] o_s_counts          Send layout (sources).
 * @param[out] o_r_counts          Receive layout.
 * @param[out] o_win_size          Elements for the RMA window (sources, RMA methods).
 */
void prepare_redistribution(size_t i_qty, size_t i_prev_qty, MPI_Datatype i_datatype, int i_numP, int i_numO,
                            int i_is_children_group, void **o_recv, struct Counts *o_s_counts,
                            struct Counts *o_r_counts, MPI_Aint *o_win_size) {
  int array_size = i_numO;
  int offset_ids = 0;
  int datasize;
  size_t total_bytes;
  struct Dist_data dist_data;

  MPI_Type_size(i_datatype, &datasize);

  if (i_prev_qty == i_qty && i_prev_qty != 0) {
    if (i_is_children_group) {
      get_block_dist(i_qty, mall->myId, i_numP, &dist_data);
      total_bytes = ((size_t)dist_data.tamBl) * ((size_t)datasize);
      *o_recv = malloc(total_bytes);
    } else if (mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->myId < i_numO) {
      get_block_dist(i_qty, mall->myId, i_numO, &dist_data);
      total_bytes = ((size_t)dist_data.tamBl) * ((size_t)datasize);
      *o_recv = malloc(total_bytes);
    }
    return;
  }

  if (i_prev_qty != 0) {
    freeCounts(o_s_counts);
    freeCounts(o_r_counts);
  }

  if (mall_conf->spawn_method == MAM_SPAWN_BASELINE) {
    offset_ids = MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL) ?
            0 : i_numP;
  } else { // Merge method
    array_size = i_numP > i_numO ? i_numP : i_numO;
  }

  mallocCounts(o_s_counts, array_size + offset_ids);
  mallocCounts(o_r_counts, array_size + offset_ids);

  if (i_is_children_group) {
    // Obtain distribution for this child
    get_block_dist(i_qty, mall->myId, i_numP, &dist_data);
    total_bytes = ((size_t)dist_data.tamBl) * ((size_t)datasize);
    *o_recv = malloc(total_bytes);
    *o_win_size = 0;
    offset_ids = 0;
    prepare_comm_alltoall(mall->myId, i_numP, i_numO, i_qty, offset_ids, o_r_counts);

    #if MAM_DEBUG >= 4
      get_block_dist(i_qty, mall->myId, i_numP, &dist_data);
      print_counts(dist_data, o_r_counts->counts, o_r_counts->displs, i_numO + offset_ids, 0, "Targets Recv");
    #endif
  } else {

    if (mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->myId < i_numO) {
      // Obtain distribution for this surviving source/target and allocate recv
      get_block_dist(i_qty, mall->myId, i_numO, &dist_data);
      total_bytes = ((size_t)dist_data.tamBl) * ((size_t)datasize);
      *o_recv = malloc(total_bytes);
      prepare_comm_alltoall(mall->myId, i_numO, i_numP, i_qty, offset_ids, o_r_counts);

      #if MAM_DEBUG >= 4
        print_counts(dist_data, o_r_counts->counts, o_r_counts->displs, array_size, 0, "Sources&Targets Recv");
      #endif
    }
    prepare_comm_alltoall(mall->myId, i_numP, i_numO, i_qty, offset_ids, o_s_counts);

    switch (mall_conf->red_method) {
      case MAM_RED_RMA_LOCKALL:
      case MAM_RED_RMA_LOCK:
        get_block_dist(i_qty, mall->myId, i_numP, &dist_data);
        *o_win_size = dist_data.tamBl;
        break;
    }

    #if MAM_DEBUG >= 4
      get_block_dist(i_qty, mall->myId, i_numP, &dist_data);
      print_counts(dist_data, o_s_counts->counts, o_s_counts->displs, i_numO + offset_ids, 0, "Sources Send");
    #endif
  }
}

/**
 * @brief Ensure @p io_requests has enough slots for the pending communication calls.
 *
 * Allocates or reallocates as needed and initialises entries to @c MPI_REQUEST_NULL.
 *
 * @param[in]     i_s_counts      Send layout (defines P2P send count).
 * @param[in]     i_r_counts      Receive layout (defines P2P recv count).
 * @param[in,out] io_requests     Request array pointer.
 * @param[in,out] io_request_qty  Current/updated request capacity.
 */
void check_requests(struct Counts i_s_counts, struct Counts i_r_counts, MPI_Request **io_requests,
                    size_t *io_request_qty) {
  size_t i, sum;
  MPI_Request *aux;

  switch (mall_conf->red_method) {
    case MAM_RED_BASELINE:
      sum = 1;
      break;
    case MAM_RED_POINT:
    default:
      sum = (size_t)i_s_counts.idE - i_s_counts.idI;
      sum += (size_t)i_r_counts.idE - i_r_counts.idI;
      break;
  }

  if (*io_requests != NULL && sum <= *io_request_qty) return; // Expected amount of requests

  if (*io_requests == NULL) {
    *io_requests = (MPI_Request *)malloc(sum * sizeof(MPI_Request));
  } else { // Array exists, but is too small
    aux = (MPI_Request *)realloc(*io_requests, sum * sizeof(MPI_Request));
    *io_requests = aux;
  }

  if (*io_requests == NULL) {
    fprintf(stderr, "Fatal error - It was not possible to allocate/reallocate memory for the MPI_Requests before the redistribution\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for (i = 0; i < sum; i++) {
    (*io_requests)[i] = MPI_REQUEST_NULL;
  }
  *io_request_qty = sum;
}
