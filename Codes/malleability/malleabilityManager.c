#include <pthread.h>
#include <string.h>
#include "malleabilityManager.h"
#include "malleabilityStates.h"
#include "malleabilityDataStructures.h"
#include "malleabilityTypes.h"
#include "malleabilityZombies.h"
#include "spawn_methods/GenericSpawn.h"
#include "CommDist.h"

#define MALLEABILITY_USE_SYNCHRONOUS 0
#define MALLEABILITY_USE_ASYNCHRONOUS 1


void send_data(int numP_children, malleability_data_t *data_struct, int is_asynchronous);
void recv_data(int numP_parents, malleability_data_t *data_struct, int is_asynchronous);

void Children_init();
int spawn_step();
int start_redistribution();
int check_redistribution();
int end_redistribution();
int shrink_redistribution();

void comm_node_data(int rootBcast, int is_child_group);
void def_nodeinfo_type(MPI_Datatype *node_type);

int thread_creation();
int thread_check();
void* thread_async_work();

void print_comms_state();

typedef struct {
  int spawn_method;
  int spawn_dist;
  int spawn_strategies;
  //int spawn_is_single;
  //int spawn_threaded;
  int comm_type;
  int comm_threaded;

  int grp;
  configuration *config_file;
  results_data *results;
} malleability_config_t;

typedef struct { //FIXME numC_spawned no se esta usando
  int myId, numP, numC, numC_spawned, root, root_parents;
  pthread_t async_thread;
  MPI_Comm comm, thread_comm;
  MPI_Comm intercomm;
  MPI_Comm user_comm;
  
  char *name_exec, *nodelist;
  int num_cpus, num_nodes, nodelist_len;
} malleability_t;

int state = MALL_UNRESERVED; //FIXME Mover a otro lado

malleability_config_t *mall_conf;
malleability_t *mall;

malleability_data_t *rep_s_data;
malleability_data_t *dist_s_data;
malleability_data_t *rep_a_data;
malleability_data_t *dist_a_data;

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
int init_malleability(int myId, int numP, int root, MPI_Comm comm, char *name_exec, char *nodelist, int num_cpus, int num_nodes) {
  MPI_Comm dup_comm, thread_comm;

  mall_conf = (malleability_config_t *) malloc(sizeof(malleability_config_t));
  mall = (malleability_t *) malloc(sizeof(malleability_t));
  rep_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_s_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  rep_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));
  dist_a_data = (malleability_data_t *) malloc(sizeof(malleability_data_t));

  MPI_Comm_dup(comm, &dup_comm);
  MPI_Comm_dup(comm, &thread_comm);
  MPI_Comm_set_name(dup_comm, "MPI_COMM_MALL");
  MPI_Comm_set_name(thread_comm, "MPI_COMM_MALL_THREAD");

  mall->myId = myId;
  mall->numP = numP;
  mall->root = root;
  mall->comm = dup_comm;
  mall->thread_comm = thread_comm;
  mall->user_comm = comm;

  mall->name_exec = name_exec;
  mall->nodelist = nodelist;
  mall->num_cpus = num_cpus;
  mall->num_nodes = num_nodes;

  rep_s_data->entries = 0;
  rep_a_data->entries = 0;
  dist_s_data->entries = 0;
  dist_a_data->entries = 0;

  state = MALL_NOT_STARTED;

  zombies_service_init();

  // Si son el primer grupo de procesos, obtienen los datos de los padres
  MPI_Comm_get_parent(&(mall->intercomm));
  if(mall->intercomm != MPI_COMM_NULL ) { 
    Children_init();
    return MALLEABILITY_CHILDREN;
  }

  if(nodelist != NULL) { //TODO To be deprecated by using Slurm or else statement
    mall->nodelist_len = strlen(nodelist);
  } else { // If no nodelist is detected, get it from the actual run
    mall->nodelist = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
    MPI_Get_processor_name(mall->nodelist, &mall->nodelist_len);
    //TODO Get name of each process and create real nodelist
  }

  return MALLEABILITY_NOT_CHILDREN;
}

