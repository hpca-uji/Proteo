#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_ini.h"
#include "ini.h"
#include "../malleability/spawn_methods/ProcessDist.h"


ext_functions_t *user_functions;

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
    if (MATCH("general", "Total_Resizes")) {
        pconfig->n_resizes = atoi(value) + 1;
        //malloc_config_resizes(pconfig); //FIXME Unknown
        user_functions->resizes_f(pconfig);
    } else if (MATCH("general", "Total_Stages")) {
        pconfig->n_stages = atoi(value);
        pconfig->stages = malloc(sizeof(iter_stage_t) * (size_t) pconfig->n_stages);
        //init_config_stages(pconfig); //FIXME Unkown
        user_functions->stages_f(pconfig);
    } else if (MATCH("general", "Granularity")) {
        pconfig->granularity = atoi(value);
    } else if (MATCH("general", "SDR")) { // TODO Refactor a nombre manual
        pconfig->sdr = atoi(value);
    } else if (MATCH("general", "ADR")) { // TODO Refactor a nombre manual
        pconfig->adr = atoi(value);
    } else if (MATCH("general", "Asynch_Redistribution_Type")) {
        pconfig->at = atoi(value);
    } else if (MATCH("general", "Spawn_Method")) {
        pconfig->sm = atoi(value);
    } else if (MATCH("general", "Spawn_Strategy")) {
        pconfig->ss = atoi(value);

    // Iter stage
    } else if (MATCH(stage_name, "Stage_Type")) {
	if(pconfig->actual_stage < pconfig->n_stages)
          pconfig->stages[pconfig->actual_stage].pt = atoi(value);
    } else if (MATCH(stage_name, "Stage_bytes")) {
	if(pconfig->actual_stage < pconfig->n_stages)
          pconfig->stages[pconfig->actual_stage].bytes = atoi(value);
    } else if (MATCH(stage_name, "Stage_time")) {
	if(pconfig->actual_stage < pconfig->n_stages) {
          pconfig->stages[pconfig->actual_stage].t_stage = (float) atof(value);
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
          pconfig->factors[pconfig->actual_resize] =(float) atof(value);
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
configuration *read_ini_file(char *file_name, ext_functions_t init_functions) {
    configuration *config = NULL;

    config = malloc(sizeof(configuration));
    if(config == NULL) {
        printf("Error when reserving configuration structure\n");
	return NULL;
    }
    config->actual_resize=0;
    config->actual_stage=0;

    user_functions = &init_functions;

    if(ini_parse(file_name, handler, config) < 0) { // Obtener configuracion
        printf("Can't load '%s'\n", file_name);
        return NULL;
    }
    return config;
}
