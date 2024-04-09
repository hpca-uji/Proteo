#ifndef MAM_CONFIGURATION_H
#define MAM_CONFIGURATION_H

#include <mpi.h>
#include "malleabilityStates.h"

#define MAM_STRAT_CLEAR_VALUE 0

#define MAM_STRATS_ADDED 1
#define MAM_STRATS_MODIFIED 2

#define MAM_MASK_PTHREAD 0x01
#define MAM_MASK_SPAWN_SINGLE 0x02
#define MAM_MASK_SPAWN_INTERCOMM 0x04
#define MAM_MASK_SPAWN_MULTIPLE 0x08
#define MAM_MASK_RED_WAIT_SOURCES 0x02
#define MAM_MASK_RED_WAIT_TARGETS 0x04

int MAM_Contains_strat(int key, unsigned int strategy, int *result);
void MAM_Set_configuration(int spawn_method, int spawn_strategies, int spawn_dist, int red_method, int red_strategies);
void MAM_Set_key_configuration(int key, int required, int *provided);
int MAM_Set_target_number(unsigned int numC);

#endif
