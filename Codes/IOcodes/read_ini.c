#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "read_ini.h"
#include "../malleability/spawn_methods/ProcessDist.h"
#include "../malleability/distribution_methods/block_distribution.h"
#include "ini.h"


void malloc_config_resizes(configuration *user_config, int resizes);
void init_config_stages(configuration *user_config);
void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type);
void def_struct_config_file_array(configuration *config_file, MPI_Datatype *config_type);
void def_struct_iter_stage(iter_stage_t *stages, int n_stages, MPI_Datatype *config_type);

/*
 * Funcion utilizada para leer el fichero de configuracion
 * y guardarlo en una estructura para utilizarlo en el futuro.
 *
 * Primero lee la seccion "general" y a continuacion cada una
 * de las secciones "resize%d".
 */
static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    configuration* pconfig = (configuration*)user;

    char *resize_name = malloc(10 * sizeof(char));
    snprintf(resize_name, 10, "resize%d", pconfig->actual_resize);

    char *stage_name = malloc(10 * sizeof(char));
    snprintf(stage_name, 10, "stage%d", pconfig->actual_stage);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("general", "R")) {
        pconfig->n_resizes = atoi(value) + 1;
        malloc_config_resizes(pconfig, pconfig->n_resizes);
    } else if (MATCH("general", "S")) {
        pconfig->n_stages = atoi(value);
        pconfig->stages = malloc(sizeof(iter_stage_t) * pconfig->n_stages);
        init_config_stages(pconfig);
    } else if (MATCH("general", "Granularity")) {
        pconfig->granularity = atoi(value);
    } else if (MATCH("general", "SDR")) {
        pconfig->sdr = atoi(value);
    } else if (MATCH("general", "ADR")) {
        pconfig->adr = atoi(value);
    } else if (MATCH("general", "AT")) {
        pconfig->at = atoi(value);
    } else if (MATCH("general", "SM")) {
        pconfig->sm = atoi(value);
    } else if (MATCH("general", "SS")) {
        pconfig->ss = atoi(value);

    // Iter stage
    } else if (MATCH(stage_name, "PT")) {
	if(pconfig->actual_stage < pconfig->n_stages)
          pconfig->stages[pconfig->actual_stage].pt = atoi(value);
    } else if (MATCH(stage_name, "bytes")) {
	if(pconfig->actual_stage < pconfig->n_stages)
          pconfig->stages[pconfig->actual_stage].bytes = atoi(value);
    } else if (MATCH(stage_name, "t_stage")) {
	if(pconfig->actual_stage < pconfig->n_stages) {
          pconfig->stages[pconfig->actual_stage].t_stage = atof(value);
          pconfig->actual_stage = pconfig->actual_stage+1; // Ultimo elemento del grupo
	}

    // Resize stage
    } else if (MATCH(resize_name, "Iters")) {
	if(pconfig->actual_resize < pconfig->n_resizes)
          pconfig->iters[pconfig->actual_resize] = atoi(value);
    } else if (MATCH(resize_name, "Procs")) {
	if(pconfig->actual_resize < pconfig->n_resizes)
          pconfig->procs[pconfig->actual_resize] = atoi(value);
    } else if (MATCH(resize_name, "FactorS")) {
	if(pconfig->actual_resize < pconfig->n_resizes)
          pconfig->factors[pconfig->actual_resize] = atof(value);
    } else if (MATCH(resize_name, "Dist")) {
	if(pconfig->actual_resize < pconfig->n_resizes) {
  	  char *aux = strdup(value);
          if (strcmp(aux, "spread") == 0) {
            pconfig->phy_dist[pconfig->actual_resize] = MALL_DIST_SPREAD;
  	  } else {
            pconfig->phy_dist[pconfig->actual_resize] = MALL_DIST_COMPACT;
	  }
	  free(aux);
          pconfig->actual_resize = pconfig->actual_resize+1; // Ultimo elemento del grupo
	}

    } else {
        return 0;  /* unknown section or name, error */
    }
 
    free(resize_name);
    free(stage_name);
    return 1;
}

