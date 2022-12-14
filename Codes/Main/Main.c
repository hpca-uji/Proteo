#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "process_stage.h"
#include "Main_datatypes.h"
#include "configuration.h"
#include "../IOcodes/results.h"
#include "../malleability/CommDist.h"
#include "../malleability/malleabilityManager.h"
#include "../malleability/malleabilityStates.h"

int work();
double iterate(int async_comm);
double iterate_relaxed(double *time, double *times_stages);
double iterate_rigid(double *time, double *times_stages);

void init_group_struct(char *argv[], int argc, int myId, int numP);
void init_application();
void obtain_op_times();
void free_application_data();

void print_general_info(int myId, int grp, int numP);
int print_local_results();
int print_final_results();
int create_out_file(char *nombre, int *ptr, int newstdout);

configuration *config_file;
group_data *group;
results_data *results;
MPI_Comm comm;
int run_id = 0; // Utilizado para diferenciar más fácilmente ejecuciones en el análisis

int main(int argc, char *argv[]) {
    int numP, myId, res;
    int req;
    int im_child;

    int num_cpus, num_nodes;
    char *nodelist = NULL;
    num_cpus = 20; //FIXME NUMERO MAGICO //TODO Usar openMP para obtener el valor con un pragma
    if (argc >= 5) {
      nodelist = argv[3];
      num_nodes = atoi(argv[4]);
      num_cpus = num_nodes * num_cpus;
    }

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &req);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    comm = MPI_COMM_WORLD;

    if(req != MPI_THREAD_MULTIPLE) {
      printf("No se ha obtenido la configuración de hilos necesaria\nSolicitada %d -- Devuelta %d\n", req, MPI_THREAD_MULTIPLE);
    }

    init_group_struct(argv, argc, myId, numP);
    im_child = init_malleability(myId, numP, ROOT, comm, argv[0], nodelist, num_cpus, num_nodes);

    if(!im_child) { //TODO REFACTOR Simplificar inicio
      init_application();

      set_benchmark_grp(group->grp);
      set_benchmark_configuration(config_file);
      set_benchmark_results(results);


      malleability_add_data(&(group->grp), 1, MAL_INT, 1, 1);
      malleability_add_data(&run_id, 1, MAL_INT, 1, 1);
      malleability_add_data(&(group->iter_start), 1, MAL_INT, 1, 1);

      MPI_Barrier(comm);
      results->exec_start = MPI_Wtime();
    } else { //Init hijos

      get_malleability_user_comm(&comm);
      get_benchmark_configuration(&config_file);
      get_benchmark_results(&results);

      // TODO Refactor - Que sea una unica funcion
      // Obtiene las variables que van a utilizar los hijos
      void *value = NULL;
      malleability_get_data(&value, 0, 1, 1);
      group->grp = *((int *)value);

      malleability_get_data(&value, 1, 1, 1);
      run_id = *((int *)value);
      
      malleability_get_data(&value, 2, 1, 1);
      group->iter_start = *((int *)value);

      set_results_post_reconfig(results, group->grp, config_file->sdr, config_file->adr); //TODO Cambio al añadir nueva redistribucion
      group->grp = group->grp + 1;
    }

    //
    // EMPIEZA LA EJECUCION-------------------------------
    //
    group->grp = group->grp - 1; // TODO REFACTOR???
    do {

      get_malleability_user_comm(&comm);
      MPI_Comm_size(comm, &(group->numP));
      MPI_Comm_rank(comm, &(group->myId));
      group->grp = group->grp + 1;
      set_benchmark_grp(group->grp);
      if(group->grp != 0) {
        obtain_op_times(0); //Obtener los nuevos valores de tiempo para el computo
      }

      if(config_file->n_resizes != group->grp + 1) { //TODO Llevar a otra funcion
        set_malleability_configuration(config_file->groups[group->grp+1].sm, config_file->groups[group->grp+1].ss, 
			config_file->groups[group->grp+1].phy_dist, config_file->groups[group->grp+1].at, -1);
        set_children_number(config_file->groups[group->grp+1].procs); // TODO TO BE DEPRECATED

        if(group->grp != 0) {
          malleability_modify_data(&(group->grp), 0, 1, MAL_INT, 1, 1);
        }
      }

      res = work();
      if(res == MALL_ZOMBIE) break;

      if(res==1) { // Se ha llegado al final de la aplicacion
        MPI_Barrier(comm); // TODO Posible error al utilizar SHRINK
        results->exec_time = MPI_Wtime() - results->exec_start - results->wasted_time;
      }
      print_local_results();
      reset_results_index(results);
    } while(config_file->n_resizes > group->grp + 1 && config_file->groups[group->grp+1].sm == MALL_SPAWN_MERGE);

    //
    // TERMINA LA EJECUCION ----------------------------------------------------------
    //
    print_final_results(); // Pasado este punto ya no pueden escribir los procesos

    MPI_Barrier(comm);
    if(comm != MPI_COMM_WORLD && comm != MPI_COMM_NULL) {
      MPI_Comm_free(&comm);
    }

    if(group->myId == ROOT && config_file->groups[group->grp].sm == MALL_SPAWN_MERGE) {
      MPI_Abort(MPI_COMM_WORLD, -100);
    }
    free_application_data(); //FIXME Error al liberar memoria de SDR/ADR

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

  maxiter = config_file->groups[group->grp].iters;
  state = MALL_NOT_STARTED;

  res = 0;
  for(iter=group->iter_start; iter < maxiter; iter++) {
    iterate(state);
  }

  if(config_file->n_resizes != group->grp + 1)
    state = malleability_checkpoint();

  iter = 0;
  while(state == MALL_DIST_PENDING || state == MALL_SPAWN_PENDING || state == MALL_SPAWN_SINGLE_PENDING || state == MALL_SPAWN_ADAPT_POSTPONE) {
    if(iter < config_file->groups[group->grp+1].iters) {
      iterate(state);
      iter++;
      group->iter_start = iter;
    }
    state = malleability_checkpoint();
  }

  
  if(config_file->n_resizes - 1 == group->grp) res=1;
  if(state == MALL_ZOMBIE) res=state;
  return res;
}


