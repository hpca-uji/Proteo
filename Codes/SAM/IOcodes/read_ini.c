#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_ini.h"
#include "ini.h"
#include "MAM.h"

/**
 * @file read_ini.c
 * @brief INI configuration parser for Proteo (inih callback-based).
 */

/** @brief Next group (resize section) index while parsing. */
size_t actual_group;
/** @brief Next phase index while parsing. */
size_t actual_phase;
/** @brief Next stage index within the current phase while parsing. */
size_t actual_stage;
/** @brief Allocation callbacks supplied to ::read_ini_file. */
ext_functions_t *user_functions;

void get_numbers_from_string(const char *i_input, size_t *o_res_len, int **o_res);

/**
 * @brief inih handler that maps INI keys into a ::configuration.
 *
 * Reads the @c general section first, then @c phaseN / @c phaseN.stageM and
 * @c resizeN sections. Stops accepting new keys once all groups and phases
 * have been filled.
 *
 * @param[in,out] io_user    Pointer to the ::configuration being filled.
 * @param[in]     i_section  Current INI section name.
 * @param[in]     i_name     Current key name.
 * @param[in]     i_value    Current value string.
 * @return 1 on success, 0 if the section/key is unknown.
 */
static int handler(void *io_user, const char *i_section, const char *i_name,
                   const char *i_value) {
    int ret_value = 1;
    int *aux;
    size_t aux_len;
    phase_t *phase;
    stage_t *stage;
    configuration *pconfig = (configuration *)io_user;

    if (actual_group >= pconfig->n_groups && actual_phase >= pconfig->n_phases) {
      return 1; // There is no more work to perform
    }

    char *resize_name = malloc(10 * sizeof(char));
    snprintf(resize_name, 10, "resize%zu", actual_group);

    char *phase_name = malloc(10 * sizeof(char));
    snprintf(phase_name, 10, "phase%zu", actual_phase);

    char *stage_name = malloc(20 * sizeof(char));
    snprintf(stage_name, 20, "phase%zu.stage%zu", actual_phase, actual_stage);

    #define MATCH(s, n) strcmp(i_section, s) == 0 && strcmp(i_name, n) == 0
    #define LAST(iter, total) iter < total
    if (MATCH("general", "Total_Resizes")) {
        pconfig->n_resizes = strtoul(i_value, NULL, 10);
        pconfig->n_groups = pconfig->n_resizes + 1;
        user_functions->resizes_f(pconfig);
    } else if (MATCH("general", "Total_Phases")) {
        pconfig->n_phases = strtoul(i_value, NULL, 10);
        user_functions->phases_f(pconfig);
    } else if (MATCH("general", "SDR")) { // TODO: Refactor to the manual name
        pconfig->sdr = strtoul(i_value, NULL, 10);
    } else if (MATCH("general", "ADR")) { // TODO: Refactor to the manual name
        pconfig->adr = strtoul(i_value, NULL, 10);
    } else if (MATCH("general", "Datasize")) { // TODO: Refactor to the manual name
        pconfig->datasize = strtoul(i_value, NULL, 10);
    } else if (MATCH("general", "Rigid")) {
        pconfig->rigid_times = atoi(i_value);
    } else if (MATCH("general", "Capture_Method")) {
        pconfig->capture_method = atoi(i_value);

    // Phase
    } else if (MATCH(phase_name, "Total_Iters") && LAST(actual_phase, pconfig->n_phases)) {
        pconfig->phases[actual_phase].qty_iters = strtoul(i_value, NULL, 10);
    } else if (MATCH(phase_name, "Total_Stages") && LAST(actual_phase, pconfig->n_phases)) {
        phase = pconfig->phases + actual_phase;
        phase->qty_stages = strtoul(i_value, NULL, 10);
        user_functions->stages_f(pconfig, actual_phase);
        actual_stage = 0;

    // Stage
    } else if (MATCH(stage_name, "Stage_Type") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->pt = atoi(i_value);
    } else if (MATCH(stage_name, "Granularity") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->granularity = atoi(i_value);
    } else if (MATCH(stage_name, "Stage_Time_Capped") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->t_capped = atoi(i_value);
    } else if (MATCH(stage_name, "Stage_Bytes") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->bytes = atoi(i_value);
    } else if (MATCH(stage_name, "Stage_Identifier") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->id = atoi(i_value);
    } else if (MATCH(stage_name, "Stage_Involved_Procs") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->involved_procs = atoi(i_value);
    } else if (MATCH(stage_name, "Stage_Time") && LAST(actual_stage, pconfig->phases[actual_phase].qty_stages)) {
        phase = pconfig->phases + actual_phase;
        stage = phase->stages + actual_stage;
        stage->t_stage = (float)atof(i_value);
        actual_stage++; // Last element of the stage
        if (actual_stage == pconfig->phases[actual_phase].qty_stages) { // Last stage of the phase
            actual_phase++;
        }

    // Resize / group
    } else if (MATCH(resize_name, "Iters") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].iters = atoi(i_value);
    } else if (MATCH(resize_name, "Procs") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].procs = atoi(i_value);
    } else if (MATCH(resize_name, "FactorS") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].factor = (float)atof(i_value);
    } else if (MATCH(resize_name, "Dist") && LAST(actual_group, pconfig->n_groups)) {
        int aux_value = MAM_PHY_DIST_COMPACT;
        if (strcmp(i_value, "spread") == 0) {
          aux_value = MAM_PHY_DIST_SPREAD;
        }
        pconfig->groups[actual_group].phy_dist = aux_value;
    } else if (MATCH(resize_name, "Redistribution_Method") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].rm = atoi(i_value);
    } else if (MATCH(resize_name, "Redistribution_Strategy") && LAST(actual_group, pconfig->n_groups)) {
        get_numbers_from_string(i_value, &aux_len, &aux);
        pconfig->groups[actual_group].rs = aux;
        pconfig->groups[actual_group].rs_len = aux_len;
    } else if (MATCH(resize_name, "Spawn_Method") && LAST(actual_group, pconfig->n_groups)) {
        pconfig->groups[actual_group].sm = atoi(i_value);
    } else if (MATCH(resize_name, "Spawn_Strategy") && LAST(actual_group, pconfig->n_groups)) {
        get_numbers_from_string(i_value, &aux_len, &aux);
        pconfig->groups[actual_group].ss = aux;
        pconfig->groups[actual_group].ss_len = aux_len;
        actual_group++; // Last element of the group structure

    // Unknown case
    } else {
        ret_value = 0;  /* unknown section or name, error */
    }

    free(resize_name);
    free(phase_name);
    free(stage_name);
    return ret_value;
}

