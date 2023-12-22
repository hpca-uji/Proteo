#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "distribution_methods/block_distribution.h"
#include "CommDist.h"

//void prepare_redistribution(int qty, int myId, int numP, int numO, int is_children_group, int is_intercomm, char **recv, struct Counts *s_counts, struct Counts *r_counts);
void prepare_redistribution(int qty, int myId, int numP, int numO, int is_children_group, int is_intercomm, int is_sync, char **recv, struct Counts *s_counts, struct Counts *r_counts); //FIXME Choose name for is_sync
void check_requests(struct Counts s_counts, struct Counts r_counts, int red_strategies, MPI_Request **requests, size_t *request_qty);

void sync_point2point(char *send, char *recv, int is_intercomm, int myId, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm);
void sync_rma(char *send, char *recv, struct Counts r_counts, int tamBl, MPI_Comm comm, int red_method);
void sync_rma_lock(char *recv, struct Counts r_counts, MPI_Win win);
void sync_rma_lockall(char *recv, struct Counts r_counts, MPI_Win win);


void async_point2point(char *send, char *recv, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm, MPI_Request *requests);
void async_rma(char *send, char *recv, struct Counts r_counts, int tamBl, MPI_Comm comm, int red_method, MPI_Request *requests, MPI_Win *win);
void async_rma_lock(char *recv, struct Counts r_counts, MPI_Win win, MPI_Request *requests);
void async_rma_lockall(char *recv, struct Counts r_counts, MPI_Win win, MPI_Request *requests);

void perform_manual_communication(char *send, char *recv, int myId, struct Counts s_counts, struct Counts r_counts);

/*
 * Reserva memoria para un vector de hasta "qty" elementos.
 * Los "qty" elementos se disitribuyen entre los "numP" procesos
 * que llaman a esta funcion.
 */
void malloc_comm_array(char **array, int qty, int myId, int numP) {
    struct Dist_data dist_data;

    get_block_dist(qty, myId, numP, &dist_data);
    if( (*array = calloc(dist_data.tamBl, sizeof(char))) == NULL) {
      printf("Memory Error (Malloc Arrays(%d))\n", dist_data.tamBl); 
      exit(1); 
    }

/*
        int i;
	for(i=0; i<dist_data.tamBl; i++) {
	  (*array)[i] = '!' + i + dist_data.ini;
	}
	
        printf("P%d Tam %d String: %s\n", myId, dist_data.tamBl, *array);
*/
}

//================================================================================
//================================================================================
//========================SYNCHRONOUS FUNCTIONS===================================
//================================================================================
//================================================================================

/*
 * Performs a communication to redistribute an array in a block distribution.
 * In the redistribution is differenciated parent group from the children and the values each group indicates can be
 * different.
 *
 * - send (IN):  Array with the data to send. This data can not be null for parents.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - qty  (IN):  Sum of elements shared by all processes that will send data.
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - numP (IN):  Size of the local group. If it is a children group, this parameter must correspond to using
 *               "MPI_Comm_size(comm)". For the parents is not always the size obtained from "comm".
 * - numO (IN):  Amount of processes in the remote group. For the parents is the target quantity of processes after the 
 *               resize, while for the children is the amount of parents.
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - comm (IN):  Communicator to use to perform the redistribution.
 *
 * returns: An integer indicating if the operation has been completed(TRUE) or not(FALSE). //FIXME In this case is always true...
 */
