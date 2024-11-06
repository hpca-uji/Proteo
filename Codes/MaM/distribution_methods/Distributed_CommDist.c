#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "block_distribution.h"
#include "Distributed_CommDist.h"
#include "../MAM_Constants.h"
#include "../MAM_Configuration.h"
#include "../MAM_DataStructures.h"

void prepare_redistribution(int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, void **recv, struct Counts *s_counts, struct Counts *r_counts);
void check_requests(struct Counts s_counts, struct Counts r_counts, MPI_Request **requests, size_t *request_qty);

void sync_point2point(void *send, void *recv, MPI_Datatype datatype, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm);
void sync_rma(void *send, void *recv, MPI_Datatype datatype, struct Counts r_counts, int tamBl, MPI_Comm comm);
void sync_rma_lock(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win);
void sync_rma_lockall(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win);

void async_point2point(void *send, void *recv, MPI_Datatype datatype, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm, MPI_Request *requests);
void async_rma(void *send, void *recv, MPI_Datatype datatype, struct Counts r_counts, int tamBl, MPI_Comm comm, MPI_Request *requests, MPI_Win *win);
void async_rma_lock(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win, MPI_Request *requests);
void async_rma_lockall(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win, MPI_Request *requests);

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
 * - numP (IN):  Size of the local group. If it is a children group, this parameter must correspond to using
 *               "MPI_Comm_size(comm)". For the parents is not always the size obtained from "comm".
 * - numO (IN):  Amount of processes in the remote group. For the parents is the target quantity of processes after the 
 *               resize, while for the children is the amount of parents.
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - comm (IN):  Communicator to use to perform the redistribution.
 *
 */
void sync_communication(void *send, void **recv, int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, MPI_Comm comm) {
  struct Counts s_counts, r_counts;
  struct Dist_data dist_data;

  /* PREPARE COMMUNICATION */
  prepare_redistribution(qty, datatype, numP, numO, is_children_group, recv, &s_counts, &r_counts);

  /* PERFORM COMMUNICATION */
  switch (mall_conf->red_method) {
  case MAM_RED_RMA_LOCKALL:
  case MAM_RED_RMA_LOCK:
    if (is_children_group) {
      dist_data.tamBl = 0;
    } else {
      get_block_dist(qty, mall->myId, numO, &dist_data);
    }
    sync_rma(send, *recv, datatype, r_counts, dist_data.tamBl, comm);
    break;

  case MAM_RED_POINT:
    sync_point2point(send, *recv, datatype, s_counts, r_counts, comm);
    break;
  case MAM_RED_BASELINE:
  default:
    MPI_Alltoallv(send, s_counts.counts, s_counts.displs, datatype, *recv, r_counts.counts, r_counts.displs, datatype, comm);
    break;
  }

  freeCounts(&s_counts);
  freeCounts(&r_counts);
}

/*
 * Performs a series of blocking point2point communications to redistribute an array in a block distribution. 
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
 *
 */