/*
 * Crea y devuelve una estructura de configuracion a traves
 * de un nombre de fichero dado.
 *
 * La memoria de la estructura se reserva en la funcion y es conveniente
 * liberarla con la funcion "free_config()"
 */
configuration *read_ini_file(char *file_name) {
    configuration *config = NULL;

    config = malloc(sizeof(configuration) * 1);
    if(config == NULL) {
        printf("Error when reserving configuration structure\n");
	return NULL;
    }
    config->actual_resize=0;
    config->actual_stage=0;

    if(ini_parse(file_name, handler, config) < 0) { // Obtener configuracion
        printf("Can't load '%s'\n", file_name);
        return NULL;
    }
    return config;
}

/*
 * Reserva de memoria para los vectores de la estructura de configuracion
 *
 * Si se llama desde fuera de este fichero, la memoria de la estructura
 * tiene que reservarse con la siguiente linea:
 * "configuration *config = malloc(sizeof(configuration));"
 *
 * Sin embargo se puede obtener a traves de las funciones
 *  - read_ini_file
 *  - recv_config_file
 */
void malloc_config_resizes(configuration *user_config, int resizes) {
    if(user_config != NULL) {
      user_config->iters = malloc(sizeof(int) * resizes);
      user_config->procs = malloc(sizeof(int) * resizes);
      user_config->factors = malloc(sizeof(float) * resizes);
      user_config->phy_dist = malloc(sizeof(int) * resizes);
    }
}

/*
 * Inicializa la memoria para las fases de iteraciones.
 * No se reserva memoria, pero si se pone a NULL
 * para poder liberar correctamente cada fase.
 *
 * Se puede obtener a traves de las funciones
 *  - read_ini_file
 *  - recv_config_file
 */
void init_config_stages(configuration *user_config) {
    int i;
    if(user_config != NULL) {
       for(i=0; i<user_config->n_stages; i++) {
        user_config->stages[i].array = NULL;
        user_config->stages[i].full_array = NULL;
        user_config->stages[i].double_array = NULL;
        user_config->stages[i].counts.counts = NULL;
        user_config->stages[i].real_bytes = 0;
        user_config->stages[i].intercept = 0;
        user_config->stages[i].slope = 0;
      }
    }
}

/*
 * Libera toda la memoria de una estructura de configuracion
 */
void free_config(configuration *user_config) {
    int i;
    if(user_config != NULL) {
      free(user_config->iters);
      free(user_config->procs);
      free(user_config->factors);
      free(user_config->phy_dist);
      
      for(i=0; i < user_config->n_stages; i++) {
	
        if(user_config->stages[i].array != NULL) {
          free(user_config->stages[i].array);
          user_config->stages[i].array = NULL;
	}
        if(user_config->stages[i].full_array != NULL) {
          free(user_config->stages[i].full_array);
          user_config->stages[i].full_array = NULL;
	}
        if(user_config->stages[i].double_array != NULL) {
          free(user_config->stages[i].double_array);
          user_config->stages[i].double_array = NULL;
	}
        if(user_config->stages[i].counts.counts != NULL) {
	  freeCounts(&(user_config->stages[i].counts));
	}
	
      }
      
      //free(user_config->stages); //FIXME ERROR de memoria relacionado con la carpeta malleability
      free(user_config);
    }
}

/*
 * Imprime por salida estandar toda la informacion que contiene
 * la configuracion pasada como argumento
 */
