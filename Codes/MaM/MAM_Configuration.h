#ifndef MAM_CONFIGURATION_H
#define MAM_CONFIGURATION_H

/**
 * @file MAM_Configuration.h
 * @brief Public API to set MaM spawn/redistribution methods and strategy bitmasks.
 */

#include <mpi.h>
#include "MAM_Constants.h"

/** @brief Cleared strategy bitmask value. */
#define MAM_STRAT_CLEAR_VALUE 0

/** @brief ::MAM_Set_key_configuration: key was newly added. */
#define MAM_STRATS_ADDED 1
/** @brief ::MAM_Set_key_configuration: key replaced an existing value. */
#define MAM_STRATS_MODIFIED 2

/** @name Spawn strategy bitmasks (OR into @c spawn_strategies) */
/**@{*/
#define MAM_MASK_PTHREAD 0x01
#define MAM_MASK_SPAWN_SINGLE 0x02
#define MAM_MASK_SPAWN_INTERCOMM 0x04
#define MAM_MASK_SPAWN_MULTIPLE 0x08
#define MAM_MASK_SPAWN_PARALLEL 0x10
/**@}*/

/** @name Redistribution strategy bitmasks (OR into @c red_strategies) */
/**@{*/
#define MAM_MASK_RED_WAIT_SOURCES 0x02
#define MAM_MASK_RED_WAIT_TARGETS 0x04
/**@}*/

/**
 * @brief Test whether a strategy ordinal is set in the current configuration.
 *
 * @param[in]  i_key       @c MAM_SPAWN_STRATEGIES or @c MAM_RED_STRATEGIES.
 * @param[in]  i_strategy  Strategy enum ordinal (::mam_spawn_strategies / ::mam_red_strategies).
 * @param[out] o_result    Optional; receives non-zero if contained (may be @c NULL).
 * @return Non-zero if the strategy bit is set, 0 otherwise.
 */
int MAM_Contains_strat(int i_key, unsigned int i_strategy, int *o_result);

/**
 * @brief Set spawn/red methods and strategy bitmasks in one call.
 *
 * Method arguments use the corresponding enums; strategy arguments are bitmasks
 * (@c MAM_MASK_*). Must be called before malleability starts (@c MAM_NOT_STARTED).
 *
 * @param[in] i_spawn_method      ::mam_spawn_methods.
 * @param[in] i_spawn_strategies  Spawn strategy bitmask.
 * @param[in] i_spawn_dist        ::mam_phy_dist_methods.
 * @param[in] i_red_method        ::mam_redistribution_methods.
 * @param[in] i_red_strategies    Redistribution strategy bitmask.
 */
void MAM_Set_configuration(int i_spawn_method, int i_spawn_strategies, int i_spawn_dist,
                           int i_red_method, int i_red_strategies);

/**
 * @brief Set one configuration key from ::mam_key_values.
 *
 * @param[in]  i_key       Configuration key.
 * @param[in]  i_required  Value to store (method enum or strategy bitmask / target count).
 * @param[out] o_provided  Optional; @c MAM_STRATS_ADDED or @c MAM_STRATS_MODIFIED.
 */
void MAM_Set_key_configuration(int i_key, int i_required, int *o_provided);

/**
 * @brief Set the desired number of target processes for the next reconfiguration.
 * @param[in] i_numC Target process count.
 * @return @c MAM_OK or @c MAM_DENIED.
 */
int MAM_Set_target_number(unsigned int i_numC);

/**
 * @brief Enable/disable Valgrind wrapper for spawned processes.
 * @param[in] i_flag Non-zero to enable.
 */
void MAM_Use_valgrind(int i_flag);

/**
 * @brief Enable/disable Extrae wrapper for spawned processes.
 * @param[in] i_flag Non-zero to enable.
 */
void MAM_Use_extrae(int i_flag);
#endif
