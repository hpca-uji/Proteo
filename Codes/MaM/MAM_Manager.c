#include <pthread.h>
#include <string.h>
#include "MAM.h"
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "MAM_Types.h"
#include "MAM_Zombies.h"
#include "MAM_Times.h"
#include "MAM_RMS.h"
#include "MAM_Init_Configuration.h"
#include "spawn_methods/GenericSpawn.h"
#include "distribution_methods/Distributed_CommDist.h"

#define MAM_USE_SYNCHRONOUS 0
#define MAM_USE_ASYNCHRONOUS 1

void MAM_Commit(int *mam_state);

void send_data(int numP_children, malleability_data_t *data_struct, int is_asynchronous);
void recv_data(int numP_parents, malleability_data_t *data_struct, int is_asynchronous);


int MAM_St_rms(int *mam_state);
int MAM_St_spawn_start();
int MAM_St_spawn_pending(int wait_completed);
int MAM_St_red_start();
int MAM_St_red_pending(int wait_completed);
int MAM_St_user_start(int *mam_state);
int MAM_St_user_pending(int *mam_state, int wait_completed, void (*user_function)(void *), void *user_args);
int MAM_St_user_completed();
int MAM_St_spawn_adapt_pending(int wait_completed);
int MAM_St_spawn_adapted(int *mam_state);
int MAM_St_red_completed(int *mam_state);
int MAM_St_completed(int *mam_state);


void Children_init(void (*user_function)(void *), void *user_args);
int spawn_step();
int start_redistribution();
int check_redistribution(int wait_completed);
int end_redistribution();
int shrink_redistribution();

int thread_creation();
int thread_check(int wait_completed);
void* thread_async_work();

int MAM_I_convert_key(char *key);
void MAM_I_create_user_struct(int is_children_group);

malleability_data_t *rep_s_data;
malleability_data_t *dist_s_data;
malleability_data_t *rep_a_data;
malleability_data_t *dist_a_data;

mam_user_reconf_t *user_reconf;

/*
 * Inicializa la reserva de memoria para el modulo de maleabilidad
 * creando todas las estructuras necesarias y copias de comunicadores
 * para no interferir en la aplicación.
 *
 * Si es llamada por un grupo de procesos creados de forma dinámica,
 * inicializan la comunicacion con sus padres. En este caso, al terminar 
 * la comunicacion los procesos hijo estan preparados para ejecutar la
 * aplicacion.
 */
int MAM_Init(int root, MPI_Comm *comm, char *name_exec, void (*user_function)(void *), void *user_args) {
  MPI_Comm dup_comm, thread_comm, original_comm;

  mall_conf = (malleability_config_t *) malloc(sizeof(malleability_config_t));
  mall = (malleability_t *) malloc(sizeof(malleability_t));
  user_reconf = (mam_user_reconf_t *) malloc(sizeof(mam_user_reconf_t));

  MPI_Comm_rank(*comm, &(mall->myId));
  MPI_Comm_size(*comm, &(mall->numP));

  #if MAM_DEBUG
    DEBUG_FUNC("Initializing MaM", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(*comm);
  #endif

  rep_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  rep_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));

  MPI_Comm_dup(*comm, &dup_comm);
  MPI_Comm_dup(*comm, &thread_comm);
  MPI_Comm_dup(*comm, &original_comm);
  MPI_Comm_set_name(dup_comm, "MAM_MAIN");
  MPI_Comm_set_name(thread_comm, "MAM_THREAD");
  MPI_Comm_set_name(original_comm, "MAM_ORIGINAL");

  mall->root = root;
  mall->root_parents = root;
  mall->zombie = 0;
  mall->comm = dup_comm;
  mall->thread_comm = thread_comm;
  mall->original_comm = original_comm;
  mall->user_comm = comm; 
  mall->tmp_comm = MPI_COMM_NULL;

  mall->name_exec = name_exec;
  mall->nodelist = NULL;
  mall->nodelist_len = 0;

  rep_s_data->entries = 0;
  rep_a_data->entries = 0;
  dist_s_data->entries = 0;
  dist_a_data->entries = 0;

  state = MAM_I_NOT_STARTED;

  MAM_Init_configuration();
  MAM_Zombies_service_init();
  init_malleability_times();
  MAM_Def_main_datatype();

  // Si son el primer grupo de procesos, obtienen los datos de los padres
  MPI_Comm_get_parent(&(mall->intercomm));
  if(mall->intercomm != MPI_COMM_NULL) { 
    Children_init(user_function, user_args);
    return MAM_TARGETS;
  }

  //TODO Check potential improvement - If check_hosts does not use slurm, internode_group could be obtained there
  MAM_check_hosts();
  mall->internode_group = MAM_Is_internode_group();
  MAM_Set_initial_configuration();

  #if MAM_USE_BARRIERS && MAM_DEBUG
    if(mall->myId == mall->root)
      printf("MaM: Using barriers to record times.\n");
  #endif

  #if MAM_DEBUG
    DEBUG_FUNC("MaM has been initialized correctly as parents", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(*comm);
  #endif

  return MAM_SOURCES;
}

