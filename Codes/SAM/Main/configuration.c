#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include "read_ini.h"
#include "read_json.h"
#include "configuration.h"

#define CONFIG_GRANULARITY 600
#define CONFIG_GRANULARITY_UNDEFINED -1

void malloc_config_resizes(configuration *user_config);
void malloc_config_phases(configuration *user_config);
void malloc_config_stages(configuration *user_config, size_t phase_ind);
void check_granularity(configuration *user_config);

void free_config_phase(phase_t *phase);
void free_config_stage(stage_t *stage, int *freed_ids, size_t *found_ids);

void print_config_phase(phase_t *phase, size_t index);

void def_struct_config_file(configuration *config_file);
void def_struct_groups(configuration *config_file);
void def_struct_groups_strategies(configuration *config_file);
void def_struct_phase(configuration *config_file);
void def_struct_stage(configuration *config_file, phase_t *phase);

/*
 * Inicializa una estructura de configuracion
 *
 * Si el parametro "file_name" no es nulo,
 * se obtiene la configuracion a partir de
 * un fichero .ini o .json
 *
 * En caso de que sea nulo, es el usuario
 * el que tiene que elegir los valores a
 * utilizar.
 */
void init_config(char *file_name, configuration **user_config) {
  if(file_name != NULL) {
    ext_functions_t mallocs;
    const char *extension;

    mallocs.resizes_f = malloc_config_resizes;
    mallocs.phases_f = malloc_config_phases;
    mallocs.stages_f = malloc_config_stages;

    extension = strrchr(file_name, '.');
    if(extension != NULL && strcmp(extension, ".json") == 0) {
      *user_config = read_json_file(file_name, mallocs);
    } else if(extension != NULL && strcmp(extension, ".ini") == 0) {
      *user_config = read_ini_file(file_name, mallocs);
    } else {
      fprintf(stderr, "Unsupported config extension for '%s' (use .ini or .json)\n", file_name);
      MPI_Abort(MPI_COMM_WORLD, -4);
      return;
    }

    if(*user_config == NULL) {
      MPI_Abort(MPI_COMM_WORLD, -4);
      return;
    }
  } else {
    configuration *config = NULL;

    config = malloc(sizeof(configuration));
    if(config == NULL) {
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
    *user_config = config;
  }
  check_granularity(*user_config);
  def_struct_config_file(*user_config);
  def_struct_groups_strategies(*user_config);
}

/*
 * Reserva de memoria para los vectores de la estructura de configuracion
 *
 * Si se llama desde fuera de este fichero, la memoria de la estructura
 * tiene que reservarse con la siguiente linea:
 * "configuration *config = malloc(sizeof(configuration));"
 *
 * Sin embargo se puede obtener a traves de las funciones
 *  - init_config
 *  - recv_config_file
 */
void malloc_config_resizes(configuration *user_config) {
  size_t i;
  if(user_config != NULL) {
    user_config->groups = malloc(sizeof(group_config_t) * user_config->n_groups);
    for(i=0; i<user_config->n_groups; i++) {
      user_config->groups[i].iters = 0;
      user_config->groups[i].procs = 1;
      user_config->groups[i].sm = 0;
      user_config->groups[i].ss = NULL;
      user_config->groups[i].ss_len = 0;
      user_config->groups[i].phy_dist = 0;
      user_config->groups[i].rm = 0;
      user_config->groups[i].rs = NULL;
      user_config->groups[i].rs_len = 0;
      user_config->groups[i].factor = 1;
    }
    def_struct_groups(user_config);
  }
}

/*
 * Inicializa la memoria para las etapas.
 * No se reserva memoria, pero si se pone a NULL
 * para poder liberar correctamente cada fase.
 *
 * Se puede obtener a traves de las funciones
 *  - init_config
 *  - recv_config_file
 */
void malloc_config_phases(configuration *user_config) {
  size_t i;
  if(user_config != NULL) {
    user_config->phases = malloc(user_config->n_phases * sizeof *(user_config->phases));
    for(i=0; i<user_config->n_phases; i++) {
      user_config->phases[i].stages = NULL;
      user_config->phases[i].qty_stages = 0;
      user_config->phases[i].qty_iters = 0;
    }
    def_struct_phase(user_config);
  }
}

/*
 * Inicializa la memoria para las fases de iteraciones.
 * No se reserva memoria, pero si se pone a NULL
 * para poder liberar correctamente cada fase.
 *
 * Se puede obtener a traves de las funciones
 *  - init_config
 *  - recv_config_file
 */
void malloc_config_stages(configuration *user_config, size_t phase_ind) {
  size_t i;
  phase_t *phase;

  if(user_config != NULL && phase_ind < user_config->n_phases) {
    phase = user_config->phases + phase_ind;
    phase->stages = malloc(sizeof(stage_t) * phase->qty_stages);
    for(i=0; i<phase->qty_stages; i++) {
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
    if (user_config->stage_type == MPI_DATATYPE_NULL) { def_struct_stage(user_config, phase); }
  }
}

/*
 * Reserva memoria para los vectores de counts/displs de las funciones
 * MPI. Todos los vectores tienen un tamaño de numP.
 *
 * El vector counts indica cuantos elementos se comunican desde este proceso
 * al proceso "i".
 *
 * El vector displs indica los desplazamientos necesarios para cada comunicacion
 * con el proceso "i".
 *
 */
void malloc_counts(struct Counts *counts, size_t numP) {
  counts->counts = calloc(numP, sizeof(int)); 
  if(counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

  counts->displs = calloc(numP, sizeof(int));
  if(counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}
  counts->len = numP;
}


/*
 * Granularity for each stage can be the one selected read in the config file
 * or the default one in this field
 */
void check_granularity(configuration *user_config) {
  size_t i_phase, i_stage;
  phase_t *phase;

  if(user_config == NULL) {
    return;
  }

  for(i_phase=0; i_phase<user_config->n_phases; i_phase++) {
    phase = user_config->phases + i_phase;
    for(i_stage=0; i_stage<phase->qty_stages; i_stage++) {
      if(phase->stages[i_stage].granularity == CONFIG_GRANULARITY_UNDEFINED) { phase->stages[i_stage].granularity = CONFIG_GRANULARITY; }
    }
  }
}


/*
 * Libera toda la memoria de una estructura de configuracion
 */
void free_config(configuration *user_config) {
    size_t i;

    if(user_config != NULL) {
      for(i=0; i < user_config->n_phases; i++) {
        free_config_phase(&(user_config->phases[i]));
      }

      for(i=0; i < user_config->n_groups; i++) {
        free(user_config->groups[i].ss);
        free(user_config->groups[i].rs);
      }
      //Liberar tipos derivados
      MPI_Type_free(&(user_config->config_type));
      user_config->config_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(user_config->group_type));
      user_config->group_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(user_config->group_strats_type));
      user_config->group_strats_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(user_config->phase_type));
      user_config->phase_type = MPI_DATATYPE_NULL;

      MPI_Type_free(&(user_config->stage_type));
      user_config->stage_type = MPI_DATATYPE_NULL;
      
      free(user_config->groups);
      free(user_config->phases);
      free(user_config);
    }
}

