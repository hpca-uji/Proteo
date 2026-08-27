#ifndef MAM_CONSTANTS_H
#define MAM_CONSTANTS_H

/**
 * @file MAM_Constants.h
 * @brief Public constants, return codes, and configuration enums for MaM.
 */

/** @brief Generic failure / denied return code. */
#define MAM_DENIED -1
/** @brief Generic success return code. */
#define MAM_OK 0

/**
 * @brief Coarse public reconfiguration states visible to applications.
 */
enum mam_states {
  MAM_UNRESERVED,   /**< Unused / unset. */
  MAM_NOT_STARTED,  /**< No malleability operation in progress. */
  MAM_PENDING,      /**< Reconfiguration still running. */
  MAM_USER_PENDING, /**< Waiting for the user callback / user phase. */
  MAM_COMPLETED     /**< Reconfiguration finished. */
};

/**
 * @brief Per-rank role after a reconfiguration (see ::mam_user_reconf_t).
 */
enum mam_proc_states {
  MAM_PROC_CONTINUE, /**< Surviving source / continuing target. */
  MAM_PROC_NEW_RANK, /**< Newly created child (target). */
  MAM_PROC_ZOMBIE    /**< Rank to be suspended (Merge shrink). */
};

/**
 * @brief Spawn methods passed to ::MAM_Set_configuration.
 */
enum mam_spawn_methods {
  MAM_SPAWN_BASELINE,      /**< Spawn all required children (Baseline). */
  MAM_SPAWN_MERGE,         /**< Reuse sources as targets when possible (Merge). */
  MAM_METHODS_SPAWN_LEN    /**< Sentinel / count of spawn methods. */
};

/**
 * @brief Spawn strategy ordinals (mapped to @c MAM_MASK_SPAWN_* bitmasks in Configuration).
 *
 * Strategy arguments to ::MAM_Set_configuration are bitmasks; these enum values
 * identify which strategy to test via ::MAM_Contains_strat.
 */
enum mam_spawn_strategies {
  MAM_STRAT_SPAWN_CLEAR,      /**< No spawn strategy bits. */
  MAM_STRAT_SPAWN_PTHREAD,    /**< Asynchronous spawn on a helper thread. */
  MAM_STRAT_SPAWN_SINGLE,     /**< Single-root spawn strategy. */
  MAM_STRAT_SPAWN_INTERCOMM,  /**< Prefer intercommunicator spawn layout. */
  MAM_STRAT_SPAWN_MULTIPLE,   /**< Multiple (per-node) spawn strategy. */
  MAM_STRAT_SPAWN_PARALLEL,   /**< Parallel cascading spawn strategy. */
  MAM_STRATS_SPAWN_LEN        /**< Sentinel / count. */
};

/**
 * @brief How newly spawned children are placed across nodes (starts at 1).
 */
enum mam_phy_dist_methods {
  MAM_PHY_DIST_SPREAD = 1,                 /**< Spread children across nodes. */
  MAM_PHY_DIST_COMPACT,                    /**< Fill nodes compactly. */
  MAM_METHODS_PHYSICAL_DISTRIBUTION_LEN    /**< Sentinel / count. */
};

/**
 * @brief How MPI_Info host mapping is expressed (starts at 1).
 */
enum mam_phy_info_methods {
  MAM_PHY_TYPE_STRING = 1,  /**< Hosts string in MPI_Info. */
  MAM_PHY_TYPE_HOSTFILE     /**< Hostfile path in MPI_Info. */
};

/**
 * @brief Data redistribution methods.
 */
enum mam_redistribution_methods {
  MAM_RED_BASELINE,       /**< Alltoallv-style baseline. */
  MAM_RED_POINT,          /**< Point-to-point redistribution. */
  MAM_RED_RMA_LOCK,       /**< RMA with per-target lock. */
  MAM_RED_RMA_LOCKALL,    /**< RMA with lock-all. */
  MAM_METHODS_RED_LEN     /**< Sentinel / count. */
};

/**
 * @brief Redistribution strategy ordinals (mapped to @c MAM_MASK_RED_* bitmasks).
 */
enum mam_red_strategies {
  MAM_STRAT_RED_CLEAR,         /**< No red strategy bits. */
  MAM_STRAT_RED_PTHREAD,       /**< Async redistribution on a helper thread. */
  MAM_STRAT_RED_WAIT_SOURCES,  /**< Wait for sources before continuing. */
  MAM_STRAT_RED_WAIT_TARGETS,  /**< Wait for targets (e.g. before disconnect). */
  MAM_STRATS_RED_LEN           /**< Sentinel / count. */
};

/**
 * @brief Keys for per-option configuration (::MAM_Set_key_configuration / env).
 */
enum mam_key_values {
  MAM_SPAWN_METHOD = 0,
  MAM_SPAWN_STRATEGIES,
  MAM_PHYSICAL_DISTRIBUTION,
  MAM_RED_METHOD,
  MAM_RED_STRATEGIES,
  MAM_NUM_TARGETS,
  MAM_KEY_COUNT
};

/** @name Environment variable names for default configuration */
/**@{*/
#define MAM_SPAWN_METHOD_ENV                 "MAM_SPAWN_METHOD"
#define MAM_SPAWN_STRATS_ENV                 "MAM_SPAWN_STRATS"
#define MAM_PHYSICAL_DISTRIBUTION_METHOD_ENV "MAM_PHYSICAL_DISTRIBUTION_METHOD"
#define MAM_RED_METHOD_ENV                   "MAM_RED_METHOD"
#define MAM_RED_STRATS_ENV                   "MAM_RED_STRATS"
#define MAM_NUM_TARGETS_ENV                  "MAM_NUM_TARGETS"
/**@}*/

/** @brief ::MAM_Checkpoint: poll without blocking until done. */
#define MAM_CHECK_COMPLETION 0
/** @brief ::MAM_Checkpoint: wait until the spawn/redistribution step completes. */
#define MAM_WAIT_COMPLETION 1

/** @brief Role flag: source / parent side of a reconfiguration. Logical/Boolean of FALSE. */
#define MAM_SOURCES 0
/** @brief Role flag: target side of a reconfiguration. Logical/Boolean of TRUE. */
#define MAM_TARGETS 1

/** @name Data-registry selectors for ::MAM_Data_add / ::MAM_Data_get_* */
/**@{*/
#define MAM_DATA_DISTRIBUTED 0  /**< Block-distributed data (not replicated). */
#define MAM_DATA_REPLICATED 1   /**< Same buffer on every rank. */
#define MAM_DATA_VARIABLE 0     /**< May change size/layout across reconfigs. */
#define MAM_DATA_CONSTANT 1     /**< Layout treated as constant. */
/**@}*/

/** @name MPI tags used by Single / Multiple spawn strategies */
/**@{*/
#define MAM_MPITAG_STRAT_SINGLE 130
#define MAM_MPITAG_STRAT_MULTIPLE 131
/**@}*/

#endif
