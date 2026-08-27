#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "Spawn_state.h"

/**
 * @file Spawn_state.c
 * @brief Implementation of spawn-state synchronisation for the threading strategy.
 *
 * Allows the two threads of a process to coordinate spawn progress checks.
 */

pthread_mutex_t spawn_mutex;
pthread_cond_t spawn_cond, completion_cond;
int spawn_state;
int waiting_redistribution = 0, waiting_completion = 0;

/**
 * @brief Initialise mutex/condition variables and set state to @c MAM_I_NOT_STARTED.
 *
 * The literal @c 1 passed to ::set_spawn_state is the enum value of
 * @c MAM_I_NOT_STARTED (@c FIXME: should use the named constant).
 */
void init_spawn_state(void) {
  pthread_mutex_init(&spawn_mutex, NULL);
  pthread_cond_init(&spawn_cond, NULL);
  pthread_cond_init(&completion_cond, NULL);
  set_spawn_state(1, 0); // FIXME: First parameter should be MAM_I_NOT_STARTED
}

/**
 * @brief Destroy spawn mutex and condition variables.
 */
void free_spawn_state(void) {
  pthread_mutex_destroy(&spawn_mutex);
  pthread_cond_destroy(&spawn_cond);
  pthread_cond_destroy(&completion_cond);
}

/**
 * @brief Read the current spawn state.
 * @param[in] i_is_async Non-zero to lock the mutex while reading (async path).
 * @return Current spawn state value (@c mam_inner_states).
 */
int get_spawn_state(int i_is_async) {
  int value;
  if (i_is_async) {
    pthread_mutex_lock(&spawn_mutex);
    value = spawn_state;
    pthread_mutex_unlock(&spawn_mutex);
  } else {
    value = spawn_state;
  }
  return value;
}

/**
 * @brief Write the spawn state.
 * @param[in] i_value    New state (@c mam_inner_states).
 * @param[in] i_is_async Non-zero to lock the mutex while writing (async path).
 */
void set_spawn_state(int i_value, int i_is_async) {
  if (i_is_async) {
    pthread_mutex_lock(&spawn_mutex);
    spawn_state = i_value;
    pthread_mutex_unlock(&spawn_mutex);
  } else {
    spawn_state = i_value;
  }
}

/**
 * @brief Block until redistribution may proceed (async path).
 * @return Spawn state after being woken (@c get_spawn_state with async lock).
 */
int wait_redistribution(void) {
  pthread_mutex_lock(&spawn_mutex);
  if (!waiting_redistribution) {
    waiting_redistribution = 1;
    pthread_cond_wait(&spawn_cond, &spawn_mutex);
  }
  waiting_redistribution = 0;
  pthread_mutex_unlock(&spawn_mutex);
  return get_spawn_state(1);
}

/**
 * @brief Signal a waiter blocked in ::wait_redistribution.
 */
void wakeup_redistribution(void) {
  pthread_mutex_lock(&spawn_mutex);
  if (waiting_redistribution) {
    pthread_cond_signal(&spawn_cond);
  }
  waiting_redistribution = 1;
  pthread_mutex_unlock(&spawn_mutex);
}

/**
 * @brief Block until spawn completion may be observed (async path).
 * @return Spawn state after being woken.
 */
int wait_completion(void) {
  pthread_mutex_lock(&spawn_mutex);
  if (!waiting_completion) {
    waiting_completion = 1;
    pthread_cond_wait(&completion_cond, &spawn_mutex);
  }
  waiting_completion = 0;
  pthread_mutex_unlock(&spawn_mutex);
  return get_spawn_state(1);
}

/**
 * @brief Signal a waiter blocked in ::wait_completion.
 */
void wakeup_completion(void) {
  pthread_mutex_lock(&spawn_mutex);
  if (waiting_completion) {
    pthread_cond_signal(&completion_cond);
  }
  waiting_completion = 1;
  pthread_mutex_unlock(&spawn_mutex);
}
