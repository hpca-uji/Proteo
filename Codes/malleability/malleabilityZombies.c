#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>
#include "malleabilityZombies.h"

#define PIDS_QTY 320
//TODO Add option to allow the usage of signal USR2 or not.
//This code asumes ROOT of each group will be the last to be zombified
//

void MAM_I_zombies_collect(int new_zombies);
void MAM_I_zombies_split();
void MAM_I_zombies_suspend();
int MAM_I_zombies_awake();
void zombies_handler_usr2() {}

int *pids = NULL;
int zombies_qty = 0;


void MAM_Zombies_service_init() {
  zombies_qty = 0;
  pids = malloc(PIDS_QTY * sizeof(int));

  for(int i=0; i<PIDS_QTY; i++) {
    pids[i] = 0;
  }
}

int MAM_Zombies_service_free() {
  int request_abort = MAM_I_zombies_awake();
  free(pids);
  return request_abort;
}


void MAM_Zombies_update() {
  int myId, numP, new_zombies;

  MPI_Comm_rank(mall->original_comm, &myId);
  MPI_Comm_size(mall->original_comm, &numP);

  MPI_Allreduce(&mall->zombie, &new_zombies, 1, MPI_INT, MPI_SUM, mall->original_comm);
  if(new_zombies && new_zombies < numP) {
    MAM_I_zombies_collect(new_zombies);
    MAM_I_zombies_split();
    MAM_I_zombies_suspend();
    if(myId == MALLEABILITY_ROOT) zombies_qty += new_zombies;
  }
}

void MAM_I_zombies_collect(int new_zombies) {
  int pid = getpid();
  int *pids_counts, *pids_displs;
  int i, count, active;
  int myId, numP;

  MPI_Comm_rank(mall->original_comm, &myId);
  MPI_Comm_size(mall->original_comm, &numP);
  pids_counts = (int *) malloc(numP * sizeof(int));
  pids_displs = (int *) malloc(numP * sizeof(int));

  #if USE_MAL_DEBUG > 2
    if(myId == MALLEABILITY_ROOT){ DEBUG_FUNC("Collecting zombies", mall->myId, mall->numP); } fflush(stdout);
  #endif

  count = mall->zombie;
  if(myId == MALLEABILITY_ROOT) {
    active = numP - new_zombies;
    for(i=0; i < active; i++) {
      pids_counts[i] = 0;
    }
    pids_displs[i-1] = -1;
    for(; i< active+new_zombies; i++) {
      pids_counts[i] = 1;
      pids_displs[i] = (pids_displs[i-1] + 1) + zombies_qty;
    }
  }
  MPI_Gatherv(&pid, count, MPI_INT, pids, pids_counts, pids_displs, MPI_INT, MALLEABILITY_ROOT, mall->original_comm);
  free(pids_counts);
  free(pids_displs);
}

void MAM_I_zombies_split() {
  int myId, color;
  MPI_Comm new_original_comm;

  MPI_Comm_rank(mall->original_comm, &myId);
  color = mall->zombie ? MPI_UNDEFINED : 1;
  MPI_Comm_split(mall->original_comm, color, myId, &new_original_comm);

  if(mall->original_comm != MPI_COMM_WORLD) MPI_Comm_free(&mall->original_comm);
  MPI_Comm_set_name(new_original_comm, "MAM_ORIGINAL");
  mall->original_comm = new_original_comm;
}

void MAM_I_zombies_suspend() {
  struct sigaction act;
  if(!mall->zombie) return;

  sigemptyset(&act.sa_mask);
  act.sa_flags=0;
  act.sa_handler=zombies_handler_usr2;

  sigaction(SIGUSR2, &act, NULL);

  sigset_t set;
  sigprocmask(SIG_SETMASK,NULL,&set);

  sigsuspend(&set);
}

int MAM_I_zombies_awake() {
  if(mall->internode_group && zombies_qty) return 1; //Request Abort
  for(int i=0; i < zombies_qty; i++) { // Despertar a los zombies
    kill(pids[i], SIGUSR2);
  }
  zombies_qty = 0;
  return 0; //Normal termination
}
