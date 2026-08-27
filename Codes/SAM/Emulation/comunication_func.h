#ifndef COMUNICATION_FUNC_H
#define COMUNICATION_FUNC_H

/**
 * @file comunication_func.h
 * @brief Synthetic MPI point-to-point patterns used by SAM communication stages.
 */

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

/**
 * @brief Ordered ring point-to-point exchange.
 *
 * Rank @c X sends to @c X+1 and receives from @c X-1 (modulo @p i_numP).
 * Rank @p i_root sends first then receives; all other ranks receive first then send.
 *
 * @param[in]     i_myId  Local MPI rank.
 * @param[in]     i_numP  Communicator size.
 * @param[in]     i_root  Rank that initiates the ring (send-first).
 * @param[in]     i_comm  MPI communicator.
 * @param[in,out] io_array Send/receive buffer of @p i_qty chars.
 * @param[in]     i_qty   Number of @c MPI_CHAR elements.
 */
void point_to_point(int i_myId, int i_numP, int i_root, MPI_Comm i_comm, char *io_array, int i_qty);

/**
 * @brief Blocking pairwise exchange with partner @c myId + numP/2.
 *
 * @param[in]  i_myId    Local MPI rank.
 * @param[in]  i_numP    Communicator size.
 * @param[in]  i_comm    MPI communicator.
 * @param[in]  i_array   Send buffer.
 * @param[out] o_r_array Receive buffer.
 * @param[in]  i_qty     Number of @c MPI_CHAR elements.
 */
void point_to_point_inter(int i_myId, int i_numP, MPI_Comm i_comm, char *i_array, char *o_r_array, int i_qty);

/**
 * @brief Non-blocking pairwise exchange with partner @c myId + numP/2.
 *
 * Posts one @c MPI_Isend and one @c MPI_Irecv into @p o_reqs[0] and @p o_reqs[1]
 * (order depends on whether the local rank is in the lower or upper half).
 *
 * @param[in]  i_myId    Local MPI rank.
 * @param[in]  i_numP    Communicator size.
 * @param[in]  i_comm    MPI communicator.
 * @param[in]  i_array   Send buffer.
 * @param[out] o_r_array Receive buffer.
 * @param[in]  i_qty     Number of @c MPI_CHAR elements.
 * @param[out] o_reqs    Array of at least two @c MPI_Request handles.
 */
void point_to_point_asynch_inter(int i_myId, int i_numP, MPI_Comm i_comm, char *i_array,
                                 char *o_r_array, int i_qty, MPI_Request *o_reqs);

#endif
