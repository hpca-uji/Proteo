#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include "read_ini.h"
#include "read_json.h"
#include "configuration.h"

/**
 * @file configuration.c
 * @brief Implementation of Proteo configuration load, free, print, and broadcast.
 */

/** @brief Default stage granularity when the config omits it. */
#define CONFIG_GRANULARITY 600
/** @brief Sentinel meaning granularity was not set in the config file. */
#define CONFIG_GRANULARITY_UNDEFINED -1

void malloc_config_resizes(configuration *io_user_config);
void malloc_config_phases(configuration *io_user_config);
void malloc_config_stages(configuration *io_user_config, size_t i_phase_ind);
void check_granularity(configuration *io_user_config);

void free_config_phase(phase_t *io_phase);
void free_config_stage(stage_t *io_stage, int *io_freed_ids, size_t *io_found_ids);

void print_config_phase(phase_t *i_phase, size_t i_index);

void def_struct_config_file(configuration *io_config_file);
void def_struct_groups(configuration *io_config_file);
void def_struct_groups_strategies(configuration *io_config_file);
void def_struct_phase(configuration *io_config_file);
void def_struct_stage(configuration *io_config_file, phase_t *i_phase);

void init_config(char *i_file_name, configuration **o_user_config) {
  if (i_file_name != NULL) {
    ext_functions_t mallocs;
    const char *extension;

    mallocs.resizes_f = malloc_config_resizes;
    mallocs.phases_f = malloc_config_phases;
    mallocs.stages_f = malloc_config_stages;

    extension = strrchr(i_file_name, '.');
    if (extension != NULL && strcmp(extension, ".json") == 0) {
      *o_user_config = read_json_file(i_file_name, mallocs);
    } else if (extension != NULL && strcmp(extension, ".ini") == 0) {
      *o_user_config = read_ini_file(i_file_name, mallocs);
    } else {
      fprintf(stderr, "Unsupported config extension for '%s' (use .ini or .json)\n", i_file_name);
      MPI_Abort(MPI_COMM_WORLD, -4);
      return;
    }

    if (*o_user_config == NULL) {
      MPI_Abort(MPI_COMM_WORLD, -4);
      return;
    }
  } else {
    configuration *config = NULL;

    config = malloc(sizeof(configuration));
    if (config == NULL) {
        perror("Error when reserving configuration structure\n");
        MPI_Abort(MPI_COMM_WORLD, -3);
        return;
    }

    config->config_type = MPI_DATATYPE_NULL;
    config->group_type = MPI_DATATYPE_NULL;
    config->group_strats_type = MPI_DATATYPE_NULL;
    config->phase_type = MPI_DATATYPE_NULL;
    config->stage_type = MPI_DATATYPE_NULL;

    config->datasize = sizeof(char);
    config->n_resizes = 0;
    config->n_groups = 1;
    malloc_config_resizes(config);
    config->n_phases = 1;
    malloc_config_phases(config);
    *o_user_config = config;
  }
  check_granularity(*o_user_config);
  def_struct_config_file(*o_user_config);
  def_struct_groups_strategies(*o_user_config);
}

/**
 * @brief Allocate and initialise the @c groups array from @c n_groups.
 *
 * Also builds the group MPI derived type. Normally invoked via ::init_config
 * or ::recv_config_file after @c n_groups is known.
 *
 * @param[in,out] io_user_config Configuration whose @c groups are allocated.
 */
void malloc_config_resizes(configuration *io_user_config) {
  size_t i;
  if (io_user_config != NULL) {
    io_user_config->groups = malloc(sizeof(group_config_t) * io_user_config->n_groups);
    for (i = 0; i < io_user_config->n_groups; i++) {
      io_user_config->groups[i].iters = 0;
      io_user_config->groups[i].procs = 1;
      io_user_config->groups[i].sm = 0;
      io_user_config->groups[i].ss = NULL;
      io_user_config->groups[i].ss_len = 0;
      io_user_config->groups[i].phy_dist = 0;
      io_user_config->groups[i].rm = 0;
      io_user_config->groups[i].rs = NULL;
      io_user_config->groups[i].rs_len = 0;
      io_user_config->groups[i].factor = 1;
    }
    def_struct_groups(io_user_config);
  }
}