int sync_communication(char *send, char **recv, int qty, int myId, int numP, int numO, int is_children_group, int red_method, MPI_Comm comm) {
    int is_intercomm, aux_comm_used = 0;
    struct Counts s_counts, r_counts;
    struct Dist_data dist_data;
    MPI_Comm aux_comm = MPI_COMM_NULL;

    /* PREPARE COMMUNICATION */
    MPI_Comm_test_inter(comm, &is_intercomm);
//    prepare_redistribution(qty, myId, numP, numO, is_children_group, is_intercomm, recv, &s_counts, &r_counts);
// TODO START REFACTOR POR DEFECTO USA SIEMPRE INTRACOMM
    prepare_redistribution(qty, myId, numP, numO, is_children_group, is_intercomm, 1, recv, &s_counts, &r_counts); //FIXME MAGICAL VALUE

    if(is_intercomm) {
      MPI_Intercomm_merge(comm, is_children_group, &aux_comm);
      aux_comm_used = 1;
    } else {
      aux_comm = comm;
    }
// FIXME END REFACTOR


    /* PERFORM COMMUNICATION */
    switch(red_method) {

      case MALL_RED_RMA_LOCKALL:
      case MALL_RED_RMA_LOCK:
        if(is_children_group) {
	  dist_data.tamBl = 0;
	} else {
          get_block_dist(qty, myId, numO, &dist_data);
	}
        sync_rma(send, *recv, r_counts, dist_data.tamBl, aux_comm, red_method);
	break;

      case MALL_RED_POINT:
        sync_point2point(send, *recv, is_intercomm, myId, s_counts, r_counts, aux_comm);
	break;
      case MALL_RED_BASELINE:
      default:
        MPI_Alltoallv(send, s_counts.counts, s_counts.displs, MPI_CHAR, *recv, r_counts.counts, r_counts.displs, MPI_CHAR, aux_comm);
	break;
    }

    if(aux_comm_used) {
      MPI_Comm_free(&aux_comm);
    } 
    freeCounts(&s_counts);
    freeCounts(&r_counts);
    return 1; //FIXME In this case is always true...
}

/*
 * Performs a series of blocking point2point communications to redistribute an array in a block distribution. 
 * It should be called after calculating how data should be redistributed.
 *
 * - send (IN):  Array with the data to send. This value can not be NULL for parents.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to 
 *               receive data. If the process receives data and is NULL, the behaviour is undefined.
 * - is_intercomm (IN): Indicates wether the communicator is an intercommunicator (TRUE) or an
 *               intracommunicator (FALSE).
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - s_counts (IN): Struct which describes how many elements will send this process to each children and 
 *               the displacements.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent 
 *               and the displacements.
 * - comm (IN):  Communicator to use to perform the redistribution.
 *
 */
void sync_point2point(char *send, char *recv, int is_intercomm, int myId, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm) {
    int i, j, init, end, total_sends;
    MPI_Request *sends;

    init = s_counts.idI;
    end = s_counts.idE;
    if(!is_intercomm && (s_counts.idI == myId || s_counts.idE == myId + 1)) {
      perform_manual_communication(send, recv, myId, s_counts, r_counts);

      if(s_counts.idI == myId) init = s_counts.idI+1;
      else end = s_counts.idE-1;
    }

    total_sends = end - init;
    j = 0;
    if(total_sends > 0) {
      sends = (MPI_Request *) malloc(total_sends * sizeof(MPI_Request));
    }
    for(i=init; i<end; i++) {
      sends[j] = MPI_REQUEST_NULL;
      MPI_Isend(send+s_counts.displs[i], s_counts.counts[i], MPI_CHAR, i, 99, comm, &(sends[j]));
      j++;
    }

    init = r_counts.idI;
    end = r_counts.idE;
    if(!is_intercomm) {
      if(r_counts.idI == myId) init = r_counts.idI+1;
      else if(r_counts.idE == myId + 1) end = r_counts.idE-1;
    }

    for(i=init; i<end; i++) {
      MPI_Recv(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, 99, comm, MPI_STATUS_IGNORE);
    }

    if(total_sends > 0) {
      MPI_Waitall(total_sends, sends, MPI_STATUSES_IGNORE);
    }
}

/*
 * Performs synchronous MPI-RMA operations to redistribute an array in a block distribution. Is should be called after calculating
 * how data should be redistributed
 *
 * - send (IN):  Array with the data to send. This value can be NULL for children.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - tamBl (IN): How many elements are stored in the parameter "send".
 * - comm (IN):  Communicator to use to perform the redistribution. Must be an intracommunicator as MPI-RMA requirements.
 * - red_method (IN): Type of data redistribution to use. In this case indicates the RMA operation(Lock or LockAll).
 *
 */