/*
 * Elimina toda la memoria reservado por el modulo
 * de maleabilidad y asegura que los zombies
 * despierten si los hubiese.
 */
void free_malleability() {	  
  free_malleability_data_struct(rep_s_data);
  free_malleability_data_struct(rep_a_data);
  free_malleability_data_struct(dist_s_data);
  free_malleability_data_struct(dist_a_data);

  free(rep_s_data);
  free(rep_a_data); // FIXME corrupted size if n_stage>1
  free(dist_s_data); // FIXME corrupted size if n_stage>1
  free(dist_a_data); // FIXME corrupted size if n_stage>1

  if(mall->comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->comm));
  if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));
  free(mall);
  free(mall_conf);

  zombies_awake();
  zombies_service_free();

  state = MALL_UNRESERVED;
}

/* 
 * TODO Reescribir
 * Se realiza el redimensionado de procesos por parte de los padres.
 *
 * Se crean los nuevos procesos con la distribucion fisica elegida y
 * a continuacion se transmite la informacion a los mismos.
 *
 * Si hay datos asincronos a transmitir, primero se comienza a
 * transmitir estos y se termina la funcion. Se tiene que comprobar con
 * llamando a la función de nuevo que se han terminado de enviar
 *
 * Si hay ademas datos sincronos a enviar, no se envian aun.
 *
 * Si solo hay datos sincronos se envian tras la creacion de los procesos
 * y finalmente se desconectan los dos grupos de procesos.
 */
int malleability_checkpoint() {
  double end_real_time;

  switch(state) {
    case MALL_UNRESERVED:
      break;
    case MALL_NOT_STARTED:
      // Comprobar si se tiene que realizar un redimensionado
      //if(CHECK_RMS()) {return MALL_DENIED;}

      state = spawn_step();

      if (state == MALL_SPAWN_COMPLETED || state == MALL_SPAWN_ADAPT_POSTPONE){
        malleability_checkpoint();
      }
      break;

    case MALL_SPAWN_PENDING: // Comprueba si el spawn ha terminado y comienza la redistribucion
    case MALL_SPAWN_SINGLE_PENDING:
      state = check_spawn_state(&(mall->intercomm), mall->comm, &end_real_time);
      if (state == MALL_SPAWN_COMPLETED || state == MALL_SPAWN_ADAPTED) {
        mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
        mall_conf->results->spawn_real_time[mall_conf->grp] = end_real_time - mall_conf->results->spawn_start;

        malleability_checkpoint();
      }
      break;

    case MALL_SPAWN_ADAPT_POSTPONE:
    case MALL_SPAWN_COMPLETED:
      state = start_redistribution();
      malleability_checkpoint();
      break;

    case MALL_DIST_PENDING:
      if(mall_conf->comm_type == MAL_USE_THREAD) {
        state = thread_check();
      } else {
        state = check_redistribution();
      }
      if(state != MALL_DIST_PENDING) {
        malleability_checkpoint();
      }
      break;

    case MALL_SPAWN_ADAPT_PENDING:
      mall_conf->results->spawn_start = MPI_Wtime();
      unset_spawn_postpone_flag(state);
      state = check_spawn_state(&(mall->intercomm), mall->comm, &end_real_time);

      if(!malleability_spawn_contains_strat(mall_conf->spawn_strategies, MALL_SPAWN_PTHREAD, NULL)) {
        mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
      }
      break;

    case MALL_SPAWN_ADAPTED:
      state = shrink_redistribution();
      malleability_checkpoint();
      break;

    case MALL_DIST_COMPLETED: //TODO No es esto muy feo?
      state = MALL_COMPLETED;
      break;
  }
  return state;
}

// Funciones solo necesarias por el benchmark
//-------------------------------------------------------------------------------------------------------------
void set_benchmark_grp(int grp) {
  mall_conf->grp = grp;
}

void set_benchmark_configuration(configuration *config_file) {
  mall_conf->config_file = config_file;
}

void get_benchmark_configuration(configuration **config_file) {
  *config_file = mall_conf->config_file;
}

void set_benchmark_results(results_data *results) {
  mall_conf->results = results;
}

