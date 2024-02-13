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
#include "../malleability/MAM.h"

#define DR_MAX_SIZE 1000000000

int work();
double iterate(int async_comm);
double iterate_relaxed(double *time, double *times_stages);
double iterate_rigid(double *time, double *times_stages);

void init_group_struct(char *argv[], int argc, int myId, int numP);
void init_application();
void obtain_op_times();
void free_application_data();
void free_zombie_process();

void print_general_info(int myId, int grp, int numP);
int print_local_results();
int print_final_results();
int create_out_file(char *nombre, int *ptr, int newstdout);


void init_originals();
void init_targets();
void update_targets();
void user_redistribution(void *args);

configuration *config_file;
group_data *group;
results_data *results;
MPI_Comm comm, new_comm;
int run_id = 0; // Utilizado para diferenciar más fácilmente ejecuciones en el análisis

int main(int argc, char *argv[]) {
    int numP, myId, res;
    int req;
    int im_child;
    int abort_needed = 0;
    size_t i;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &req);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    comm = MPI_COMM_WORLD;
    new_comm = MPI_COMM_NULL;

    if(req != MPI_THREAD_MULTIPLE) {
      printf("No se ha obtenido la configuración de hilos necesaria\nSolicitada %d -- Devuelta %d\n", req, MPI_THREAD_MULTIPLE);
      fflush(stdout);
      MPI_Abort(MPI_COMM_WORLD, -50);
    }

    init_group_struct(argv, argc, myId, numP);
    im_child = MAM_Init(ROOT, &comm, argv[0], user_redistribution, NULL);

    if(im_child) {
      update_targets();

    } else {
      init_application();
      init_originals();

      MPI_Barrier(comm);
      results->exec_start = MPI_Wtime();
    }

    //
    // EMPIEZA LA EJECUCION-------------------------------
    //
    do {
      MPI_Comm_size(comm, &(group->numP));
      MPI_Comm_rank(comm, &(group->myId));

      if(group->grp != 0) {
        obtain_op_times(0); //Obtener los nuevos valores de tiempo para el computo
        MAM_Retrieve_times(&results->spawn_time[group->grp - 1], &results->sync_time[group->grp - 1], &results->async_time[group->grp - 1], &results->malleability_time[group->grp - 1]);
      }

      if(config_file->n_groups != group->grp + 1) { //TODO Llevar a otra funcion
        MAM_Set_configuration(config_file->groups[group->grp+1].sm, MAM_STRAT_SPAWN_CLEAR, 
			config_file->groups[group->grp+1].phy_dist, config_file->groups[group->grp+1].rm, MAM_STRAT_RED_CLEAR);
	for(i=0; i<config_file->groups[group->grp+1].ss_len; i++) {
	  MAM_Set_key_configuration(MAM_SPAWN_STRATEGIES, config_file->groups[group->grp+1].ss[i], &req);
	}
	for(i=0; i<config_file->groups[group->grp+1].rs_len; i++) {
	  MAM_Set_key_configuration(MAM_RED_STRATEGIES, config_file->groups[group->grp+1].rs[i], &req);
	}
        MAM_Set_target_number(config_file->groups[group->grp+1].procs); // TODO TO BE DEPRECATED

        if(group->grp != 0) {
          malleability_modify_data(&(group->grp), 0, 1, MPI_INT, 1, 1);
          malleability_modify_data(&(group->iter_start), 0, 1, MPI_INT, 1, 0);
        }
      }

      res = work();

      if(res==1) { // Se ha llegado al final de la aplicacion
        MPI_Barrier(comm);
        results->exec_time = MPI_Wtime() - results->exec_start - results->wasted_time;
        print_local_results();
      }
      

      reset_results_index(results);

      group->grp = group->grp + 1;
    } while(config_file->n_groups > group->grp);

    //
    // TERMINA LA EJECUCION ----------------------------------------------------------
    // 
    print_final_results(); // Pasado este punto ya no pueden escribir los procesos

    MPI_Barrier(comm);
    if(comm != MPI_COMM_WORLD && comm != MPI_COMM_NULL) {
      MPI_Comm_free(&comm);
    }

    if(group->myId == ROOT && config_file->groups[group->grp-1].sm == MALL_SPAWN_MERGE) {
      abort_needed = 1;
    }
    free_application_data();

    if(abort_needed) { MPI_Abort(MPI_COMM_WORLD, -100); }
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
  int wait_completed = MAM_CHECK_COMPLETION;

  maxiter = config_file->groups[group->grp].iters;
  state = MAM_NOT_STARTED;
  res = 0;

  for(iter=group->iter_start; iter < maxiter; iter++) {
    iterate(state);
  }

  if(config_file->n_groups != group->grp + 1)
    MAM_Checkpoint(&state, wait_completed, user_redistribution, NULL);

  iter = 0;
  while(state == MAM_PENDING || state == MAM_USER_PENDING) {
    if(group->grp+1 < config_file->n_groups && iter < config_file->groups[group->grp+1].iters) {
      iterate(state);
      iter++;
      group->iter_start = iter;
    } else { wait_completed = MAM_WAIT_COMPLETION; }
    MAM_Checkpoint(&state, wait_completed, user_redistribution, NULL);
  }

  //if(state == MAM_COMPLETED) {}
  if(config_file->n_groups == group->grp + 1) { res=1; }
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
  if(async_comm == MAM_PENDING) { 
    // TODO Que diferencie entre tipo de partes asincronas?
    results->iters_async += 1;
  }

  // TODO Pasar el resto de este código a results.c
  if(results->iter_index == results->iters_size) { // Aumentar tamaño de ambos vectores de resultados
    realloc_results_iters(results, config_file->n_stages, results->iters_size + 100);
  }
  results->iters_time[results->iter_index] = time;
  for(i=0; i < config_file->n_stages; i++) {
    results->stage_times[i][results->iter_index] = times_stages_aux[i];
  }
  results->iter_index = results->iter_index + 1;
  // TODO Pasar hasta aqui

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
    start_time_stage = MPI_Wtime();
    aux+= process_stage(*config_file, config_file->stages[i], *group, comm);
    MPI_Barrier(comm);
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

  // This function causes an overhead in the recorded time for last group
  compute_results_iter(results, group->myId, group->numP, ROOT, config_file->n_stages, config_file->capture_method, comm);
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

    if(config_file->n_groups == group->grp) {
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
  int i, last_index;

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
    group->sync_data_groups = config_file->sdr % DR_MAX_SIZE ? config_file->sdr/DR_MAX_SIZE+1 : config_file->sdr/DR_MAX_SIZE;
    group->sync_qty = (int *) malloc(group->sync_data_groups * sizeof(int));
    group->sync_array = (char **) malloc(group->sync_data_groups * sizeof(char *));
    last_index = group->sync_data_groups-1; 
    for(i=0; i<last_index; i++) {
      group->sync_qty[i] = DR_MAX_SIZE;
      malloc_comm_array(&(group->sync_array[i]), group->sync_qty[i], group->myId, group->numP);
    }
    group->sync_qty[last_index] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    malloc_comm_array(&(group->sync_array[last_index]), group->sync_qty[last_index], group->myId, group->numP);
  }

  if(config_file->adr) {
    group->async_data_groups = config_file->adr % DR_MAX_SIZE ? config_file->adr/DR_MAX_SIZE+1 : config_file->adr/DR_MAX_SIZE;
    group->async_qty = (int *) malloc(group->async_data_groups * sizeof(int));
    group->async_array = (char **) malloc(group->async_data_groups * sizeof(char *));
    last_index = group->async_data_groups-1; 
    for(i=0; i<last_index; i++) {
      group->async_qty[i] = DR_MAX_SIZE;
      malloc_comm_array(&(group->async_array[i]), group->async_qty[i], group->myId, group->numP);
    }
    group->async_qty[last_index] = config_file->adr % DR_MAX_SIZE ? config_file->adr % DR_MAX_SIZE : DR_MAX_SIZE;
    malloc_comm_array(&(group->async_array[last_index]), group->async_qty[last_index], group->myId, group->numP);
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
  size_t i;

  if(config_file->sdr && group->sync_array != NULL) {
    for(i=0; i<group->sync_data_groups; i++) {
      free(group->sync_array[i]);
      group->sync_array[i] = NULL;
    }
    free(group->sync_qty);
    group->sync_qty = NULL;
    free(group->sync_array);
    group->sync_array = NULL;

  }
  if(config_file->adr && group->async_array != NULL) {
    for(i=0; i<group->async_data_groups; i++) {
      free(group->async_array[i]);
      group->async_array[i] = NULL;
    }
    free(group->async_qty);
    group->async_qty = NULL;
    free(group->async_array);
    group->async_array = NULL;
  }
/*  MPI_Barrier(MPI_COMM_WORLD); fflush(stdout);
  printf("TEST %d\n", group->myId);
  MPI_Barrier(MPI_COMM_WORLD); fflush(stdout);*/
  MAM_Finalize();
  free_zombie_process();
}