/**
 * @brief Allocate the @c phases array and clear each phase's @c stages pointer.
 *
 * Also builds the phase MPI derived type.
 *
 * @param[in,out] io_user_config Configuration whose @c phases are allocated.
 */
void malloc_config_phases(configuration *io_user_config) {
  size_t i;
  if (io_user_config != NULL) {
    io_user_config->phases = malloc(io_user_config->n_phases * sizeof *(io_user_config->phases));
    for (i = 0; i < io_user_config->n_phases; i++) {
      io_user_config->phases[i].stages = NULL;
      io_user_config->phases[i].qty_stages = 0;
      io_user_config->phases[i].qty_iters = 0;
    }
    def_struct_phase(io_user_config);
  }
}

/**
 * @brief Allocate and initialise stages for one phase.
 *
 * @param[in,out] io_user_config Configuration being filled.
 * @param[in]     i_phase_ind    Phase index whose stages are allocated.
 */
void malloc_config_stages(configuration *io_user_config, size_t i_phase_ind) {
  size_t i;
  phase_t *phase;

  if (io_user_config != NULL && i_phase_ind < io_user_config->n_phases) {
    phase = io_user_config->phases + i_phase_ind;
    phase->stages = malloc(sizeof(stage_t) * phase->qty_stages);
    for (i = 0; i < phase->qty_stages; i++) {
      phase->stages[i].array = NULL;
      phase->stages[i].full_array = NULL;
      phase->stages[i].double_array = NULL;
      phase->stages[i].reqs = NULL;
      phase->stages[i].counts.counts = NULL;
      phase->stages[i].bytes = 0;
      phase->stages[i].my_bytes = 0;
      phase->stages[i].real_bytes = 0;
      phase->stages[i].operations = 0;
      phase->stages[i].granularity = CONFIG_GRANULARITY_UNDEFINED;
      phase->stages[i].involved_procs = 0;
      phase->stages[i].pt = 0;
      phase->stages[i].fd = -1;
      phase->stages[i].id = -1;
      phase->stages[i].t_op = 0;
      phase->stages[i].t_stage = 0;
      phase->stages[i].t_capped = 0;
    }
    if (io_user_config->stage_type == MPI_DATATYPE_NULL) { def_struct_stage(io_user_config, phase); }
  }
}

void malloc_counts(struct Counts *io_counts, size_t i_numP) {
  io_counts->counts = calloc(i_numP, sizeof(int));
  if (io_counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2); }

  io_counts->displs = calloc(i_numP, sizeof(int));
  if (io_counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2); }
  io_counts->len = i_numP;
}

/**
 * @brief Replace undefined stage granularities with ::CONFIG_GRANULARITY.
 * @param[in,out] io_user_config Configuration to normalise.
 */
void check_granularity(configuration *io_user_config) {
  size_t i_phase, i_stage;
  phase_t *phase;

  if (io_user_config == NULL) {
    return;
  }

  for (i_phase = 0; i_phase < io_user_config->n_phases; i_phase++) {
    phase = io_user_config->phases + i_phase;
    for (i_stage = 0; i_stage < phase->qty_stages; i_stage++) {
      if (phase->stages[i_stage].granularity == CONFIG_GRANULARITY_UNDEFINED) { phase->stages[i_stage].granularity = CONFIG_GRANULARITY; }
    }
  }
}

void free_config(configuration *io_user_config) {
    size_t i;

    if (io_user_config != NULL) {
      for (i = 0; i < io_user_config->n_phases; i++) {
        free_config_phase(&(io_user_config->phases[i]));
      }

      for (i = 0; i < io_user_config->n_groups; i++) {
        free(io_user_config->groups[i].ss);
        free(io_user_config->groups[i].rs);
      }
      // Free derived types
      MPI_Type_free(&(io_user_config->config_type));
      io_user_config->config_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(io_user_config->group_type));
      io_user_config->group_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(io_user_config->group_strats_type));
      io_user_config->group_strats_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(io_user_config->phase_type));
      io_user_config->phase_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(io_user_config->stage_type));
      io_user_config->stage_type = MPI_DATATYPE_NULL;

      free(io_user_config->groups);
      free(io_user_config->phases);
      free(io_user_config);
    }
}

