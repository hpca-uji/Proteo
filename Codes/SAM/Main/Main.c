#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "process_phase.h"
#include "Main_datatypes.h"
#include "configuration.h"
#include "../IOcodes/results.h"
#include "../MaM/distribution_methods/Distributed_CommDist.h"
#include "../MaM/MAM.h"

#define DR_MAX_SIZE 1000000000

void init_group_struct(char *argv[], int argc, int myId, int numP);
void init_application();
void free_application_data();
void free_zombie_process();

void print_general_info(int myId, int grp, int numP);
int print_local_results();
int print_final_results();
int create_out_file(char *nombre, int *ptr, int newstdout);

void modify_configuration();
void init_originals();
void init_targets();
void update_surviving_targets();
void update_targets();
void user_redistribution(void *args);

configuration *config_file;
group_data *group;
results_data *results;
MPI_Comm comm, new_comm;
int run_id = 0; // Utilizado para diferenciar más fácilmente ejecuciones en el análisis

int main(int argc, char *argv[]) {
    int numP, myId;
    int req;
    int im_child;

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

    //MAM_Use_valgrind(1);

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
        init_phases(group, config_file, results, 0, comm);  //Obtener los nuevos valores de tiempo para el computo
        MAM_Retrieve_times(&results->spawn_time[group->grp - 1], &results->sync_time[group->grp - 1], &results->async_time[group->grp - 1], &results->user_time[group->grp - 1], &results->malleability_time[group->grp - 1]);
      }
      modify_configuration();
    
      phase_normal(group, config_file, results, comm);
      if(config_file->n_groups != group->grp + 1) {
        phase_reconf(group, config_file, results, user_redistribution, comm); // FIXME Como llamo a user_redist?
        reset_results_index(results, group->actual_phase);
        update_targets(); 
      } else { group->grp++; }
    } while(config_file->n_groups != group->grp);
    //
    // TERMINA LA EJECUCION ----------------------------------------------------------
    // 

    MPI_Barrier(comm);
    results->exec_time = MPI_Wtime() - results->exec_start - results->wasted_time;
    group->grp = group->grp - 1; //Adapt to real grp that ends the execution
    print_local_results();
    group->grp = group->grp + 1;
    print_final_results(); // Pasado este punto ya no pueden escribir los procesos

    MPI_Barrier(comm);
    if(comm != MPI_COMM_WORLD && comm != MPI_COMM_NULL) {
      MPI_Comm_free(&comm);
    }
    free_application_data();

    MPI_Finalize();
    return 0;
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
  //printf("P%d Nuevo GRUPO %d de %d procs en nodo %s con %s\n", myId, grp, numP, name, version);
  printf("P%d Nuevo GRUPO %d de %d procs en nodo %s -- PID=%d\n", myId, grp, numP, name, getpid());

  free(name);
  free(version);
}

/*
 * Pide al proceso raiz imprimir los datos sobre las iteraciones realizadas por el grupo de procesos.
 */
int print_local_results() {
  int ptr_local, ptr_out, err;
  size_t i;
  char *file_name;

  // This function causes an overhead in the recorded time for last group
  compute_results_iter(results, group->myId, group->numP, ROOT, config_file->n_phases, config_file->capture_method, comm);
  if(group->myId == ROOT) {
    ptr_out = dup(1);

    file_name = NULL;
    file_name = malloc(40 * sizeof(char));
    if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
    err = snprintf(file_name, 40, "R%d_G%dNP%dID%d.out", run_id, group->grp, group->numP, group->myId);
    if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero
    create_out_file(file_name, &ptr_local, 1);
  
    print_config_group(config_file, group->grp);
    for(i = group->start_phase; i < group->actual_phase; i++) {
      print_iter_results(*results, i);
      print_stage_results(*results, i);
    }
    free(file_name);

    fflush(stdout);
    close(1);
    err = dup(ptr_out);
    if(err < 0) { return -3; } // No ha sido posible redireccionar a salida estandar
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
      err = dup(ptr_out);
      if(err < 0) { return -3; } // No ha sido posible redireccionar a salida estandar
    }
  }
  return 0;
}

/*
 * Inicializa la estructura group
 */
