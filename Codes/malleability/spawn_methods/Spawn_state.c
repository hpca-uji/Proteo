#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "Spawn_state.h"

pthread_mutex_t spawn_mutex;
pthread_cond_t spawn_cond, completion_cond;
int spawn_state;
int waiting_redistribution=0, waiting_completion=0;

void init_spawn_state() {
  pthread_mutex_init(&spawn_mutex,NULL);
  pthread_cond_init(&spawn_cond,NULL);
  pthread_cond_init(&completion_cond,NULL);
  set_spawn_state(1,0); //FIXME First parameter is a horrible magical number
}

void free_spawn_state() {
  pthread_mutex_destroy(&spawn_mutex);
  pthread_cond_destroy(&spawn_cond);
  pthread_cond_destroy(&completion_cond);
}

int get_spawn_state(int is_async) {
  int value;
  if(is_async) {
    pthread_mutex_lock(&spawn_mutex);
    value = spawn_state;
    pthread_mutex_unlock(&spawn_mutex);
  } else {
    value = spawn_state;
  }
  return value;
}

void set_spawn_state(int value, int is_async) {
  if(is_async) {
    pthread_mutex_lock(&spawn_mutex);
    spawn_state = value;
    pthread_mutex_unlock(&spawn_mutex);
  } else {
    spawn_state = value;
  }
}

int wait_redistribution() {
  pthread_mutex_lock(&spawn_mutex);
  if(!waiting_redistribution) {
    waiting_redistribution=1;
    pthread_cond_wait(&spawn_cond, &spawn_mutex);
  }
  waiting_redistribution=0;
  pthread_mutex_unlock(&spawn_mutex);
  return get_spawn_state(1);
}

void wakeup_redistribution() {
  pthread_mutex_lock(&spawn_mutex);
  if(waiting_redistribution) {
    pthread_cond_signal(&spawn_cond);
  }
  waiting_redistribution=1;
  pthread_mutex_unlock(&spawn_mutex);
}

int wait_completion() {
  pthread_mutex_lock(&spawn_mutex);
  if(!waiting_completion) {
    waiting_completion=1;
    pthread_cond_wait(&completion_cond, &spawn_mutex);
  }
  waiting_completion=0;
  pthread_mutex_unlock(&spawn_mutex);
  return get_spawn_state(1);
}

void wakeup_completion() {
  pthread_mutex_lock(&spawn_mutex);
  if(waiting_completion) {
    pthread_cond_signal(&completion_cond);
  }
  waiting_completion=1;
  pthread_mutex_unlock(&spawn_mutex);
}