/**
 * @brief Free all stages of one phase.
 * @param[in,out] io_phase Phase whose stages are freed.
 */
void free_config_phase(phase_t *io_phase) {
  size_t i, found_ids;
  int *freed_ids = NULL;
  found_ids = 0;

  if (io_phase->qty_stages) {
    freed_ids = (int *)malloc(io_phase->qty_stages * sizeof(int));
    for (i = 0; i < io_phase->qty_stages; i++) {
      free_config_stage(&(io_phase->stages[i]), freed_ids, &found_ids);
    }
    free(freed_ids);
    free(io_phase->stages);
  }
  io_phase->qty_iters = 0;
  io_phase->qty_stages = 0;
}

/**
 * @brief Free buffers owned by one stage.
 *
 * Stages that share an @c id share @c reqs; @p io_freed_ids tracks which
 * request arrays were already freed to avoid double-free.
 *
 * @param[in,out] io_stage      Stage to free.
 * @param[in,out] io_freed_ids  Ids whose @c reqs were already released.
 * @param[in,out] io_found_ids  Number of valid entries in @p io_freed_ids.
 */
void free_config_stage(stage_t *io_stage, int *io_freed_ids, size_t *io_found_ids) {
  size_t i;
  int mpi_index, free_reqs;

  free_reqs = 1;
  if (io_stage->id > -1) {
    for (i = 0; i < *io_found_ids; i++) {
      if (io_stage->id == io_freed_ids[i]) {
        free_reqs = 0;
        break;
      }
    }
    if (free_reqs) {
      io_freed_ids[*io_found_ids] = io_stage->id;
      *io_found_ids = *io_found_ids + 1;
    }
  }

  if (io_stage->fd > -1) {
    close(io_stage->fd);
  }

  if (io_stage->array != NULL) {
    free(io_stage->array);
    io_stage->array = NULL;
  }
  if (io_stage->full_array != NULL) {
    free(io_stage->full_array);
    io_stage->full_array = NULL;
  }
  if (io_stage->double_array != NULL) {
    free(io_stage->double_array);
    io_stage->double_array = NULL;
  }
  if (io_stage->reqs != NULL && free_reqs) {
    for (mpi_index = 0; mpi_index < io_stage->req_count; mpi_index++) {
      if (io_stage->reqs[mpi_index] != MPI_REQUEST_NULL) {
        MPI_Request_free(&(io_stage->reqs[mpi_index]));
        io_stage->reqs[mpi_index] = MPI_REQUEST_NULL;
      }
    }
    free(io_stage->reqs);
    io_stage->reqs = NULL;
  }
  if (io_stage->counts.counts != NULL) {
    free_counts(&(io_stage->counts));
  }
}

void free_counts(struct Counts *io_counts) {
  if (io_counts == NULL) {
    return;
  }

  if (io_counts->counts != NULL) {
    free(io_counts->counts);
    io_counts->counts = NULL;
  }
  if (io_counts->displs != NULL) {
    free(io_counts->displs);
    io_counts->displs = NULL;
  }
}

void print_config(configuration *i_user_config) {
  if (i_user_config != NULL) {
    size_t i, j;
    printf("Config loaded: R=%zu, Phases=%zu, SDR=%zu, ADR=%zu, Rigid=%d, Capture_Method=%d\n",
        i_user_config->n_resizes, i_user_config->n_phases, i_user_config->sdr, i_user_config->adr, i_user_config->rigid_times, i_user_config->capture_method);

    for (i = 0; i < i_user_config->n_phases; i++) {
      print_config_phase(i_user_config->phases + i, i);
    }

    for (i = 0; i < i_user_config->n_groups; i++) {
      printf("Group %zu: Iters=%d, Procs=%d, Factors=%f, Dist=%d, RM=%d, SM=%d",
        i, i_user_config->groups[i].iters, i_user_config->groups[i].procs, i_user_config->groups[i].factor,
        i_user_config->groups[i].phy_dist, i_user_config->groups[i].rm, i_user_config->groups[i].sm);

      printf(", RS=%d", i_user_config->groups[i].rs[0]);
      for (j = 1; j < i_user_config->groups[i].rs_len; j++) {
        printf("/%d", i_user_config->groups[i].rs[j]);
      }
      printf(", SS=%d", i_user_config->groups[i].ss[0]);
      for (j = 1; j < i_user_config->groups[i].ss_len; j++) {
        printf("/%d", i_user_config->groups[i].ss[j]);
      }
      printf("\n");
    }
  }
}