/*
 * Elimina toda la memoria reservado por el modulo
 * de maleabilidad y asegura que los zombies
 * despierten si los hubiese.
 */
int MAM_Finalize() {	  
  int request_abort;
  free_malleability_data_struct(rep_s_data);
  free_malleability_data_struct(rep_a_data);
  free_malleability_data_struct(dist_s_data);
  free_malleability_data_struct(dist_a_data);

  free(rep_s_data);
  free(rep_a_data);
  free(dist_s_data);
  free(dist_a_data);
  if(mall->nodelist != NULL) free(mall->nodelist);

  MAM_Free_main_datatype();
  request_abort = MAM_Zombies_service_free();
  free_malleability_times();
  if(mall->comm != MPI_COMM_WORLD && mall->comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->comm));
  if(mall->thread_comm != MPI_COMM_WORLD && mall->thread_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->thread_comm));
  if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) { MPI_Comm_disconnect(&(mall->intercomm)); } //FIXME Error en OpenMPI + Merge
  if(mall->original_comm != MPI_COMM_WORLD && mall->original_comm != MPI_COMM_NULL) MPI_Comm_free(&(mall->original_comm));
  free(mall);
  free(mall_conf);
  free(user_reconf);

  state = MAM_I_UNRESERVED;
  return request_abort;
}

/* 
 * TODO Reescribir
 * Comprueba el estado de la maleabilidad. Intenta avanzar en la misma
 * si es posible. Funciona como una máquina de estados.
 * Retorna el estado de la maleabilidad concreto y modifica el argumento
 * "mam_state" a uno generico.
 *
 * El argumento "wait_completed" se utiliza para esperar a la finalización de
 * las tareas llevadas a cabo por parte de MAM.
 *
 */
int MAM_Checkpoint(int *mam_state, int wait_completed, void (*user_function)(void *), void *user_args) {
  int call_checkpoint = 0;

  //TODO This could be changed to an array with the functions to call in each case
  switch(state) {
    case MAM_I_UNRESERVED:
      *mam_state = MAM_UNRESERVED;
      break;
    case MAM_I_NOT_STARTED:
      call_checkpoint = MAM_St_rms(mam_state);
      break;
    case MAM_I_RMS_COMPLETED:
      call_checkpoint = MAM_St_spawn_start();
      break;

    case MAM_I_SPAWN_PENDING: // Comprueba si el spawn ha terminado
    case MAM_I_SPAWN_SINGLE_PENDING:
      call_checkpoint = MAM_St_spawn_pending(wait_completed);
      break;

    case MAM_I_SPAWN_ADAPT_POSTPONE:
    case MAM_I_SPAWN_COMPLETED:
      call_checkpoint = MAM_St_red_start();
      break;

    case MAM_I_DIST_PENDING:
      call_checkpoint = MAM_St_red_pending(wait_completed);
      break;

    case MAM_I_USER_START:
      call_checkpoint = MAM_St_user_start(mam_state);
      break;

    case MAM_I_USER_PENDING:
      call_checkpoint = MAM_St_user_pending(mam_state, wait_completed, user_function, user_args);
      break;

    case MAM_I_USER_COMPLETED:
      call_checkpoint = MAM_St_user_completed();
      break;

    case MAM_I_SPAWN_ADAPT_PENDING:
      call_checkpoint = MAM_St_spawn_adapt_pending(wait_completed);
      break;

    case MAM_I_SPAWN_ADAPTED:
    case MAM_I_DIST_COMPLETED:
      call_checkpoint = MAM_St_completed(mam_state);
      break;
  }

  if(call_checkpoint) { MAM_Checkpoint(mam_state, wait_completed, user_function, user_args); }
  if(state > MAM_I_NOT_STARTED && state < MAM_I_COMPLETED) *mam_state = MAM_PENDING;
  return state;
}

/*
 * TODO
 */
void MAM_Resume_redistribution(int *mam_state) {
  state = MAM_I_USER_COMPLETED;
  if(mam_state != NULL) *mam_state = MAM_PENDING;
}

/*
 * TODO
 */
