#ifndef MAM_BLOCK_DISTRIBUTION_H
#define MAM_BLOCK_DISTRIBUTION_H

/**
 * @file block_distribution.h
 * @brief Block-distribution helpers for MaM data redistribution layouts.
 *
 * Computes per-rank ownership ranges and Alltoallv/Allgatherv count/displacement
 * tables between source and target process groups.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

/**
 * @brief Local ownership range of a block-distributed array.
 */
struct Dist_data {
  size_t ini;   /**< First global element index owned by this rank (inclusive). */
  size_t fin;   /**< End of the owned range (exclusive). */
  size_t tamBl; /**< Number of local elements (@c fin - @c ini). */
  size_t qty;   /**< Total number of elements being redistributed. */
  int myId;     /**< Rank used when computing the range. */
  int numP;     /**< Number of ranks in the distribution. */
};

/**
 * @brief Send/receive layout for MaM redistribution collectives and RMA.
 *
 * Distinct from SAM's @c struct Counts (which uses @c int counts/displs).
 */
struct Counts {
  int len;                       /**< Allocated length of @c counts / @c displs. */
  int idI;                       /**< First peer rank to communicate with (inclusive). */
  int idE;                       /**< End of peer rank range (exclusive). */
  MPI_Aint first_target_displs;  /**< RMA: displacement at the first target for @c MPI_Get. */
  MPI_Count *counts;             /**< Elements exchanged with each peer rank. */
  MPI_Aint *displs;              /**< Displacements into the local buffer per peer. */
};

/**
 * @brief Fill send/recv counts from this rank to @p i_numP_other peers for @p i_n elements.
 *
 * @param[in]     i_myId        Local rank in this group.
 * @param[in]     i_numP        Size of the local group.
 * @param[in]     i_numP_other  Size of the remote group.
 * @param[in]     i_n           Total element count.
 * @param[in]     i_offset_ids  Rank-index offset applied to peer ids (spawn layout).
 * @param[in,out] io_counts     Counts structure (arrays must already be allocated).
 */
void prepare_comm_alltoall(int i_myId, int i_numP, int i_numP_other, size_t i_n, int i_offset_ids,
                           struct Counts *io_counts);

/**
 * @brief Fill Allgatherv counts/displs for @p i_n elements across @p i_numP ranks.
 *
 * Allocates the ::Counts arrays via ::mallocCounts; free with ::freeCounts.
 *
 * @param[in]  i_numP     Communicator size.
 * @param[in]  i_n        Total element count.
 * @param[out] o_counts   Counts structure to fill.
 */
void prepare_comm_allgatherv(int i_numP, int i_n, struct Counts *o_counts);

/**
 * @brief Compute the block owned by rank @p i_id among @p i_numP ranks for @p i_qty elements.
 *
 * @param[in]  i_qty       Total element count.
 * @param[in]  i_id        Rank index.
 * @param[in]  i_numP      Number of ranks.
 * @param[out] o_dist_data Receives @c ini / @c fin / @c tamBl and metadata.
 */
void get_block_dist(size_t i_qty, int i_id, int i_numP, struct Dist_data *o_dist_data);

/**
 * @brief Allocate @c counts / @c displs of length @p i_numP and reset id/RMA fields.
 * @param[in,out] io_counts Counts structure to initialise.
 * @param[in]     i_numP    Array length.
 */
void mallocCounts(struct Counts *io_counts, size_t i_numP);

/**
 * @brief Free the internal arrays of a ::Counts structure (not the struct itself).
 * @param[in,out] io_counts Counts structure whose arrays are freed.
 */
void freeCounts(struct Counts *io_counts);

/**
 * @brief Debug-print non-zero (or all) count/displacement entries for one rank.
 *
 * @param[in] i_data_dist    Dist metadata (for rank labels).
 * @param[in] i_xcounts      Counts array.
 * @param[in] i_xdispls      Displacements array.
 * @param[in] i_size         Length of the arrays.
 * @param[in] i_include_zero Non-zero to also print zero counts.
 * @param[in] i_name         Label prefix for the printed lines.
 */
void print_counts(struct Dist_data i_data_dist, MPI_Count *i_xcounts, MPI_Aint *i_xdispls, int i_size,
                  int i_include_zero, const char *i_name);

#endif
