#ifndef MALLEABILITY_ZOMBIES_H
#define MALLEABILITY_ZOMBIES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>

void zombies_collect_suspended(MPI_Comm comm, int myId, int numP, int numC, int root, void *results_void, size_t n_stages, int capture_method);
void zombies_service_init();
void zombies_service_free();
void zombies_awake();

#endif