/*
 * Libera la memoria asociada a un proceso Zombie
 */
void free_zombie_process() {
  free_results_data(results, config_file->n_stages);
  free(results);

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


//======================================================||
//======================================================||
//================ INIT MALLEABILITY ===================||
//======================================================||
//======================================================||

void init_originals() {
  size_t i;

  if(config_file->n_groups > 1) {
    malleability_add_data(&(group->grp), 1, MPI_INT, MAM_DATA_REPLICATED, MAM_DATA_CONSTANT);
    malleability_add_data(&run_id, 1, MPI_INT, MAM_DATA_REPLICATED, MAM_DATA_CONSTANT);
    malleability_add_data(&(group->iter_start), 1, MPI_INT, MAM_DATA_REPLICATED, MAM_DATA_VARIABLE);

    if(config_file->sdr) {
      for(i=0; i<group->sync_data_groups; i++) {
        malleability_add_data(group->sync_array[i], group->sync_qty[i], MPI_CHAR, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      }
    }
    if(config_file->adr) {
      for(i=0; i<group->async_data_groups; i++) {
        malleability_add_data(group->async_array[i], group->async_qty[i], MPI_CHAR, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      }
    }
  }
}

void init_targets() {
  size_t i, entries;
  void *value = NULL;

  malleability_get_data(&value, 0, MAM_DATA_REPLICATED, MAM_DATA_CONSTANT);
  group->grp = *((int *)value);
  group->grp = group->grp + 1;

  recv_config_file(ROOT, new_comm, &config_file);
  results = malloc(sizeof(results_data));
  init_results_data(results, config_file->n_resizes, config_file->n_stages, config_file->groups[group->grp].iters);
  results_comm(results, ROOT, config_file->n_resizes, new_comm);

  malleability_get_data(&value, 1, MAM_DATA_REPLICATED, MAM_DATA_CONSTANT);
  run_id = *((int *)value);
      
  if(config_file->adr) {
    malleability_get_entries(&entries, 0, 1);
    group->async_qty = (int *) malloc(entries * sizeof(int));
    group->async_array = (char **) malloc(entries * sizeof(char *));
    for(i=0; i<entries; i++) {
      malleability_get_data(&value, i, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      group->async_array[i] = (char *)value;
      group->async_qty[i] = DR_MAX_SIZE;
    }
    group->async_qty[entries-1] = config_file->adr % DR_MAX_SIZE ? config_file->adr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->async_data_groups = entries;
  }
}

void update_targets() { //FIXME Should not be needed after redist -- Declarar antes
  size_t i, entries;
  void *value = NULL;

  malleability_get_data(&value, 0, MAM_DATA_REPLICATED, MAM_DATA_VARIABLE);
  group->iter_start = *((int *)value);

  if(config_file->sdr) {
    malleability_get_entries(&entries, 0, 0);
    group->sync_qty = (int *) malloc(entries * sizeof(int));
    group->sync_array = (char **) malloc(entries * sizeof(char *));
    for(i=0; i<entries; i++) {
      malleability_get_data(&value, i, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      group->sync_array[i] = (char *)value;
      group->sync_qty[i] = DR_MAX_SIZE;
    }
    group->sync_qty[entries-1] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->sync_data_groups = entries;
  }
}

void user_redistribution(void *args) {
  int commited;
  mam_user_reconf_t user_reconf;

  MAM_Get_Reconf_Info(&user_reconf);
  new_comm = user_reconf.comm;
  if(user_reconf.rank_state == MAM_PROC_NEW_RANK) {
    init_targets();
  } else {
    send_config_file(config_file, ROOT, new_comm);
    results_comm(results, ROOT, config_file->n_resizes, new_comm);

    print_local_results();
    if(user_reconf.rank_state == MAM_PROC_ZOMBIE) {
      free_zombie_process();
    }
  }

  MAM_Resume_redistribution(&commited);
}