void get_benchmark_results(results_data **results) {
  *results = mall_conf->results;
}
//-------------------------------------------------------------------------------------------------------------

void set_malleability_configuration(int spawn_method, int spawn_strategies, int spawn_dist, int comm_type, int comm_threaded) {
  mall_conf->spawn_method = spawn_method;
  mall_conf->spawn_strategies = spawn_strategies;
  mall_conf->spawn_dist = spawn_dist;
  mall_conf->comm_type = comm_type;
  mall_conf->comm_threaded = comm_threaded;
}

/*
 * To be deprecated
 * Tiene que ser llamado despues de setear la config
 */
void set_children_number(int numC){
  if((mall_conf->spawn_method == MALL_SPAWN_MERGE) && (numC >= mall->numP)) {
    mall->numC = numC;
    mall->numC_spawned = numC - mall->numP;

    if(numC == mall->numP) { // Migrar
      mall->numC_spawned = numC;
      mall_conf->spawn_method = MALL_SPAWN_BASELINE;
    }
  } else {
    mall->numC = numC;
    mall->numC_spawned = numC;
  }
}

/*
 * TODO
 */
void get_malleability_user_comm(MPI_Comm *comm) {
  *comm = mall->user_comm;
}

/*
 * Anyade a la estructura concreta de datos elegida
 * el nuevo set de datos "data" de un total de "total_qty" elementos.
 *
 * Los datos variables se tienen que anyadir cuando quieran ser mandados, no antes
 *
 * Mas informacion en la funcion "add_data".
 */
void malleability_add_data(void *data, size_t total_qty, int type, int is_replicated, int is_constant) {

  if(is_constant) {
    if(is_replicated) {
      add_data(data, total_qty, type, 0, rep_s_data); //FIXME Numero magico
    } else {
      add_data(data, total_qty, type, 0, dist_s_data); //FIXME Numero magico
    }
  } else {
    if(is_replicated) {
      add_data(data, total_qty, type, 0, rep_a_data); //FIXME Numero magico || Un request?
    } else {
      size_t total_reqs = 0;
      
      if(mall_conf->comm_type  == MAL_USE_NORMAL) {
        total_reqs = 1;
      } else if(mall_conf->comm_type  == MAL_USE_IBARRIER) {
        total_reqs = 2;
      } else if(mall_conf->comm_type  == MAL_USE_POINT) {
        total_reqs = mall->numC;
      }
      
      add_data(data, total_qty, type, total_reqs, dist_a_data);
    }
  }
}

/*
 * Modifica en la estructura concreta de datos elegida en el indice "index"
 * con el set de datos "data" de un total de "total_qty" elementos.
 *
 * Los datos variables se tienen que modificar cuando quieran ser mandados, no antes
 *
 * Mas informacion en la funcion "modify_data".
 */
void malleability_modify_data(void *data, size_t index, size_t total_qty, int type, int is_replicated, int is_constant) {
  if(is_constant) {
    if(is_replicated) {
      modify_data(data, index, total_qty, type, 0, rep_s_data); //FIXME Numero magico
    } else {
      modify_data(data, index, total_qty, type, 0, dist_s_data); //FIXME Numero magico
    }
  } else {
    if(is_replicated) {
      modify_data(data, index, total_qty, type, 0, rep_a_data); //FIXME Numero magico || UN request?
    } else {
      size_t total_reqs = 0;
      
      if(mall_conf->comm_type  == MAL_USE_NORMAL) {
        total_reqs = 1;
      } else if(mall_conf->comm_type  == MAL_USE_IBARRIER) {
        total_reqs = 2;
      } else if(mall_conf->comm_type  == MAL_USE_POINT) {
        total_reqs = mall->numC;
      }
      
      modify_data(data, index, total_qty, type, total_reqs, dist_a_data); //FIXME Numero magico
    }
  }
}

/*
 * Devuelve el numero de entradas para la estructura de descripcion de 
 * datos elegida.
 */
