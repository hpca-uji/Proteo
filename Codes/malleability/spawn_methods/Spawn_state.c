#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

pthread_mutex_t spawn_mutex;
pthread_cond_t spawn_cond;
int spawn_state;

void init_spawn_state() {
  pthread_mutex_init(&spawn_mutex,NULL);
  pthread_cond_init(&spawn_cond,NULL);
}

void free_spawn_state() {
  pthread_mutex_destroy(&spawn_mutex);
  pthread_cond_destroy(&spawn_cond);
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

int wait_wakeup() {
  pthread_cond_wait(&spawn_cond, &spawn_mutex);
  return get_spawn_state(1);
}

void wakeup() {
  pthread_cond_signal(&spawn_cond);
}