void MAM_Commit(int *mam_state) {
  int request_abort;
  #if MAM_DEBUG
    if(mall->myId == mall->root){ DEBUG_FUNC("Trying to commit", mall->myId, mall->numP); } fflush(stdout);
  #endif

  // Get times before commiting
  if(mall_conf->spawn_method == MAM_SPAWN_BASELINE) {
    // This communication is only needed when the root process will become a zombie
    malleability_times_broadcast(mall->root_collectives);
  }

  // Free unneded communicators
  if(mall->tmp_comm != MPI_COMM_WORLD && mall->tmp_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&(mall->tmp_comm));
  if(*(mall->user_comm) != MPI_COMM_WORLD && *(mall->user_comm) != MPI_COMM_NULL) MPI_Comm_disconnect(mall->user_comm);

  // Zombies Treatment
  MAM_Zombies_update();
  if(mall->zombie) {
    #if MAM_DEBUG >= 1
      DEBUG_FUNC("Is terminating as zombie", mall->myId, mall->numP); fflush(stdout);
    #endif
    request_abort = MAM_Finalize();
    if(request_abort) { MPI_Abort(MPI_COMM_WORLD, -101); }
    MPI_Finalize();
    exit(0);
  }

  // Reset/Free communicators
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) { MAM_comms_update(mall->intercomm); }
  if(mall->intercomm != MPI_COMM_NULL && mall->intercomm != MPI_COMM_WORLD) { MPI_Comm_disconnect(&(mall->intercomm)); } //FIXME Error en OpenMPI + Merge

  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);
  mall->root = mall_conf->spawn_method == MAM_SPAWN_BASELINE ? mall->root : mall->root_parents;
  mall->root_parents = mall->root;
  state = MAM_I_NOT_STARTED;
  if(mam_state != NULL) *mam_state = MAM_COMPLETED;

  // Set new communicator
  MPI_Comm_dup(mall->comm, mall->user_comm);
  #if MAM_DEBUG
    if(mall->myId == mall->root) DEBUG_FUNC("Reconfiguration has been commited", mall->myId, mall->numP); fflush(stdout);
  #endif

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->malleability_end = MPI_Wtime();
}

/*
 * This function adds data to a data structure based on whether the operation is synchronous or asynchronous,
 * and whether the data is replicated or distributed. It takes the following parameters:
 * - data: a pointer to the data to be added
 * - index: a pointer to a size_t variable where the index of the added data will be stored
 * - total_qty: the amount of elements in data
 * - type: the MPI datatype of the data
 * - is_replicated: a flag indicating whether the data is replicated (MAM_DATA_REPLICATED) or not (MAM_DATA_DISTRIBUTED)
 * - is_constant: a flag indicating whether the operation is asynchronous (MAM_DATA_CONSTANT) or synchronous (MAM_DATA_VARIABLE)
 * Finally, it updates the index with the index of the last added data if index is not NULL.
 */
void MAM_Data_add(void *data, size_t *index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant) {
  size_t total_reqs = 0, returned_index;

  if(is_constant) { //Async
    if(is_replicated) {
      total_reqs = 1;
      add_data(data, total_qty, type, total_reqs, rep_a_data);
      returned_index = rep_a_data->entries-1;
    } else {
      if(mall_conf->red_method  == MAM_RED_BASELINE) {
        total_reqs = 1;
      } else if(mall_conf->red_method  == MAM_RED_POINT || mall_conf->red_method  == MAM_RED_RMA_LOCK || mall_conf->red_method  == MAM_RED_RMA_LOCKALL) {
        total_reqs = mall->numC;
      } 
      
      add_data(data, total_qty, type, total_reqs, dist_a_data);
      returned_index = dist_a_data->entries-1;
    }
  } else { //Sync
    if(is_replicated) {
      add_data(data, total_qty, type, total_reqs, rep_s_data);
      returned_index = rep_s_data->entries-1;
    } else {
      add_data(data, total_qty, type, total_reqs, dist_s_data);
      returned_index = dist_s_data->entries-1;
    }
  }

  if(index != NULL) *index = returned_index;
}

/*
 * This function modifies a data entry to a data structure based on whether the operation is synchronous or asynchronous,
 * and whether the data is replicated or distributed. It takes the following parameters:
 * - data: a pointer to the data to be added
 * - index: a value indicating which entry will be modified
 * - total_qty: the amount of elements in data
 * - type: the MPI datatype of the data
 * - is_replicated: a flag indicating whether the data is replicated (MAM_DATA_REPLICATED) or not (MAM_DATA_DISTRIBUTED)
 * - is_constant: a flag indicating whether the operation is asynchronous (MAM_DATA_CONSTANT) or synchronous (MAM_DATA_VARIABLE)
 */
