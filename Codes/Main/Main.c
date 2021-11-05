#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include "computing_func.h"
#include "../IOcodes/read_ini.h"
#include "../IOcodes/results.h"
#include "../malleability/ProcessDist.h"
#include "../malleability/CommDist.h"

#define ROOT 0

int work();
void Sons_init();

int checkpoint(int iter, int state, MPI_Request **comm_req);
int TC(int numS, int comm_type);
int start_redistribution(int numS, MPI_Request **comm_req);
int check_redistribution(int iter, MPI_Request **comm_req);
int end_redistribution(int iter);

int thread_creation();
int thread_check(int iter);
void* thread_async_work(void* void_arg);

void iterate(double *matrix, int n, int async_comm);

void init_group_struct(char *argv[], int argc, int myId, int numP);
void init_application();
void obtain_op_times();
void free_application_data();

void print_general_info(int myId, int grp, int numP);
int print_final_results();
int create_out_file(char *nombre, int *ptr, int newstdout);

typedef struct {
  int myId;
  int numP;
  int grp;
  int iter_start;
  int argc;

  int numS; // Cantidad de procesos hijos
  int commAsync;
  MPI_Comm children, parents;

  char *compute_comm_array;
  char **argv;
  char *sync_array, *async_array;
} group_data;

typedef struct {
  int myId, numP, numS, adr;
  MPI_Comm children;
  char *sync_array;
} thread_data;

configuration *config_file;
group_data *group;
results_data *results;
int run_id = 0; // Utilizado para diferenciar más fácilmente ejecuciones en el análisis

pthread_t async_thread; // TODO Cambiar de sitio?

int main(int argc, char *argv[]) {
    int numP, myId, res;
    int req;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &req);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);

    if(req != MPI_THREAD_MULTIPLE) {
      printf("No se ha obtenido la configuración de hilos necesaria\nSolicitada %d -- Devuelta %d\n", req, MPI_THREAD_MULTIPLE);
    }

    init_group_struct(argv, argc, myId, numP);

    MPI_Comm_get_parent(&(group->parents));
    if(group->parents == MPI_COMM_NULL ) { // Si son el primer grupo de procesos, recogen la configuracion inicial
      init_application();
    } else { // Si son procesos hijos deben comunicarse con las padres
      Sons_init();
    }

    if(group->grp == 0) {
      MPI_Barrier(MPI_COMM_WORLD);
      results->exec_start = MPI_Wtime();
    }

    res = work();

    if(res) { // Se he llegado al final de la aplicacion
      MPI_Barrier(MPI_COMM_WORLD);
      results->exec_time = MPI_Wtime() - results->exec_start;
    }
    print_final_results();

    free_application_data();
    MPI_Finalize();
    return 0;
}

/*
 * Función de trabajo principal.
 *
 * Incializa los datos para realizar el computo y a continuacion
 * pasa a realizar "maxiter" iteraciones de computo.
 *
 * Terminadas las iteraciones realiza el redimensionado de procesos.
 * Si el redimensionado se realiza de forma asincrona se 
 * siguen realizando iteraciones de computo hasta que termine la 
 * comunicacion asincrona y realizar entonces la sincrona.
 *
 * Si el grupo de procesos es el ultimo que va a ejecutar, se devuelve
 * el valor 1 para indicar que no se va a seguir trabajando con nuevos grupos
 * de procesos. En caso contrario se devuelve 0.
 */
