#include <pthread.h>
#include "malleabilityManager.h"
#include "malleabilityStates.h"
#include "malleabilityTypes.h"
#include "malleabilityZombies.h"
#include "ProcessDist.h"
#include "CommDist.h"

#define MALLEABILITY_ROOT 0
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

int thread_creation();
int thread_check();
void* thread_async_work(void* void_arg);

typedef struct {
  int spawn_type;
  int spawn_dist;
  int spawn_is_single;
  int spawn_threaded;
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
  int num_cpus, num_nodes;
} malleability_t;

int state = MAL_UNRESERVED; //FIXME Mover a otro lado

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

  mall->myId = myId;
  mall->numP = numP;
  mall->root = root;
  mall->comm = dup_comm;
  mall->thread_comm = thread_comm; // TODO Refactor -- Crear solo si es necesario?
  mall->user_comm = comm;

  mall->name_exec = name_exec;
  mall->nodelist = nodelist;
  mall->num_cpus = num_cpus;
  mall->num_nodes = num_nodes;

  rep_s_data->entries = 0;
  rep_a_data->entries = 0;
  dist_s_data->entries = 0;
  dist_a_data->entries = 0;

  state = MAL_NOT_STARTED;

  // Si son el primer grupo de procesos, obtienen los datos de los padres
  MPI_Comm_get_parent(&(mall->intercomm));
  if(mall->intercomm != MPI_COMM_NULL ) { 
    Children_init();
    return MALLEABILITY_CHILDREN;
  }

  zombies_service_init();
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
  free(rep_a_data);
  free(dist_s_data);
  free(dist_a_data);

  //MPI_Comm_free(&(mall->comm)); // TODO Revisar si hace falta?
  //MPI_Comm_free(&(mall->thread_comm));
  free(mall);
  free(mall_conf);

  zombies_awake();
  zombies_service_free();

  state = MAL_UNRESERVED;
}

/*
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
  
  if(state == MAL_UNRESERVED) return MAL_UNRESERVED;

  if(state == MAL_NOT_STARTED) {
    // Comprobar si se tiene que realizar un redimensionado
    //if(CHECK_RMS()) {return MAL_DENIED;}
    
    state = spawn_step();

    if (state == MAL_SPAWN_COMPLETED){
      state = start_redistribution();
    }

  } else if(state == MAL_SPAWN_PENDING || state == MAL_SPAWN_SINGLE_PENDING) { // Comprueba si el spawn ha terminado y comienza la redistribucion
    double end_real_time;
    state = check_slurm_comm(mall->myId, mall->root, mall->numP, &(mall->intercomm), mall->comm, mall->thread_comm, &end_real_time);
    if (state == MAL_SPAWN_COMPLETED) {  
      mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
      if(mall_conf->spawn_type == COMM_SPAWN_PTHREAD || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) {
        mall_conf->results->spawn_real_time[mall_conf->grp] = end_real_time - mall_conf->results->spawn_start;
      }

      //TODO Si es MERGE SHRINK, metodo diferente de redistribucion de datos
      state = start_redistribution();
    }

  } else if(state == MAL_DIST_PENDING) {
    if(mall_conf->comm_type == MAL_USE_THREAD) {
      state = thread_check();
    } else {
      state = check_redistribution();
    }
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

void set_malleability_configuration(int spawn_type, int spawn_is_single, int spawn_dist, int spawn_threaded, int comm_type, int comm_threaded) {
  mall_conf->spawn_type = spawn_type;
  mall_conf->spawn_is_single = spawn_is_single;
  mall_conf->spawn_dist = spawn_dist;
  mall_conf->spawn_threaded = spawn_threaded;
  mall_conf->comm_type = comm_type;
  mall_conf->comm_threaded = comm_threaded;
}

/*
 * To be deprecated
 * Tiene que ser llamado despues de setear la config
 */