void MAM_Data_modify(void *data, size_t index, size_t total_qty, MPI_Datatype type, int is_replicated, int is_constant) {
  size_t total_reqs = 0;

  if(is_constant) {
    if(is_replicated) {
      total_reqs = 1;
      modify_data(data, index, total_qty, type, total_reqs, rep_a_data); //FIXME total_reqs==0 ??? 
    } else {    
      if(mall_conf->red_method  == MAM_RED_BASELINE) {
        total_reqs = 1;
      } else if(mall_conf->red_method  == MAM_RED_POINT || mall_conf->red_method  == MAM_RED_RMA_LOCK || mall_conf->red_method  == MAM_RED_RMA_LOCKALL) {
        total_reqs = mall->numC;
      }
      
      modify_data(data, index, total_qty, type, total_reqs, dist_a_data);
    }
  } else {
    if(is_replicated) {
      modify_data(data, index, total_qty, type, total_reqs, rep_s_data);
    } else {
      modify_data(data, index, total_qty, type, total_reqs, dist_s_data);
    }
  }
}

/*
 * This functions returns how many data entries are available for one of the specific data structures.
 * It takes the following parameters:
 * - is_replicated: a flag indicating whether the structure is replicated (MAM_DATA_REPLICATED) or not (MAM_DATA_DISTRIBUTED)
 * - is_constant: a flag indicating whether the operation is asynchronous (MAM_DATA_CONSTANT) or synchronous (MAM_DATA_VARIABLE)
 * - entries: a pointer where the amount of entries will be stored
 */
void MAM_Data_get_entries(int is_replicated, int is_constant, size_t *entries){
  
  if(is_constant) {
    if(is_replicated) {
      *entries = rep_a_data->entries;
    } else {
      *entries = dist_a_data->entries;
    }
  } else {
    if(is_replicated) {
      *entries = rep_s_data->entries;
    } else {
      *entries = dist_s_data->entries;
    }
  }
}

/*
 * This function returns a data entry to a data structure based on whether the operation is synchronous or asynchronous,
 * and whether the data is replicated or distributed. It takes the following parameters:
 * - index: a value indicating which entry will be modified
 * - is_replicated: a flag indicating whether the data is replicated (MAM_DATA_REPLICATED) or not (MAM_DATA_DISTRIBUTED)
 * - is_constant: a flag indicating whether the operation is asynchronous (MAM_DATA_CONSTANT) or synchronous (MAM_DATA_VARIABLE)
 * - data: a pointer where the data will be stored. The user must free it
 * - total_qty: the amount of elements in data for all ranks
 * - local_qty: the amount of elements in data for this rank
 */
void MAM_Data_get_pointer(void **data, size_t index, size_t *total_qty, MPI_Datatype *type, int is_replicated, int is_constant) {
  malleability_data_t *data_struct;

  if(is_constant) {
    if(is_replicated) {
      data_struct = rep_a_data;
    } else {
      data_struct = dist_a_data;
    }
  } else {
    if(is_replicated) {
      data_struct = rep_s_data;
    } else {
      data_struct = dist_s_data;
    }
  }

  *data = data_struct->arrays[index];
  if(total_qty != NULL) *total_qty = data_struct->qty[index];
  if(type != NULL) *type = data_struct->types[index];
  //get_block_dist(qty, mall->myId, mall->numP, &dist_data); //FIXME Asegurar que numP es correcto
}

/*
 * @brief Returns a structure to perform data redistribution during a reconfiguration.
 *
 * This function is intended to be called when the state of MaM is MAM_I_USER_PENDING only. 
 * It is designed to provide the necessary information for the user to perform data redistribution.
 *
 * Parameters:
 *   - mam_user_reconf_t *reconf_info: A pointer to a mam_user_reconf_t structure where the function will store the required information for data redistribution.
 *
 * Return Value:
 *   - MAM_OK: If the function successfully retrieves the reconfiguration information.
 *   - MAM_DENIED: If the function is called when the state of the MaM is not MAM_I_USER_PENDING.
 */
int MAM_Get_Reconf_Info(mam_user_reconf_t *reconf_info) {
  if(state != MAM_I_USER_PENDING) return MAM_DENIED;

  *reconf_info = *user_reconf;
  return MAM_OK;
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//================DATA COMMUNICATION====================||
//======================================================||
//======================================================||

/*
 * Funcion generalizada para enviar datos desde los hijos.
 * La asincronizidad se refiere a si el hilo padre e hijo lo hacen
 * de forma bloqueante o no. El padre puede tener varios hilos.
 */
void send_data(int numP_children, malleability_data_t *data_struct, int is_asynchronous) {
  size_t i;
  void *aux_send, *aux_recv;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux_send = data_struct->arrays[i];
      aux_recv = NULL;
      async_communication_start(aux_send, &aux_recv, data_struct->qty[i], data_struct->types[i], mall->numP, numP_children, MAM_SOURCES,  
		      mall->intercomm, &(data_struct->requests[i]), &(data_struct->request_qty[i]), &(data_struct->windows[i]));
      if(aux_recv != NULL) data_struct->arrays[i] = aux_recv;
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux_send = data_struct->arrays[i];
      aux_recv = NULL;
      sync_communication(aux_send, &aux_recv, data_struct->qty[i], data_struct->types[i], mall->numP, numP_children, MAM_SOURCES, mall->intercomm);
      if(aux_recv != NULL) data_struct->arrays[i] = aux_recv;
    }
  }
}