void sync_rma(char *send, char *recv, struct Counts r_counts, int tamBl, MPI_Comm comm, int red_method) {
  MPI_Win win;
  MPI_Win_create(send, (MPI_Aint)tamBl, sizeof(char), MPI_INFO_NULL, comm, &win);

  switch(red_method) {
    case MALL_RED_RMA_LOCKALL:
      sync_rma_lockall(recv, r_counts, win);
      break;
    case MALL_RED_RMA_LOCK:
      sync_rma_lock(recv, r_counts, win);
      break;
  }

  MPI_Win_free(&win);
}



/*
 * Performs a passive MPI-RMA data redistribution for a single array using the passive epochs Lock/Unlock.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - win (IN):   Window to use to perform the redistribution.
 *
 */
void sync_rma_lock(char *recv, struct Counts r_counts, MPI_Win win) {
  int i, target_displs;

  target_displs = r_counts.first_target_displs;
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, win);
    MPI_Get(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, target_displs, r_counts.counts[i], MPI_CHAR, win);
    MPI_Win_unlock(i, win);
    target_displs=0;
  }
}


/*
 * Performs a passive MPI-RMA data redistribution for a single array using the passive epochs Lockall/Unlockall.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - win (IN):   Window to use to perform the redistribution.
 *
 */
void sync_rma_lockall(char *recv, struct Counts r_counts, MPI_Win win) {
  int i, target_displs;

  target_displs = r_counts.first_target_displs;
  MPI_Win_lock_all(MPI_MODE_NOCHECK, win);
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    MPI_Get(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, target_displs, r_counts.counts[i], MPI_CHAR, win);
    target_displs=0;
  }
  MPI_Win_unlock_all(win);
}

//================================================================================
//================================================================================
//========================ASYNCHRONOUS FUNCTIONS==================================
//================================================================================
//================================================================================

/*
 * Performs a communication to redistribute an array in a block distribution with non-blocking MPI functions.
 * In the redistribution is differenciated parent group from the children and the values each group indicates can be
 * different.
 *
 * - send (IN):  Array with the data to send. This data can not be null for parents.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - qty  (IN):  Sum of elements shared by all processes that will send data.
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - numP (IN):  Size of the local group. If it is a children group, this parameter must correspond to using
 *               "MPI_Comm_size(comm)". For the parents is not always the size obtained from "comm".
 * - numO (IN):  Amount of processes in the remote group. For the parents is the target quantity of processes after the 
 *               resize, while for the children is the amount of parents.
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - comm (IN):  Communicator to use to perform the redistribution.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended. If the pointer 
 *               is null or not enough space has been reserved the pointer is allocated/reallocated.
 * - request_qty (OUT): Quantity of requests to be used. If a process sends and receives data, this value will be 
 *               modified to the expected value.
 *
 * returns: An integer indicating if the operation has been completed(TRUE) or not(FALSE). //FIXME In this case is always false...
 */