/*
 * Libera la memoria de una etapa
 */
void free_config_phase(phase_t *phase) {
  size_t i, found_ids;
  int *freed_ids = NULL;
  found_ids = 0;

  if(phase->qty_stages) {
    freed_ids = (int *) malloc(phase->qty_stages * sizeof(int));
    for(i=0; i < phase->qty_stages; i++) {
      free_config_stage(&(phase->stages[i]), freed_ids, &found_ids);
    }
    free(freed_ids);
    free(phase->stages);
  }
  phase->qty_iters = 0;
  phase->qty_stages = 0;
}

/*
 * Libera toda la memoria de una stage
 */
void free_config_stage(stage_t *stage, int *freed_ids, size_t *found_ids) {
  size_t i;
  int mpi_index, free_reqs;

  free_reqs = 1;
  if(stage->id > -1) {
    for(i=0; i<*found_ids; i++) {
      if(stage->id == freed_ids[i]) {
  	free_reqs = 0;
        break;
      }
    }
    if(free_reqs) {
      freed_ids[*found_ids] = stage->id;
      *found_ids=*found_ids + 1;
    }
  }

  if(stage->fd > -1) {
    close(stage->fd);
  }
	
  if(stage->array != NULL) {
    free(stage->array);
    stage->array = NULL;
  }
  if(stage->full_array != NULL) {
    free(stage->full_array);
    stage->full_array = NULL;
  }
  if(stage->double_array != NULL) {
    free(stage->double_array);
    stage->double_array = NULL;
  }
  if(stage->reqs != NULL && free_reqs) {
    for(mpi_index=0; mpi_index<stage->req_count; mpi_index++) {
      if(stage->reqs[mpi_index] != MPI_REQUEST_NULL) {
        MPI_Request_free(&(stage->reqs[mpi_index]));
	      stage->reqs[mpi_index] = MPI_REQUEST_NULL;
      }
    }
    free(stage->reqs);
    stage->reqs = NULL;
  }
  if(stage->counts.counts != NULL) {
    free_counts(&(stage->counts));
  }
}