/*
 * Funcion generalizada para recibir datos desde los hijos.
 * La asincronizidad se refiere a si el hilo padre e hijo lo hacen
 * de forma bloqueante o no. El padre puede tener varios hilos.
 */
void recv_data(int numP_parents, malleability_data_t *data_struct, int is_asynchronous) {
  size_t i;
  void *aux, *aux_s = NULL;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux = data_struct->arrays[i];
      async_communication_start(aux_s, &aux, data_struct->qty[i], data_struct->types[i], mall->numP, numP_parents, MAM_TARGETS,
		      mall->intercomm, &(data_struct->requests[i]), &(data_struct->request_qty[i]), &(data_struct->windows[i]));
      data_struct->arrays[i] = aux;
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux = data_struct->arrays[i];
      sync_communication(aux_s, &aux, data_struct->qty[i], data_struct->types[i], mall->numP, numP_parents, MAM_TARGETS, mall->intercomm);
      data_struct->arrays[i] = aux;
    }
  }
}


//======================================================||
//================PRIVATE FUNCTIONS=====================||
//====================MAM STAGES========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||

int MAM_St_rms(int *mam_state) {
  reset_malleability_times();
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->malleability_start = MPI_Wtime();

  MAM_Check_configuration();
  *mam_state = MAM_NOT_STARTED;
  state = MAM_I_RMS_COMPLETED;
  mall->wait_targets_posted = 0;

  //if(CHECK_RMS()) {return MAM_DENIED;}    
  return 1;
}

int MAM_St_spawn_start() {
  mall->num_parents = mall->numP;
  state = spawn_step();
  //FIXME Esto es necesario pero feo
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->myId >= mall->numC){ mall->zombie = 1; }
  else if(mall_conf->spawn_method == MAM_SPAWN_BASELINE){ mall->zombie = 1; }

  if (state == MAM_I_SPAWN_COMPLETED || state == MAM_I_SPAWN_ADAPT_POSTPONE){
    return 1;
  }
  return 0;
}

int MAM_St_spawn_pending(int wait_completed) {
  state = check_spawn_state(&(mall->intercomm), mall->comm, wait_completed);
  if (state == MAM_I_SPAWN_COMPLETED || state == MAM_I_SPAWN_ADAPTED) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->comm);
    #endif
    mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->malleability_start;
    return 1;
  }
  return 0;
}

int MAM_St_red_start() {
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    mall->root_collectives = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;
  } else {
    mall->root_collectives = mall->root;
  }

  state = start_redistribution();
  return 1;
}

int MAM_St_red_pending(int wait_completed) {
  if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
    state = thread_check(wait_completed);
  } else {
    state = check_redistribution(wait_completed);
  }

  if(state != MAM_I_DIST_PENDING) { 
    state = MAM_I_USER_START;
    return 1;
  }
  return 0;
}

int MAM_St_user_start(int *mam_state) {
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  mall_conf->times->user_start = MPI_Wtime(); // Obtener timestamp de cuando termina user redist
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    MPI_Intercomm_merge(mall->intercomm, MAM_SOURCES, &mall->tmp_comm); //El que pone 0 va primero
  } else {
    MPI_Comm_dup(mall->intercomm, &mall->tmp_comm);
  }
  MPI_Comm_set_name(mall->tmp_comm, "MAM_USER_TMP");
  state = MAM_I_USER_PENDING;
  *mam_state = MAM_USER_PENDING;
  return 1;
}

int MAM_St_user_pending(int *mam_state, int wait_completed, void (*user_function)(void *), void *user_args) {
  #if MAM_DEBUG
    if(mall->myId == mall->root) DEBUG_FUNC("Starting USER redistribution", mall->myId, mall->numP); fflush(stdout);
  #endif
  if(user_function != NULL) {
    MAM_I_create_user_struct(MAM_SOURCES);
    do {
      user_function(user_args);
    } while(wait_completed && state == MAM_I_USER_PENDING);
  } else {
    MAM_Resume_redistribution(mam_state);
  }

  if(state != MAM_I_USER_PENDING) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->user_end = MPI_Wtime(); // Obtener timestamp de cuando termina user redist
    #if MAM_DEBUG
      if(mall->myId == mall->root) DEBUG_FUNC("Ended USER redistribution", mall->myId, mall->numP); fflush(stdout);
    #endif
    return 1;
  }
  return 0;
}

int MAM_St_user_completed() {
  state = end_redistribution();
  return 1;
}