void print_config_group(configuration *i_user_config, size_t i_grp) {
  size_t i;
  if (i_user_config != NULL) {
    int parents, sons;
    parents = sons = 0;
    if (i_grp > 0) {
      parents = i_user_config->groups[i_grp - 1].procs;
    }
    if (i_grp < i_user_config->n_groups - 1) {
      sons = i_user_config->groups[i_grp + 1].procs;
    }

    printf("Config: SDR=%zu, ADR=%zu, Rigid=%d, Capture_Method=%d\n",
        i_user_config->sdr, i_user_config->adr, i_user_config->rigid_times, i_user_config->capture_method);
    for (i = 0; i < i_user_config->n_phases; i++) {
      print_config_phase(i_user_config->phases + i, i);
    }

    printf("Group %zu: Iters=%d, Procs=%d, Factors=%f, Dist=%d, RM=%d, SM=%d", i_grp, i_user_config->groups[i_grp].iters, i_user_config->groups[i_grp].procs, i_user_config->groups[i_grp].factor,
      i_user_config->groups[i_grp].phy_dist, i_user_config->groups[i_grp].rm, i_user_config->groups[i_grp].sm);

    printf(", RS=%d", i_user_config->groups[i_grp].rs[0]);
    for (i = 1; i < i_user_config->groups[i_grp].rs_len; i++) {
      printf("/%d", i_user_config->groups[i_grp].rs[i]);
    }
    printf(", SS=%d", i_user_config->groups[i_grp].ss[0]);
    for (i = 1; i < i_user_config->groups[i_grp].ss_len; i++) {
      printf("/%d", i_user_config->groups[i_grp].ss[i]);
    }
    printf(", parents=%d, children=%d\n", parents, sons);
  }
}

/**
 * @brief Print one phase and its stages to stdout.
 * @param[in] i_phase Phase to print.
 * @param[in] i_index Phase index (for the label only).
 */
void print_config_phase(phase_t *i_phase, size_t i_index) {
  size_t i;
  stage_t *stage;

  printf("Phase %zu: Qty_Stages=%zu, Qty_Iters=%zu\n",
    i_index, i_phase->qty_stages, i_phase->qty_iters);
  for (i = 0; i < i_phase->qty_stages; i++) {
    stage = i_phase->stages + i;
    printf("\tStage %zu: PT=%d, T_stage=%lf, bytes=%d, Granularity=%d, Involved_Procs=%d, T_capped=%d\n",
      i, stage->pt, stage->t_stage, stage->bytes, stage->granularity, stage->involved_procs, stage->t_capped);
  }
}


//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//| INTRACOMM BROADCAST OF CONFIGURATION STRUCTURE                ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| |/

void send_config_file(configuration *i_config_file, int i_root, MPI_Comm i_comm) {
  size_t i;
  phase_t *phase;

  MPI_Bcast(i_config_file, 1, i_config_file->config_type, i_root, i_comm);
  MPI_Bcast(i_config_file->phases, i_config_file->n_phases, i_config_file->phase_type, i_root, i_comm);
  for (i = 0; i < i_config_file->n_phases; i++) {
    phase = i_config_file->phases + i;
    MPI_Bcast(phase->stages, phase->qty_stages, i_config_file->stage_type, i_root, i_comm);
  }
  MPI_Bcast(i_config_file->groups, i_config_file->n_groups, i_config_file->group_type, i_root, i_comm);
  MPI_Bcast(i_config_file->groups, 1, i_config_file->group_strats_type, i_root, i_comm);
}

