#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "read_ini.h"
#include "../malleability/ProcessDist.h"
#include "ini.h"


void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type);
void def_struct_config_file_array(configuration *config_file, MPI_Datatype *config_type);

static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    configuration* pconfig = (configuration*)user;

    char *resize_name = malloc(10 * sizeof(char));
    int act_resize = pconfig->actual_resize;
    snprintf(resize_name, 10, "resize%d", act_resize);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("general", "resizes")) {
        pconfig->resizes = atoi(value) + 1;
        malloc_config_arrays(pconfig, pconfig->resizes);

    } else if (MATCH("general", "matrix_tam")) {
        pconfig->matrix_tam = atoi(value);
    } else if (MATCH("general", "SDR")) {
        pconfig->sdr = atoi(value);
    } else if (MATCH("general", "ADR")) {
        pconfig->adr = atoi(value);
    } else if (MATCH("general", "time")) {
        pconfig->general_time = atof(value);

    // Resize	
    } else if (MATCH(resize_name, "iters")) {
        pconfig->iters[act_resize] = atoi(value);
    } else if (MATCH(resize_name, "procs")) {
        pconfig->procs[act_resize] = atoi(value);
    } else if (MATCH(resize_name, "factor")) {
        pconfig->factors[act_resize] = atof(value);
    } else if (MATCH(resize_name, "physical_dist")) {

	char *aux = strdup(value);
        if (strcmp(aux, "node") == 0) {
          pconfig->phy_dist[act_resize] = COMM_PHY_NODES;
	} else {
          pconfig->phy_dist[act_resize] = COMM_PHY_CPU;
	}
	free(aux);
        pconfig->actual_resize = pconfig->actual_resize+1; // Ultimo elemento del grupo

    } else {
        return 0;  /* unknown section or name, error */
    }
 
    free(resize_name);
    return 1;
}

configuration *read_ini_file(char *file_name) {
    configuration *config = NULL;

    config = malloc(sizeof(configuration) * 1);
    if(config == NULL) {
        printf("Error when reserving configuration structure\n");
	return NULL;
    }
    config->actual_resize=0;

    if(ini_parse(file_name, handler, config) < 0) { // Obtener configuracion
        printf("Can't load '%s'\n", file_name);
        return NULL;
    }
    return config;
}

/*
 * Reserva de memoria para los vectores de la estructura de configuracion
 *
 * Si se llama desde fuera de este codigo, tiene que reservarse la estructura
 */
void malloc_config_arrays(configuration *user_config, int resizes) {
    if(user_config != NULL) {
      user_config->iters = malloc(sizeof(int) * resizes);
      user_config->procs = malloc(sizeof(int) * resizes);
      user_config->factors = malloc(sizeof(float) * resizes);
      user_config->phy_dist = malloc(sizeof(int) * resizes);
    }
}

void free_config(configuration *user_config) {
    if(user_config != NULL) {
      free(user_config->iters);
      free(user_config->procs);
      free(user_config->factors);
      free(user_config->phy_dist);

      free(user_config);
    }
}

void print_config(configuration *user_config) {
  if(user_config != NULL) {
    int i;
    printf("Config loaded: resizes=%d, matrix=%d, sdr=%d, adr=%d, time=%f\n",
        user_config->resizes, user_config->matrix_tam, user_config->sdr, user_config->adr, user_config->general_time);
    for(i=0; i<user_config->resizes; i++) {
      printf("Resize %d: Iters=%d, Procs=%d, Factors=%f, Phy=%d\n",
        i, user_config->iters[i], user_config->procs[i], user_config->factors[i], user_config->phy_dist[i]);
    }
  }
}



//
//
//
//
//

void send_config_file(configuration *config_file, int root, MPI_Comm intercomm) {

  MPI_Datatype config_type, config_type_array;

  def_struct_config_file(config_file, &config_type);
  MPI_Bcast(config_file, 1, config_type, root, intercomm);

  def_struct_config_file_array(config_file, &config_type_array);
  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->resizes, MPI_FLOAT, root, intercomm);

  MPI_Type_free(&config_type);
  MPI_Type_free(&config_type_array);
}

configuration *recv_config_file(int root, MPI_Comm intercomm) {

  MPI_Datatype config_type, config_type_array;

  configuration *config_file = malloc(sizeof(configuration) * 1);
  def_struct_config_file(config_file, &config_type);

  MPI_Bcast(config_file, 1, config_type, root, intercomm);

  malloc_config_arrays(config_file, config_file->resizes);
  def_struct_config_file_array(config_file, &config_type_array);

  MPI_Bcast(config_file, 1, config_type_array, root, intercomm);
  MPI_Bcast(config_file->factors, config_file->resizes, MPI_FLOAT, root, intercomm);

  MPI_Type_free(&config_type);
  MPI_Type_free(&config_type_array);

  return config_file;
}

void def_struct_config_file(configuration *config_file, MPI_Datatype *config_type) {
  int i, counts = 6;
  int blocklengths[6] = {1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = MPI_INT;
  types[5] = MPI_FLOAT;

  //Rellenar vector displs
  MPI_Get_address(config_file, &dir);

  MPI_Get_address(&(config_file->resizes), &displs[0]);
  MPI_Get_address(&(config_file->actual_resize), &displs[1]);
  MPI_Get_address(&(config_file->matrix_tam), &displs[2]);
  MPI_Get_address(&(config_file->sdr), &displs[3]);
  MPI_Get_address(&(config_file->adr), &displs[4]);
  MPI_Get_address(&(config_file->general_time), &displs[5]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, config_type);
  MPI_Type_commit(config_type);
}

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

  MPI_Type_create_struct(counts, blocklengths, displs, types, &aux);
  MPI_Type_create_resized(aux, 0, 1*sizeof(int), config_type);
  MPI_Type_commit(config_type);
}