int MAM_St_spawn_adapt_pending(int wait_completed) {
  wait_completed = MAM_WAIT_COMPLETION;
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_start = MPI_Wtime();
  unset_spawn_postpone_flag(state);
  state = check_spawn_state(&(mall->intercomm), mall->comm, wait_completed);
/* TODO Comentar problema, basicamente indicar que no es posible de la forma actual
 * Ademas es solo para una operación que hemos visto como "extremadamente" rápida
 * NO es posible debido a que solo se puede hacer tras enviar los datos variables 
 * y por tanto pierden validez dichos datos
  if(!MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, NULL)) {
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->comm);
    #endif
    mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->spawn_start;
    return 1;
  }
  return 0;
  */
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->spawn_start;
  return 1;
}

int MAM_St_completed(int *mam_state) {
  MAM_Commit(mam_state);
  return 0;
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
/*
 * Inicializacion de los datos de los hijos.
 * En la misma se reciben datos de los padres: La configuracion
 * de la ejecucion a realizar; y los datos a recibir de los padres
 * ya sea de forma sincrona, asincrona o ambas.
 */
void Children_init(void (*user_function)(void *), void *user_args) {
  size_t i;

  #if MAM_DEBUG
    DEBUG_FUNC("MaM will now initialize spawned processes", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  malleability_connect_children(&(mall->intercomm));
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) { // For Merge Method, these processes will be added
    MPI_Comm_rank(mall->intercomm, &mall->myId);
    MPI_Comm_size(mall->intercomm, &mall->numP);
  }
  mall->root_collectives = mall->root_parents;

  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL)
    || MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, NULL)) {
    mall->internode_group = 0;
  } else {
    mall->internode_group = MAM_Is_internode_group();
  }

  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed spawn step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  comm_data_info(rep_a_data, dist_a_data, MAM_TARGETS);
  if(dist_a_data->entries || rep_a_data->entries) { // Recibir datos asincronos
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
        async_communication_end(dist_a_data->requests[i], dist_a_data->request_qty[i], &(dist_a_data->windows[i]));
      }
      for(i=0; i<rep_a_data->entries; i++) {
        async_communication_end(rep_a_data->requests[i], rep_a_data->request_qty[i], &(rep_a_data->windows[i]));
      }
    }

    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->async_end= MPI_Wtime(); // Obtener timestamp de cuando termina comm asincrona
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Spawned have completed asynchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    MPI_Intercomm_merge(mall->intercomm, MAM_TARGETS, &mall->tmp_comm); //El que pone 0 va primero
  } else {
    MPI_Comm_dup(mall->intercomm, &mall->tmp_comm);
  }
  MPI_Comm_set_name(mall->tmp_comm, "MAM_USER_TMP");
  if(user_function != NULL) {
    state = MAM_I_USER_PENDING;
    MAM_I_create_user_struct(MAM_TARGETS);
    user_function(user_args);
  }
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  mall_conf->times->user_end = MPI_Wtime(); // Obtener timestamp de cuando termina user redist

  comm_data_info(rep_s_data, dist_s_data, MAM_TARGETS);
  if(dist_s_data->entries || rep_s_data->entries) { // Recibir datos sincronos
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
    mall_conf->times->sync_end = MPI_Wtime(); // Obtener timestamp de cuando termina comm sincrona
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Targets have completed synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  MAM_Commit(NULL);

  #if MAM_DEBUG
    DEBUG_FUNC("MaM has been initialized correctly for new ranks", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================PARENTS==========================||
//======================================================||
//======================================================||
//======================================================||
//======================================================||

/*
 * Se encarga de realizar la creacion de los procesos hijos.
 * Si se pide en segundo plano devuelve el estado actual.
 */
int spawn_step(){
  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->comm);
  #endif
  mall_conf->times->spawn_start = MPI_Wtime();
 
  state = init_spawn(mall->thread_comm, &(mall->intercomm));

  if(!MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, NULL)) {
      #if MAM_USE_BARRIERS
        MPI_Barrier(mall->comm);
      #endif
      mall_conf->times->spawn_time = MPI_Wtime() - mall_conf->times->malleability_start;
  }
  return state;
}


/*
 * Comienza la redistribucion de los datos con el nuevo grupo de procesos.
 *
 * Primero se envia la configuracion a utilizar al nuevo grupo de procesos y a continuacion
 * se realiza el envio asincrono y/o sincrono si lo hay.
 *
 * En caso de que haya comunicacion asincrona, se comienza y se termina la funcion 
 * indicando que se ha comenzado un envio asincrono.
 *
 * Si no hay comunicacion asincrono se pasa a realizar la sincrona si la hubiese.
 *
 * Finalmente se envian datos sobre los resultados a los hijos y se desconectan ambos
 * grupos de procesos.
 */
