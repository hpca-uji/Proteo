#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>
#include "malleabilityZombies.h"

#define PIDS_QTY 320

void zombies_suspend();

int offset_pids, *pids = NULL;


void gestor_usr2() {}

void zombies_collect_suspended(MPI_Comm comm, int myId, int numP, int numC, int root) {
  int pid = getpid();
  int *pids_counts = malloc(numP * sizeof(int));
  int *pids_displs = malloc(numP * sizeof(int));
  int i, count=1;

  if(myId < numC) {
    count = 0;
    if(myId == root) {
      for(i=0; i < numC; i++) {
	pids_counts[i] = 0;
      }
      for(i=numC; i<numP; i++) {
  	pids_counts[i] = 1;
	pids_displs[i] = (i + offset_pids) - numC;
      }
      offset_pids += numP - numC;
      }
  }
  MPI_Gatherv(&pid, count, MPI_INT, pids, pids_counts, pids_displs, MPI_INT, root, comm);
  free(pids_counts);
  free(pids_displs);

  if(myId >= numC) {
    zombies_suspend();
  }
}

void zombies_service_init() {
  offset_pids = 0;
  pids = malloc(PIDS_QTY * sizeof(int));

  for(int i=0; i<PIDS_QTY; i++) {
    pids[i] = 0;
  }
}

void zombies_service_free() {
  free(pids);
}

void zombies_suspend() {
  struct sigaction act;

  sigemptyset(&act.sa_mask);
  act.sa_flags=0;
  act.sa_handler=gestor_usr2;

  sigaction(SIGUSR2, &act, NULL);

  sigset_t set;
  sigprocmask(SIG_SETMASK,NULL,&set);

  sigsuspend(&set);
}

void zombies_awake() {
  for(int i=0; i < offset_pids; i++) { // Despertar a los zombies
    kill(pids[i], SIGUSR2);
  }
}