/////////////////////////////////////////
/////////////////////////////////////////
//COMPUTE FUNCTIONS
/////////////////////////////////////////
/////////////////////////////////////////


/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 * que dura al menos un tiempo determinado por la suma de todas las
 * etapas definidas en la configuracion.
 */
double iterate(int async_comm) {
  double time, *times_stages_aux;
  size_t i;
  double aux = 0;

  times_stages_aux = malloc(config_file->n_stages * sizeof(double));

  if(config_file->rigid_times) {
    aux = iterate_rigid(&time, times_stages_aux);
  } else {
    aux = iterate_relaxed(&time, times_stages_aux);
  }

  // Se esta realizando una redistribucion de datos asincrona
  if(async_comm == MALL_DIST_PENDING || async_comm == MALL_SPAWN_PENDING || async_comm == MALL_SPAWN_SINGLE_PENDING) { 
  // TODO Que diferencie entre ambas en el IO
    results->iters_async += 1;
  }

  if(results->iter_index == results->iters_size) { // Aumentar tamaño de ambos vectores de resultados
    realloc_results_iters(results, config_file->n_stages, results->iters_size + 100);
  }
  results->iters_time[results->iter_index] = time;
  for(i=0; i < config_file->n_stages; i++) {
    results->stage_times[i][results->iter_index] = times_stages_aux[i];
  }
  results->iter_index = results->iter_index + 1;

  free(times_stages_aux);

  return aux;
}


/*
 * Performs an iteration. The gathered times for iterations
 * and stages could be IMPRECISE in order to ensure the 
 * global execution time is precise.
 */
