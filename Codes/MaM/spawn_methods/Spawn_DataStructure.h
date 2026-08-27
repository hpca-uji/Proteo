#ifndef MAM_SPAWN_DATASTRUCTURE_H
#define MAM_SPAWN_DATASTRUCTURE_H

/**
 * @file Spawn_DataStructure.h
 * @brief Core data types for MaM process spawning and remapping.
 *
 * Terminology used throughout spawn_methods:
 * - Sources: ranks that exist before the reconfiguration (all sources are also parents).
 * - Parents: the spawn / parent side of an intercommunicator (sources).
 * - Children: ranks created in this reconfiguration via spawn (always targets).
 * - Targets: ranks that continue after the reconfiguration (children plus reused
 *   sources under Merge; a process can be a target without being a child).
 */

#include <mpi.h>

/**
 * @brief One MPI_Comm_spawn invocation (command, count, and mapping info).
 */
typedef struct {
  int spawn_qty;      /**< Number of processes to create in this set. */
  char *cmd;          /**< Executable / wrapper command for the spawn. */
  MPI_Info mapping;   /**< MPI_Info describing host/hostfile placement. */
} Spawn_set;

/**
 * @brief MPI port and (optional) published service names for group discovery.
 */
typedef struct {
  int opened_port;       /**< Non-zero if this rank opened the local port. */
  char *port_name;       /**< Local port name (or placeholder for non-openers). */
  char *service_name;    /**< Published service name for this group's port, if any. */
  char *remote_port;     /**< Looked-up remote port name for connect. */
  char *remote_service;  /**< Service name used for remote lookup. */
} Spawn_ports;

/**
 * @brief Full spawn configuration and communicators for one reconfiguration.
 */
typedef struct {
  int spawn_qty;    /**< Processes to spawn in the current operation. */
  int initial_qty;  /**< Process count before the reconfiguration (sources). */
  int target_qty;   /**< Desired process count after the reconfiguration (targets). */
  int already_created; /**< Processes already present when building the new mapping
                            (@c 0 for full-spawn paths; @c initial_qty under Merge reuse). */
  int total_spawns; /**< Number of spawn sets / groups to create. */
  int spawn_is_single;     /**< Non-zero if Single strategy is active. */
  int spawn_is_async;      /**< Non-zero if spawn runs asynchronously (threading). */
  int spawn_is_intercomm;  /**< Non-zero if an intercommunicator spawn path is used. */
  int spawn_is_multiple;   /**< Non-zero if Multiple strategy is active. */
  int spawn_is_parallel;   /**< Non-zero if Parallel strategy is active. */
  int mapping_fill_method; /**< How mapping is filled: @c MAM_PHY_TYPE_STRING or @c MAM_PHY_TYPE_HOSTFILE. */

  MPI_Comm comm;          /**< Communicator among sources for this spawn (sources only). */
  MPI_Comm returned_comm; /**< Communicator returned to the application (sources only). */
  Spawn_set *sets;        /**< Array of spawn sets (@c total_spawns entries). */
} Spawn_data;

#endif
