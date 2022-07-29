#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

pthread_mutex_t spawn_mutex;
int spawn_state;

void init_spawn_state() {
  pthread_mutex_init(&spawn_mutex,NULL);
}

void free_spawn_state() {
  pthread_mutex_destroy(&spawn_mutex);
}

int get_spawn_state(int is_async) {
  int value;
  if(is_async) {
    pthread_mutex_lock(&(spawn_data->spawn_mutex));
    value = spawn_state;
    pthread_mutex_unlock(&(spawn_data->spawn_mutex));
  } else {
    value = spawn_state;
  }
  return value
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
