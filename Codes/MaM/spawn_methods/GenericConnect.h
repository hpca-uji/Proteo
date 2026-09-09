#ifndef MAM_SPAWN_GENERICCONNECT_H
#define MAM_SPAWN_GENERICCONNECT_H


void MAM_Init_job_connect_port();
void MAM_Prepare_job_comms(int is_children);
void MAM_Repair_job_comms(int is_children, int children_type);

int MAM_Connect_jobs_as_source(void);
void MAM_Connect_jobs_as_target(void);
void MAM_Connect_jobs_as_children(void);

#endif