void recv_config_file(int i_root, MPI_Comm i_comm, configuration **o_config_file_out) {
  size_t i;
  phase_t *phase;
  configuration *config_file = malloc(sizeof(configuration));

  config_file->config_type = MPI_DATATYPE_NULL;
  config_file->group_type = MPI_DATATYPE_NULL;
  config_file->group_strats_type = MPI_DATATYPE_NULL;
  config_file->phase_type = MPI_DATATYPE_NULL;
  config_file->stage_type = MPI_DATATYPE_NULL;
  def_struct_config_file(config_file);

  MPI_Bcast(config_file, 1, config_file->config_type, i_root, i_comm);

  // Initialise nested structures
  config_file->n_resizes = config_file->n_groups - 1;
  malloc_config_phases(config_file); // Allocate phases (stages still NULL)
  malloc_config_resizes(config_file); // Allocate and default groups

  MPI_Bcast(config_file->phases, config_file->n_phases, config_file->phase_type, i_root, i_comm);
  for (i = 0; i < config_file->n_phases; i++) {
    phase = config_file->phases + i;
    malloc_config_stages(config_file, i);
    MPI_Bcast(phase->stages, phase->qty_stages, config_file->stage_type, i_root, i_comm);
  }

  MPI_Bcast(config_file->groups, config_file->n_groups, config_file->group_type, i_root, i_comm);
  for (i = 0; i < config_file->n_groups; i++) {
    config_file->groups[i].ss = (int *)malloc(config_file->groups[i].ss_len * sizeof(int));
    config_file->groups[i].rs = (int *)malloc(config_file->groups[i].rs_len * sizeof(int));
  }
  def_struct_groups_strategies(config_file); // Build strategy derived type
  MPI_Bcast(config_file->groups, 1, config_file->group_strats_type, i_root, i_comm);

  *o_config_file_out = config_file;
}

/**
 * @brief Commit an MPI derived type for seven scalar configuration fields.
 *
 * Packs @c n_groups, @c n_phases, @c sdr, @c adr, @c datasize, @c rigid_times,
 * and @c capture_method into @c config_type.
 *
 * @param[in,out] io_config_file Configuration that receives @c config_type.
 */
