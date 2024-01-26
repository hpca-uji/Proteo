#ifndef MALLEABILITY_TIMES_H
#define MALLEABILITY_TIMES_H

#include <mpi.h>

void init_malleability_times();
void reset_malleability_times();
void free_malleability_times();

void malleability_times_broadcast(int root);

void MAM_I_retrieve_times(double *sp_time, double *sy_time, double *asy_time, double *mall_time);

#endif