void print_config(configuration *user_config, int grp) {
  if(user_config != NULL) {
    int i;
    printf("Config loaded: R=%d, S=%d, granularity=%d, SDR=%d, ADR=%d, AT=%d, SM=%d, SS=%d, latency=%2.8f, bw=%lf || grp=%d\n",
        user_config->n_resizes, user_config->n_stages, user_config->granularity, user_config->sdr, user_config->adr, 
	user_config->at, user_config->sm, user_config->ss, user_config->latency_m, user_config->bw_m, grp);
    for(i=0; i<user_config->n_stages; i++) {
      printf("Stage %d: PT=%d, T_stage=%lf, bytes=%d, Intercept=%lf, Slope=%lf\n",
        i, user_config->stages[i].pt, user_config->stages[i].t_stage, user_config->stages[i].real_bytes, user_config->stages[i].intercept, user_config->stages[i].slope);
    }
    for(i=0; i<user_config->n_resizes; i++) {
      printf("Resize %d: Iters=%d, Procs=%d, Factors=%f, Dist=%d\n",
        i, user_config->iters[i], user_config->procs[i], user_config->factors[i], user_config->phy_dist[i]);
    }
  }
}


/*
 * Imprime por salida estandar la informacion relacionada con un
 * solo grupo de procesos en su configuracion.
 */
void print_config_group(configuration *user_config, int grp) {
  int i;
  if(user_config != NULL) {
    int parents, sons;
    parents = sons = 0;
    if(grp > 0) {
      parents = user_config->procs[grp-1];
    }
    if(grp < user_config->n_resizes - 1) {
      sons = user_config->procs[grp+1];
    }

    printf("Config: granularity=%d, SDR=%d, ADR=%d, AT=%d, SM=%d, SS=%d, latency=%2.8f, bw=%lf\n",
        user_config->granularity, user_config->sdr, user_config->adr, user_config->at, user_config->sm, user_config->ss, user_config->latency_m, user_config->bw_m);
    for(i=0; i<user_config->n_stages; i++) {
      printf("Stage %d: PT=%d, T_stage=%lf, bytes=%d, Intercept=%lf, Slope=%lf\n",
        i, user_config->stages[i].pt, user_config->stages[i].t_stage, user_config->stages[i].real_bytes, user_config->stages[i].intercept, user_config->stages[i].slope);
    }
    printf("Config Group: iters=%d, factor=%f, phy=%d, procs=%d, parents=%d, sons=%d\n",
        user_config->iters[grp], user_config->factors[grp], user_config->phy_dist[grp], user_config->procs[grp], parents, sons);
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

  MPI_Datatype config_type, config_type_array, iter_stage_type;

  // Obtener un tipo derivado para enviar todos los
  // datos escalares con una sola comunicacion
  def_struct_config_file(config_file, &config_type);


  // Obtener un tipo derivado para enviar los tres vectores
  // de enteros con una sola comunicacion
  def_struct_config_file_array(config_file, &config_type_array);

  // Obtener un tipo derivado para enviar las estructuras de fases de iteracion
  // con una sola comunicacion
  def_struct_iter_stage(&(config_file->stages[0]), config_file->n_stages, &iter_stage_type);

  MPI_Bcast(config_file, 1, config_type, root, intercomm);
  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->n_resizes, MPI_FLOAT, root, intercomm);
  MPI_Bcast(config_file->stages, config_file->n_stages, iter_stage_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&config_type);
  MPI_Type_free(&config_type_array);
  MPI_Type_free(&iter_stage_type);
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

  MPI_Datatype config_type, config_type_array, iter_stage_type;


  configuration *config_file = malloc(sizeof(configuration) * 1);

  // Obtener un tipo derivado para recibir todos los
  // datos escalares con una sola comunicacion
  def_struct_config_file(config_file, &config_type);
  MPI_Bcast(config_file, 1, config_type, root, intercomm);

  //Inicializado de estructuras internas
  malloc_config_resizes(config_file, config_file->n_resizes); // Reserva de memoria de los vectores
  config_file->stages = malloc(sizeof(iter_stage_t) * config_file->n_stages);

  // Obtener un tipo derivado para enviar los tres vectores
  // de enteros con una sola comunicacion
  def_struct_config_file_array(config_file, &config_type_array);
  def_struct_iter_stage(&(config_file->stages[0]), config_file->n_stages, &iter_stage_type);

  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->n_resizes, MPI_FLOAT, root, intercomm);
  MPI_Bcast(config_file->stages, config_file->n_stages, iter_stage_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&config_type);
  MPI_Type_free(&config_type_array);
  MPI_Type_free(&iter_stage_type);

  init_config_stages(config_file); // Inicializar a NULL vectores
  *config_file_out = config_file;
}