int start_redistribution() {
  size_t i;

  if(mall->intercomm == MPI_COMM_NULL) {
    // Si no tiene comunicador creado, se debe a que se ha pospuesto el Spawn
    //   y se trata del spawn Merge Shrink
    MPI_Comm_dup(mall->comm, &(mall->intercomm));
  }

  comm_data_info(rep_a_data, dist_a_data, MAM_SOURCES);
  if(dist_a_data->entries || rep_a_data->entries) { // Enviar datos asincronos
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->async_start = MPI_Wtime();
    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_PTHREAD, NULL)) {
      return thread_creation();
    } else {
      send_data(mall->numC, dist_a_data, MAM_USE_ASYNCHRONOUS);
      for(i=0; i<rep_a_data->entries; i++) {
        MPI_Ibcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm, &(rep_a_data->requests[i][0]));
      } 

      if(mall->zombie && MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
      }
      return MAM_I_DIST_PENDING; 
    }
  } 
  return MAM_I_USER_START;
}


/*
 * Comprueba si la redistribucion asincrona ha terminado. 
 * Si no ha terminado la funcion termina indicandolo, en caso contrario,
 * se continua con la comunicacion sincrona, el envio de resultados y
 * se desconectan los grupos de procesos.
 *
 * Esta funcion permite dos modos de funcionamiento al comprobar si la
 * comunicacion asincrona ha terminado.
 * Si se utiliza el modo "MAL_USE_NORMAL" o "MAL_USE_POINT", se considera 
 * terminada cuando los padres terminan de enviar.
 * Si se utiliza el modo "MAL_USE_IBARRIER", se considera terminada cuando
 * los hijos han terminado de recibir.
 * //FIXME Modificar para que se tenga en cuenta rep_a_data
 */
int check_redistribution(int wait_completed) {
  int completed, local_completed, all_completed;
  size_t i, req_qty;
  MPI_Request *req_completed;
  MPI_Win window;
  local_completed = 1;
  #if MAM_DEBUG >= 2
    DEBUG_FUNC("Sources are testing for all asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  if(wait_completed) {
    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL) && !mall->wait_targets_posted) {
      MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
      mall->wait_targets_posted = 1;
    }
    for(i=0; i<dist_a_data->entries; i++) {
      req_completed = dist_a_data->requests[i];
      req_qty = dist_a_data->request_qty[i];
      async_communication_wait(req_completed, req_qty);
    }
    for(i=0; i<rep_a_data->entries; i++) {
      req_completed = rep_a_data->requests[i];
      req_qty = rep_a_data->request_qty[i];
      async_communication_wait(req_completed, req_qty);
    }

    if(MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) { MPI_Wait(&mall->wait_targets, MPI_STATUS_IGNORE); }
  } else {
    if(mall->wait_targets_posted) { 
      MPI_Test(&mall->wait_targets, &local_completed, MPI_STATUS_IGNORE); 
    } else {
      for(i=0; i<dist_a_data->entries; i++) {
        req_completed = dist_a_data->requests[i];
        req_qty = dist_a_data->request_qty[i];
        completed = async_communication_check(MAM_SOURCES, req_completed, req_qty);
        local_completed = local_completed && completed;
      }
      for(i=0; i<rep_a_data->entries; i++) {
        req_completed = rep_a_data->requests[i];
        req_qty = rep_a_data->request_qty[i];
        completed = async_communication_check(MAM_SOURCES, req_completed, req_qty);
        local_completed = local_completed && completed;
      }

      if(local_completed && MAM_Contains_strat(MAM_RED_STRATEGIES, MAM_STRAT_RED_WAIT_TARGETS, NULL)) {
        MPI_Ibarrier(mall->intercomm, &mall->wait_targets);
        mall->wait_targets_posted = 1;
        MPI_Test(&mall->wait_targets, &local_completed, MPI_STATUS_IGNORE); //TODO - Figure out if last process takes profit from calling here
      }
    }
    #if MAM_DEBUG >= 2
      DEBUG_FUNC("Sources will now check a global decision", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
    #endif

    MPI_Allreduce(&local_completed, &all_completed, 1, MPI_INT, MPI_MIN, mall->comm);
    if(!all_completed) return MAM_I_DIST_PENDING; // Continue only if asynchronous send has ended 
  }

  #if MAM_DEBUG >= 2
    DEBUG_FUNC("Sources sent asynchronous redistributions", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(MPI_COMM_WORLD);
  #endif

  for(i=0; i<dist_a_data->entries; i++) {
    req_completed = dist_a_data->requests[i];
    req_qty = dist_a_data->request_qty[i];
    window = dist_a_data->windows[i];
    async_communication_end(req_completed, req_qty, &window);
  }
  for(i=0; i<rep_a_data->entries; i++) {
    req_completed = rep_a_data->requests[i];
    req_qty = rep_a_data->request_qty[i];
    window = rep_a_data->windows[i];
    async_communication_end(req_completed, req_qty, &window);
  }

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->async_end = MPI_Wtime(); // Merge method only
  return MAM_I_USER_START;
}

/*
 * Termina la redistribución de los datos con los hijos, comprobando
 * si se han realizado iteraciones con comunicaciones en segundo plano
 * y enviando cuantas iteraciones se han realizado a los hijos.
 *
 * Además se realizan las comunicaciones síncronas se las hay.
 * Finalmente termina enviando los datos temporales a los hijos.
 */ 
int end_redistribution() {
  size_t i;
  int local_state;

  comm_data_info(rep_s_data, dist_s_data, MAM_SOURCES);
  if(dist_s_data->entries || rep_s_data->entries) { // Enviar datos sincronos
    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    mall_conf->times->sync_start = MPI_Wtime();
    send_data(mall->numC, dist_s_data, MAM_USE_SYNCHRONOUS);

    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], rep_s_data->types[i], mall->root_collectives, mall->intercomm);
    }

    #if MAM_USE_BARRIERS
      MPI_Barrier(mall->intercomm);
    #endif
    if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->sync_end = MPI_Wtime(); // Merge method only
  }
  #if MAM_DEBUG
    DEBUG_FUNC("Sources have completed synchronous data redistribution step", mall->myId, mall->numP); fflush(stdout); MPI_Barrier(mall->comm);
  #endif

  local_state = MAM_I_DIST_COMPLETED;
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE && mall->numP > mall->numC) { // Merge Shrink
    local_state = MAM_I_SPAWN_ADAPT_PENDING;
  }

  return local_state;
}