void malleability_get_entries(size_t *entries, int is_replicated, int is_constant){
  
  if(is_constant) {
    if(is_replicated) {
      *entries = rep_s_data->entries;
    } else {
      *entries = dist_s_data->entries;
    }
  } else {
    if(is_replicated) {
      *entries = rep_a_data->entries;
    } else {
      *entries = dist_a_data->entries;
    }
  }
}

/*
 * Devuelve el elemento de la lista "index" al usuario.
 * La devolución es en el mismo orden que lo han metido los padres
 * con la funcion "malleability_add_data()".
 * Es tarea del usuario saber el tipo de esos datos.
 * TODO Refactor a que sea automatico
 */
void malleability_get_data(void **data, int index, int is_replicated, int is_constant) {
  malleability_data_t *data_struct;

  if(is_constant) {
    if(is_replicated) {
      data_struct = rep_s_data;
    } else {
      data_struct = dist_s_data;
    }
  } else {
    if(is_replicated) {
      data_struct = rep_a_data;
    } else {
      data_struct = dist_a_data;
    }
  }

  *data = data_struct->arrays[index];
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
  char *aux;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      send_async(aux, data_struct->qty[i], mall->myId, mall->numP, mall->intercomm, numP_children, data_struct->requests, mall_conf->comm_type);
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      send_sync(aux, data_struct->qty[i], mall->myId, mall->numP, mall->intercomm, numP_children);
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
  char *aux;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      recv_async(&aux, data_struct->qty[i], mall->myId, mall->numP, mall->intercomm, numP_parents, mall_conf->comm_type);
      data_struct->arrays[i] = (void *) aux;
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      recv_sync(&aux, data_struct->qty[i], mall->myId, mall->numP, mall->intercomm, numP_parents);
      data_struct->arrays[i] = (void *) aux;
    }
  }
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================CHILDREN=========================||
//======================================================||
//======================================================||

/*
 * Inicializacion de los datos de los hijos.
 * En la misma se reciben datos de los padres: La configuracion
 * de la ejecucion a realizar; y los datos a recibir de los padres
 * ya sea de forma sincrona, asincrona o ambas.
 */
void Children_init() {
  size_t i;
  int numP_parents, root_parents;
  int is_intercomm;

  malleability_connect_children(mall->myId, mall->numP, mall->root, mall->comm, &numP_parents, &root_parents, &(mall->intercomm));
  MPI_Comm_test_inter(mall->intercomm, &is_intercomm);
  // TODO A partir de este punto tener en cuenta si es BASELINE o MERGE

  recv_config_file(mall->root, mall->intercomm, &(mall_conf->config_file));
  comm_node_data(root_parents, MALLEABILITY_CHILDREN);

  mall_conf->results = (results_data *) malloc(sizeof(results_data));
  init_results_data(mall_conf->results, mall_conf->config_file->n_resizes, mall_conf->config_file->n_stages, RESULTS_INIT_DATA_QTY);

  comm_data_info(rep_a_data, dist_a_data, MALLEABILITY_CHILDREN, mall->myId, root_parents, mall->intercomm);
  if(dist_a_data->entries || rep_a_data->entries) { // Recibir datos asincronos

    if(mall_conf->comm_type == MAL_USE_NORMAL || mall_conf->comm_type == MAL_USE_IBARRIER || mall_conf->comm_type == MAL_USE_POINT) {
      recv_data(numP_parents, dist_a_data, 1);

    } else if (mall_conf->comm_type == MAL_USE_THREAD) { //TODO Modificar uso para que tenga sentido comm_threaded
      recv_data(numP_parents, dist_a_data, 0);
    }
    mall_conf->results->async_end= MPI_Wtime(); // Obtener timestamp de cuando termina comm asincrona
  }
  
  comm_data_info(rep_s_data, dist_s_data, MALLEABILITY_CHILDREN, mall->myId, root_parents, mall->intercomm);
  if(dist_s_data->entries || rep_s_data->entries) { // Recibir datos sincronos
    recv_data(numP_parents, dist_s_data, 0);

    mall_conf->results->sync_end = MPI_Wtime(); // Obtener timestamp de cuando termina comm sincrona

    // TODO Crear funcion especifica y anyadir para Asinc
    // TODO Tener en cuenta el tipo y qty
    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Datatype datatype;
      if(rep_s_data->types[i] == MAL_INT) {
        datatype = MPI_INT;
      } else {
        datatype = MPI_CHAR;
      }
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], datatype, root_parents, mall->intercomm);
    } 
  }

  // Guardar los resultados de esta transmision
  comm_results(mall_conf->results, mall->root, mall_conf->config_file->n_resizes, mall->intercomm);
  if(!is_intercomm) {
    if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));
    if(mall->comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->comm));
    if(mall->user_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->user_comm)); //TODO No es peligroso?

    MPI_Comm_dup(mall->intercomm, &(mall->thread_comm));
    MPI_Comm_dup(mall->intercomm, &(mall->comm));
    MPI_Comm_dup(mall->intercomm, &(mall->user_comm)); 
  }

  MPI_Comm_disconnect(&(mall->intercomm));
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=====================PARENTS==========================||
//======================================================||
//======================================================||