void init_group_struct(char *argv[], int argc, int myId, int numP) {
  group = malloc(sizeof(group_data)); // Valgrind not freed
  group->myId          = myId;
  group->numP          = numP;
  group->grp           = 0;
  group->actual_iter   = 0;
  group->actual_phase  = 0;
  group->start_phase   = 0;
  group->argc          = argc;
  group->argv          = argv;
  group->sync_array    = NULL;
  group->async_array   = NULL;
  group->sync_qty      = NULL;
  group->async_qty     = NULL;
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
  size_t index, *array_iters_aux, *array_stages_aux;

  if(group->argc < 2) {
    printf("Falta el fichero de configuracion. Uso:\n./programa config.ini id\nEl argumento numerico id es opcional\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
  }
  if(group->argc > 2) {
    run_id = atoi(group->argv[2]);
  }
  init_config(group->argv[1], &config_file);
  group->grp_config = config_file->groups[group->grp];

  // Init results
  results = malloc(sizeof(results_data));
  array_iters_aux = malloc(config_file->n_phases * sizeof *array_iters_aux);
  array_stages_aux = malloc(config_file->n_phases * sizeof *array_stages_aux);
  for(index = 0; index < config_file->n_phases; index++) {
    array_iters_aux[index] = config_file->phases[index].qty_iters;
    array_stages_aux[index] = config_file->phases[index].qty_stages;
  }
  init_results_data(results, config_file->n_resizes, config_file->n_phases, array_stages_aux, array_iters_aux);
  free(array_iters_aux);
  free(array_stages_aux);

  // Init distribution arrays for reconfigurations
  if(config_file->sdr) {
    group->sync_data_groups = config_file->sdr % DR_MAX_SIZE ? config_file->sdr/DR_MAX_SIZE+1 : config_file->sdr/DR_MAX_SIZE;
    group->sync_qty = (int *) malloc(group->sync_data_groups * sizeof(int)); // FIXME Valgrind not freed
    group->sync_array = (char **) malloc(group->sync_data_groups * sizeof(char *)); // Valgrind not freed
    last_index = group->sync_data_groups-1; 
    for(i=0; i<last_index; i++) {
      group->sync_qty[i] = DR_MAX_SIZE;
      malloc_comm_array(&(group->sync_array[i]), group->sync_qty[i], group->myId, group->numP);
    }
    group->sync_qty[last_index] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    malloc_comm_array(&(group->sync_array[last_index]), group->sync_qty[last_index], group->myId, group->numP); // Valgrind not freed
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

  init_phases(group, config_file, results, 1, comm);
}

/*
 * Libera toda la memoria asociada con la aplicacion
 */
void free_application_data() {
  int abort_needed;
  size_t i;

  if(config_file->sdr && group->sync_array != NULL) {
    for(i=0; i<group->sync_data_groups; i++) {
      if(group->sync_array[i] != NULL) {
        free(group->sync_array[i]);
        group->sync_array[i] = NULL;
      }
    }
  }
  if(config_file->adr && group->async_array != NULL) {
    for(i=0; i<group->async_data_groups; i++) {
      if(group->async_array[i] != NULL) {
        free(group->async_array[i]);
        group->async_array[i] = NULL;
      }
    }
  }

  abort_needed = MAM_Finalize();
  free_zombie_process();
  if(abort_needed) { MPI_Abort(MPI_COMM_WORLD, -100); }
}


/*
 * Libera la memoria asociada a un proceso Zombie
 */
void free_zombie_process() {
  
  if(config_file->sdr && group->sync_array != NULL) {
    free(group->sync_qty);
    group->sync_qty = NULL;
    free(group->sync_array);
    group->sync_array = NULL;
  }

  if(config_file->adr && group->async_array != NULL) {
    free(group->async_qty);
    group->async_qty = NULL;
    free(group->async_array);
    group->async_array = NULL;
  }
  
  free_results_data(results, config_file->n_phases);
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

void modify_configuration() {
  int req;
  size_t i;
  if(config_file->n_groups != group->grp + 1) {
    MAM_Set_configuration(config_file->groups[group->grp+1].sm, MAM_STRAT_SPAWN_CLEAR, 
      config_file->groups[group->grp+1].phy_dist, config_file->groups[group->grp+1].rm, MAM_STRAT_RED_CLEAR);
    for(i=0; i<config_file->groups[group->grp+1].ss_len; i++) {
	    MAM_Set_key_configuration(MAM_SPAWN_STRATEGIES, config_file->groups[group->grp+1].ss[i], &req);
	  }
	  for(i=0; i<config_file->groups[group->grp+1].rs_len; i++) {
	    MAM_Set_key_configuration(MAM_RED_STRATEGIES, config_file->groups[group->grp+1].rs[i], &req);
	  }
    MAM_Set_target_number(config_file->groups[group->grp+1].procs); // TODO TO BE DEPRECATED
  }
}

void init_originals() {
  size_t i;

  if(config_file->n_groups > 1) {
    if(config_file->sdr) {
      for(i=0; i<group->sync_data_groups; i++) {
        MAM_Data_add(group->sync_array[i], NULL, group->sync_qty[i], MPI_CHAR, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      }
    }
    if(config_file->adr) {
      for(i=0; i<group->async_data_groups; i++) {
        MAM_Data_add(group->async_array[i], NULL, group->async_qty[i], MPI_CHAR, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      }
    }
  }
}

void init_targets() {
  size_t index, *array_iters_aux, *array_stages_aux;

  MPI_Bcast(&group->grp, 1, MPI_INT, ROOT, new_comm);
  MPI_Bcast(&group->actual_iter, 1, MPI_INT, ROOT, new_comm);
  MPI_Bcast(&group->actual_phase, 1, MPI_INT, ROOT, new_comm);
  MPI_Bcast(&run_id, 1, MPI_INT, ROOT, new_comm);
  group->start_phase = group->actual_phase;

  recv_config_file(ROOT, new_comm, &config_file);

  results = malloc(sizeof(results_data));
  array_iters_aux = malloc(config_file->n_phases * sizeof *array_iters_aux);
  array_stages_aux = malloc(config_file->n_phases * sizeof *array_stages_aux);
  for(index = 0; index < config_file->n_phases; index++) {
    array_iters_aux[index] = config_file->phases[index].qty_iters;
    array_stages_aux[index] = config_file->phases[index].qty_stages;
  }
  init_results_data(results, config_file->n_resizes, config_file->n_phases, array_stages_aux, array_iters_aux);
  results_comm(results, ROOT, config_file->n_resizes, new_comm);

  free(array_iters_aux);
  free(array_stages_aux);
}

void update_targets() {
  size_t i, entries, total_qty;
  void *value = NULL;
  MPI_Datatype type;

  group->grp = group->grp + 1;
  group->grp_config = config_file->groups[group->grp];
  update_surviving_targets();
  if(config_file->sdr) {
    MAM_Data_get_entries(MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE, &entries);
    group->sync_qty = (int *) malloc(entries * sizeof(int));
    group->sync_array = (char **) malloc(entries * sizeof(char *));
    for(i=0; i<entries; i++) {
      MAM_Data_get_pointer(&value, i, &total_qty, &type, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      group->sync_array[i] = (char *)value;
      group->sync_qty[i] = DR_MAX_SIZE;
    }
    group->sync_qty[entries-1] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->sync_data_groups = entries;
  }

  if(config_file->adr) {
    MAM_Data_get_entries(MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT, &entries);
    group->async_qty = (int *) malloc(entries * sizeof(int));
    group->async_array = (char **) malloc(entries * sizeof(char *));
    for(i=0; i<entries; i++) {
      MAM_Data_get_pointer(&value, i, &total_qty, &type, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      group->async_array[i] = (char *)value;
      group->async_qty[i] = DR_MAX_SIZE;
    }
    group->async_qty[entries-1] = config_file->adr % DR_MAX_SIZE ? config_file->adr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->async_data_groups = entries;
  }
}

void update_surviving_targets() {
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
}

void user_redistribution(void *args) {
  int commited;
  mam_user_reconf_t user_reconf;

  MAM_Get_Reconf_Info(&user_reconf);
  new_comm = user_reconf.comm;
  if(user_reconf.rank_state == MAM_PROC_NEW_RANK) {
    init_targets();
  } else {
    MPI_Bcast(&group->grp, 1, MPI_INT, ROOT, new_comm);
    MPI_Bcast(&group->actual_iter, 1, MPI_INT, ROOT, new_comm);
    MPI_Bcast(&group->actual_phase, 1, MPI_INT, ROOT, new_comm);
    MPI_Bcast(&run_id, 1, MPI_INT, ROOT, new_comm);
    group->start_phase = group->actual_phase;

    send_config_file(config_file, ROOT, new_comm);
    results_comm(results, ROOT, config_file->n_resizes, new_comm);

    print_local_results();
    if(user_reconf.rank_state == MAM_PROC_ZOMBIE) {
      free_zombie_process();
    }
  }

  MAM_Resume_redistribution(&commited);
}