int async_communication_start(char *send, char **recv, int qty, int myId, int numP, int numO, int is_children_group, int red_method, int red_strategies, MPI_Comm comm, MPI_Request **requests, size_t *request_qty, MPI_Win *win) {
    int is_intercomm, aux_comm_used = 0;
    struct Counts s_counts, r_counts;
    struct Dist_data dist_data;
    MPI_Comm aux_comm = MPI_COMM_NULL;

    /* PREPARE COMMUNICATION */
    MPI_Comm_test_inter(comm, &is_intercomm);
// TODO START REFACTOR POR DEFECTO USA SIEMPRE INTRACOMM
    //prepare_redistribution(qty, myId, numP, numO, is_children_group, is_intercomm, recv, &s_counts, &r_counts);
    prepare_redistribution(qty, myId, numP, numO, is_children_group, is_intercomm, 1, recv, &s_counts, &r_counts); // TODO MAGICAL VALUE
    if(is_intercomm) {
      MPI_Intercomm_merge(comm, is_children_group, &aux_comm);
      aux_comm_used = 1;
    } else {
      aux_comm = comm;
    }
// FIXME END REFACTOR
    check_requests(s_counts, r_counts, red_strategies, requests, request_qty);

    /* PERFORM COMMUNICATION */
    switch(red_method) {

      case MALL_RED_RMA_LOCKALL:
      case MALL_RED_RMA_LOCK:
        if(is_children_group) {
	  dist_data.tamBl = 0;
	} else {
          get_block_dist(qty, myId, numO, &dist_data);
	}
        async_rma(send, *recv, r_counts, dist_data.tamBl, aux_comm, red_method, *requests, win);
	break;
      case MALL_RED_POINT:
        async_point2point(send, *recv, s_counts, r_counts, aux_comm, *requests);
	break;
      case MALL_RED_BASELINE:
      default:
        MPI_Ialltoallv(send, s_counts.counts, s_counts.displs, MPI_CHAR, *recv, r_counts.counts, r_counts.displs, MPI_CHAR, aux_comm, &((*requests)[0]));
	break;
    }

    /* POST REQUESTS CHECKS */
    if(malleability_red_contains_strat(red_strategies, MALL_RED_IBARRIER, NULL)) {
      if(!is_children_group && (is_intercomm || myId >= numO)) {
        MPI_Ibarrier(comm, &((*requests)[*request_qty-1]) ); //FIXME Not easy to read...
      }
    }

    if(aux_comm_used) {
      MPI_Comm_free(&aux_comm);
    } 

    freeCounts(&s_counts);
    freeCounts(&r_counts);
    return 0; //FIXME In this case is always false...
}

/*
 * Checks if a set of requests have been completed (1) or not (0).
 *
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - red_strategies (IN):
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 *
 * returns: An integer indicating if the operation has been completed(TRUE) or not(FALSE).
 */
int async_communication_check(int myId, int is_children_group, int red_strategies, MPI_Comm comm, MPI_Request *requests, size_t request_qty) {
  int completed, req_completed, all_req_null, test_err, aux_condition;
  size_t i;
  completed = 1;
  all_req_null = 1;
  test_err = MPI_SUCCESS;

  if (is_children_group) return 1;

  if(malleability_red_contains_strat(red_strategies, MALL_RED_IBARRIER, NULL)) {

    // The Ibarrier should only be posted at this point if the process
    // has other requests which has not confirmed as completed yet,
    // but are confirmed now.
    if (requests[request_qty-1] == MPI_REQUEST_NULL) {
      for(i=0; i<request_qty; i++) {
	aux_condition = requests[i] == MPI_REQUEST_NULL;
	all_req_null  = all_req_null && aux_condition;
        test_err = MPI_Test(&(requests[i]), &req_completed, MPI_STATUS_IGNORE);
        completed = completed && req_completed;
      }
      if(completed && !all_req_null) { MPI_Ibarrier(comm, &(requests[request_qty-1])); }
    }
    test_err = MPI_Test(&(requests[request_qty-1]), &completed, MPI_STATUS_IGNORE);

  } else {
    for(i=0; i<request_qty; i++) {
      test_err = MPI_Test(&(requests[i]), &req_completed, MPI_STATUS_IGNORE);
      completed = completed && req_completed;
    }
//  test_err = MPI_Testall(request_qty, requests, &completed, MPI_STATUSES_IGNORE); //FIXME Some kind of bug with Mpich.
  }

  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  return completed;
}