/*
 * Libera la memoria interna de una estructura Counts.
 *
 * No libera la memoria de la estructura counts si se ha alojado
 * de forma dinamica.
 */
void free_counts(struct Counts *counts) {
  if(counts == NULL) {
    return;
  }

  if(counts->counts != NULL) {
    free(counts->counts);
    counts->counts = NULL;
  }
  if(counts->displs != NULL) {
    free(counts->displs);
    counts->displs = NULL;
  }
}



/*
 * Imprime por salida estandar toda la informacion que contiene
 * la configuracion pasada como argumento
 */
void print_config(configuration *user_config) {
  if(user_config != NULL) {
    size_t i, j;
    printf("Config loaded: R=%zu, Phases=%zu, SDR=%zu, ADR=%zu, Rigid=%d, Capture_Method=%d\n",
        user_config->n_resizes, user_config->n_phases, user_config->sdr, user_config->adr, user_config->rigid_times, user_config->capture_method);
    
    for(i=0; i<user_config->n_phases; i++) {
      print_config_phase(user_config->phases+i, i);
    }

    for(i=0; i<user_config->n_groups; i++) {
      printf("Group %zu: Iters=%d, Procs=%d, Factors=%f, Dist=%d, RM=%d, SM=%d",
        i, user_config->groups[i].iters, user_config->groups[i].procs, user_config->groups[i].factor, 
	user_config->groups[i].phy_dist, user_config->groups[i].rm, user_config->groups[i].sm);

      printf(", RS=%d", user_config->groups[i].rs[0]);
      for(j=1; j<user_config->groups[i].rs_len; j++) {
        printf("/%d", user_config->groups[i].rs[j]);
      }
      printf(", SS=%d", user_config->groups[i].ss[0]);
      for(j=1; j<user_config->groups[i].ss_len; j++) {
        printf("/%d", user_config->groups[i].ss[j]);
      }
      printf("\n");
    }
  }
}


/*
 * Imprime por salida estandar la informacion relacionada con un
 * solo grupo de procesos en su configuracion.
 */