/**
 * @brief Extracts numbers from a comma-separated string into a new array.
 *
 * Converts each token to an integer and stores them in a dynamically
 * allocated array. The caller must free @p o_res.
 *
 * @param[in]  i_input   Input string with comma-separated numbers.
 * @param[out] o_res_len Length of the resulting array, or ignored if @c NULL.
 * @param[out] o_res     Receives the allocated integer array.
 */
void get_numbers_from_string(const char *i_input, size_t *o_res_len, int **o_res) {
  char *aux, *token;
  int num;
  size_t len, malloc_len;
  len = 0;
  malloc_len = 10;
  *o_res = (int *)malloc(malloc_len * sizeof(int));
  aux = (char *)malloc((strlen(i_input) + 1) * sizeof(char));
  strcpy(aux, i_input);

  token = strtok(aux, ",");
  while (token != NULL) {
    num = atoi(token);

    if (len == malloc_len) {
      malloc_len += 10;
      *o_res = (int *)realloc(*o_res, malloc_len * sizeof(int));
    }
    (*o_res)[len] = num;
    len++;

    token = strtok(NULL, ",");
  }

  if (o_res_len != NULL) *o_res_len = len;
  if (len != malloc_len) {
    *o_res = (int *)realloc(*o_res, len * sizeof(int));
  }

  free(aux);
}

configuration *read_ini_file(char *i_file_name, ext_functions_t i_init_functions) {
    configuration *config = NULL;

    config = malloc(sizeof(configuration));
    if (config == NULL) {
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

    actual_group = 0;
    actual_phase = 0;
    actual_stage = 0;

    user_functions = &i_init_functions;

    if (ini_parse(i_file_name, handler, config) < 0) { // Load configuration
        printf("Can't load '%s'\n", i_file_name);
        return NULL;
    }
    return config;
}
