#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "read_ini.h"
#include "../malleability/ProcessDist.h"
#include "../malleability/distribution_methods/block_distribution.h"
#include "ini.h"


void malloc_config_resizes(configuration *user_config, int resizes);
void init_config_stages(configuration *user_config, int stages);
void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type);
void def_struct_config_file_array(configuration *config_file, MPI_Datatype *config_type);
void def_struct_iter_stage(iter_stage_t *iter_stage, int stages, MPI_Datatype *config_type);

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
    int act_resize = pconfig->actual_resize;
    snprintf(resize_name, 10, "resize%d", act_resize);

    char *iter_name = malloc(10 * sizeof(char));
    int act_iter = pconfig->actual_iter;
    snprintf(iter_name, 10, "stage%d", act_iter);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("general", "resizes")) {
        pconfig->resizes = atoi(value) + 1;
        malloc_config_resizes(pconfig, pconfig->resizes);
    } else if (MATCH("general", "iter_stages")) {
        pconfig->iter_stages = atoi(value);
        pconfig->iter_stage = malloc(sizeof(iter_stage_t) * pconfig->iter_stages);
        init_config_stages(pconfig, pconfig->iter_stages);
    } else if (MATCH("general", "matrix_tam")) { //TODO Refactor cambiar nombre
        pconfig->matrix_tam = atoi(value);
    } else if (MATCH("general", "SDR")) {
        pconfig->sdr = atoi(value);
    } else if (MATCH("general", "ADR")) {
        pconfig->adr = atoi(value);
    } else if (MATCH("general", "AIB")) { //TODO Refactor cambiar nombre
        pconfig->aib = atoi(value);
    } else if (MATCH("general", "CST")) {
        pconfig->cst = atoi(value);
    } else if (MATCH("general", "CSS")) {
        pconfig->css = atoi(value);

    // Iter stage
    } else if (MATCH(iter_name, "PT")) {
	if(pconfig->actual_iter < pconfig->iter_stages)
          pconfig->iter_stage[act_iter].pt = atoi(value);
    } else if (MATCH(iter_name, "bytes")) {
	if(pconfig->actual_iter < pconfig->iter_stages)
          pconfig->iter_stage[act_iter].bytes = atoi(value);
    } else if (MATCH(iter_name, "t_stage")) {
	if(pconfig->actual_iter < pconfig->iter_stages) {
          pconfig->iter_stage[act_iter].t_stage = atof(value);
          pconfig->actual_iter = pconfig->actual_iter+1; // Ultimo elemento del grupo
	}

    // Resize stage
    } else if (MATCH(resize_name, "iters")) {
	if(pconfig->actual_resize < pconfig->resizes)
          pconfig->iters[act_resize] = atoi(value);
    } else if (MATCH(resize_name, "procs")) {
	if(pconfig->actual_resize < pconfig->resizes)
          pconfig->procs[act_resize] = atoi(value);
    } else if (MATCH(resize_name, "factor")) {
	if(pconfig->actual_resize < pconfig->resizes)
          pconfig->factors[act_resize] = atof(value);
    } else if (MATCH(resize_name, "physical_dist")) {
	if(pconfig->actual_resize < pconfig->resizes) {
  	  char *aux = strdup(value);
          if (strcmp(aux, "node") == 0) {
            pconfig->phy_dist[act_resize] = COMM_PHY_NODES;
  	  } else {
            pconfig->phy_dist[act_resize] = COMM_PHY_CPU;
	  }
	  free(aux);
          pconfig->actual_resize = pconfig->actual_resize+1; // Ultimo elemento del grupo
	}

    } else {
        return 0;  /* unknown section or name, error */
    }
 
    free(resize_name);
    free(iter_name);
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
    config->actual_iter=0;

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
void init_config_stages(configuration *user_config, int stages) {
    int i;
    if(user_config != NULL) {
       for(i=0; i<user_config->iter_stages; i++) {
        user_config->iter_stage[i].array = NULL;
        user_config->iter_stage[i].full_array = NULL;
        user_config->iter_stage[i].double_array = NULL;
        user_config->iter_stage[i].counts.counts = NULL;
        user_config->iter_stage[i].real_bytes = 0;
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
      
      for(i=0; i < user_config->iter_stages; i++) {
	
        if(user_config->iter_stage[i].array != NULL) {
          free(user_config->iter_stage[i].array);
          user_config->iter_stage[i].array = NULL;
	}
        if(user_config->iter_stage[i].full_array != NULL) {
          free(user_config->iter_stage[i].full_array);
          user_config->iter_stage[i].full_array = NULL;
	}
        if(user_config->iter_stage[i].double_array != NULL) {
          free(user_config->iter_stage[i].double_array);
          user_config->iter_stage[i].double_array = NULL;
	}
        if(user_config->iter_stage[i].counts.counts != NULL) {
	  freeCounts(&(user_config->iter_stage[i].counts));
	}
	
      }
      
      //free(user_config->iter_stage); //FIXME ERROR de memoria relacionado con la carpeta malleability
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
    printf("Config loaded: resizes=%d, stages=%d, matrix=%d, sdr=%d, adr=%d, aib=%d, css=%d, cst=%d || grp=%d\n",
        user_config->resizes, user_config->iter_stages, user_config->matrix_tam, user_config->sdr, user_config->adr, user_config->aib, user_config->css, user_config->cst, grp);
    for(i=0; i<user_config->iter_stages; i++) {
      printf("Stage %d: PT=%d, T_stage=%lf, bytes=%d\n",
        i, user_config->iter_stage[i].pt, user_config->iter_stage[i].t_stage, user_config->iter_stage[i].real_bytes);
    }
    for(i=0; i<user_config->resizes; i++) {
      printf("Resize %d: Iters=%d, Procs=%d, Factors=%f, Phy=%d\n",
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
    if(grp < user_config->resizes - 1) {
      sons = user_config->procs[grp+1];
    }

    printf("Config: matrix=%d, sdr=%d, adr=%d, aib=%d, css=%d, cst=%d, latency=%lf, bw=%lf\n",
        user_config->matrix_tam, user_config->sdr, user_config->adr, user_config->aib, user_config->css, user_config->cst, user_config->latency_m, user_config->bw_m);
    for(i=0; i<user_config->iter_stages; i++) {
      printf("Stage %d: PT=%d, T_stage=%lf, bytes=%d\n",
        i, user_config->iter_stage[i].pt, user_config->iter_stage[i].t_stage, user_config->iter_stage[i].real_bytes);
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
  def_struct_iter_stage(&(config_file->iter_stage[0]), config_file->iter_stages, &iter_stage_type);

  MPI_Bcast(config_file, 1, config_type, root, intercomm);
  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->resizes, MPI_FLOAT, root, intercomm);
  MPI_Bcast(config_file->iter_stage, config_file->iter_stages, iter_stage_type, root, intercomm);

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
  malloc_config_resizes(config_file, config_file->resizes); // Reserva de memoria de los vectores
  config_file->iter_stage = malloc(sizeof(iter_stage_t) * config_file->iter_stages);

  // Obtener un tipo derivado para enviar los tres vectores
  // de enteros con una sola comunicacion
  def_struct_config_file_array(config_file, &config_type_array);
  def_struct_iter_stage(&(config_file->iter_stage[0]), config_file->iter_stages, &iter_stage_type);

  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->resizes, MPI_FLOAT, root, intercomm);
  MPI_Bcast(config_file->iter_stage, config_file->iter_stages, iter_stage_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&config_type);
  MPI_Type_free(&config_type_array);
  MPI_Type_free(&iter_stage_type);

  init_config_stages(config_file, config_file->iter_stages); // Inicializar a NULL vectores
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

  MPI_Get_address(&(config_file->resizes), &displs[0]);
  MPI_Get_address(&(config_file->iter_stages), &displs[1]);
  MPI_Get_address(&(config_file->actual_resize), &displs[2]);
  MPI_Get_address(&(config_file->matrix_tam), &displs[3]);
  MPI_Get_address(&(config_file->sdr), &displs[4]);
  MPI_Get_address(&(config_file->adr), &displs[5]);
  MPI_Get_address(&(config_file->aib), &displs[6]);
  MPI_Get_address(&(config_file->css), &displs[7]);
  MPI_Get_address(&(config_file->cst), &displs[8]);
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
  blocklengths[0] = blocklengths[1] = blocklengths[2] = config_file->resizes;

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
void def_struct_iter_stage(iter_stage_t *iter_stage, int stages, MPI_Datatype *config_type) {
  int i, counts = 4;
  int blocklengths[4] = {1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype aux, types[counts];

  // Rellenar vector types
  types[0] = types[3] = MPI_INT;
  types[1] = MPI_FLOAT;
  types[2] = MPI_DOUBLE;

  // Rellenar vector displs
  MPI_Get_address(iter_stage, &dir);

  MPI_Get_address(&(iter_stage->pt), &displs[0]);
  MPI_Get_address(&(iter_stage->t_stage), &displs[1]);
  MPI_Get_address(&(iter_stage->t_op), &displs[2]);
  MPI_Get_address(&(iter_stage->bytes), &displs[3]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  if (stages == 1) {
    MPI_Type_create_struct(counts, blocklengths, displs, types, config_type);
  } else { // Si hay mas de una fase(estructura), el "extent" se modifica.
    MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
    // Tipo derivado para enviar N elementos de la estructura
    MPI_Type_create_resized(aux, 0, 1*sizeof(iter_stage_t), config_type); 
  }
  MPI_Type_commit(config_type);
}