void print_config_group(configuration *user_config, size_t grp) {
  size_t i;
  if(user_config != NULL) {
    int parents, sons;
    parents = sons = 0;
    if(grp > 0) {
      parents = user_config->groups[grp-1].procs;
    }
    if(grp < user_config->n_groups - 1) {
      sons = user_config->groups[grp+1].procs;
    }

    printf("Config: SDR=%zu, ADR=%zu, Rigid=%d, Capture_Method=%d\n",
        user_config->sdr, user_config->adr, user_config->rigid_times, user_config->capture_method);
    for(i=0; i<user_config->n_phases; i++) {
      print_config_phase(user_config->phases+i, i);
    }

    printf("Group %zu: Iters=%d, Procs=%d, Factors=%f, Dist=%d, RM=%d, SM=%d", grp, user_config->groups[grp].iters, user_config->groups[grp].procs, user_config->groups[grp].factor,
      user_config->groups[grp].phy_dist, user_config->groups[grp].rm, user_config->groups[grp].sm);

    printf(", RS=%d", user_config->groups[grp].rs[0]);
    for(i=1; i<user_config->groups[grp].rs_len; i++) {
      printf("/%d", user_config->groups[grp].rs[i]);
    }
    printf(", SS=%d", user_config->groups[grp].ss[0]);
    for(i=1; i<user_config->groups[grp].ss_len; i++) {
      printf("/%d", user_config->groups[grp].ss[i]);
    }
    printf(", parents=%d, children=%d\n", parents, sons);
  }
}

/*
 * Imprime por salida estandar la configuracion de una etapa
 * con sus respectivas fases.
 */
void print_config_phase(phase_t *phase, size_t index) {
  size_t i;
  stage_t *stage;

  printf("Phase %zu: Qty_Stages=%zu, Qty_Iters=%zu\n", 
    index, phase->qty_stages, phase->qty_iters);
  for(i=0; i<phase->qty_stages; i++) {
    stage = phase->stages+i;
    printf("\tStage %zu: PT=%d, T_stage=%lf, bytes=%d, Granularity=%d, Involved_Procs=%d, T_capped=%d\n",
      i, stage->pt, stage->t_stage, stage->bytes, stage->granularity, stage->involved_procs, stage->t_capped);
  }
}


//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//| FUNCIONES DE INTERCOMUNICACION DE ESTRUCTURA DE CONFIGURACION ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| ||
//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||| |/

/*
 * Envia una estructura de configuracion al grupo de procesos al que se 
 * enlaza este grupo a traves del intercomunicador pasado como argumento.
 *
 * Esta funcion tiene que ser llamada por todos los procesos del mismo grupo
 * e indicar cual es el proceso raiz que se encargara de enviar la
 * configuracion al otro grupo.
 */
void send_config_file(configuration *config_file, int root, MPI_Comm intercomm) {
  size_t i;
  phase_t *phase;

  MPI_Bcast(config_file, 1, config_file->config_type, root, intercomm);
  MPI_Bcast(config_file->phases, config_file->n_phases, config_file->phase_type, root, intercomm);
  for (i=0; i < config_file->n_phases; i++) {
    phase = config_file->phases+i;
    MPI_Bcast(phase->stages, phase->qty_stages, config_file->stage_type, root, intercomm);
  }
  MPI_Bcast(config_file->groups, config_file->n_groups, config_file->group_type, root, intercomm);
  MPI_Bcast(config_file->groups, 1, config_file->group_strats_type, root, intercomm);
}



/*
 * Recibe una estructura de configuracion desde otro grupo de procesos
 * y la devuelve. La memoria de la estructura se reserva en esta funcion.
 *
 * Esta funcion tiene que ser llamada por todos los procesos del mismo grupo
 * e indicar cual es el proceso raiz del otro grupo que se encarga de enviar
 * la configuracion a este grupo.
 *
 * La memoria de la configuracion devuelta tiene que ser liberada con
 * la funcion "free_config".
 */
