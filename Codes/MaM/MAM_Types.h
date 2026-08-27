#ifndef MAM_TYPES_H
#define MAM_TYPES_H

/**
 * @file MAM_Types.h
 * @brief Registered application data entries for redistribution across reconfigurations.
 *
 * Four registries are selected by @c is_replicated × @c is_constant
 * (@c MAM_DATA_*). Caller owns @c arrays[] buffers; MaM owns metadata,
 * @c requests, @c windows, and @c idS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "MAM_Constants.h"

/** @brief Initial capacity hint when allocating a ::malleability_data_t. */
#define MAM_TYPES_INIT_DATA_QTY 100

/**
 * @brief One registry of data arrays to redistribute or replicate.
 */
typedef struct {
  size_t entries;       /**< Number of registered arrays currently in use. */
  size_t max_entries;   /**< Allocated capacity of the parallel arrays. */
  size_t *qty;          /**< Global element count per entry. */
  MPI_Datatype *types;  /**< MPI datatype of each entry. */

  size_t *request_qty;  /**< Async request slots reserved per entry (0 if unused). */
  int *idS;             /**< RMA unlock peer range pairs [idI,idE); sized by entries. */
  MPI_Request **requests; /**< Per-entry async request arrays. */
  MPI_Win *windows;     /**< Per-entry RMA windows (@c MPI_WIN_NULL if unused). */
  void **arrays;        /**< Caller-owned local buffers (one per entry). */

} malleability_data_t;

/**
 * @brief Append one data entry to a registry.
 *
 * @param[in]     i_data         Local buffer for this rank (caller-owned).
 * @param[in]     i_total_qty    Global element count for the distributed array.
 * @param[in]     i_type         Element MPI datatype.
 * @param[in]     i_request_qty  Number of async request slots to allocate (0 if none).
 * @param[in,out] io_data_struct Registry to extend.
 */
void add_data(void *i_data, size_t i_total_qty, MPI_Datatype i_type, size_t i_request_qty,
              malleability_data_t *io_data_struct);

/**
 * @brief Replace an existing entry at @p i_index.
 *
 * @param[in]     i_data         New local buffer (caller-owned).
 * @param[in]     i_index        Entry index (no-op if out of range).
 * @param[in]     i_total_qty    Global element count.
 * @param[in]     i_type         Element MPI datatype.
 * @param[in]     i_request_qty  Async request slots to allocate.
 * @param[in,out] io_data_struct Registry to update.
 */
void modify_data(void *i_data, size_t i_index, size_t i_total_qty, MPI_Datatype i_type,
                 size_t i_request_qty, malleability_data_t *io_data_struct);

/**
 * @brief Exchange entry metadata between parents and children over @c mall->intercomm.
 *
 * Structures need not be pre-initialised on children; buffers for replicated
 * entries are allocated on the children side.
 *
 * @param[in,out] io_data_struct_rep  Replicated-data registry.
 * @param[in,out] io_data_struct_dist Distributed-data registry.
 * @param[in]     i_is_children_group Non-zero on the children / newly spawned side.
 */
void comm_data_info(malleability_data_t *io_data_struct_rep, malleability_data_t *io_data_struct_dist,
                    int i_is_children_group);

/**
 * @brief Free metadata of a registry (and array buffers only for zombie ranks).
 * @param[in,out] io_data_struct Registry to free.
 */
void free_malleability_data_struct(malleability_data_t *io_data_struct);

#endif
