#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "process_phase.h"
#include "Main_datatypes.h"
#include "configuration.h"
#include "results.h"
#include "../MaM/distribution_methods/Distributed_CommDist.h"
#include "MAM.h"

/**
 * @file Main.c
 * @brief SAM entry point: initialise MaM, run phases, and print results.
 */

/** @brief Maximum elements per MaM data chunk when splitting SDR/ADR payloads. */
#define DR_MAX_SIZE 1000000000

void init_group_struct(char *i_argv[], int i_argc, int i_myId, int i_numP);
void init_application(void);
void free_application_data(void);
void free_zombie_process(void);

void print_general_info(int i_myId, int i_grp, int i_numP);
int print_local_results(void);
int print_final_results(void);
int create_out_file(char *i_name, int *o_ptr, int i_newstdout);

void modify_configuration(void);
void init_originals(void);
void init_targets(void);
void update_surviving_targets(void);
void update_targets(void);
void user_redistribution(void *i_args);

/** @brief Loaded Proteo configuration for this run. */
configuration *config_file;
/** @brief Current process-group runtime state. */
group_data *group;
/** @brief Aggregated timing results. */
results_data *results;
/** @brief Active application communicator (may change after resize). */
MPI_Comm comm;
/** @brief Communicator used during user redistribution / target init. */
MPI_Comm new_comm;
/** @brief Optional run id (argv[2]) to distinguish analysis outputs. */
int run_id = 0;

/**
 * @brief SAM main: initialise MPI/MaM, emulate phases with resizes, print, finalize.
 *
 * Originals load the config and register redistribution data; spawned children
 * attach via MaM and call ::update_targets. The loop alternates ::phase_normal
 * and ::phase_reconf until all groups complete.
 *
 * @param[in] argc Argument count.
 * @param[in] argv Arguments (@c argv[1] = config path; optional @c argv[2] = run id).
 * @return 0 on success.
 */