void recv_config_file(int root, MPI_Comm intercomm, configuration **config_file_out) {
  size_t i;
  phase_t *phase;
  configuration *config_file = malloc(sizeof(configuration));

  config_file->config_type = MPI_DATATYPE_NULL;
  config_file->group_type = MPI_DATATYPE_NULL;
  config_file->group_strats_type = MPI_DATATYPE_NULL;
  config_file->phase_type = MPI_DATATYPE_NULL;
  config_file->stage_type = MPI_DATATYPE_NULL;
  def_struct_config_file(config_file);

  MPI_Bcast(config_file, 1, config_file->config_type, root, intercomm);

  //Inicializado de estructuras internas
  config_file->n_resizes = config_file->n_groups-1;
  malloc_config_phases(config_file); // Inicializar a NULL vectores phase
  malloc_config_resizes(config_file); // Inicializar valores de grupos

  MPI_Bcast(config_file->phases, config_file->n_phases, config_file->phase_type, root, intercomm);
  for (i=0; i < config_file->n_phases; i++) {
    phase = config_file->phases+i;
    malloc_config_stages(config_file, i);
    MPI_Bcast(phase->stages, phase->qty_stages, config_file->stage_type, root, intercomm);
  }

  MPI_Bcast(config_file->groups, config_file->n_groups, config_file->group_type, root, intercomm);
  for(i=0; i<config_file->n_groups; i++) {
    config_file->groups[i].ss = (int *) malloc(config_file->groups[i].ss_len * sizeof(int));
    config_file->groups[i].rs = (int *) malloc(config_file->groups[i].rs_len * sizeof(int));
  }
  def_struct_groups_strategies(config_file); // Inicializar vectores de grupos
  MPI_Bcast(config_file->groups, 1, config_file->group_strats_type, root, intercomm);

  *config_file_out = config_file;
}

/*
 * Tipo derivado para enviar 6 elementos especificos
 * de la estructura de configuracion con una sola comunicacion.
 */
