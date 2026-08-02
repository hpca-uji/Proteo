#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <mpi.h>
#include <signal.h>
#include "MAM_Zombies.h"
#include "MAM_DataStructures.h"

/**
 * @file MAM_Zombies.c
 * @brief Implementation of Merge-shrink zombie suspend/awake via SIGTSTP/SIGCONT path.
 *
 * Assumes the root of each group is the last rank to be zombified.
 */

/** @brief Fixed PID table capacity (@c FIXME: should be sized at runtime). */
#define PIDS_QTY 1024
// TODO: Add option to allow the usage of signal USR2 or not.

void MAM_I_zombies_collect(int i_new_zombies);
void MAM_I_zombies_split(void);
void MAM_I_zombies_suspend(void);
int MAM_I_zombies_awake(void);
void zombies_handler_usr2() {}

int *pids = NULL;
int zombies_qty = 0;


/**
 * @brief Allocate the PID table for zombie tracking (call once at init).
 */
void MAM_Zombies_service_init(void) {
  int numP;
  MPI_Comm_size(mall->original_comm, &numP);
  if (numP > PIDS_QTY) {
    perror("MAM_Zombies.c: Surpassed PID_QTY, increase it?\nOnly increases memory usage");
    MPI_Abort(MPI_COMM_WORLD, -1);
  }

  zombies_qty = 0;
  pids = malloc(PIDS_QTY * sizeof(int));

  for (int i = 0; i < PIDS_QTY; i++) {
    pids[i] = 0;
  }
}

/**
 * @brief Awake suspended zombies and free the PID table.
 * @return Non-zero if abort is requested (internode zombies remain), else 0.
 */
int MAM_Zombies_service_free(void) {
  int request_abort = MAM_I_zombies_awake();
  free(pids);
  return request_abort;
}


/**
 * @brief Collect new zombies, split them out of @c original_comm, and suspend them.
 */
void MAM_Zombies_update(void) {
  int myId, numP, new_zombies;

  MPI_Comm_rank(mall->original_comm, &myId);
  MPI_Comm_size(mall->original_comm, &numP);

  MPI_Allreduce(&mall->zombie, &new_zombies, 1, MPI_INT, MPI_SUM, mall->original_comm);
  if (new_zombies && new_zombies < numP) {
    MAM_I_zombies_collect(new_zombies);
    MAM_I_zombies_split();
    MAM_I_zombies_suspend();
    if (myId == MAM_ROOT) zombies_qty += new_zombies;
  }
}

/**
 * @brief Gather PIDs of newly marked zombies onto the root.
 * @param[in] i_new_zombies Number of ranks with @c mall->zombie set in this round.
 */
void MAM_I_zombies_collect(int i_new_zombies) {
  int pid = getpid();
  int *pids_counts, *pids_displs;
  int i, count, active;
  int myId, numP;

  MPI_Comm_rank(mall->original_comm, &myId);
  MPI_Comm_size(mall->original_comm, &numP);
  pids_counts = (int *)malloc(numP * sizeof(int));
  pids_displs = (int *)malloc(numP * sizeof(int));

  #if MAM_DEBUG > 2
    if (myId == MAM_ROOT) { DEBUG_FUNC("Collecting zombies", mall->myId, mall->numP); } fflush(stdout);
  #endif

  count = mall->zombie;
  if (myId == MAM_ROOT) {
    active = numP - i_new_zombies;
    for (i = 0; i < active; i++) {
      pids_counts[i] = 0;
    }
    pids_displs[i - 1] = -1;
    for (; i < active + i_new_zombies; i++) {
      pids_counts[i] = 1;
      pids_displs[i] = (pids_displs[i - 1] + 1) + zombies_qty;
    }
  }
  MPI_Gatherv(&pid, count, MPI_INT, pids, pids_counts, pids_displs, MPI_INT, MAM_ROOT, mall->original_comm);
  free(pids_counts);
  free(pids_displs);
}

/**
 * @brief Split zombies out of @c mall->original_comm (undefined color).
 */
void MAM_I_zombies_split(void) {
  int myId, color;
  MPI_Comm new_original_comm;

  MPI_Comm_rank(mall->original_comm, &myId);
  color = mall->zombie ? MPI_UNDEFINED : 1;
  MPI_Comm_split(mall->original_comm, color, myId, &new_original_comm);

  if (mall->original_comm != MPI_COMM_WORLD) MPI_Comm_free(&mall->original_comm);
  if (new_original_comm != MPI_COMM_NULL) MPI_Comm_set_name(new_original_comm, "MAM_ORIGINAL");
  mall->original_comm = new_original_comm;
}

/**
 * @brief Suspend this rank if it is a zombie (@c sigsuspend until SIGUSR2).
 */
void MAM_I_zombies_suspend(void) {
  struct sigaction act;
  if (!mall->zombie) return;

  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  act.sa_handler = zombies_handler_usr2;

  sigaction(SIGUSR2, &act, NULL);

  sigset_t set;
  sigprocmask(SIG_SETMASK, NULL, &set);

  sigsuspend(&set);
}

/**
 * @brief Send SIGUSR2 to collected zombie PIDs (root side).
 * @return Non-zero if internode zombies remain (request abort), else 0.
 */
int MAM_I_zombies_awake(void) {
  if (mall->internode_group && zombies_qty) return 1; // Request Abort
  for (int i = 0; i < zombies_qty; i++) { // Wake zombies
    kill(pids[i], SIGUSR2);
  }
  zombies_qty = 0;
  return 0; // Normal termination
}
