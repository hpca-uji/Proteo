#ifndef MAM_INIT_CONFIGURATION_H
#define MAM_INIT_CONFIGURATION_H

/**
 * @file MAM_Init_Configuration.h
 * @brief Internal helpers to initialise and validate MaM configuration (not in @c MAM.h).
 */

#include <mpi.h>
#include "MAM_Constants.h"

/**
 * @brief Allocate/default the configuration table used at MaM start-up.
 */
void MAM_Init_configuration(void);

/**
 * @brief Apply environment defaults and initial method/strategy values.
 */
void MAM_Set_initial_configuration(void);

/**
 * @brief Validate and normalise incompatible strategy combinations.
 */
void MAM_Check_configuration(void);

#endif
