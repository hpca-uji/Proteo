#ifndef MAM_SPAWN_STATE_H
#define MAM_SPAWN_STATE_H

/**
 * @file Spawn_state.h
 * @brief Thread-safe spawn state and condition variables for async (threading) spawn.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/**
 * @brief Initialise mutex/condition variables and set state to @c MAM_I_NOT_STARTED.
 */
void init_spawn_state(void);

/**
 * @brief Destroy spawn mutex and condition variables.
 */
void free_spawn_state(void);

/**
 * @brief Read the current spawn state.
 * @param[in] i_is_async Non-zero to lock the mutex while reading (async path).
 * @return Current spawn state value (@c mam_inner_states).
 */
int get_spawn_state(int i_is_async);

/**
 * @brief Write the spawn state.
 * @param[in] i_value    New state (@c mam_inner_states).
 * @param[in] i_is_async Non-zero to lock the mutex while writing (async path).
 */
void set_spawn_state(int i_value, int i_is_async);

/**
 * @brief Block until redistribution may proceed (async path).
 * @return Spawn state after being woken (@c get_spawn_state with async lock).
 */
int wait_redistribution(void);

/**
 * @brief Signal a waiter blocked in ::wait_redistribution.
 */
void wakeup_redistribution(void);

/**
 * @brief Block until spawn completion may be observed (async path).
 * @return Spawn state after being woken.
 */
int wait_completion(void);

/**
 * @brief Signal a waiter blocked in ::wait_completion.
 */
void wakeup_completion(void);

#endif
