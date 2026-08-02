#ifndef MAM_DISTRIBUTED_COMMDIST_H
#define MAM_DISTRIBUTED_COMMDIST_H

/**
 * @file Distributed_CommDist.h
 * @brief MaM distributed data redistribution (sync/async, P2P / Alltoallv / RMA).
 */

#include <mpi.h>
#include "MAM_Types.h"

/**
 * @brief Send registered data entries toward the children / target group.
 *
 * @param[in]     i_numP_children    Size of the remote (children) group.
 * @param[in,out] io_data_struct     MaM data structure to redistribute.
 * @param[in]     i_is_asynchronous  Non-zero for non-blocking redistribution.
 */
void send_data(int i_numP_children, malleability_data_t *io_data_struct, int i_is_asynchronous);

/**
 * @brief Receive registered data entries from the parents / source group.
 *
 * @param[in]     i_numP_parents     Size of the remote (parents) group.
 * @param[in,out] io_data_struct     MaM data structure to fill.
 * @param[in]     i_is_asynchronous  Non-zero for non-blocking redistribution.
 */
void recv_data(int i_numP_parents, malleability_data_t *io_data_struct, int i_is_asynchronous);

/**
 * @brief Test whether a set of async redistribution requests has completed.
 *
 * @param[in] i_is_children_group Non-zero if this rank is in the children group.
 * @param[in] i_requests          Request array to test.
 * @param[in] i_request_qty       Number of requests.
 * @return Non-zero if completed, 0 otherwise.
 *
 * @note Children currently return 1 immediately (@c FIXME: should return a negative code).
 */
int async_communication_check(int i_is_children_group, MPI_Request *i_requests, size_t i_request_qty);

/**
 * @brief Wait until all async redistribution requests complete.
 * @param[in,out] io_requests    Request array.
 * @param[in]     i_request_qty  Number of requests.
 */
void async_communication_wait(MPI_Request *io_requests, size_t i_request_qty);

/**
 * @brief Finalise one async redistribution (wait if needed, unlock/free RMA window).
 *
 * @param[in,out] io_requests    Request array from ::async_communication_start.
 * @param[in]     i_request_qty  Number of requests.
 * @param[in,out] io_win         RMA window (may be @c MPI_WIN_NULL).
 * @param[in]     i_idS          Pair [idI, idE) of ranks to unlock for Lock RMA.
 */
void async_communication_end(MPI_Request *io_requests, size_t i_request_qty, MPI_Win *io_win, int *i_idS);

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
void malloc_comm_array(void **o_array, size_t i_qty, size_t i_datasize, int i_myId, int i_numP, int i_init);

/**
 * @brief Verify that a buffer filled by ::fill_char_range matches the expected pattern.
 * @param[in] i_array Local block to check.
 * @param[in] i_qty   Global element count used for the distribution.
 */
void check_ordered(const char *i_array, size_t i_qty);

#endif
