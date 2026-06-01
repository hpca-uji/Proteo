#ifndef MAM_DISTRIBUTED_COMMDIST_H
#define MAM_DISTRIBUTED_COMMDIST_H

#include <mpi.h>
#include "../MAM_Types.h"

void send_data(int numP_children, malleability_data_t *data_struct, int is_asynchronous);
void recv_data(int numP_parents, malleability_data_t *data_struct, int is_asynchronous);

int async_communication_check(int is_children_group, MPI_Request *requests, size_t request_qty);
void async_communication_wait(MPI_Request *requests, size_t request_qty);
void async_communication_end(MPI_Request *requests, size_t request_qty, MPI_Win *win, int *idS);


void malloc_comm_array(void **array, size_t qty, size_t datasize, int myId, int numP, int init);
void check_ordered(const char *array, size_t qty);
#endif