/*
 * Se encarga de realizar la creacion de los procesos hijos.
 * Si se pide en segundo plano devuelve el estado actual.
 */
int spawn_step(){
  mall_conf->results->spawn_start = MPI_Wtime();
 
  state = init_spawn(mall->name_exec, mall->num_cpus, mall->num_nodes, mall->nodelist, mall->myId, mall->numP, mall->numC, mall->root, mall_conf->spawn_dist, mall_conf->spawn_method, mall_conf->spawn_strategies, mall->thread_comm, &(mall->intercomm));

  if(!malleability_spawn_contains_strat(mall_conf->spawn_strategies, MALL_SPAWN_PTHREAD, NULL)) {
      mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
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
  int rootBcast, is_intercomm;

  is_intercomm = 0;
  if(mall->intercomm != MPI_COMM_NULL) {
    MPI_Comm_test_inter(mall->intercomm, &is_intercomm);
  } else { 
    // Si no tiene comunicador creado, se debe a que se ha pospuesto el Spawn
    //   y se trata del spawn Merge Shrink
    MPI_Comm_dup(mall->comm, &(mall->intercomm));
  }

  if(is_intercomm) {
    rootBcast = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;
  } else {
    rootBcast = mall->root;
  }

  send_config_file(mall_conf->config_file, rootBcast, mall->intercomm);
  comm_node_data(rootBcast, MALLEABILITY_NOT_CHILDREN);

  comm_data_info(rep_a_data, dist_a_data, MALLEABILITY_NOT_CHILDREN, mall->myId, mall->root, mall->intercomm);
  if(dist_a_data->entries || rep_a_data->entries) { // Enviar datos asincronos
    mall_conf->results->async_time[mall_conf->grp] = MPI_Wtime();
    if(mall_conf->comm_type == MAL_USE_THREAD) {
      return thread_creation();
    } else {
      send_data(mall->numC, dist_a_data, MALLEABILITY_USE_ASYNCHRONOUS);
      return MALL_DIST_PENDING;
    }
  } 
  return end_redistribution();
}


/*
 * @deprecated
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
 */
int check_redistribution() {
  int completed, all_completed, test_err;
  MPI_Request *req_completed;
//dist_a_data->requests[0][X] //FIXME Numero magico 0 -- Modificar para que sea un for?

  if (mall_conf->comm_type == MAL_USE_POINT) {
    test_err = MPI_Testall(mall->numC, dist_a_data->requests[0], &completed, MPI_STATUSES_IGNORE);
  } else {
    if(mall_conf->comm_type == MAL_USE_NORMAL) {
      req_completed = &(dist_a_data->requests[0][0]);
    } else if (mall_conf->comm_type == MAL_USE_IBARRIER) {
      req_completed = &(dist_a_data->requests[0][1]);
    }

    test_err = MPI_Test(req_completed, &completed, MPI_STATUS_IGNORE);
  }
 
  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", mall->myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  MPI_Allreduce(&completed, &all_completed, 1, MPI_INT, MPI_MIN, mall->comm);
  if(!all_completed) return MALL_DIST_PENDING; // Continue only if asynchronous send has ended 
  

  if(mall_conf->comm_type == MAL_USE_IBARRIER) {
    MPI_Wait(&(dist_a_data->requests[0][0]), MPI_STATUS_IGNORE); // Indicar como completado el envio asincrono
    //Para la desconexión de ambos grupos de procesos es necesario indicar a MPI que esta comm
    //ha terminado, aunque solo se pueda llegar a este punto cuando ha terminado
  }
  return end_redistribution();
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
  int is_intercomm, rootBcast, local_state;

  is_intercomm = 0;
  if(mall->intercomm != MPI_COMM_NULL) {
    MPI_Comm_test_inter(mall->intercomm, &is_intercomm);
  } else { 
    // Si no tiene comunicador creado, se debe a que se ha pospuesto el Spawn
    //   y se trata del spawn Merge Shrink
    mall->intercomm = mall->comm;
  }
  if(is_intercomm) {
    rootBcast = mall->myId == mall->root ? MPI_ROOT : MPI_PROC_NULL;
  } else {
    rootBcast = mall->root;
  }
  
  comm_data_info(rep_s_data, dist_s_data, MALLEABILITY_NOT_CHILDREN, mall->myId, mall->root, mall->intercomm);
  if(dist_s_data->entries || rep_s_data->entries) { // Enviar datos sincronos
    mall_conf->results->sync_time[mall_conf->grp] = MPI_Wtime();
    send_data(mall->numC, dist_s_data, MALLEABILITY_USE_SYNCHRONOUS);

    // TODO Crear funcion especifica y anyadir para Asinc
    // TODO Tener en cuenta el tipo
    for(i=0; i<rep_s_data->entries; i++) {
      MPI_Datatype datatype;
      if(rep_s_data->types[i] == MAL_INT) {
        datatype = MPI_INT;
      } else {
        datatype = MPI_CHAR;
      }
      MPI_Bcast(rep_s_data->arrays[i], rep_s_data->qty[i], datatype, rootBcast, mall->intercomm);
    } 
  }

  comm_results(mall_conf->results, rootBcast, mall_conf->config_file->n_resizes, mall->intercomm);

  local_state = MALL_DIST_COMPLETED;
  if(!is_intercomm) { // Merge Spawn
    if(mall->numP < mall->numC) { // Expand
      if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));
      if(mall->comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->comm));
      if(mall->user_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->user_comm)); //TODO No es peligroso?

      MPI_Comm_dup(mall->intercomm, &(mall->thread_comm));
      MPI_Comm_dup(mall->intercomm, &(mall->comm));
      MPI_Comm_dup(mall->intercomm, &(mall->user_comm));

      MPI_Comm_set_name(mall->thread_comm, "MPI_COMM_MALL_THREAD");
      MPI_Comm_set_name(mall->comm, "MPI_COMM_MALL");
      MPI_Comm_set_name(mall->user_comm, "MPI_COMM_MALL_USER");
    } else { // Shrink || Merge Shrink requiere de mas tareas
      local_state = MALL_SPAWN_ADAPT_PENDING;
    }
  }


  if(mall->intercomm != MPI_COMM_NULL && mall->intercomm != MPI_COMM_WORLD) {
    MPI_Comm_disconnect(&(mall->intercomm));
  }

  return local_state;
}