void set_children_number(int numC){
  if((mall_conf->spawn_type == COMM_SPAWN_MERGE || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) && (numC - mall->numP >= 0)) {
    mall->numC = numC;
    mall->numC_spawned = numC - mall->numP;

    if(numC == mall->numP) { // Migrar
      mall->numC_spawned = numC;
      if(mall_conf->spawn_type == COMM_SPAWN_MERGE)
        mall_conf->spawn_type = COMM_SPAWN_SERIAL;
      else
	mall_conf->spawn_type = COMM_SPAWN_PTHREAD;
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
void malleability_add_data(void *data, int total_qty, int type, int is_replicated, int is_constant) {

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
      int total_reqs = 0;
      
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
 * Devuelve el numero de entradas para la estructura de descripcion de 
 * datos elegida.
 */
void malleability_get_entries(int *entries, int is_replicated, int is_constant){
  
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
  int i;
  char *aux;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      send_async(aux, data_struct->qty[i], mall->myId, mall->numP, mall->root, mall->intercomm, numP_children, data_struct->requests, mall_conf->comm_type);
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      send_sync(aux, data_struct->qty[i], mall->myId, mall->numP, mall->root, mall->intercomm, numP_children);
    }
  }
}

/*
 * Funcion generalizada para recibir datos desde los hijos.
 * La asincronizidad se refiere a si el hilo padre e hijo lo hacen
 * de forma bloqueante o no. El padre puede tener varios hilos.
 */
void recv_data(int numP_parents, malleability_data_t *data_struct, int is_asynchronous) {
  int i;
  char *aux;

  if(is_asynchronous) {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      recv_async(&aux, data_struct->qty[i], mall->myId, mall->numP, mall->root, mall->intercomm, numP_parents, mall_conf->comm_type);
      data_struct->arrays[i] = (void *) aux;
    }
  } else {
    for(i=0; i < data_struct->entries; i++) {
      aux = (char *) data_struct->arrays[i]; //TODO Comprobar que realmente es un char
      recv_sync(&aux, data_struct->qty[i], mall->myId, mall->numP, mall->root, mall->intercomm, numP_parents);
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
  int numP_parents, root_parents, i;
  int spawn_is_single;
  MPI_Comm aux;

  MPI_Bcast(&spawn_is_single, 1, MPI_INT, MALLEABILITY_ROOT, mall->intercomm); 
  if(spawn_is_single) {
    malleability_establish_connection(mall->myId, MALLEABILITY_ROOT, &(mall->intercomm));
  }
  MPI_Bcast(&(mall_conf->spawn_type), 1, MPI_INT, MALLEABILITY_ROOT, mall->intercomm); 

  MPI_Bcast(&root_parents, 1, MPI_INT, MALLEABILITY_ROOT, mall->intercomm); 
  MPI_Bcast(&numP_parents, 1, MPI_INT, root_parents, mall->intercomm);

  mall_conf->config_file = recv_config_file(mall->root, mall->intercomm);
  mall_conf->results = (results_data *) malloc(sizeof(results_data));
  init_results_data(mall_conf->results, mall_conf->config_file->resizes, RESULTS_INIT_DATA_QTY);

  if(dist_a_data->entries || rep_a_data->entries) { // Recibir datos asincronos
    comm_data_info(rep_a_data, dist_a_data, MALLEABILITY_CHILDREN, mall->myId, root_parents, mall->intercomm);

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

  if(mall_conf->spawn_type == COMM_SPAWN_MERGE || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) {
    proc_adapt_expand(&(mall->numP), mall->numP+numP_parents, mall->intercomm, &(mall->comm), MALLEABILITY_CHILDREN);

    if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));

    MPI_Comm_dup(mall->comm, &aux);
    mall->thread_comm = aux;
    MPI_Comm_dup(mall->comm, &aux);
    mall->user_comm = aux;
  } 

  // Guardar los resultados de esta transmision
  recv_results(mall_conf->results, mall->root, mall_conf->config_file->resizes, mall->intercomm);

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

  if((mall_conf->spawn_type == COMM_SPAWN_MERGE || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) && mall->numP > mall->numC) {
    state = shrink_redistribution();
    return state; 
  }
 
  state = init_slurm_comm(mall->name_exec, mall->num_cpus, mall->num_nodes, mall->nodelist, mall->myId, mall->numP, mall->numC, mall->root, mall_conf->spawn_dist, mall_conf->spawn_type, mall_conf->spawn_is_single, mall->thread_comm, &(mall->intercomm));

  if(mall_conf->spawn_type == COMM_SPAWN_SERIAL || mall_conf->spawn_type == COMM_SPAWN_MERGE)
      mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
  else if(mall_conf->spawn_type == COMM_SPAWN_PTHREAD || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) {
      //mall_conf->results->spawn_thread_time[mall_conf->grp] = MPI_Wtime() - mall_conf->results->spawn_start;
      //mall_conf->results->spawn_start = MPI_Wtime();
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
  int rootBcast = MPI_PROC_NULL;
  if(mall->myId == mall->root) rootBcast = MPI_ROOT;

  MPI_Bcast(&(mall_conf->spawn_type), 1, MPI_INT, rootBcast, mall->intercomm);
  MPI_Bcast(&(mall->root), 1, MPI_INT, rootBcast, mall->intercomm);
  MPI_Bcast(&(mall->numP), 1, MPI_INT, rootBcast, mall->intercomm);

  send_config_file(mall_conf->config_file, rootBcast, mall->intercomm);

  if(dist_a_data->entries || rep_a_data->entries) { // Recibir datos asincronos
    mall_conf->results->async_start = MPI_Wtime();
    comm_data_info(rep_a_data, dist_a_data, MALLEABILITY_NOT_CHILDREN, mall->myId, mall->root, mall->intercomm);
    if(mall_conf->comm_type == MAL_USE_THREAD) {
      return thread_creation();
    } else {
      send_data(mall->numC, dist_a_data, MALLEABILITY_USE_ASYNCHRONOUS);
      return MAL_DIST_PENDING;
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
  if(!all_completed) return MAL_DIST_PENDING; // Continue only if asynchronous send has ended 
  

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
  int result, i, rootBcast = MPI_PROC_NULL;
  MPI_Comm aux;
  if(mall->myId == mall->root) rootBcast = MPI_ROOT;

  if(dist_s_data->entries || rep_s_data->entries) { // Enviar datos sincronos
    comm_data_info(rep_s_data, dist_s_data, MALLEABILITY_NOT_CHILDREN, mall->myId, mall->root, mall->intercomm);
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
    
  if(mall_conf->spawn_type == COMM_SPAWN_MERGE || mall_conf->spawn_type == COMM_SPAWN_MERGE_PTHREAD) {
    double time_adapt = MPI_Wtime();

    proc_adapt_expand(&(mall->numP), mall->numC, mall->intercomm, &(mall->comm), MALLEABILITY_NOT_CHILDREN);

    if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_free(&(mall->thread_comm));

    MPI_Comm_dup(mall->comm, &aux);
    mall->thread_comm = aux;
    MPI_Comm_dup(mall->comm, &aux);
    mall->user_comm = aux;
    mall_conf->results->spawn_time[mall_conf->grp] += MPI_Wtime() - time_adapt;
	
    
//    result = MAL_DIST_ADAPTED;
  }

  send_results(mall_conf->results, rootBcast, mall_conf->config_file->resizes, mall->intercomm);
  result = MAL_DIST_COMPLETED;

  MPI_Comm_disconnect(&(mall->intercomm));
  state = MAL_NOT_STARTED;
  return result;
}

int shrink_redistribution() {
    double time_adapt = MPI_Wtime();
    MPI_Comm aux_comm;
    MPI_Comm_dup(mall->comm, &aux_comm);

    proc_adapt_shrink( mall->numC, &(mall->comm), mall->myId);
    zombies_collect_suspended(aux_comm, mall->myId, mall->numP, mall->numC, mall->root, (void *) mall_conf->results, mall->user_comm);
    MPI_Comm_free(&aux_comm);
    
    if(mall->myId < mall->numC) {
      MPI_Comm_dup(mall->comm, &aux_comm);
      mall->thread_comm = aux_comm;
      MPI_Comm_dup(mall->comm, &aux_comm);
      mall->user_comm = aux_comm;

      mall_conf->results->spawn_time[mall_conf->grp] = MPI_Wtime() - time_adapt;
      return MAL_DIST_COMPLETED; //FIXME Refactor Poner a SPAWN_COMPLETED
    } else {
      return MAL_ZOMBIE;
    }
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
  return MAL_DIST_PENDING;
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
  if(all_completed != MAL_DIST_COMPLETED) return MAL_DIST_PENDING; // Continue only if asynchronous send has ended 
  //FIXME No se tiene en cuenta el estado MAL_APP_ENDED

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
void* thread_async_work(void* void_arg) {
  send_data(mall->numC, dist_a_data, MALLEABILITY_USE_SYNCHRONOUS);
  state = MAL_DIST_COMPLETED;
  pthread_exit(NULL);
}
