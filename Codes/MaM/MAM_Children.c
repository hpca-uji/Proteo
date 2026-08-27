#include "MAM_Children.h"
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "MAM_Types.h"
#include "GenericSpawn.h"
#include "Distributed_CommDist.h"

void MAM_I_children_connect(void);
void MAM_I_children_spawn(void);
void MAM_I_children_help_connect(void);
void MAM_I_children_check_configuration(int children_type);
void MAM_I_children_async_comm(malleability_data_t *rep_a_data, malleability_data_t *dist_a_data);
void MAM_I_children_sync_comm(malleability_data_t *rep_s_data, malleability_data_t *dist_s_data);
void MAM_I_children_user_comm(void (*i_user_function)(void *), void *i_user_args);

// Only to be called once. This is because it fails after the reconfiguration is committed
// It will also mismatch a children if called twice when the children is created with a new job.
int MAM_Check_children_type(void) {
  char *tmp = NULL;

  MPI_Comm_get_parent(&(mall->intercomm));
  if(mall->intercomm != MPI_COMM_NULL) { 
    return MAM_TARGETS;
  }

  tmp = getenv(MAM_ENV);
  if(tmp != NULL) { // If it is a new job, assign the service name
    if (mall->service_name != NULL) { free(mall->service_name); }
    mall->service_name = malloc((strlen(tmp) + 1) * sizeof *mall->service_name);
    strcpy(mall->service_name, tmp);
    unsetenv(MAM_ENV);
    return MAM_TARGETS;
  }

  return MAM_SOURCES;
}

/**
 * @brief Initialise the data of the spawned children.
 *
 * The children connect to their parents (the sources) and receive from them the
 * configuration of the execution to be performed, followed by the data itself,
 * either asynchronously, synchronously or both. The asynchronous (constant) data is
 * received first, then the user callback is given the chance to redistribute its own
 * data, and finally the synchronous (variable) data is received. The function ends by
 * committing the reconfiguration, after which the children are ready to run the
 * application.
 *
 * @param[in] i_user_function Optional user callback for the user redistribution phase.
 * @param[in] i_user_args     Opaque argument forwarded to @p i_user_function.
 */