double iterate_relaxed(double *time, double *times_stages) {
  size_t i;
  double start_time, start_time_stage, aux=0;
  start_time = MPI_Wtime(); // Imprecise timings

  for(i=0; i < config_file->n_stages; i++) {
    start_time_stage = MPI_Wtime(); 
    aux+= process_stage(*config_file, config_file->stages[i], *group, comm);
    times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  *time = MPI_Wtime() - start_time; // Guardar tiempos
  return aux;
}

/*
 * Performs an iteration. The gathered times for iterations
 * and stages are ensured to be precise but the global 
 * execution time could be imprecise.
 */
double iterate_rigid(double *time, double *times_stages) {
  size_t i;
  double start_time, start_time_stage, aux=0;

  MPI_Barrier(comm);
  start_time = MPI_Wtime();

  for(i=0; i < config_file->n_stages; i++) {
    MPI_Barrier(comm);
    start_time_stage = MPI_Wtime();
    aux+= process_stage(*config_file, config_file->stages[i], *group, comm);
    times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  MPI_Barrier(comm);
  *time = MPI_Wtime() - start_time; // Guardar tiempos
  return aux;
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
 */
int print_local_results() {
  int ptr_local, ptr_out, err;
  char *file_name;

  compute_results_iter(results, group->myId, group->numP, ROOT, comm);
  compute_results_stages(results, group->myId, group->numP, config_file->n_stages, ROOT, comm);
  if(group->myId == ROOT) {
    ptr_out = dup(1);

    file_name = NULL;
    file_name = malloc(40 * sizeof(char));
    if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
    err = snprintf(file_name, 40, "R%d_G%dNP%dID%d.out", run_id, group->grp, group->numP, group->myId);
    if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero
    create_out_file(file_name, &ptr_local, 1);
  
    print_config_group(config_file, group->grp);
    print_iter_results(*results);
    print_stage_results(*results, config_file->n_stages);
    free(file_name);

    fflush(stdout);
    close(1);
    dup(ptr_out);
    close(ptr_out);
  }
  return 0;
}

/*
 * Si es el ultimo grupo de procesos, pide al proceso raiz mostrar los datos obtenidos de tiempo de ejecucion, creacion de procesos
 * y las comunicaciones.
 */
int print_final_results() {
  int ptr_global, err, ptr_out;
  char *file_name;

  if(group->myId == ROOT) {

    if(group->grp == config_file->n_resizes -1) {
      file_name = NULL;
      file_name = malloc(20 * sizeof(char));
      if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
      err = snprintf(file_name, 20, "R%d_Global.out", run_id);
      if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero

      ptr_out = dup(1);
      create_out_file(file_name, &ptr_global, 1);
      print_config(config_file);
      print_global_results(*results, config_file->n_resizes);
      fflush(stdout);
      free(file_name);

      close(1);
      dup(ptr_out);
    }
  }
  return 0;
}

/*
 * Inicializa la estructura group
 */
void init_group_struct(char *argv[], int argc, int myId, int numP) {
  group = malloc(sizeof(group_data));
  group->myId        = myId;
  group->numP        = numP;
  group->grp         = 0;
  group->iter_start  = 0;
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
    MPI_Abort(MPI_COMM_WORLD, -1);
  }
  if(group->argc > 2) {
    run_id = atoi(group->argv[2]);
  }

  init_config(group->argv[1], &config_file);
  results = malloc(sizeof(results_data));
  init_results_data(results, config_file->n_resizes, config_file->n_stages, config_file->groups[group->grp].iters);
  if(config_file->sdr) {
    malloc_comm_array(&(group->sync_array), config_file->sdr , group->myId, group->numP);
  }
  if(config_file->adr) {
    malloc_comm_array(&(group->async_array), config_file->adr , group->myId, group->numP);
  }

  obtain_op_times(1);
}

/*
 * Obtiene cuanto tiempo es necesario para realizar una operacion de PI
 *
 * Si compute esta a 1 se considera que se esta inicializando el entorno
 * y realizará trabajo extra.
 *
 * Si compute esta a 0 se considera un entorno inicializado y solo hay que
 * realizar algunos cambios de reserva de memoria. Si es necesario recalcular
 * algo se obtiene el total de tiempo utilizado en dichas tareas y se resta
 * al tiempo total de ejecucion.
 */
void obtain_op_times(int compute) {
  size_t i;
  double time = 0;
  for(i=0; i<config_file->n_stages; i++) {
    time+=init_stage(config_file, i, *group, comm, compute);
  }
  if(!compute) {results->wasted_time += time;}
}

/*
 * Libera toda la memoria asociada con la aplicacion
 */
void free_application_data() {
  if(config_file->sdr) {
    free(group->sync_array);
  }
  if(config_file->adr) {
    free(group->async_array);
  }
  
  free_malleability();

  if(group->grp == 0) { //FIXME Revisar porque cuando es diferente a 0 no funciona
    free_results_data(results, config_file->n_stages);
    free(results);
  }
  free_config(config_file);
  free(group);
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