int work() {
  int iter, maxiter, state, res;
  double *matrix;
  MPI_Request *async_comm;

  maxiter = config_file->iters[group->grp];
  //initMatrix(&matrix, config_file->matrix_tam);
  state = MAL_COMM_UNINITIALIZED;

  res = 0;
  for(iter=group->iter_start; iter < maxiter; iter++) {
    iterate(matrix, config_file->matrix_tam, state);
  }
  state = checkpoint(iter, state, &async_comm);
  
  iter = 0;
  while(state == MAL_ASYNC_PENDING || state == COMM_IN_PROGRESS) {
    iterate(matrix, config_file->matrix_tam, state);
    iter++;
    state = checkpoint(iter, state, &async_comm);
  }
  
  if(config_file->resizes - 1 == group->grp) res=1;
  return res;
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
int checkpoint(int iter, int state, MPI_Request **comm_req) {
  
  if(state == MAL_COMM_UNINITIALIZED) {
    // Comprobar si se tiene que realizar un redimensionado
    if(config_file->iters[group->grp] > iter || config_file->resizes == group->grp + 1) {return MAL_COMM_UNINITIALIZED;}

    group->numS = config_file->procs[group->grp +1];
    int comm_type = COMM_SPAWN_PTHREAD; // TODO Pasar a CONFIG

    if(group->myId == ROOT) { printf("Malleability\n");}
    state = TC(group->numS, comm_type);

    if (state == COMM_FINISHED){
      state = start_redistribution(group->numS, comm_req);
    }

  } else if(state == COMM_IN_PROGRESS) { // Comprueba si el spawn ha terminado y comienza la redistribucion
    state = check_slurm_comm(group->myId, ROOT, &(group->children));

    if (state == COMM_FINISHED) {  
        results->spawn_time[group->grp] = MPI_Wtime() - results->spawn_start;
      state = start_redistribution(group->numS, comm_req);
    }

  } else if(state == MAL_ASYNC_PENDING) {
    if(config_file->aib == MAL_USE_THREAD) {
      state = thread_check(iter);
    } else {
      state = check_redistribution(iter, comm_req);
    }
  }

  return state;
}

/*
 * Se encarga de realizar la creacion de los procesos hijos.
 * Si se pide en segundo plano devuelve el estado actual.
 */
int TC(int numS, int comm_type){
  // Inicialización de la comunicación con SLURM
  int dist = config_file->phy_dist[group->grp +1];
  int comm_state;

      results->spawn_start = MPI_Wtime();
  comm_state = init_slurm_comm(group->argv, group->myId, numS, ROOT, dist, comm_type, MPI_COMM_WORLD, &(group->children));
  if(comm_type == COMM_SPAWN_SERIAL)
      results->spawn_time[group->grp] = MPI_Wtime() - results->spawn_start;
  return comm_state;
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
int start_redistribution(int numS, MPI_Request **comm_req) {
  int rootBcast = MPI_PROC_NULL;
  if(group->myId == ROOT) rootBcast = MPI_ROOT;

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, rootBcast, group->children);
  MPI_Bcast(&run_id, 1, MPI_INT, rootBcast, group->children);
  send_config_file(config_file, rootBcast, group->children);

  if(config_file->adr > 0) {
    results->async_start = MPI_Wtime();
    if(config_file->aib == MAL_USE_THREAD) {
      return thread_creation();
    } else {
      send_async(group->async_array, config_file->adr, group->myId, group->numP, ROOT, group->children, group->numS, comm_req, config_file->aib);
      return MAL_ASYNC_PENDING;
    }
  } 
  return end_redistribution(0);
}

/*
 * Crea una hebra para ejecutar una comunicación en segundo plano.
 */
int thread_creation() {
  if(pthread_create(&async_thread, NULL, thread_async_work, NULL)) {
    printf("Error al crear el hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return MAL_ASYNC_PENDING;
}

/*
 * Comprobación por parte de una hebra maestra que indica
 * si una hebra esclava ha terminado su comunicación en segundo plano.
 *
 * El estado de la comunicación es devuelto al finalizar la función. 
 */
int thread_check(int iter) {
  int all_completed = 0;

  // Comprueba que todos los hilos han terminado la distribucion (Mismo valor en commAsync)
  MPI_Allreduce(&group->commAsync, &all_completed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  if(all_completed != MAL_COMM_COMPLETED) return MAL_ASYNC_PENDING; // Continue only if asynchronous send has ended 

  if(pthread_join(async_thread, NULL)) {
    printf("Error al esperar al hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -2;
  } 
  return end_redistribution(iter);
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
  send_sync(group->async_array, config_file->adr, group->myId, group->numP, ROOT, group->children, group->numS);
  group->commAsync = MAL_COMM_COMPLETED;
  pthread_exit(NULL);
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
int check_redistribution(int iter, MPI_Request **comm_req) {
  int completed, all_completed, test_err;
  MPI_Request *req_completed;

  if (config_file->aib == MAL_USE_POINT) {
    test_err = MPI_Testall(group->numS, *comm_req, &completed, MPI_STATUSES_IGNORE);
  } else {
    if(config_file->aib == MAL_USE_NORMAL) {
      req_completed = &(*comm_req)[0];
    } else if (config_file->aib == MAL_USE_IBARRIER) {
      req_completed = &(*comm_req)[1];
    }
    test_err = MPI_Test(req_completed, &completed, MPI_STATUS_IGNORE);
  }
 
  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", group->myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  MPI_Allreduce(&completed, &all_completed, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
  if(!all_completed) return MAL_ASYNC_PENDING; // Continue only if asynchronous send has ended 
  

  if(config_file->aib == MAL_USE_IBARRIER) {
    MPI_Wait(&(*comm_req)[0], MPI_STATUS_IGNORE); // Indicar como completado el envio asincrono
    //Para la desconexión de ambos grupos de procesos es necesario indicar a MPI que esta comm
    //ha terminado, aunque solo se pueda llegar a este punto cuando ha terminado
  }
  free(*comm_req);
  return end_redistribution(iter);
}


/*
 * Termina la redistribución de los datos con los hijos, comprobando
 * si se han realizado iteraciones con comunicaciones en segundo plano
 * y enviando cuantas iteraciones se han realizado a los hijos.
 *
 * Además se realizan las comunicaciones síncronas se las hay.
 * Finalmente termina enviando los datos temporales a los hijos.
 */ 
int end_redistribution(int iter) {
  int rootBcast = MPI_PROC_NULL;
  if(group->myId == ROOT) rootBcast = MPI_ROOT;

  if(iter > 0) { // Mandar a los hijos iteracion en la que comenzar
    MPI_Bcast(&iter, 1, MPI_INT, rootBcast, group->children);
  }
  if(config_file->sdr > 0) { // Realizar envio sincrono
      results->sync_start = MPI_Wtime();
    send_sync(group->sync_array, config_file->sdr, group->myId, group->numP, ROOT, group->children, group->numS);
  }

  send_results(results, rootBcast, config_file->resizes, group->children);
  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->children));
  return MAL_COMM_COMPLETED;
}

/*
 * Inicializacion de los datos de los hijos.
 * En la misma se reciben datos de los padres: La configuracion
 * de la ejecucion a realizar; y los datos a recibir de los padres
 * ya sea de forma sincrona, asincrona o ambas.
 */
void Sons_init() {

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, ROOT, group->parents);
  MPI_Bcast(&run_id, 1, MPI_INT, ROOT, group->parents);
  group->grp++;

  config_file = recv_config_file(ROOT, group->parents);
  int numP_parents = config_file->procs[group->grp -1];
  init_results_data(&results, config_file->resizes - 1, config_file->iters[group->grp]);

  if(config_file->comm_tam) {
    group->compute_comm_array = malloc(config_file->comm_tam * sizeof(char));
  }
  if(config_file->adr) { // Recibir datos asincronos
    if(config_file->aib == MAL_USE_NORMAL || config_file->aib == MAL_USE_IBARRIER || config_file->aib == MAL_USE_POINT) {
      recv_async(&(group->async_array), config_file->adr, group->myId, group->numP, ROOT, group->parents, numP_parents, config_file->aib);
    } else if (config_file->aib == MAL_USE_THREAD) {
      recv_sync(&(group->async_array), config_file->adr, group->myId, group->numP, ROOT, group->parents, numP_parents);
    }

      results->async_time[group->grp] = MPI_Wtime();
    MPI_Bcast(&(group->iter_start), 1, MPI_INT, ROOT, group->parents);
  }
  if(config_file->sdr) { // Recibir datos sincronos
    recv_sync(&(group->sync_array), config_file->sdr, group->myId, group->numP, ROOT, group->parents, numP_parents);
    results->sync_time[group->grp] = MPI_Wtime();
  }

  // Guardar los resultados de esta transmision
  recv_results(results, ROOT, config_file->resizes, group->parents);
  if(config_file->sdr) { // Si no hay datos sincronos, el tiempo es 0
    results->sync_time[group->grp]  = MPI_Wtime() - results->sync_start;
  } else {
    results->sync_time[group->grp]  = 0;
  }
  if(config_file->adr) { // Si no hay datos asincronos, el tiempo es 0
    results->async_time[group->grp]  = MPI_Wtime() - results->async_start;
  } else {
    results->async_time[group->grp]  = 0;
  }

  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->parents));
}


/////////////////////////////////////////
/////////////////////////////////////////
//COMPUTE FUNCTIONS
/////////////////////////////////////////
/////////////////////////////////////////


/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 * que dura al menos un tiempo de "time" segundos.
 */
void iterate(double *matrix, int n, int async_comm) {
  double start_time, actual_time;
  double time = config_file->general_time * config_file->factors[group->grp];
  double Top = config_file->Top;
  int i, operations = 0;
  double aux = 0;

  start_time = actual_time = MPI_Wtime();

  operations = time / Top;
  for(i=0; i < operations; i++) {
    aux += computePiSerial(n);
  }

  if(config_file->comm_tam) {
    MPI_Bcast(group->compute_comm_array, config_file->comm_tam, MPI_CHAR, ROOT, MPI_COMM_WORLD);
  }

  actual_time = MPI_Wtime(); // Guardar tiempos
  if(async_comm == MAL_ASYNC_PENDING) { // Se esta realizando una redistribucion de datos asincrona
    operations=0;
  }


  if(results->iter_index == results->iters_size) { // Aumentar tamaño de ambos vectores de resultados
    realloc_results_iters(results, results->iters_size + 100);
  }
  results->iters_time[results->iter_index] = actual_time - start_time;
  results->iters_type[results->iter_index] = operations;
  results->iter_index = results->iter_index + 1;

}

//======================================================||
//======================================================||
//=============INIT/FREE/PRINT FUNCTIONS================||
//======================================================||
//======================================================||

/*
 * Muestra datos generales sobre los procesos, su grupo,
 * en que nodo residen y la version de MPI utilizada.
 */
void print_general_info(int myId, int grp, int numP) {
  int len;
  char *name = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  char *version = malloc(MPI_MAX_LIBRARY_VERSION_STRING * sizeof(char));
  MPI_Get_processor_name(name, &len);
  MPI_Get_library_version(version, &len);
  printf("P%d Nuevo GRUPO %d de %d procs en nodo %s con %s\n", myId, grp, numP, name, version);

  free(name);
  free(version);
}

/*
 * Pide al proceso raiz imprimir los datos sobre las iteraciones realizadas por el grupo de procesos.
 *
 * Si es el ultimo grupo de procesos, muestra los datos obtenidos de tiempo de ejecucion, creacion de procesos
 * y las comunicaciones.
 */
int print_final_results() {
  int ptr_local, ptr_global, err;
  char *file_name;

  if(group->myId == ROOT) {
    file_name = NULL;
    file_name = malloc(40 * sizeof(char));
    if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
    err = snprintf(file_name, 40, "R%d_G%dNP%dID%d.out", run_id, group->grp, group->numP, group->myId);
    if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero
    create_out_file(file_name, &ptr_local, 1);
  
    print_config_group(config_file, group->grp);
    print_iter_results(results, config_file->iters[group->grp] -1);
    free(file_name);

    if(group->grp == config_file->resizes -1) {
      file_name = NULL;
      file_name = malloc(20 * sizeof(char));
      if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
      err = snprintf(file_name, 20, "R%d_Global.out", run_id);
      if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero

      create_out_file(file_name, &ptr_global, 1);
      print_config(config_file, group->grp);
      print_global_results(results, config_file->resizes);
      free(file_name);
      
    }
  }
  return 0;
}

/*
 * Inicializa la estructura group
 */
void init_group_struct(char *argv[], int argc, int myId, int numP) {
  group = malloc(1 * sizeof(group_data));
  group->myId        = myId;
  group->numP        = numP;
  group->grp         = 0;
  group->iter_start  = 0;
  group->commAsync   = MAL_COMM_UNINITIALIZED;
  group->argc        = argc;
  group->argv        = argv;
}

/*
 * Inicializa los datos para este grupo de procesos.
 *
 * En caso de ser el primer grupo de procesos, lee el fichero de configuracion
 * e inicializa los vectores de comunicacion.
 *
 * En caso de ser otro grupo de procesos entra a la funcion "Sons_init()" donde
 * se comunican con los padres para inicializar sus datos.
 */
void init_application() {
  if(group->argc < 2) {
    printf("Falta el fichero de configuracion. Uso:\n./programa config.ini id\nEl argumento numerico id es opcional\n");
    exit(0);
  }
  if(group->argc > 2) {
    run_id = atoi(group->argv[2]);
  }

  config_file = read_ini_file(group->argv[1]);
  init_results_data(&results, config_file->resizes, config_file->iters[group->grp]);
  if(config_file->comm_tam) {
    group->compute_comm_array = malloc(config_file->comm_tam * sizeof(char));
  }
  if(config_file->sdr) {
    malloc_comm_array(&(group->sync_array), config_file->sdr , group->myId, group->numP);
  }
  if(config_file->adr) {
    malloc_comm_array(&(group->async_array), config_file->adr , group->myId, group->numP);
  }
   
  obtain_op_times();
}

/*
 * Obtiene cuanto tiempo es necesario para realizar una operacion de PI
 */
void obtain_op_times() {
  double result, start_time = MPI_Wtime();
  int i, qty = 20000;
  result = 0;
  for(i=0; i<qty; i++) {
    result += computePiSerial(config_file->matrix_tam);
  }
  //printf("Creado Top con valor %lf\n", result);
  //fflush(stdout);

  config_file->Top = (MPI_Wtime() - start_time) / qty; //Tiempo de una operacion
  MPI_Bcast(&(config_file->Top), 1, MPI_DOUBLE, ROOT, MPI_COMM_WORLD); 
}

/*
 * Libera toda la memoria asociada con la aplicacion
 */
void free_application_data() {
  if(config_file->comm_tam) {
    free(group->compute_comm_array);
  }
  if(config_file->sdr) {
    free(group->sync_array);
  }
  if(config_file->adr) {
    free(group->async_array);
  }
  
  free(group);
  free_config(config_file);
  free_results_data(&results);
}


/* 
 * Función para crear un fichero con el nombre pasado como argumento.
 * Si el nombre ya existe, se escribe la informacion a continuacion.
 *
 * El proceso que llama a la función pasa a tener como salida estandar
 * dicho fichero si el valor "newstdout" es verdadero.
 *
 */
int create_out_file(char *nombre, int *ptr, int newstdout) {
  int err;

  *ptr = open(nombre, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if(*ptr < 0) return -1; // No ha sido posible crear el fichero

  if(newstdout) {
    err = close(1);
    if(err < 0) return -2; // No es posible modificar la salida estandar
    err = dup(*ptr);
    if(err < 0) return -3; // No es posible modificar la salida estandar
  }

  return 0;
}