void sync_point2point(void *send, void *recv, MPI_Datatype datatype, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm) {
    int i, j, init, end, total_sends, datasize;
    size_t offset, offset2;
    MPI_Request *sends;

    MPI_Type_size(datatype, &datasize);
    init = s_counts.idI;
    end = s_counts.idE;
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE && (s_counts.idI == mall->myId || s_counts.idE == mall->myId + 1)) {
      offset = s_counts.displs[mall->myId] * datasize;
      offset2 = r_counts.displs[mall->myId] * datasize;
      memcpy(recv+offset2, send+offset, s_counts.counts[mall->myId]);
      
      if(s_counts.idI == mall->myId) init = s_counts.idI+1;
      else end = s_counts.idE-1;
    }

    total_sends = end - init;
    j = 0;
    if(total_sends > 0) {
      sends = (MPI_Request *) malloc(total_sends * sizeof(MPI_Request));
    }
    for(i=init; i<end; i++) {
      sends[j] = MPI_REQUEST_NULL;
      offset = s_counts.displs[i] * datasize;
      MPI_Isend(send+offset, s_counts.counts[i], datatype, i, 99, comm, &(sends[j]));
      j++;
    }

    init = r_counts.idI;
    end = r_counts.idE;
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE) {
      if(r_counts.idI == mall->myId) init = r_counts.idI+1;
      else if(r_counts.idE == mall->myId + 1) end = r_counts.idE-1;
    }

    for(i=init; i<end; i++) {
      offset = r_counts.displs[i] * datasize;
      MPI_Recv(recv+offset, r_counts.counts[i], datatype, i, 99, comm, MPI_STATUS_IGNORE);
    }

    if(total_sends > 0) {
      MPI_Waitall(total_sends, sends, MPI_STATUSES_IGNORE);
      free(sends);
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
 *
 * FIXME: In libfabric one of these macros defines the maximum amount of BYTES that can be communicated in a SINGLE MPI_Get
 * A window can have more bytes than the amount shown in those macros, therefore, if you want to read more than that amount
 * you need to perform multiples Gets.
 * prov/psm3/psm3/psm_config.h:179:#define MQ_SHM_THRESH_RNDV 16000
 * prov/psm3/psm3/ptl_am/am_config.h:62:#define PSMI_MQ_RV_THRESH_CMA      16000
 * prov/psm3/psm3/ptl_am/am_config.h:65:#define PSMI_MQ_RV_THRESH_NO_KASSIST 16000
 */
void sync_rma(void *send, void *recv, MPI_Datatype datatype, struct Counts r_counts, int tamBl, MPI_Comm comm) {
  int datasize;
  MPI_Win win;
  MPI_Type_size(datatype, &datasize);
  MPI_Win_create(send, (MPI_Aint)tamBl * datasize, datasize, MPI_INFO_NULL, comm, &win);

  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Created Window for synchronous RMA communication", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(comm);
  #endif
  switch(mall_conf->red_method) {
    case MAM_RED_RMA_LOCKALL:
      sync_rma_lockall(recv, datatype, r_counts, win);
      break;
    case MAM_RED_RMA_LOCK:
      sync_rma_lock(recv, datatype, r_counts, win);
      break;
  }
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Completed synchronous RMA communication", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(comm);
  #endif
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
void sync_rma_lock(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win) {
  int i, target_displs, datasize;
  size_t offset;

  MPI_Type_size(datatype, &datasize);
  target_displs = r_counts.first_target_displs; //TODO Check that datasize is not needed

  for(i=r_counts.idI; i<r_counts.idE; i++) {
    offset = r_counts.displs[i] * datasize;
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, win);
    MPI_Get(recv+offset, r_counts.counts[i], datatype, i, target_displs, r_counts.counts[i], datatype, win);
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
void sync_rma_lockall(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win) {
  int i, target_displs, datasize;
  size_t offset;

  MPI_Type_size(datatype, &datasize);
  target_displs = r_counts.first_target_displs; //TODO Check that datasize is not needed

  MPI_Win_lock_all(MPI_MODE_NOCHECK, win);
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    offset = r_counts.displs[i] * datasize;
    MPI_Get(recv+offset, r_counts.counts[i], datatype, i, target_displs, r_counts.counts[i], datatype, win);
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
 */
void async_communication_start(void *send, void **recv, int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, MPI_Comm comm, MPI_Request **requests, size_t *request_qty, MPI_Win *win) {
    struct Counts s_counts, r_counts;
    struct Dist_data dist_data;

    /* PREPARE COMMUNICATION */
    prepare_redistribution(qty, datatype, numP, numO, is_children_group, recv, &s_counts, &r_counts); 
    check_requests(s_counts, r_counts, requests, request_qty); //FIXME Error related to second reconf if Merge Shrink + P2P -->Invalid requests

    /* PERFORM COMMUNICATION */
    switch(mall_conf->red_method) {

      case MAM_RED_RMA_LOCKALL:
      case MAM_RED_RMA_LOCK:
        if(is_children_group) {
	  dist_data.tamBl = 0;
	} else {
          get_block_dist(qty, mall->myId, numO, &dist_data);
	}
        async_rma(send, *recv, datatype, r_counts, dist_data.tamBl, comm, *requests, win);
	break;
      case MAM_RED_POINT:
        async_point2point(send, *recv, datatype, s_counts, r_counts, comm, *requests);
	break;
      case MAM_RED_BASELINE:
      default:
        MPI_Ialltoallv(send, s_counts.counts, s_counts.displs, datatype, *recv, r_counts.counts, r_counts.displs, datatype, comm, &((*requests)[0]));
	break;
    }

    freeCounts(&s_counts);
    freeCounts(&r_counts);
}

/*
 * Checks if a set of requests have been completed (1) or not (0).
 *
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 *
 * returns: An integer indicating if the operation has been completed(TRUE) or not(FALSE).
 */
int async_communication_check(int is_children_group, MPI_Request *requests, size_t request_qty) {
  int completed, req_completed, test_err;
  size_t i;
  completed = 1;
  test_err = MPI_SUCCESS;

  if (is_children_group) return 1; //FIXME Deberia devolver un num negativo

  for(i=0; i<request_qty; i++) {
    test_err = MPI_Test(&(requests[i]), &req_completed, MPI_STATUS_IGNORE);
    completed = completed && req_completed;
  }
  //test_err = MPI_Testall(request_qty, requests, &completed, MPI_STATUSES_IGNORE); //FIXME Some kind of bug with Mpich.

  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", mall->myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  return completed;
}


/*
 * Waits until the completion of a set of requests. If the Ibarrier strategy
 * is being used, the corresponding ibarrier is posted.
 *
 * - comm (IN): Communicator to use to confirm finalizations of redistribution
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 */
void async_communication_wait(MPI_Request *requests, size_t request_qty) {
  MPI_Waitall(request_qty, requests, MPI_STATUSES_IGNORE); 
  #if MAM_DEBUG >= 3
    DEBUG_FUNC("Processes Waitall completed", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

/*
 * Frees Requests/Windows associated to a particular redistribution.
 * Should be called for each output result of calling "async_communication_start".
 *
 * - requests (IN): Pointer to array of requests to be used to determine if the communication has ended.
 * - request_qty (IN): Quantity of requests in "requests".
 * - win (IN): Window to free.
 */
void async_communication_end(MPI_Request *requests, size_t request_qty, MPI_Win *win) {

  //Para la desconexión de ambos grupos de procesos es necesario indicar a MPI que esta comm
  //ha terminado, aunque solo se pueda llegar a este punto cuando ha terminado
  if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) { MPI_Waitall(request_qty, requests, MPI_STATUSES_IGNORE); }

  if((mall_conf->red_method == MAM_RED_RMA_LOCKALL || mall_conf->red_method == MAM_RED_RMA_LOCK) 
		  && *win != MPI_WIN_NULL) { MPI_Win_free(win); }
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
void async_point2point(void *send, void *recv, MPI_Datatype datatype, struct Counts s_counts, struct Counts r_counts, MPI_Comm comm, MPI_Request *requests) {
    int i, j = 0, datasize;
    size_t offset;
    MPI_Type_size(datatype, &datasize);

    for(i=s_counts.idI; i<s_counts.idE; i++) {
      offset = s_counts.displs[i] * datasize;
      MPI_Isend(send+offset, s_counts.counts[i], datatype, i, 99, comm, &(requests[j]));
      j++;
    }

    for(i=r_counts.idI; i<r_counts.idE; i++) {
      offset = r_counts.displs[i] * datasize;
      MPI_Irecv(recv+offset, r_counts.counts[i], datatype, i, 99, comm, &(requests[j]));
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
 * - window (OUT): Pointer to a window object used for the RMA operations.
 * - requests (OUT): Pointer to array of requests to be used to determine if the communication has ended.
 *
 */
void async_rma(void *send, void *recv, MPI_Datatype datatype, struct Counts r_counts, int tamBl, MPI_Comm comm, MPI_Request *requests, MPI_Win *win) {
  int datasize;

  MPI_Type_size(datatype, &datasize);
  MPI_Win_create(send, (MPI_Aint)tamBl * datasize, datasize, MPI_INFO_NULL, comm, win);
  switch(mall_conf->red_method) {
    case MAM_RED_RMA_LOCKALL:
      async_rma_lockall(recv, datatype, r_counts, *win, requests);
      break;
    case MAM_RED_RMA_LOCK:
      async_rma_lock(recv, datatype, r_counts, *win, requests);
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
void async_rma_lock(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win, MPI_Request *requests) {
  int i, target_displs, j = 0, datasize;
  size_t offset;

  MPI_Type_size(datatype, &datasize);
  target_displs = r_counts.first_target_displs; //TODO Check that datasize is not needed

  for(i=r_counts.idI; i<r_counts.idE; i++) {
    offset = r_counts.displs[i] * datasize;
    MPI_Win_lock(MPI_LOCK_SHARED, i, MPI_MODE_NOCHECK, win);
    MPI_Rget(recv+offset, r_counts.counts[i], datatype, i, target_displs, r_counts.counts[i], datatype, win, &(requests[j]));
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
void async_rma_lockall(void *recv, MPI_Datatype datatype, struct Counts r_counts, MPI_Win win, MPI_Request *requests) {
  int i, target_displs, j = 0, datasize;
  size_t offset;

  MPI_Type_size(datatype, &datasize);
  target_displs = r_counts.first_target_displs; //TODO Check that datasize is not needed

  MPI_Win_lock_all(MPI_MODE_NOCHECK, win);
  for(i=r_counts.idI; i<r_counts.idE; i++) {
    offset = r_counts.displs[i] * datasize;
    MPI_Rget(recv+offset, r_counts.counts[i], datatype, i, target_displs, r_counts.counts[i], datatype, win, &(requests[j]));
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
 * - numP (IN):  Size of the local group. If it is a children group, this parameter must correspond to using
 *               "MPI_Comm_size(comm)". For the parents is not always the size obtained from "comm".
 * - numO (IN):  Amount of processes in the remote group. For the parents is the target quantity of processes after the 
 *               resize, while for the children is the amount of parents.
 * - is_children_group (IN): Indicates wether this MPI rank is a children(TRUE) or a parent(FALSE).
 * - recv (OUT): Array where data will be written. A NULL value is allowed if the process is not going to receive data.
 *               process receives data and is NULL, the behaviour is undefined.
 * - s_counts (OUT): Struct where is indicated how many elements sends this process to processes in the new group.
 * - r_counts (OUT): Struct where is indicated how many elements receives this process from other processes in the previous group.
 *
 */
void prepare_redistribution(int qty, MPI_Datatype datatype, int numP, int numO, int is_children_group, void **recv, struct Counts *s_counts, struct Counts *r_counts) {
  int array_size = numO;
  int offset_ids = 0;
  int datasize;
  size_t total_bytes;
  struct Dist_data dist_data;

  if(mall_conf->spawn_method == MAM_SPAWN_BASELINE) {
    offset_ids =  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL) ? 
	    0 : numP;
  } else { // Merge method
    array_size = numP > numO ? numP : numO;
  }

  mallocCounts(s_counts, array_size+offset_ids);
  mallocCounts(r_counts, array_size+offset_ids);
  MPI_Type_size(datatype, &datasize); //FIXME Right now derived datatypes are not ensured to work

  if(is_children_group) {
    offset_ids = 0;
    prepare_comm_alltoall(mall->myId, numP, numO, qty, offset_ids, r_counts);
    
    // Obtener distribución para este hijo
    get_block_dist(qty, mall->myId, numP, &dist_data);
    total_bytes = ((size_t) dist_data.tamBl) * ((size_t) datasize);
    *recv = malloc(total_bytes);

    #if MAM_DEBUG >= 4
      get_block_dist(qty, mall->myId, numP, &dist_data);
      print_counts(dist_data, r_counts->counts, r_counts->displs, numO+offset_ids, 0, "Targets Recv");
    #endif
  } else {
    #if MAM_DEBUG >= 4
      get_block_dist(qty, mall->myId, numP, &dist_data);
    #endif

    prepare_comm_alltoall(mall->myId, numP, numO, qty, offset_ids, s_counts);
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->myId < numO) {
      prepare_comm_alltoall(mall->myId, numO, numP, qty, offset_ids, r_counts);
      // Obtener distribución para este hijo y reservar vector de recibo
      get_block_dist(qty, mall->myId, numO, &dist_data);
      total_bytes = ((size_t) dist_data.tamBl) * ((size_t) datasize);
      *recv = malloc(total_bytes);
      #if MAM_DEBUG >= 4
        print_counts(dist_data, r_counts->counts, r_counts->displs, array_size, 0, "Sources&Targets Recv");
      #endif
    }
    #if MAM_DEBUG >= 4
      print_counts(dist_data, s_counts->counts, s_counts->displs, numO+offset_ids, 0, "Sources Send");
    #endif
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
void check_requests(struct Counts s_counts, struct Counts r_counts, MPI_Request **requests, size_t *request_qty) {
  size_t i, sum;
  MPI_Request *aux;

  switch(mall_conf->red_method) {
    case MAM_RED_BASELINE:
      sum = 1;
      break;
    case MAM_RED_POINT:
    default:
      sum = (size_t) s_counts.idE - s_counts.idI;
      sum += (size_t) r_counts.idE - r_counts.idI;
      break;
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