int main(int argc, char *argv[]) {
    int numP, myId;
    int req;
    int im_child;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &req);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    comm = MPI_COMM_WORLD;
    new_comm = MPI_COMM_NULL;

    if (req != MPI_THREAD_MULTIPLE) {
      printf("No se ha obtenido la configuración de hilos necesaria\nSolicitada %d -- Devuelta %d\n", req, MPI_THREAD_MULTIPLE);
      fflush(stdout);
      MPI_Abort(MPI_COMM_WORLD, -50);
    }

    init_group_struct(argv, argc, myId, numP);
    im_child = MAM_Init(ROOT, &comm, argv[0], user_redistribution, NULL);

    //MAM_Use_valgrind(1);

    if (im_child) {
      update_targets();

    } else {
      init_application();
      init_originals();

      MPI_Barrier(comm);
      results->exec_start = MPI_Wtime();
    }

    //
    // START EXECUTION -------------------------------
    //
    do {
      MPI_Comm_size(comm, &(group->numP));
      MPI_Comm_rank(comm, &(group->myId));
      if (group->grp != 0) {
        init_phases(group, config_file, results, 0, comm);  // Refresh compute timings for the new group
        MAM_Retrieve_times(&results->spawn_time[group->grp - 1], &results->sync_time[group->grp - 1], &results->async_time[group->grp - 1], &results->user_time[group->grp - 1], &results->malleability_time[group->grp - 1]);
      }
      modify_configuration();

      phase_normal(group, config_file, results, comm); // TODO: The return value can simplify the next if?
      if (config_file->n_groups != group->grp + 1) { // FIXME: What if there are more groups but the app ended?
        phase_reconf(group, config_file, results, user_redistribution, comm);
        reset_results_index(results, group->actual_phase);
        update_targets();
      } else if (group->actual_phase == config_file->n_phases) { group->grp++; } // ENDING clause
    } while (config_file->n_groups != group->grp);
    //
    // END EXECUTION ----------------------------------------------------------
    //

    MPI_Barrier(comm);
    results->exec_time = MPI_Wtime() - results->exec_start - results->wasted_time;
    group->grp = group->grp - 1; // Adapt to real grp that ends the execution
    print_local_results();
    group->grp = group->grp + 1;
    print_final_results(); // After this point processes must not write results again

    MPI_Barrier(comm);
    if (comm != MPI_COMM_WORLD && comm != MPI_COMM_NULL) {
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

/**
 * @brief Print rank, group, host name, and PID for diagnostics.
 * @param[in] i_myId Local MPI rank.
 * @param[in] i_grp  Group index.
 * @param[in] i_numP Communicator size.
 */
void print_general_info(int i_myId, int i_grp, int i_numP) {
  int len;
  char *name = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  char *version = malloc(MPI_MAX_LIBRARY_VERSION_STRING * sizeof(char));
  MPI_Get_processor_name(name, &len);
  MPI_Get_library_version(version, &len);
  printf("P%d Nuevo GRUPO %d de %d procs en nodo %s -- PID=%d\n", i_myId, i_grp, i_numP, name, getpid());

  free(name);
  free(version);
}

/**
 * @brief On root, write per-group iteration/stage results to an output file.
 *
 * Reduces per-iteration times first. Redirects stdout to
 * @c R{run_id}_G{grp}NP{numP}ID{rank}.out while printing.
 *
 * @retval  0 Success.
 * @retval -1 Allocation failure.
 * @retval -2 File-name formatting failure.
 * @retval -3 Failed to restore stdout.
 */
int print_local_results(void) {
  int ptr_local, ptr_out, err;
  size_t i;
  char *file_name;

  // This function causes an overhead in the recorded time for last group
  compute_results_iter(results, group->myId, group->numP, ROOT, config_file->n_phases, group->actual_phase, config_file->capture_method, comm);
  if (group->myId == ROOT) {
    ptr_out = dup(1);

    file_name = NULL;
    file_name = malloc(40 * sizeof(char));
    if (file_name == NULL) return -1; // Could not allocate memory
    err = snprintf(file_name, 40, "R%d_G%dNP%dID%d.out", run_id, group->grp, group->numP, group->myId);
    if (err < 0) return -2; // Could not build the file name
    create_out_file(file_name, &ptr_local, 1);

    print_config_group(config_file, group->grp);
    for (i = group->start_phase; i < group->actual_phase; i++) {
      print_iter_results(*results, i);
      print_stage_results(*results, i);
    }
    free(file_name);

    fflush(stdout);
    close(1);
    err = dup(ptr_out);
    if (err < 0) { return -3; } // Could not restore stdout
    close(ptr_out);
  }
  return 0;
}

/**
 * @brief On root of the last group, write global config and timing summary.
 *
 * Output file: @c R{run_id}_Global.out.
 *
 * @retval  0 Success (or nothing to print).
 * @retval -1 Allocation failure.
 * @retval -2 File-name formatting failure.
 * @retval -3 Failed to restore stdout.
 */
int print_final_results(void) {
  int ptr_global, err, ptr_out;
  char *file_name;

  if (group->myId == ROOT) {

    if (config_file->n_groups == group->grp) {
      file_name = NULL;
      file_name = malloc(20 * sizeof(char));
      if (file_name == NULL) return -1; // Could not allocate memory
      err = snprintf(file_name, 20, "R%d_Global.out", run_id);
      if (err < 0) return -2; // Could not build the file name

      ptr_out = dup(1);
      create_out_file(file_name, &ptr_global, 1);
      print_config(config_file);
      print_global_results(*results, config_file->n_resizes);
      fflush(stdout);
      free(file_name);

      close(1);
      err = dup(ptr_out);
      if (err < 0) { return -3; } // Could not restore stdout
    }
  }
  return 0;
}

/**
 * @brief Allocate and initialise the global ::group structure.
 *
 * @param[in] i_argv Process arguments.
 * @param[in] i_argc Argument count.
 * @param[in] i_myId Local MPI rank.
 * @param[in] i_numP Communicator size.
 */
void init_group_struct(char *i_argv[], int i_argc, int i_myId, int i_numP) {
  group = malloc(sizeof(group_data)); // FIXME: Valgrind not freed
  group->myId          = i_myId;
  group->numP          = i_numP;
  group->grp           = 0;
  group->actual_iter   = 0;
  group->actual_phase  = 0;
  group->start_phase   = 0;
  group->exec_iters    = 0;
  group->argc          = i_argc;
  group->argv          = i_argv;
  group->sync_array    = NULL;
  group->async_array   = NULL;
  group->sync_qty      = NULL;
  group->async_qty     = NULL;
}

/**
 * @brief Initialise the first (original) process group.
 *
 * Loads the configuration from @c argv[1], optional @c run_id from @c argv[2],
 * allocates results and SDR/ADR redistribution buffers, then calibrates stages
 * via ::init_phases. Spawned children do not call this; they receive state
 * through MaM / ::user_redistribution → ::init_targets.
 */
void init_application(void) {
  int i, last_index;
  int init_array = 0;
  size_t index, *array_iters_aux, *array_stages_aux;

  if (group->argc < 2) {
    printf("Falta el fichero de configuracion. Uso:\n./programa config.ini|config.json id\nEl argumento numerico id es opcional\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
  }
  if (group->argc > 2) {
    run_id = atoi(group->argv[2]);
  }
  init_config(group->argv[1], &config_file);
  group->grp_config = config_file->groups[group->grp];

  // Init results
  results = malloc(sizeof(results_data));
  array_iters_aux = malloc(config_file->n_phases * sizeof *array_iters_aux);
  array_stages_aux = malloc(config_file->n_phases * sizeof *array_stages_aux);
  for (index = 0; index < config_file->n_phases; index++) {
    array_iters_aux[index] = config_file->phases[index].qty_iters;
    array_stages_aux[index] = config_file->phases[index].qty_stages;
  }
  init_results_data(results, config_file->n_resizes, config_file->n_phases, array_stages_aux, array_iters_aux);
  free(array_iters_aux);
  free(array_stages_aux);

  // Init distribution arrays for reconfigurations
  if (config_file->sdr) {
    group->sync_data_groups = config_file->sdr % DR_MAX_SIZE ? config_file->sdr / DR_MAX_SIZE + 1 : config_file->sdr / DR_MAX_SIZE;
    group->sync_qty = (size_t *)malloc(group->sync_data_groups * sizeof(size_t)); // FIXME: Valgrind not freed
    group->sync_array = (void **)malloc(group->sync_data_groups * sizeof(void *)); // FIXME: Valgrind not freed
    last_index = group->sync_data_groups - 1;
    for (i = 0; i < last_index; i++) {
      group->sync_qty[i] = DR_MAX_SIZE;
      malloc_comm_array(&(group->sync_array[i]), group->sync_qty[i], config_file->datasize, group->myId, group->numP, init_array);
    }
    group->sync_qty[last_index] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    malloc_comm_array(&(group->sync_array[last_index]), group->sync_qty[last_index], config_file->datasize, group->myId, group->numP, init_array); // FIXME: Valgrind not freed
  }

  if (config_file->adr) {
    group->async_data_groups = config_file->adr % DR_MAX_SIZE ? config_file->adr / DR_MAX_SIZE + 1 : config_file->adr / DR_MAX_SIZE;
    group->async_qty = (size_t *)malloc(group->async_data_groups * sizeof(size_t));
    group->async_array = (void **)malloc(group->async_data_groups * sizeof(void *));
    last_index = group->async_data_groups - 1;
    for (i = 0; i < last_index; i++) {
      group->async_qty[i] = DR_MAX_SIZE;
      malloc_comm_array(&(group->async_array[i]), group->async_qty[i], config_file->datasize, group->myId, group->numP, init_array);
    }
    group->async_qty[last_index] = config_file->adr % DR_MAX_SIZE ? config_file->adr % DR_MAX_SIZE : DR_MAX_SIZE;
    malloc_comm_array(&(group->async_array[last_index]), group->async_qty[last_index], config_file->datasize, group->myId, group->numP, init_array);
  }

  init_phases(group, config_file, results, 1, comm);
}

/**
 * @brief Free redistribution buffers, finalise MaM, then free zombie state.
 */
void free_application_data(void) {
  int abort_needed;
  size_t i;

  if (config_file->sdr && group->sync_array != NULL) {
    for (i = 0; i < group->sync_data_groups; i++) {
      if (group->sync_array[i] != NULL) {
        free(group->sync_array[i]);
        group->sync_array[i] = NULL;
      }
    }
  }
  if (config_file->adr && group->async_array != NULL) {
    for (i = 0; i < group->async_data_groups; i++) {
      if (group->async_array[i] != NULL) {
        free(group->async_array[i]);
        group->async_array[i] = NULL;
      }
    }
  }

  abort_needed = MAM_Finalize();
  free_zombie_process();
  if (abort_needed) { MPI_Abort(MPI_COMM_WORLD, -100); }
}

/**
 * @brief Free config/results/group owned by a process that becomes a MaM zombie.
 */
void free_zombie_process(void) {

  if (config_file->sdr && group->sync_array != NULL) {
    free(group->sync_qty);
    group->sync_qty = NULL;
    free(group->sync_array);
    group->sync_array = NULL;
  }

  if (config_file->adr && group->async_array != NULL) {
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

/**
 * @brief Create or append to an output file; optionally redirect stdout to it.
 *
 * @param[in]  i_name      File path.
 * @param[out] o_ptr       Receives the opened file descriptor.
 * @param[in]  i_newstdout Non-zero to make the file the process stdout.
 * @retval  0 Success.
 * @retval -1 Could not open/create the file.
 * @retval -2 Could not close stdout before redirect.
 * @retval -3 Could not dup the new descriptor onto stdout.
 */
int create_out_file(char *i_name, int *o_ptr, int i_newstdout) {
  int err;

  *o_ptr = open(i_name, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (*o_ptr < 0) return -1; // Could not create the file

  if (i_newstdout) {
    err = close(1);
    if (err < 0) return -2; // Could not modify stdout
    err = dup(*o_ptr);
    if (err < 0) return -3; // Could not modify stdout
  }

  return 0;
}


//======================================================||
//======================================================||
//================ INIT MALLEABILITY ===================||
//======================================================||
//======================================================||

/**
 * @brief Load MaM spawn/redistribution settings for the upcoming resize.
 *
 * Reads the next group's methods and strategies from @c config_file and
 * applies them via MaM configuration APIs.
 */
void modify_configuration(void) {
  int req;
  size_t i;
  if (config_file->n_groups != group->grp + 1) {
    MAM_Set_configuration(config_file->groups[group->grp + 1].sm, MAM_STRAT_SPAWN_CLEAR,
      config_file->groups[group->grp + 1].phy_dist, config_file->groups[group->grp + 1].rm, MAM_STRAT_RED_CLEAR);
    for (i = 0; i < config_file->groups[group->grp + 1].ss_len; i++) {
      MAM_Set_key_configuration(MAM_SPAWN_STRATEGIES, config_file->groups[group->grp + 1].ss[i], &req);
    }
    for (i = 0; i < config_file->groups[group->grp + 1].rs_len; i++) {
      MAM_Set_key_configuration(MAM_RED_STRATEGIES, config_file->groups[group->grp + 1].rs[i], &req);
    }
    MAM_Set_target_number(config_file->groups[group->grp + 1].procs); // TODO: TO BE DEPRECATED
  }
}

/**
 * @brief Register the first group's SDR/ADR buffers with MaM for redistribution.
 */
void init_originals(void) {
  size_t i;
  MPI_Datatype dist_type;

  if (config_file->n_groups > 1) {
    MPI_Type_match_size(MPI_TYPECLASS_INTEGER, config_file->datasize, &dist_type);
    if (config_file->sdr) {
      for (i = 0; i < group->sync_data_groups; i++) {
        MAM_Data_add(group->sync_array[i], NULL, group->sync_qty[i], dist_type, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      }
    }
    if (config_file->adr) {
      for (i = 0; i < group->async_data_groups; i++) {
        MAM_Data_add(group->async_array[i], NULL, group->async_qty[i], dist_type, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      }
    }
  }
}

/**
 * @brief Initialise a newly spawned target: receive config, progress, and results.
 *
 * Broadcasts group/phase/iter/@c run_id from root over @c new_comm, then
 * receives the configuration and results via ::recv_config_file / ::results_comm.
 * Not required for sources that survive into the target group.
 */
void init_targets(void) {
  size_t index, *array_iters_aux, *array_stages_aux;
  MPI_Datatype type_size_t;

  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);
  MPI_Bcast(&group->grp, 1, MPI_INT, ROOT, new_comm);
  MPI_Bcast(&group->actual_iter, 1, type_size_t, ROOT, new_comm);
  MPI_Bcast(&group->actual_phase, 1, type_size_t, ROOT, new_comm);
  MPI_Bcast(&run_id, 1, MPI_INT, ROOT, new_comm);

  recv_config_file(ROOT, new_comm, &config_file);

  results = malloc(sizeof(results_data));
  array_iters_aux = malloc(config_file->n_phases * sizeof *array_iters_aux);
  array_stages_aux = malloc(config_file->n_phases * sizeof *array_stages_aux);
  for (index = 0; index < config_file->n_phases; index++) {
    array_iters_aux[index] = config_file->phases[index].qty_iters;
    array_stages_aux[index] = config_file->phases[index].qty_stages;
  }
  init_results_data(results, config_file->n_resizes, config_file->n_phases, array_stages_aux, array_iters_aux);
  results_comm(results, ROOT, config_file->n_resizes, new_comm);

  free(array_iters_aux);
  free(array_stages_aux);
}

/**
 * @brief Advance to the next group and refresh local pointers into MaM data.
 *
 * Increments @c grp, copies the new group config, clears surviving-source
 * buffers via ::update_surviving_targets, then obtains SDR/ADR entry pointers
 * from MaM.
 */
void update_targets(void) {
  size_t i, entries, total_qty;
  void *value = NULL;
  MPI_Datatype type;

  group->grp = group->grp + 1;
  group->grp_config = config_file->groups[group->grp];
  group->start_phase = group->actual_phase;

  update_surviving_targets();
  if (config_file->sdr) {
    MAM_Data_get_entries(MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE, &entries);
    group->sync_qty = (size_t *)malloc(entries * sizeof(size_t));
    group->sync_array = (void **)malloc(entries * sizeof(void *));
    for (i = 0; i < entries; i++) {
      MAM_Data_get_pointer(&value, i, &total_qty, &type, MAM_DATA_DISTRIBUTED, MAM_DATA_VARIABLE);
      group->sync_array[i] = value;
      group->sync_qty[i] = DR_MAX_SIZE;
    }
    group->sync_qty[entries - 1] = config_file->sdr % DR_MAX_SIZE ? config_file->sdr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->sync_data_groups = entries;
  }

  if (config_file->adr) {
    MAM_Data_get_entries(MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT, &entries);
    group->async_qty = (size_t *)malloc(entries * sizeof(size_t));
    group->async_array = (void **)malloc(entries * sizeof(void *));
    for (i = 0; i < entries; i++) {
      MAM_Data_get_pointer(&value, i, &total_qty, &type, MAM_DATA_DISTRIBUTED, MAM_DATA_CONSTANT);
      group->async_array[i] = value;
      group->async_qty[i] = DR_MAX_SIZE;
    }
    group->async_qty[entries - 1] = config_file->adr % DR_MAX_SIZE ? config_file->adr % DR_MAX_SIZE : DR_MAX_SIZE;
    group->async_data_groups = entries;
  }
}

/**
 * @brief Drop old SDR/ADR buffers for sources that survive into the target group.
 *
 * Resets @c exec_iters and frees previous sync/async arrays so
 * ::update_targets can install the post-redistribution pointers.
 */
void update_surviving_targets(void) {
  size_t i;
  group->exec_iters = 0;

  if (config_file->sdr && group->sync_array != NULL) {
    for (i = 0; i < group->sync_data_groups; i++) {
      free(group->sync_array[i]);
      group->sync_array[i] = NULL;
    }
    free(group->sync_qty);
    group->sync_qty = NULL;
    free(group->sync_array);
    group->sync_array = NULL;
  }

  if (config_file->adr && group->async_array != NULL) {
    for (i = 0; i < group->async_data_groups; i++) {
      free(group->async_array[i]);
      group->async_array[i] = NULL;
    }
    free(group->async_qty);
    group->async_qty = NULL;
    free(group->async_array);
    group->async_array = NULL;
  }
}

/**
 * @brief MaM user-redistribution callback for non-MaM-managed application state.
 *
 * New ranks call ::init_targets. Sources broadcast progress, send config and
 * results over @c new_comm, print local results, and zombies call
 * ::free_zombie_process. Always ends with @c MAM_Resume_redistribution.
 *
 * @param[in] i_args Unused MaM callback argument.
 */
void user_redistribution(void *i_args) {
  int commited;
  MPI_Datatype type_size_t;
  mam_user_reconf_t user_reconf;

  (void)i_args;
  MAM_Get_Reconf_Info(&user_reconf);
  new_comm = user_reconf.comm;
  if (user_reconf.rank_state == MAM_PROC_NEW_RANK) {
    init_targets();
  } else {
    MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);
    MPI_Bcast(&group->grp, 1, MPI_INT, ROOT, new_comm);
    MPI_Bcast(&group->actual_iter, 1, type_size_t, ROOT, new_comm);
    MPI_Bcast(&group->actual_phase, 1, type_size_t, ROOT, new_comm);
    MPI_Bcast(&run_id, 1, MPI_INT, ROOT, new_comm);

    send_config_file(config_file, ROOT, new_comm);
    results_comm(results, ROOT, config_file->n_resizes, new_comm);

    group->actual_phase++;
    print_local_results();
    group->actual_phase--;

    if (user_reconf.rank_state == MAM_PROC_ZOMBIE) {
      free_zombie_process();
    }
  }

  MAM_Resume_redistribution(&commited);
}