/*
 * Waits until the completion of a set of requests. If the Ibarrier strategy
 * is being used, the corresponding ibarrier is posted.
 *
 * - red_strategies (IN):
 * - comm (IN): Communicator to use to confirm finalizations of redistribution
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 */
void async_communication_wait(int red_strategies, MPI_Comm comm, MPI_Request *requests, size_t request_qty) {
  MPI_Waitall(request_qty, requests, MPI_STATUSES_IGNORE); 
  if(malleability_red_contains_strat(red_strategies, MALL_RED_IBARRIER, NULL)) { 
    MPI_Ibarrier(comm, &(requests[request_qty-1]) );
    MPI_Wait(&(requests[request_qty-1]), MPI_STATUS_IGNORE); //TODO Is it really needed? It will be ensured later
  }
}

/*
 * Frees Requests/Windows associated to a particular redistribution.
 * Should be called for each output result of calling "async_communication_start".
 *
 * - red_method (IN):
 * - red_strategies (IN):
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 * - win (IN): Window to free.
 */
void async_communication_end(int red_method, int red_strategies, MPI_Request *requests, size_t request_qty, MPI_Win *win) {

  //Para la desconexión de ambos grupos de procesos es necesario indicar a MPI que esta comm
  //ha terminado, aunque solo se pueda llegar a este punto cuando ha terminado
  if(malleability_red_contains_strat(red_strategies, MALL_RED_IBARRIER, NULL)) { MPI_Waitall(request_qty, requests, MPI_STATUSES_IGNORE); }

  if(red_method == MALL_RED_RMA_LOCKALL || red_method == MALL_RED_RMA_LOCK) { MPI_Win_free(win); }
}

/*
 * Performs a series of non-blocking point2point communications to redistribute an array in a block distribution. 
 * It should be called after calculating how data should be redistributed.
 *
 * - send (IN):  Array with the data to send. This value can not be NULL for parents.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to 
 *               receive data. If the process receives data and is NULL, the behaviour is undefined.
 * - s_counts (IN): Struct which describes how many elements will send this process to each children and 
 *               the displacements.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent 
 *               and the displacements.
 * - comm (IN):  Communicator to use to perform the redistribution.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended.
 *
 */
void async_point2point(char *send, char *recv, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm, MPI_Request *requests) {
    int i, j = 0;

    for(i=s_counts.idI; i<s_counts.idE; i++) {
      MPI_Isend(send+s_counts.displs[i], s_counts.counts[i], MPI_CHAR, i, 99, comm, &(requests[j]));
      j++;
    }

    for(i=r_counts.idI; i<r_counts.idE; i++) {
      MPI_Irecv(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, 99, comm, &(requests[j]));
      j++;
    }
}

/*
 * Performs asynchronous MPI-RMA operations to redistribute an array in a block distribution. Is should be called after calculating
 * how data should be redistributed.
 *
 * - send (IN):  Array with the data to send. This value can be NULL for children.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - tamBl (IN): How many elements are stored in the parameter "send".
 * - comm (IN):  Communicator to use to perform the redistribution. Must be an intracommunicator as MPI-RMA requirements.
 * - red_method (IN): Type of data redistribution to use. In this case indicates the RMA operation(Lock or LockAll).
 * - window (OUT): Pointer to a window object used for the RMA operations.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended.
 *
 */
void async_rma(char *send, char *recv, struct Counts r_counts, int tamBl, MPI_Comm comm, int red_method, MPI_Request *requests, MPI_Win *win) {

  MPI_Win_create(send, (MPI_Aint)tamBl, sizeof(char), MPI_INFO_NULL, comm, win);
  switch(red_method) {
    case MALL_RED_RMA_LOCKALL:
      async_rma_lockall(recv, r_counts, *win, requests);
      break;
    case MALL_RED_RMA_LOCK:
      async_rma_lock(recv, r_counts, *win, requests);
      break;
  }
}

/*
 * Performs an asynchronous and passive MPI-RMA data redistribution for a single array using the passive epochs Lock/Unlock.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - win (IN):   Window to use to perform the redistribution.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended.
 *
 */