void def_struct_config_file(configuration *io_config_file) {
  int i, counts = 7;
  int blocklengths[7] = {1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Fill types vector
  types[0] = types[1] = types[2] = types[3] = types[4] = type_size_t;
  types[5] = types[6] = MPI_INT;

  // Fill displs vector
  MPI_Get_address(io_config_file, &dir);

  MPI_Get_address(&(io_config_file->n_groups), &displs[0]);
  MPI_Get_address(&(io_config_file->n_phases), &displs[1]);
  MPI_Get_address(&(io_config_file->sdr), &displs[2]);
  MPI_Get_address(&(io_config_file->adr), &displs[3]);
  MPI_Get_address(&(io_config_file->datasize), &displs[4]);
  MPI_Get_address(&(io_config_file->rigid_times), &displs[5]);
  MPI_Get_address(&(io_config_file->capture_method), &displs[6]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, &(io_config_file->config_type));
  MPI_Type_commit(&(io_config_file->config_type));
}

/**
 * @brief Commit an MPI derived type for per-group scalar fields.
 * @param[in,out] io_config_file Configuration that receives @c group_type.
 */
void def_struct_groups(configuration *io_config_file) {
  int i, counts = 8;
  int blocklengths[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t, aux;
  group_config_t *groups = io_config_file->groups;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Fill types vector
  types[0] = types[1] = types[2] = types[4] = types[5] = MPI_INT;
  types[3] = types[6] = type_size_t;
  types[7] = MPI_FLOAT;

  // Fill displs vector
  MPI_Get_address(groups, &dir);

  MPI_Get_address(&(groups->iters), &displs[0]);
  MPI_Get_address(&(groups->procs), &displs[1]);
  MPI_Get_address(&(groups->sm), &displs[2]);
  MPI_Get_address(&(groups->ss_len), &displs[3]);
  MPI_Get_address(&(groups->phy_dist), &displs[4]);
  MPI_Get_address(&(groups->rm), &displs[5]);
  MPI_Get_address(&(groups->rs_len), &displs[6]);
  MPI_Get_address(&(groups->factor), &displs[7]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  if (io_config_file->n_groups == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, &(io_config_file->group_type));
    MPI_Type_commit(&(io_config_file->group_type));
  } else { // More than one group: resize extent for an array of structs
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    MPI_Type_create_resized(aux, 0, sizeof(group_config_t), &(io_config_file->group_type));
    MPI_Type_commit(&(io_config_file->group_type));
    MPI_Type_free(&aux);
  }
}

/**
 * @brief Commit an MPI derived type covering all groups' @c ss / @c rs arrays.
 * @param[in,out] io_config_file Configuration that receives @c group_strats_type.
 */
void def_struct_groups_strategies(configuration *io_config_file) {
  int i, counts = io_config_file->n_groups * 2;
  int *blocklengths;
  MPI_Aint *displs, dir;
  MPI_Datatype *types;
  group_config_t *group;

  blocklengths = (int *)malloc(counts * sizeof(int));
  displs = (MPI_Aint *)malloc(counts * sizeof(MPI_Aint));
  types = (MPI_Datatype *)malloc(counts * sizeof(MPI_Datatype));

  MPI_Get_address(io_config_file->groups, &dir);
  for (i = 0; i < counts; i += 2) {
    group = &(io_config_file->groups[i / 2]);

    MPI_Get_address(group->ss, &displs[i]);
    MPI_Get_address(group->rs, &displs[i + 1]);
    displs[i] -= dir;
    displs[i + 1] -= dir;
    types[i] = types[i + 1] = MPI_INT;
    blocklengths[i] = group->ss_len;
    blocklengths[i + 1] = group->rs_len;
  }

  MPI_Type_create_struct(counts, blocklengths, displs, types, &io_config_file->group_strats_type);
  MPI_Type_commit(&io_config_file->group_strats_type);

  free(blocklengths);
  free(displs);
  free(types);
}

/**
 * @brief Commit an MPI derived type for phase scalars (@c qty_stages, @c qty_iters).
 * @param[in,out] io_config_file Configuration that receives @c phase_type.
 */
void def_struct_phase(configuration *io_config_file) {
  int i, counts = 2;
  int blocklengths[2] = {1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t, aux;
  phase_t *phases = io_config_file->phases;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Fill types vector
  types[0] = types[1] = types[2] = type_size_t;

  // Fill displs vector
  MPI_Get_address(phases, &dir);

  MPI_Get_address(&(phases->qty_stages), &displs[0]);
  MPI_Get_address(&(phases->qty_iters), &displs[1]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  if (io_config_file->n_phases == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, &(io_config_file->phase_type));
    MPI_Type_commit(&(io_config_file->phase_type));
  } else { // More than one phase: resize extent for an array of structs
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    MPI_Type_create_resized(aux, 0, sizeof(phase_t), &(io_config_file->phase_type));
    MPI_Type_commit(&(io_config_file->phase_type));
    MPI_Type_free(&aux);
  }
}

/**
 * @brief Commit an MPI derived type for stage scalar fields used in broadcast.
 * @param[in,out] io_config_file Configuration that receives @c stage_type.
 * @param[in]     i_phase        Phase whose first stage defines the layout.
 */
void def_struct_stage(configuration *io_config_file, phase_t *i_phase) {
  int i, counts = 8;
  int blocklengths[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype aux, types[counts];
  stage_t *stages = i_phase->stages;

  // Fill types vector
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = MPI_INT;
  types[6] = types[7] = MPI_DOUBLE;

  // Fill displs vector
  MPI_Get_address(stages, &dir);

  MPI_Get_address(&(stages->pt), &displs[0]);
  MPI_Get_address(&(stages->id), &displs[1]);
  MPI_Get_address(&(stages->bytes), &displs[2]);
  MPI_Get_address(&(stages->involved_procs), &displs[3]);
  MPI_Get_address(&(stages->t_capped), &displs[4]);
  MPI_Get_address(&(stages->granularity), &displs[5]);
  MPI_Get_address(&(stages->t_stage), &displs[6]);
  MPI_Get_address(&(stages->t_op), &displs[7]);

  for (i = 0; i < counts; i++) displs[i] -= dir;

  // More than one stage possible: resize extent for an array of structs
  MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
  MPI_Type_create_resized(aux, 0, sizeof(stage_t), &(io_config_file->stage_type));
  MPI_Type_commit(&(io_config_file->stage_type));
  MPI_Type_free(&aux);
}
