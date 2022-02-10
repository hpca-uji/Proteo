#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
//#include <slurm/slurm.h>
#include <signal.h>

void zombies_collect_suspended(MPI_Comm comm, int myId, int numP, int numC, int root, void *results_void, MPI_Comm user_comm);
void zombies_service_init();
void zombies_service_free();
void zombies_awake();