///=============================================
///=============================================
///=============================================
//TODO Add comment
int shrink_redistribution() {
    double time_extra = MPI_Wtime();

    //TODO REFACTOR -- Que solo la llamada de collect iters este fuera de los hilos
    zombies_collect_suspended(mall->comm, mall->myId, mall->numP, mall->numC, mall->root, (void *) mall_conf->results, mall_conf->config_file->n_stages);
    
    if(mall->myId < mall->numC) {
      if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));
      if(mall->comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->comm));
      if(mall->user_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->user_comm)); //TODO No es peligroso?

      MPI_Comm_dup(mall->intercomm, &(mall->thread_comm));
      MPI_Comm_dup(mall->intercomm, &(mall->comm));
      MPI_Comm_dup(mall->intercomm, &(mall->user_comm));

      MPI_Comm_set_name(mall->thread_comm, "MPI_COMM_MALL_THREAD");
      MPI_Comm_set_name(mall->comm, "MPI_COMM_MALL");
      MPI_Comm_set_name(mall->user_comm, "MPI_COMM_MALL_USER");

      MPI_Comm_free(&(mall->intercomm));

      mall_conf->results->spawn_time[mall_conf->grp] += MPI_Wtime() - time_extra;
      if(malleability_spawn_contains_strat(mall_conf->spawn_strategies,MALL_SPAWN_PTHREAD, NULL)) {
          mall_conf->results->spawn_real_time[mall_conf->grp] += MPI_Wtime() - time_extra;
      }
      return MALL_DIST_COMPLETED;
    } else {
      return MALL_ZOMBIE;
    }
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//=================COMM NODE INFO ======================||
//======================================================||
//======================================================||
//TODO Add comment
void comm_node_data(int rootBcast, int is_child_group) {
  MPI_Datatype node_type;

  def_nodeinfo_type(&node_type);
  MPI_Bcast(mall, 1, node_type, rootBcast, mall->intercomm);

  if(is_child_group) {
    mall->nodelist = malloc((mall->nodelist_len+1) * sizeof(char));
    mall->nodelist[mall->nodelist_len] = '\0';
  }
  MPI_Bcast(mall->nodelist, mall->nodelist_len, MPI_CHAR, rootBcast, mall->intercomm);

  MPI_Type_free(&node_type);
}