/*
 * Tipo derivado para enviar 11 elementos especificos
 * de la estructura de configuracion con una sola comunicacion.
 */
void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type) {
  int i, counts = 11;
  int blocklengths[11] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = types[6] = types[7] = types[8] = MPI_INT;
  types[9] = types[10] = MPI_DOUBLE;

  // Rellenar vector displs
  MPI_Get_address(config_file, &dir);

  MPI_Get_address(&(config_file->n_resizes), &displs[0]);
  MPI_Get_address(&(config_file->n_stages), &displs[1]);
  MPI_Get_address(&(config_file->actual_resize), &displs[2]); // TODO Refactor Es necesario enviarlo?
  MPI_Get_address(&(config_file->granularity), &displs[3]);
  MPI_Get_address(&(config_file->sdr), &displs[4]);
  MPI_Get_address(&(config_file->adr), &displs[5]);
  MPI_Get_address(&(config_file->at), &displs[6]);
  MPI_Get_address(&(config_file->ss), &displs[7]);
  MPI_Get_address(&(config_file->sm), &displs[8]);
  MPI_Get_address(&(config_file->latency_m), &displs[9]);
  MPI_Get_address(&(config_file->bw_m), &displs[10]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, config_type);
  MPI_Type_commit(config_type);
}

/*
 * Tipo derivado para enviar tres vectores de enteros
 * de la estructura de configuracion con una sola comunicacion.
 */
void def_struct_config_file_array(configuration *config_file, MPI_Datatype *config_type) {
  int i, counts = 3;
  int blocklengths[3] = {1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype aux, types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = MPI_INT;

  // Modificar blocklengths al valor adecuado
  blocklengths[0] = blocklengths[1] = blocklengths[2] = config_file->n_resizes;

  //Rellenar vector displs
  MPI_Get_address(config_file, &dir);

  MPI_Get_address(config_file->iters, &displs[0]);
  MPI_Get_address(config_file->procs, &displs[1]);
  MPI_Get_address(config_file->phy_dist, &displs[2]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  // Tipo derivado para enviar un solo elemento de tres vectores
  MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
  // Tipo derivado para enviar N elementos de tres vectores(3N en total)
  MPI_Type_create_resized(aux, 0, 1*sizeof(int), config_type); 
  MPI_Type_commit(config_type);
}


/*
 * Tipo derivado para enviar elementos especificos
 * de la estructuras de fases de iteracion en una sola comunicacion.
 */
void def_struct_iter_stage(iter_stage_t *stages, int n_stages, MPI_Datatype *config_type) {
  int i, counts = 4;
  int blocklengths[4] = {1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype aux, types[counts];

  // Rellenar vector types
  types[0] = types[3] = MPI_INT;
  types[1] = MPI_FLOAT;
  types[2] = MPI_DOUBLE;

  // Rellenar vector displs
  MPI_Get_address(stages, &dir);

  MPI_Get_address(&(stages->pt), &displs[0]);
  MPI_Get_address(&(stages->t_stage), &displs[1]);
  MPI_Get_address(&(stages->t_op), &displs[2]);
  MPI_Get_address(&(stages->bytes), &displs[3]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  if (n_stages == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, config_type);
  } else { // Si hay mas de una fase(estructura), el "extent" se modifica.
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    // Tipo derivado para enviar N elementos de la estructura
    MPI_Type_create_resized(aux, 0, sizeof(iter_stage_t), config_type); 
  }
  MPI_Type_commit(config_type);
}
