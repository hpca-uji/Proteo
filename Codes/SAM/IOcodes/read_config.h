#ifndef READ_CONFIG_H
#define READ_CONFIG_H

/**
 * @file read_config.h
 * @brief Shared types for SAM configuration readers (INI and JSON).
 *
 * Callers supply allocation callbacks used once the reader knows how many
 * groups, phases, or stages to allocate inside a ::configuration.
 */

#include "Main_datatypes.h"

/**
 * @brief Callback that allocates configuration arrays sized from global counts.
 * @param[in,out] io_user_config Configuration whose counts are already set
 *                               (e.g. @c n_groups or @c n_phases); the callback
 *                               allocates the corresponding arrays.
 */
typedef void (*Malloc_conf)(configuration *io_user_config);

/**
 * @brief Callback that allocates stages for one phase.
 * @param[in,out] io_user_config Configuration being filled.
 * @param[in]     i_index        Phase index whose @c stages array must be allocated.
 */
typedef void (*Malloc_conf_index)(configuration *io_user_config, size_t i_index);

/**
 * @brief Bundle of allocation callbacks used by INI/JSON readers.
 *
 * @c resizes_f is invoked after @c Total_Resizes is known (allocates groups).
 * @c phases_f is invoked after @c Total_Phases is known (allocates phases).
 * @c stages_f is invoked after a phase's @c Total_Stages is known.
 */
typedef struct {
  Malloc_conf resizes_f;       /**< Allocate @c groups from @c n_groups. */
  Malloc_conf phases_f;        /**< Allocate @c phases from @c n_phases. */
  Malloc_conf_index stages_f;  /**< Allocate @c stages for phase @c i_index. */
} ext_functions_t;

#endif
