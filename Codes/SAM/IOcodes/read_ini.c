#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_ini.h"
#include "ini.h"
#include "../MaM/MAM.h"

size_t actual_group, actual_phase, actual_stage; // Used for IO data only
ext_functions_t *user_functions;

void get_numbers_from_string(const char *input, size_t *res_len, int **res);

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
    int *aux;
    size_t aux_len;
    phase_t *phase;
    stage_t *stage;
    configuration* pconfig = (configuration*)user;

    if(actual_group >= pconfig->n_groups && actual_phase >= pconfig->n_phases) {
      return 1; // There is no more work to perform
    }

    char *resize_name = malloc(10 * sizeof(char));
    snprintf(resize_name, 10, "resize%zu", actual_group);

    char *phase_name = malloc(10 * sizeof(char));
    snprintf(phase_name, 10, "phase%zu", actual_phase);

    char *stage_name = malloc(20 * sizeof(char));
    snprintf(stage_name, 20, "phase%zu.stage%zu", actual_phase, actual_stage);

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    #define LAST(iter, total) iter < total
    if (MATCH("general", "Total_Resizes")) {
        pconfig->n_resizes = strtoul(value, NULL, 10);
        pconfig->n_groups = pconfig->n_resizes+1;
        user_functions->resizes_f(pconfig);
    } else if (MATCH("general", "Total_Phases")) {
        pconfig->n_phases = strtoul(value, NULL, 10);
        user_functions->phases_f(pconfig);
    } else if (MATCH("general", "SDR")) { // TODO Refactor a nombre manual
        pconfig->sdr = strtoul(value, NULL, 10);
    } else if (MATCH("general", "ADR")) { // TODO Refactor a nombre manual
        pconfig->adr = strtoul(value, NULL, 10);
    } else if (MATCH("general", "Rigid")) {
        pconfig->rigid_times = atoi(value);
    } else if (MATCH("general", "Capture_Method")) {
        pconfig->capture_method = atoi(value);

    // Phase
    } else if (MATCH(phase_name, "Total_Iters") && LAST(actual_phase, pconfig->n_phases)) {
        pconfig->phases[actual_phase].qty_iters = strtoul(value, NULL, 10);
    } else if (MATCH(phase_name, "Total_Stages") && LAST(actual_phase, pconfig->n_phases)) {
        phase = pconfig->phases+actual_phase;
        phase->qty_stages = strtoul(value, NULL, 10);
        user_functions->stages_f(pconfig, actual_phase);
        actual_stage = 0;

    // Stage
    } else if (MATCH(stage_name, "Stage_Type") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->pt = atoi(value);
    } else if (MATCH(stage_name, "Granularity") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->granularity = atoi(value);
    } else if (MATCH(stage_name, "Stage_Time_Capped") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->t_capped = atoi(value);
    } else if (MATCH(stage_name, "Stage_Bytes") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->bytes = atoi(value);
    } else if (MATCH(stage_name, "Stage_Identifier") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->id = atoi(value);
    } else if (MATCH(stage_name, "Stage_Time") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases+actual_phase;
        stage = phase->stages+actual_stage;
        stage->t_stage = (float) atof(value);
        actual_stage++; // Ultimo elemento del grupo
        if(actual_stage == pconfig->phases[actual_phase].qty_stages) { // Ultimo stage de la phase
            actual_phase++; 
        }

    // Resize stage
    } else if (MATCH(resize_name, "Iters") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].iters = atoi(value);
    } else if (MATCH(resize_name, "Procs") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].procs = atoi(value);
    } else if (MATCH(resize_name, "FactorS") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].factor =(float) atof(value);
    } else if (MATCH(resize_name, "Dist") && LAST(actual_group, pconfig->n_groups)) {
	int aux_value = MAM_PHY_DIST_COMPACT;
        if (strcmp(value, "spread") == 0) {
          aux_value = MAM_PHY_DIST_SPREAD;
  	}
        pconfig->groups[actual_group].phy_dist = aux_value;
    } else if (MATCH(resize_name, "Redistribution_Method") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].rm = atoi(value);
    } else if (MATCH(resize_name, "Redistribution_Strategy") && LAST(actual_group, pconfig->n_groups)) {
        get_numbers_from_string(value, &aux_len, &aux);
        pconfig->groups[actual_group].rs = aux;
        pconfig->groups[actual_group].rs_len = aux_len;
    } else if (MATCH(resize_name, "Spawn_Method") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].sm = atoi(value);
    } else if (MATCH(resize_name, "Spawn_Strategy") && LAST(actual_group, pconfig->n_groups)) {
        get_numbers_from_string(value, &aux_len, &aux);
        pconfig->groups[actual_group].ss = aux;
        pconfig->groups[actual_group].ss_len = aux_len;
        actual_group++; // Ultimo elemento de la estructura

    // Unkown case
    } else {
        ret_value = 0;  /* unknown section or name, error */
    }
 
    free(resize_name);
    free(phase_name);
    free(stage_name);
    return ret_value;
}

/**
 * @brief Extracts numbers from a comma-separated string and stores them in an array.
 *
 * This function takes a string containing a sequence of numbers separated by commas,
 * converts each number to an integer, and stores them in a dynamically allocated array.
 *
 * @param input The input string containing comma-separated numbers.
 * @param res_len Pointer to an integer that will hold the length of the resulting array.
 *            Note: Null can be passed if the caller does not need it.
 * @param res Pointer to an integer array where the extracted numbers will be stored.
 *            Note: The memory for this array is dynamically allocated and should be freed by the caller.
 */
void get_numbers_from_string(const char *input, size_t *res_len, int **res) {
  char *aux, *token;
  int num;
  size_t len, malloc_len;
  len = 0;
  malloc_len = 10;
  *res = (int *) malloc(malloc_len * sizeof(int));
  aux = (char *) malloc((strlen(input)+1) * sizeof(char));
  strcpy(aux, input);

  token = strtok(aux, ",");
  while (token != NULL) {
    num = atoi(token);

    if(len == malloc_len) {
      malloc_len += 10;
      *res = (int *) realloc(*res, malloc_len * sizeof(int));
    }
    (*res)[len] = num;
    len++;

    token = strtok(NULL, ",");
    }

  if(res_len != NULL) *res_len = len;
  if(len != malloc_len) {
    *res = (int *) realloc(*res, len * sizeof(int));
  }

  free(aux);
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

    config->config_type = MPI_DATATYPE_NULL;
    config->group_type = MPI_DATATYPE_NULL;
    config->group_strats_type = MPI_DATATYPE_NULL;
    config->phase_type = MPI_DATATYPE_NULL;
    config->stage_type = MPI_DATATYPE_NULL;
    config->capture_method = 0;
    config->rigid_times = 0;
    config->n_resizes = 0;
    config->n_groups = 1;
    config->n_phases = 1;

    actual_group=0;
    actual_phase=0;
    actual_stage=0;

    user_functions = &init_functions;

    if(ini_parse(file_name, handler, config) < 0) { // Obtener configuracion
        printf("Can't load '%s'\n", file_name);
        return NULL;
    }
    return config;
}
