#ifndef MAIN_DATATYPES_H
#define MAIN_DATATYPES_H

/**
 * @file Main_datatypes.h
 * @brief Core SAM datatypes for configuration, stages, phases, and process groups.
 */

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

/** @brief Default MPI root rank used by SAM collectives. */
#define ROOT 0

/**
 * @brief Per-rank counts and displacements for MPI collective buffers.
 */
struct Counts {
  int len;      /**< Length of @c counts / @c displs (typically communicator size). */
  int *counts;  /**< Elements contributed by / for each rank. */
  int *displs;  /**< Displacements into the full receive buffer per rank. */
};

/**
 * @brief One emulated application stage (compute, communication, or I/O).
 */
typedef struct
{
  int pt; /**< Procedure type (::compute_methods / JSON @c Stage_Type). */
  int id; /**< Stage identifier (links WAIT stages to async P2P stages). */
  /** Whether the stage stops after @c operations (0) or after @c t_stage seconds (1). */
  int t_capped;
  double t_stage; /**< Target wall time for the stage (seconds), scaled by group factor. */

  double t_op;           /**< Calibrated time per operation. */
  int operations;        /**< Number of kernel/comm/I/O operations to run. */
  int granularity;       /**< Kernel size or default byte granularity. */
  int bytes;             /**< Configured payload size (0 = derive from granularity). */
  int real_bytes;        /**< Effective bytes used at runtime. */
  int my_bytes;          /**< Local share for block-distributed collectives. */
  int involved_procs;    /**< Ranks that participate in I/O (0 = all). */

  char *array;           /**< Local communication / I/O buffer. */
  char *full_array;      /**< Full / receive buffer for collectives and P2P. */
  double *double_array;  /**< Matrix buffer for ::COMP_MATRIX. */
  int req_count;         /**< Number of entries in @c reqs. */
  MPI_Request *reqs;     /**< Non-blocking request handles. */

  int fd;                /**< Open file descriptor for I/O stages (@c -1 if unused). */

  struct Counts counts;  /**< Allgatherv counts/displacements. */
} stage_t;

/**
 * @brief One application phase: ordered stages repeated for @c qty_iters iterations.
 */
typedef struct
{
  size_t qty_stages; /**< Number of stages in this phase. */
  size_t qty_iters;  /**< Iterations to run in this phase. */
  stage_t *stages;   /**< Array of stages. */
} phase_t;

/**
 * @brief Per-process-group malleability and scheduling settings.
 */
typedef struct
{
  int iters;       /**< Iteration budget for this group before the next resize. */
  int procs;       /**< Target number of MPI processes. */
  int sm;          /**< Spawn method. */
  int phy_dist;    /**< Physical distribution (compact/spread). */
  int rm;          /**< Redistribution method. */
  int *ss;         /**< Spawn strategy values. */
  int *rs;         /**< Redistribution strategy values. */
  size_t ss_len;   /**< Length of @c ss. */
  size_t rs_len;   /**< Length of @c rs. */
  float factor;    /**< Time-scale factor applied to stage @c t_stage. */
} group_config_t;

/**
 * @brief Full Proteo configuration loaded from JSON/INI (or received over MPI).
 */
typedef struct
{
    size_t n_groups;  /**< Number of process groups (@c n_resizes + 1). */
    size_t n_resizes; /**< Number of resizes. */
    size_t n_phases;  /**< Number of application phases. */
    int rigid_times;      /**< Non-zero: barrier-based (rigid) iteration timing. */
    int capture_method;   /**< How to reduce per-iter times (::capture_methods). */
    size_t sdr;           /**< Synchronous redistribution element count. */
    size_t adr;           /**< Asynchronous redistribution element count. */
    size_t datasize;      /**< Bytes per redistribution element. */

    MPI_Datatype config_type;        /**< Derived type for scalar config fields. */
    MPI_Datatype group_type;         /**< Derived type for group scalars. */
    MPI_Datatype group_strats_type;  /**< Derived type for strategy arrays. */
    MPI_Datatype phase_type;         /**< Derived type for phase scalars. */
    MPI_Datatype stage_type;         /**< Derived type for stage scalars. */
    phase_t *phases;                 /**< Phase array. */
    group_config_t *groups;          /**< Group array. */
} configuration;

/**
 * @brief Runtime state for the current SAM process group.
 */
typedef struct {
  int myId;                      /**< Local MPI rank. */
  int numP;                      /**< Communicator size. */
  unsigned int grp;              /**< Current group index. */
  int argc;                      /**< Process argument count. */
  size_t sync_data_groups;       /**< Number of synchronous MaM data chunks. */
  size_t async_data_groups;      /**< Number of asynchronous MaM data chunks. */
  size_t start_phase;            /**< First phase index owned by this group. */
  size_t actual_phase;           /**< Current phase index. */
  size_t actual_iter;            /**< Current iteration within the phase. */
  int exec_iters;                /**< Iterations executed by this group so far. */

  char **argv;                   /**< Process arguments. */
  void **sync_array;             /**< Synchronous redistribution buffers. */
  void **async_array;            /**< Asynchronous redistribution buffers. */
  size_t *sync_qty;              /**< Element counts for @c sync_array entries. */
  size_t *async_qty;             /**< Element counts for @c async_array entries. */
  group_config_t grp_config;     /**< Copy of this group's configuration entry. */
} group_data;

#endif
