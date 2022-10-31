#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "../IOcodes/read_ini.h"
#include "configuration.h"
#include "../malleability/spawn_methods/ProcessDist.h"
#include "../malleability/distribution_methods/block_distribution.h"

void malloc_config_resizes(configuration *user_config);
void init_config_stages(configuration *user_config);

void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type);
void def_struct_config_file_array(configuration *config_file, MPI_Datatype *config_type);
void def_struct_iter_stage(iter_stage_t *stages, size_t n_stages, MPI_Datatype *config_type);

/*
 * Inicializa una estructura de configuracion
 *
 * Si el parametro "file_name" no es nulo,
 * se obtiene la configuracion a partir de 
 * un fichero .ini
 *
 * En caso de que sea nulo, es el usuario
 * el que tiene que elegir los valores a
 * utilizar.
 */
void init_config(char *file_name, configuration **user_config) {
  if(file_name != NULL) {
    ext_functions_t mallocs;
    mallocs.resizes_f = malloc_config_resizes;
    mallocs.stages_f = init_config_stages;
    *user_config = read_ini_file(file_name, mallocs);
  } else {
    configuration *config = NULL;

    config = malloc(sizeof(configuration));
    if(config == NULL) {
        perror("Error when reserving configuration structure\n");
	MPI_Abort(MPI_COMM_WORLD, -3);
	return;
    }
    *user_config=config;
  }
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
void malloc_config_resizes(configuration *user_config) {
  size_t n_resizes = user_config->n_resizes;
   
  if(user_config != NULL) {
    user_config->iters = malloc(sizeof(int) * n_resizes);
    user_config->procs = malloc(sizeof(int) * n_resizes);
    user_config->factors = malloc(sizeof(float) * n_resizes);
    user_config->phy_dist = malloc(sizeof(int) * n_resizes);
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
  def_struct_iter_stage(&(config_file->stages[0]), (size_t) config_file->n_stages, &iter_stage_type);

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
  malloc_config_resizes(config_file); // Reserva de memoria de los vectores
  config_file->stages = malloc(sizeof(iter_stage_t) * (size_t) config_file->n_stages);

  // Obtener un tipo derivado para enviar los tres vectores
  // de enteros con una sola comunicacion
  def_struct_config_file_array(config_file, &config_type_array);
  def_struct_iter_stage(&(config_file->stages[0]), (size_t) config_file->n_stages, &iter_stage_type);

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
void def_struct_iter_stage(iter_stage_t *stages, size_t n_stages, MPI_Datatype *config_type) {
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