void async_rma_lock(char *recv, struct Counts r_counts, MPI_Win win, MPI_Request *requests) {
  int i, target_displs, j = 0;

  target_displs = r_counts.first_target_displs;
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, win);
    MPI_Rget(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, target_displs, r_counts.counts[i], MPI_CHAR, win, &(requests[j]));
    MPI_Win_unlock(i, win);
    target_displs=0;
    j++;
  }
}
/*
 * Performs an asynchronous and passive MPI-RMA data redistribution for a single array using the passive epochs Lockall/Unlockall.
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               If the process receives data and is NULL, the behaviour is undefined.
 * - r_counts (IN): Structure which describes how many elements will receive this process from each parent and the
 *               displacements.
 * - win (IN):   Window to use to perform the redistribution.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended.
 *
 */
void async_rma_lockall(char *recv, struct Counts r_counts, MPI_Win win, MPI_Request *requests) {
  int i, target_displs, j = 0;

  target_displs = r_counts.first_target_displs;
  MPI_Win_lock_all(MPI_MODE_NOCHECK, win);
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    MPI_Rget(recv+r_counts.displs[i], r_counts.counts[i], MPI_CHAR, i, target_displs, r_counts.counts[i], MPI_CHAR, win, &(requests[j]));
    target_displs=0;
    j++;
  }
  MPI_Win_unlock_all(win);
}

/*
 * ========================================================================================
 * ========================================================================================
 * ================================DISTRIBUTION FUNCTIONS==================================
 * ========================================================================================
 * ========================================================================================
*/

/*
 * Performs a communication to redistribute an array in a block distribution. For each process calculates
 * how many elements sends/receives to other processes for the new group.
 *
 * - qty  (IN):  Sum of elements shared by all processes that will send data.
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - numP (IN):  Size of the local group. If it is a children group, this parameter must correspond to using
 *               "MPI_Comm_size(comm)". For the parents is not always the size obtained from "comm".
 * - numO (IN):  Amount of processes in the remote group. For the parents is the target quantity of processes after the 
 *               resize, while for the children is the amount of parents.
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - is_intercomm (IN): Indicates wether the used communicator is a intercomunicator(TRUE) or intracommunicator(FALSE).
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               process receives data and is NULL, the behaviour is undefined.
 * - s_counts (OUT): Struct where is indicated how many elements sends this process to processes in the new group.
 * - r_counts (OUT): Struct where is indicated how many elements receives this process from other processes in the previous group.
 *
 */
//FIXME Ensure name for is_sync variable
void prepare_redistribution(int qty, int myId, int numP, int numO, int is_children_group, int is_intercomm, int is_sync, char **recv, struct Counts *s_counts, struct Counts *r_counts) {
  int array_size = numO;
  int offset_ids = 0;
  struct Dist_data dist_data;

  if(is_intercomm) {
    offset_ids = is_sync ? numP : 0; //FIXME Modify only if active?
  } else {
    array_size = numP > numO ? numP : numO;
  }
  mallocCounts(s_counts, array_size+offset_ids);
  mallocCounts(r_counts, array_size+offset_ids);

  if(is_children_group) {
    prepare_comm_alltoall(myId, numP, numO, qty, offset_ids, r_counts);
    
    // Obtener distribución para este hijo
    get_block_dist(qty, myId, numP, &dist_data);
    *recv = malloc(dist_data.tamBl * sizeof(char));
//get_block_dist(qty, myId, numP, &dist_data);
//print_counts(dist_data, r_counts->counts, r_counts->displs, numO+offset_ids, 0, "Children C ");
  } else {
//get_block_dist(qty, myId, numP, &dist_data);

    prepare_comm_alltoall(myId, numP, numO, qty, offset_ids, s_counts);
    if(!is_intercomm && myId < numO) {
        prepare_comm_alltoall(myId, numO, numP, qty, offset_ids, r_counts);
        // Obtener distribución para este hijo y reservar vector de recibo
        get_block_dist(qty, myId, numO, &dist_data);
        *recv = malloc(dist_data.tamBl * sizeof(char));
//print_counts(dist_data, r_counts->counts, r_counts->displs, array_size, 0, "Children P ");
    }
//print_counts(dist_data, s_counts->counts, s_counts->displs, numO+offset_ids, 0, "Parents ");
  }
}

