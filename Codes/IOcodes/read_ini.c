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
    int ret_value=1;
    configuration* pconfig = (configuration*)user;

    if(pconfig->actual_resize >= pconfig->n_resizes && pconfig->actual_stage >= pconfig->n_stages) {
      return 1; // There is no more work to perform
    }

    char *resize_name = malloc(10 * sizeof(char));
    snprintf(resize_name, 10, "resize%zu", pconfig->actual_resize);

    char *stage_name = malloc(10 * sizeof(char));
    snprintf(stage_name, 10, "stage%zu", pconfig->actual_stage);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    #define LAST(iter, total) iter < total
    if (MATCH("general", "Total_Resizes")) {
        pconfig->n_resizes = strtoul(value, NULL, 10) + 1;
        user_functions->resizes_f(pconfig);
    } else if (MATCH("general", "Total_Stages")) {
        pconfig->n_stages = strtoul(value, NULL, 10);
        user_functions->stages_f(pconfig); 
    } else if (MATCH("general", "Granularity")) {
        pconfig->granularity = atoi(value);
    } else if (MATCH("general", "SDR")) { // TODO Refactor a nombre manual
        pconfig->sdr = atoi(value);
    } else if (MATCH("general", "ADR")) { // TODO Refactor a nombre manual
        pconfig->adr = atoi(value);
    } else if (MATCH("general", "Rigid")) {
        pconfig->rigid_times = atoi(value);

    // Iter stage
    } else if (MATCH(stage_name, "Stage_Type") && LAST(pconfig->actual_stage, pconfig->n_stages)) {
        pconfig->stages[pconfig->actual_stage].pt = atoi(value);
    } else if (MATCH(stage_name, "Stage_Time_Capped") && LAST(pconfig->actual_stage, pconfig->n_stages)) {
        pconfig->stages[pconfig->actual_stage].t_capped = atoi(value);
    } else if (MATCH(stage_name, "Stage_Bytes") && LAST(pconfig->actual_stage, pconfig->n_stages)) {
        pconfig->stages[pconfig->actual_stage].bytes = atoi(value);
    } else if (MATCH(stage_name, "Stage_Time") && LAST(pconfig->actual_stage, pconfig->n_stages)) {
        pconfig->stages[pconfig->actual_stage].t_stage = (float) atof(value);
        pconfig->actual_stage = pconfig->actual_stage+1; // Ultimo elemento del grupo

    // Resize stage
    } else if (MATCH(resize_name, "Iters") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
	//if(pconfig->actual_resize < pconfig->n_resizes)
        pconfig->groups[pconfig->actual_resize].iters = atoi(value);
    } else if (MATCH(resize_name, "Procs") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
        pconfig->groups[pconfig->actual_resize].procs = atoi(value);
    } else if (MATCH(resize_name, "FactorS") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
        pconfig->groups[pconfig->actual_resize].factor =(float) atof(value);
    } else if (MATCH(resize_name, "Dist") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
	int aux_value = MALL_DIST_COMPACT;
        if (strcmp(value, "spread") == 0) {
          aux_value = MALL_DIST_SPREAD;
  	}
        pconfig->groups[pconfig->actual_resize].phy_dist = aux_value;
    } else if (MATCH(resize_name, "Asynch_Redistribution_Type") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
        pconfig->groups[pconfig->actual_resize].at = atoi(value);
    } else if (MATCH(resize_name, "Spawn_Method") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
        pconfig->groups[pconfig->actual_resize].sm = atoi(value);
    } else if (MATCH(resize_name, "Spawn_Strategy") && LAST(pconfig->actual_resize, pconfig->n_resizes)) {
        pconfig->groups[pconfig->actual_resize].ss = atoi(value);
        pconfig->actual_resize = pconfig->actual_resize+1; // Ultimo elemento del grupo

    // Unkown case
    } else {
        ret_value = 0;  /* unknown section or name, error */
    }
 
    free(resize_name);
    free(stage_name);
    return ret_value;
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
    config->n_resizes = 1;
    config->n_stages = 1;
    config->actual_resize=0;
    config->actual_stage=0;

    user_functions = &init_functions;

    if(ini_parse(file_name, handler, config) < 0) { // Obtener configuracion
        printf("Can't load '%s'\n", file_name);
        return NULL;
    }
    return config;
}