//TODO Add comment
void def_nodeinfo_type(MPI_Datatype *node_type) {
  int i, counts = 3;
  int blocklengths[3] = {1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = MPI_INT;

  // Rellenar vector displs
  MPI_Get_address(mall, &dir);

  MPI_Get_address(&(mall->num_cpus), &displs[0]);
  MPI_Get_address(&(mall->num_nodes), &displs[1]);
  MPI_Get_address(&(mall->nodelist_len), &displs[2]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, node_type);
  MPI_Type_commit(node_type);
}

// TODO MOVER A OTRO LADO??
//======================================================||
//================PRIVATE FUNCTIONS=====================||
//===============COMM PARENTS THREADS===================||
//======================================================||
//======================================================||

/*
 * Crea una hebra para ejecutar una comunicación en segundo plano.
 */
int thread_creation() {
  if(pthread_create(&(mall->async_thread), NULL, thread_async_work, NULL)) {
    printf("Error al crear el hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return MALL_DIST_PENDING;
}

/*
 * Comprobación por parte de una hebra maestra que indica
 * si una hebra esclava ha terminado su comunicación en segundo plano.
 *
 * El estado de la comunicación es devuelto al finalizar la función. 
 */
int thread_check() {
  int all_completed = 0;

  // Comprueba que todos los hilos han terminado la distribucion (Mismo valor en commAsync)
  MPI_Allreduce(&state, &all_completed, 1, MPI_INT, MPI_MAX, mall->comm);
  if(all_completed != MALL_DIST_COMPLETED) return MALL_DIST_PENDING; // Continue only if asynchronous send has ended 
  //FIXME No se tiene en cuenta el estado MALL_APP_ENDED

  if(pthread_join(mall->async_thread, NULL)) {
    printf("Error al esperar al hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -2;
  } 
  return end_redistribution();
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
  send_data(mall->numC, dist_a_data, MALLEABILITY_USE_SYNCHRONOUS);
  state = MALL_DIST_COMPLETED;
  pthread_exit(NULL);
}


//==============================================================================
/*
 * Muestra por pantalla el estado actual de todos los comunicadores
 */
void print_comms_state() {
  int tester;
  char *test = malloc(MPI_MAX_OBJECT_NAME * sizeof(char));

  MPI_Comm_get_name(mall->comm, test, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, mall->comm, test);
  MPI_Comm_get_name(mall->user_comm, test, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, mall->user_comm, test);
  if(mall->intercomm != MPI_COMM_NULL) {
    MPI_Comm_get_name(mall->intercomm, test, &tester);
    printf("P%d Comm=%d Name=%s\n", mall->myId, mall->intercomm, test);
  }
  free(test);
}