/*
 * Ensures that the array of request of a process has an amount of elements equal to the amount of communication
 * functions the process will perform. In case the array is not initialized or does not have enough space it is
 * allocated/reallocated to the minimum amount of space needed.
 *
 * - s_counts (IN): Struct where is indicated how many elements sends this process to processes in the new group.
 * - r_counts (IN): Struct where is indicated how many elements receives this process from other processes in the previous group.
 * - requests (IN/OUT): Pointer to array of requests to be used to determine if the communication has ended. If the pointer 
 *               is null or not enough space has been reserved the pointer is allocated/reallocated.
 * - request_qty (IN/OUT): Quantity of requests to be used. If the value is smaller than the amount of communication
 *               functions to perform, it is modified to the minimum value.
 */
void check_requests(struct Counts s_counts, struct Counts r_counts, int red_strategies, MPI_Request **requests, size_t *request_qty) {
  size_t i, sum;
  MPI_Request *aux;

  sum = (size_t) s_counts.idE - s_counts.idI;
  sum += (size_t) r_counts.idE - r_counts.idI;
  if(malleability_red_contains_strat(red_strategies, MALL_RED_IBARRIER, NULL)) {
    sum++;
  }

  if (*requests != NULL && sum <= *request_qty) return; // Expected amount of requests

  if (*requests == NULL) {
    *requests = (MPI_Request *) malloc(sum * sizeof(MPI_Request));
  } else { // Array exists, but is too small
    aux = (MPI_Request *) realloc(*requests, sum * sizeof(MPI_Request));
    *requests = aux;
  }

  if (*requests == NULL) {
    fprintf(stderr, "Fatal error - It was not possible to allocate/reallocate memory for the MPI_Requests before the redistribution\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for(i=0; i < sum; i++) {
    (*requests)[i] = MPI_REQUEST_NULL;
  }
  *request_qty = sum;
}


/*
 * Special case to perform a manual copy of data when a process has to send data to itself. Only used
 * when the MPI communication is not able to hand this situation. An example is when using point to point
 * communications and the process has to perform a Send and Recv to itself
 * - send (IN):  Array with the data to send. This value can not be NULL.
 * - recv (OUT): Array where data will be written. This value can not be NULL.
 * - myId (IN):  Rank of the MPI process in the local communicator. For the parents is not the rank obtained from "comm".
 * - s_counts (IN): Struct where is indicated how many elements sends this process to processes in the new group.
 * - r_counts (IN): Struct where is indicated how many elements receives this process from other processes in the previous group.
 */
void perform_manual_communication(char *send, char *recv, int myId, struct Counts s_counts, struct Counts r_counts) {
  int i;
  for(i=0; i<s_counts.counts[myId];i++) {
    recv[i+r_counts.displs[myId]] = send[i+s_counts.displs[myId]];
  }
}

/*
 * Función para obtener si entre las estrategias elegidas, se utiliza
 * la estrategia pasada como segundo argumento.
 *
 * Devuelve en "result" 1(Verdadero) si utiliza la estrategia, 0(Falso) en caso
 * contrario.
 */
int malleability_red_contains_strat(int comm_strategies, int strategy, int *result) {
  int value = comm_strategies % strategy ? 0 : 1;
  if(result != NULL) *result = value;
  return value;
}


/*
 * Función para anyadir una estrategia a un conjunto.
 *
 * Devuelve en "result" 1(Verdadero) si se ha anyadido, 0(Falso) en caso
 * contrario.
 */
int malleability_red_add_strat(int *comm_strategies, int strategy) {
  if(malleability_red_contains_strat(*comm_strategies, strategy, NULL)) return 1;
  *comm_strategies = *comm_strategies * strategy;
  return 1;
}