// TODO MOVER A OTRO LADO??
//======================================================||
//================PRIVATE FUNCTIONS=====================||
//===============COMM PARENTS THREADS===================||
//======================================================||
//======================================================||


int comm_state; //FIXME Usar un handler
/*
 * Crea una hebra para ejecutar una comunicación en segundo plano.
 */
int thread_creation() {
  comm_state = MAM_I_DIST_PENDING;
  if(pthread_create(&(mall->async_thread), NULL, thread_async_work, NULL)) {
    printf("Error al crear el hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return comm_state;
}

/*
 * Comprobación por parte de una hebra maestra que indica
 * si una hebra esclava ha terminado su comunicación en segundo plano.
 *
 * El estado de la comunicación es devuelto al finalizar la función. 
 */
int thread_check(int wait_completed) {
  int all_completed = 0;

  if(wait_completed && comm_state == MAM_I_DIST_PENDING) {
    if(pthread_join(mall->async_thread, NULL)) {
      printf("Error al esperar al hilo\n");
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -2;
    } 
  }

  // Comprueba que todos los hilos han terminado la distribucion (Mismo valor en commAsync)
  MPI_Allreduce(&comm_state, &all_completed, 1, MPI_INT, MPI_MAX, mall->comm);
  if(all_completed != MAM_I_DIST_COMPLETED) return MAM_I_DIST_PENDING; // Continue only if asynchronous send has ended 

  if(pthread_join(mall->async_thread, NULL)) {
    printf("Error al esperar al hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -2;
  } 

  #if MAM_USE_BARRIERS
    MPI_Barrier(mall->intercomm);
  #endif
  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) mall_conf->times->async_end = MPI_Wtime(); // Merge method only
  return MAM_I_USER_START;
}


/*
 * Función ejecutada por una hebra.
 * Ejecuta una comunicación síncrona con los hijos que
 * para el usuario se puede considerar como en segundo plano.
 *
 * Cuando termina la comunicación la hebra maestra puede comprobarlo
 * por el valor "commAsync".
 */
void* thread_async_work() {
  size_t i;

  send_data(mall->numC, dist_a_data, MAM_USE_SYNCHRONOUS);
  for(i=0; i<rep_a_data->entries; i++) {
    MPI_Bcast(rep_a_data->arrays[i], rep_a_data->qty[i], rep_a_data->types[i], mall->root_collectives, mall->intercomm);
  } 
  comm_state = MAM_I_DIST_COMPLETED;
  pthread_exit(NULL);
}


//==============================================================================

/*
 * TODO Por hacer
 */
void MAM_I_create_user_struct(int is_children_group) {
  user_reconf->comm = mall->tmp_comm;

  if(is_children_group) {
    user_reconf->rank_state = MAM_PROC_NEW_RANK;
    user_reconf->numS = mall->num_parents;
    user_reconf->numT = mall->numP;
  } else {
    user_reconf->numS = mall->numP;
    user_reconf->numT = mall->numC;
    if(mall->zombie) user_reconf->rank_state = MAM_PROC_ZOMBIE;
    else user_reconf->rank_state = MAM_PROC_CONTINUE;
  }
}
