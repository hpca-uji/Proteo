#ifndef MALLEABILITY_SPAWN_STATE_H
#define MALLEABILITY_SPAWN_STATE_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void init_spawn_state();
void free_spawn_state();

int get_spawn_state(int is_async);
void set_spawn_state(int value, int is_async);

int wait_redistribution();
void wakeup_redistribution();

int wait_completion();
void wakeup_completion();

#endif