void def_struct_config_file(configuration *config_file) {
  int i, counts = 7;
  int blocklengths[7] = {1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = type_size_t;
  types[5] = types[6] = MPI_INT;

  // Rellenar vector displs
  MPI_Get_address(config_file, &dir);

  MPI_Get_address(&(config_file->n_groups), &displs[0]);
  MPI_Get_address(&(config_file->n_phases), &displs[1]);
  MPI_Get_address(&(config_file->sdr), &displs[2]);
  MPI_Get_address(&(config_file->adr), &displs[3]);
  MPI_Get_address(&(config_file->datasize), &displs[4]);
  MPI_Get_address(&(config_file->rigid_times), &displs[5]);
  MPI_Get_address(&(config_file->capture_method), &displs[6]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, &(config_file->config_type));
  MPI_Type_commit(&(config_file->config_type));
}

/*
 * Tipo derivado para enviar elementos especificos
 * de la estructuras de la configuracion de cada grupo 
 * en una sola comunicacion.
 */
void def_struct_groups(configuration *config_file) {
  int i, counts = 8;
  int blocklengths[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t, aux;
  group_config_t *groups = config_file->groups;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[4] = types[5] = MPI_INT;
  types[3] = types[6] = type_size_t;
  types[7] = MPI_FLOAT;

  // Rellenar vector displs
  MPI_Get_address(groups, &dir);

  MPI_Get_address(&(groups->iters), &displs[0]);
  MPI_Get_address(&(groups->procs), &displs[1]);
  MPI_Get_address(&(groups->sm), &displs[2]);
  MPI_Get_address(&(groups->ss_len), &displs[3]);
  MPI_Get_address(&(groups->phy_dist), &displs[4]);
  MPI_Get_address(&(groups->rm), &displs[5]);
  MPI_Get_address(&(groups->rs_len), &displs[6]);
  MPI_Get_address(&(groups->factor), &displs[7]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  if (config_file->n_groups == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, &(config_file->group_type));
    MPI_Type_commit(&(config_file->group_type));
  } else { // Si hay mas de una fase(estructura), el "extent" se modifica.
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    // Tipo derivado para enviar N elementos de la estructura
    MPI_Type_create_resized(aux, 0, sizeof(group_config_t), &(config_file->group_type));
    MPI_Type_commit(&(config_file->group_type));
    MPI_Type_free(&aux);
  }
}

/*
 * Tipo derivado para enviar las estrategias
 * de cada grupo con una sola comunicacion.
 */
void def_struct_groups_strategies(configuration *config_file) {
  int i, counts = config_file->n_groups*2;
  int *blocklengths;
  MPI_Aint *displs, dir;
  MPI_Datatype *types;
  group_config_t *group;

  blocklengths = (int *) malloc(counts * sizeof(int));
  displs = (MPI_Aint *) malloc(counts * sizeof(MPI_Aint));
  types = (MPI_Datatype *) malloc(counts * sizeof(MPI_Datatype));

  MPI_Get_address(config_file->groups, &dir);
  for(i = 0; i < counts; i+=2) {
    group = &(config_file->groups[i/2]);

    MPI_Get_address(group->ss, &displs[i]);
    MPI_Get_address(group->rs, &displs[i+1]);
    displs[i] -= dir;
    displs[i+1] -= dir;
    types[i] = types[i+1] = MPI_INT;
    blocklengths[i] = group->ss_len;
    blocklengths[i+1] = group->rs_len;
  }

  MPI_Type_create_struct(counts, blocklengths, displs, types, &config_file->group_strats_type);
  MPI_Type_commit(&config_file->group_strats_type);

  free(blocklengths);
  free(displs);
  free(types);
}

/*
 * Tipo derivado para enviar elementos especificos
 * de la estructuras de fases de iteracion en una sola comunicacion.
 */
void def_struct_phase(configuration *config_file) {
  int i, counts = 2;
  int blocklengths[2] = {1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts], type_size_t, aux;
  phase_t *phases = config_file->phases;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  // Rellenar vector types
  types[0] = types[1] = types[2] = type_size_t;

  // Rellenar vector displs
  MPI_Get_address(phases, &dir);

  MPI_Get_address(&(phases->qty_stages), &displs[0]);
  MPI_Get_address(&(phases->qty_iters), &displs[1]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  if (config_file->n_phases == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, &(config_file->phase_type));
    MPI_Type_commit(&(config_file->phase_type));
  } else { // Si hay mas de una etapa(estructura), el "extent" se modifica.
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    // Tipo derivado para enviar N elementos de la estructura
    MPI_Type_create_resized(aux, 0, sizeof(phase_t), &(config_file->phase_type)); 
    MPI_Type_commit(&(config_file->phase_type));
    MPI_Type_free(&aux);
  }
}

/*
 * Tipo derivado para enviar elementos especificos
 * de la estructuras de fases de iteracion en una sola comunicacion.
 */
void def_struct_stage(configuration *config_file, phase_t *phase) {
  int i, counts = 8;
  int blocklengths[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype aux, types[counts];
  stage_t *stages = phase->stages;

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = MPI_INT;
  types[6] = types[7] = MPI_DOUBLE;

  // Rellenar vector displs
  MPI_Get_address(stages, &dir);

  MPI_Get_address(&(stages->pt), &displs[0]);
  MPI_Get_address(&(stages->id), &displs[1]);
  MPI_Get_address(&(stages->bytes), &displs[2]);
  MPI_Get_address(&(stages->involved_procs), &displs[3]);
  MPI_Get_address(&(stages->t_capped), &displs[4]);
  MPI_Get_address(&(stages->granularity), &displs[5]);
  MPI_Get_address(&(stages->t_stage), &displs[6]);
  MPI_Get_address(&(stages->t_op), &displs[7]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  // Como puede haber mas de una fase(estructura), el "extent" se modifica.
  MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
  // Tipo derivado para enviar N elementos de la estructura
  MPI_Type_create_resized(aux, 0, sizeof(stage_t), &(config_file->stage_type)); 
  MPI_Type_commit(&(config_file->stage_type));
  MPI_Type_free(&aux);
}
