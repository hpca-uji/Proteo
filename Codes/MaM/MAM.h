#ifndef MAM_H
#define MAM_H

/**
 * @file MAM.h
 * @brief Public umbrella header for the MaM malleability library.
 *
 * Include this header to access the application-facing API: constants,
 * lifecycle/checkpoint (::MAM_Manager.h), configuration
 * (::MAM_Configuration.h), and timing retrieval (::MAM_Times_retrieve.h).
 * Internal modules (spawn_methods, distribution_methods, RMS, zombies, …)
 * are not part of this public include set.
 */

#include "MAM_Constants.h"
#include "MAM_Manager.h"
#include "MAM_Configuration.h"
#include "MAM_Times_retrieve.h"

#endif