void MAM_Children_init(void (*i_user_function)(void *), void *i_user_args, malleability_data_t *rep_s_data, malleability_data_t *dist_s_data, 
                    malleability_data_t *rep_a_data, malleability_data_t *dist_a_data) {
  int children_type;
  size_t i;

  //------------------------------ SPAWN
  if(mall->service_name != NULL) {
    children_type = 1;
    MAM_I_children_connect();
  } else {
    children_type = 0;
    MAM_I_children_spawn();
    MAM_I_children_help_connect();
    
  }
  MAM_I_children_check_configuration(children_type);

  //------------------------------ ASYNCH
  MAM_I_children_async_comm(rep_a_data, dist_a_data);


  //------------------------------ USER
  MAM_I_children_user_comm(i_user_function, i_user_args); 


  //------------------------------ SYNCH
  MAM_I_children_sync_comm(rep_s_data, dist_s_data);

  #if MAM_DEBUG
    DEBUG_FUNC("MaM has been initialized correctly for new ranks", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================CHILDREN=========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||



void MAM_I_children_connect(void) {
  #if MAM_DEBUG
    DEBUG_FUNC("MaM will now connect processes of the new job", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  MAM_Connect_jobs_as_target();
  MAM_Comm_main_structures(mall->intercomm, MAM_ROOT);

  #if MAM_DEBUG
    DEBUG_FUNC("New job has been connected to old job", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

void MAM_I_children_spawn(void) {
  #if MAM_DEBUG
    DEBUG_FUNC("MaM will now initialize spawned processes", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  MAM_Comm_main_structures(mall->intercomm, MAM_ROOT); // FIXME: What if root is another id different to 0? Send from spawn to root id?
  malleability_connect_children(&(mall->intercomm));


  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed spawn step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

void MAM_I_children_help_connect(void) {
  if(mall->new_job_id != MAM_DENIED) { 
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned join sources to connect to new job", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
    MAM_Prepare_job_comms(MAM_TARGETS);
    MAM_Connect_jobs_as_children();
    MAM_Comm_main_structures(mall->intercomm, MAM_ROOT);
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned connected to new job", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
  }
}

void MAM_I_children_check_configuration(int children_type) {
  if(mall->new_job_id != MAM_DENIED) { 
    MAM_Repair_job_comms(MAM_TARGETS, children_type); 
    if(mall_conf->spawn_method == MAM_SPAWN_BASELINE) { // For Baseline Method, new ranks may have been added
      MPI_Comm_rank(mall->comm, &mall->myId);
      MPI_Comm_size(mall->comm, &mall->numP);
    }
  }

  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) { // For Merge Method, these processes will be added
    MPI_Comm_rank(mall->intercomm, &mall->myId);
    MPI_Comm_size(mall->intercomm, &mall->numP);
  }

  mall->root_collectives = mall->root_parents;

  if((MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL)
    || MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, NULL))
    && children_type == 0) {
    mall->internode_group = 0;
  } else {
    mall->internode_group = MAM_Is_internode_group();
  }
}

void MAM_I_children_async_comm(malleability_data_t *rep_a_data, malleability_data_t *dist_a_data) {
  size_t i;

  comm_data_info(rep_a_data, dist_a_data, MAM_TARGETS);
  if(dist_a_data->entries || rep_a_data->entries) { // Receive asynchronous data
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned start asynchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif

    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
      recv_data(mall->num_parents, dist_a_data, MAM_USE_SYNCHRONOUS);
      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Bcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm);
      } 
    } else {
      recv_data(mall->num_parents, dist_a_data, MAM_USE_ASYNCHRONOUS); 

      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Ibcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm, &(rep_a_data->requests[i][0]));
      } 
      #if MAM_DEBUG >= 2
        DEBUG_FUNC("Spawned started asynchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
      #endif

      for(i=0; i<rep_a_data->entries; i++) {
        async_communication_wait(rep_a_data->requests[i], rep_a_data->request_qty[i]);
      }
      for(i=0; i<dist_a_data->entries; i++) {
        async_communication_wait(dist_a_data->requests[i], dist_a_data->request_qty[i]);
      }
      if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
        MPI_Wait(&mall->wait_targets, MPI_STATUS_IGNORE);
      }

      #if MAM_DEBUG >= 2
        DEBUG_FUNC("Spawned waited for all asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
      #endif
      for(i=0; i<dist_a_data->entries; i++) {
        async_communication_end(dist_a_data->requests[i], dist_a_data->request_qty[i], &(dist_a_data->windows[i]), &dist_a_data->idS[i*2]);
      }
      free(dist_a_data->idS); dist_a_data->idS = NULL;
      for(i=0; i<rep_a_data->entries; i++) {
        async_communication_end(rep_a_data->requests[i], rep_a_data->request_qty[i], &(rep_a_data->windows[i]), &rep_a_data->idS[i*2]);
      }
    }

    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->async_end= MPI_Wtime(); // Timestamp of when the asynchronous communication ends
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed asynchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

void MAM_I_children_sync_comm(malleability_data_t *rep_s_data, malleability_data_t *dist_s_data) {
  size_t i;

  #if MAM_DEBUG >= 2
      DEBUG_FUNC("Spawned start synchronous redistribution", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif
  comm_data_info(rep_s_data, dist_s_data, MAM_TARGETS);
  if(dist_s_data->entries || rep_s_data->entries) { // Receive synchronous data
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    recv_data(mall->num_parents, dist_s_data, MAM_USE_SYNCHRONOUS);

    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], rep_s_data->types[i], mall->root_collectives, mall->intercomm);
    } 
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->sync_end = MPI_Wtime(); // Timestamp of when the synchronous communication ends
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Targets have completed synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

void MAM_I_children_user_comm(void (*i_user_function)(void *), void *i_user_args) {

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    MPI_Intercomm_merge(mall->intercomm, MAM_TARGETS, &mall->tmp_comm); //The group passing 0 is placed first
  } else {
    MPI_Comm_dup(mall->intercomm, &mall->tmp_comm);
  }
  MPI_Comm_set_name(mall->tmp_comm, "MAM_USER_TMP");
  if(i_user_function != NULL) {
    state = MAM_I_USER_PENDING;
    MAM_I_create_user_struct(MAM_TARGETS);
    i_user_function(i_user_args);
  }
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  mall_conf->times->user_end = MPI_Wtime(); // Timestamp of when the user redistribution ends
}