#ifndef MAM_DATA_STRUCTURES_H
#define MAM_DATA_STRUCTURES_H

/**
 * @file MAM_DataStructures.h
 * @brief Internal MaM globals: config, process state, communicators, and timing.
 */

#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <pthread.h>
#include "MAM_Constants.h"

/**
 * @brief Debug printf with rank, file, function, and line.
 * @param debug_string Message text.
 * @param rank         Local rank label.
 * @param numP         Communicator size label.
 */
#define DEBUG_FUNC(debug_string, rank, numP) printf("MaM [P%d/%d]: %s -- %s:%s:%d\n", rank, numP, debug_string, __FILE__, __func__, __LINE__)

/** @brief Default root rank used by MaM collectives and spawn helpers. */
#define MAM_ROOT 0

/**
 * @brief Fine-grained internal state machine (distinct from public ::mam_states).
 */
enum mam_inner_states {
  MAM_I_UNRESERVED,
  MAM_I_NOT_STARTED,
  MAM_I_RMS_COMPLETED,
  MAM_I_SPAWN_PENDING,
  MAM_I_SPAWN_SINGLE_PENDING,
  MAM_I_SPAWN_SINGLE_COMPLETED,
  MAM_I_SPAWN_ADAPT_POSTPONE,
  MAM_I_SPAWN_COMPLETED,
  MAM_I_DIST_PENDING,
  MAM_I_DIST_COMPLETED,
  MAM_I_SPAWN_ADAPT_PENDING,
  MAM_I_USER_START,
  MAM_I_USER_PENDING,
  MAM_I_USER_COMPLETED,
  MAM_I_SPAWN_ADAPTED,
  MAM_I_COMPLETED
};

/** @brief @c external_usage: wrap spawn with Valgrind. */
#define MAM_USE_VALGRIND 1
/** @brief @c external_usage: wrap spawn with Extrae. */
#define MAM_USE_EXTRAE 2

#define MAM_VALGRIND_SCRIPT "./worker_valgrind.sh"
#define MAM_EXTRAE_SCRIPT "./worker_extrae.sh"

/**
 * @brief Wall-clock timestamps and durations for one reconfiguration.
 *
 * ::MAM_Retrieve_times returns five duration fields derived from these
 * scalars; the MPI datatype packs those durations (not every start/end).
 */
typedef struct {
  double spawn_start;        /**< Spawn phase start time. */
  double spawn_time;         /**< Spawn phase duration. */
  double sync_start;         /**< Sync redistribution start. */
  double sync_end;           /**< Sync redistribution end. */
  double async_start;        /**< Async redistribution start. */
  double async_end;          /**< Async redistribution end. */
  double user_start;         /**< User-callback phase start. */
  double user_end;           /**< User-callback phase end. */
  double malleability_start; /**< Whole malleability operation start. */
  double malleability_end;   /**< Whole malleability operation end. */

  MPI_Datatype times_type;   /**< Derived type for broadcasting selected fields. */
} malleability_times_t;

/**
 * @brief Runtime configuration (methods, strategy bitmasks, timing pointer).
 */
typedef struct {
  unsigned int spawn_method;      /**< ::mam_spawn_methods value. */
  unsigned int spawn_dist;        /**< ::mam_phy_dist_methods value. */
  unsigned int spawn_strategies;  /**< Bitmask of spawn strategies (@c MAM_MASK_SPAWN_*). */
  unsigned int red_method;        /**< ::mam_redistribution_methods value. */
  unsigned int red_strategies;    /**< Bitmask of red strategies (@c MAM_MASK_RED_*). */

  int external_usage; /**< 0 = normal exec; @c MAM_USE_VALGRIND / @c MAM_USE_EXTRAE. */

  malleability_times_t *times; /**< Timing buffers for the current operation. */
} malleability_config_t;

/**
 * @brief Per-process MaM runtime state (ranks, communicators, node mapping).
 */
typedef struct {
  int myId;             /**< Rank in @c comm. */
  int numP;             /**< Size of @c comm (sources' group size before/during spawn). */
  int numC;             /**< Target / children group size for this reconfiguration. */
  int zombie;           /**< Non-zero if this rank is a Merge-shrink zombie. */
  int root;             /**< Local root rank. */
  int root_collectives; /**< Root used for intercomm broadcasts to children. */
  int num_parents;      /**< Size of the parents group (children view). */
  int root_parents;     /**< Parents' root rank on the intercomm. */
  int gid;              /**< Parallel-spawn group id (not thread-safe when written during spawn). */
  pthread_t async_thread; /**< Helper thread for async spawn/redistribution. */
  MPI_Comm comm;          /**< Main MaM intracomm. */
  MPI_Comm thread_comm;   /**< Duplicate used by async threading paths. */
  MPI_Comm original_comm; /**< Application communicator saved at init. */
  MPI_Comm intercomm;     /**< Parent–children intercommunicator when active. */
  MPI_Comm tmp_comm;      /**< Scratch communicator. */
  MPI_Comm *user_comm;    /**< Pointer to the application's communicator (updated in place). */
  MPI_Datatype struct_type; /**< Derived type packing config + key mall fields. */

  int wait_targets_posted;  /**< Non-zero if @c WAIT_TARGETS request was posted. */
  MPI_Request wait_targets; /**< Outstanding wait-targets request. */

  char *name_exec;   /**< Executable name for spawn. */
  char *nodelist;    /**< Packed host/node list string. */
  int num_nodes;     /**< Number of nodes in the allocation. */
  int nodelist_len;  /**< Length of @c nodelist including NUL. */
  int *max_cpus;     /**< Per-node core capacity. */
  int *assigned_cpus; /**< Per-node ranks already assigned (sources). */
  int *spawned_cpus;  /**< Per-node ranks to spawn / occupancy after mapping. */
  int internode_group; /**< Non-zero if the job spans multiple nodes. */
} malleability_t;

extern malleability_config_t *mall_conf; /**< Global configuration singleton. */
extern malleability_t *mall;             /**< Global process-state singleton. */
extern int state;                        /**< Current ::mam_inner_states value. */

/**
 * @brief Build and commit the MPI datatype packing main config/process fields.
 */
void MAM_Def_main_datatype(void);

/**
 * @brief Free ::mall->struct_type if committed.
 */
void MAM_Free_main_datatype(void);

/**
 * @brief Broadcast main MaM structures (and nodelist/CPU arrays) from sources to targets.
 *
 * @param[in] i_comm      Inter- or intracomm used for the broadcast.
 * @param[in] i_rootBcast Root for the Bcast (@c MPI_ROOT / @c MPI_PROC_NULL on intercomm).
 */
void MAM_Comm_main_structures(MPI_Comm i_comm, int i_rootBcast);

/**
 * @brief Print names of the main MaM communicators (debug).
 */
void MAM_print_comms_state(void);

/**
 * @brief Replace @c mall->comm and @c mall->thread_comm with duplicates of @p i_comm.
 * @param[in] i_comm New intracomm among the continuing targets.
 */
void MAM_comms_update(MPI_Comm i_comm);

#endif
