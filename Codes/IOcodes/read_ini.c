#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_ini.h"
#include "ini.h"

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
          pconfig->phy_dist[act_resize] = 1; //FIXME MAGICAL NUM
	} else {
          pconfig->phy_dist[act_resize] = 2; //FIXME MAGICAL NUM
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
      user_config->factors = malloc(sizeof(int) * resizes);
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
