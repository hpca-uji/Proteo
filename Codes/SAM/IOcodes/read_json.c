#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_json.h"
#include "cJSON.h"
#include "MAM.h"

/**
 * @file read_json.c
 * @brief JSON configuration parser for Proteo (cJSON-based).
 */

static char *read_file_contents(const char *i_file_name, long *o_out_len);
static void free_config_partial(configuration *io_config);
static cJSON *require_child(cJSON *i_parent, const char *i_key, int i_expect_array);
static int require_size_t(cJSON *i_obj, const char *i_key, size_t *o_out);
static int require_int(cJSON *i_obj, const char *i_key, int *o_out);
static int optional_int(cJSON *i_obj, const char *i_key, int *o_out);
static int require_double(cJSON *i_obj, const char *i_key, double *o_out);
static int copy_int_array(cJSON *i_array, int **o_out, size_t *o_out_len);
static int parse_general(cJSON *i_general, configuration *io_config, ext_functions_t *i_funcs);
static int parse_phases(cJSON *i_phases, configuration *io_config, ext_functions_t *i_funcs);
static int parse_groups(cJSON *i_groups, configuration *io_config);
static int parse_stage(cJSON *i_stage_json, stage_t *o_stage);

/**
 * @brief Read an entire file into a NUL-terminated buffer.
 *
 * @param[in]  i_file_name Path to the file.
 * @param[out] o_out_len   Receives the byte length (excluding the NUL).
 * @return Allocated buffer, or @c NULL on failure. Caller must @c free it.
 */
static char *read_file_contents(const char *i_file_name, long *o_out_len) {
  FILE *file;
  char *buffer;
  long length;

  file = fopen(i_file_name, "rb");
  if (file == NULL) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  length = ftell(file);
  if (length < 0) {
    fclose(file);
    return NULL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  buffer = malloc((size_t)length + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL;
  }

  if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
    free(buffer);
    fclose(file);
    return NULL;
  }

  buffer[length] = '\0';
  fclose(file);
  *o_out_len = length;
  return buffer;
}

/**
 * @brief Free a partially filled configuration after a parse error.
 * @param[in,out] io_config Configuration to free (may be @c NULL).
 */
static void free_config_partial(configuration *io_config) {
  size_t i;

  if (io_config == NULL) {
    return;
  }

  if (io_config->phases != NULL) {
    for (i = 0; i < io_config->n_phases; i++) {
      free(io_config->phases[i].stages);
    }
    free(io_config->phases);
  }

  if (io_config->groups != NULL) {
    for (i = 0; i < io_config->n_groups; i++) {
      free(io_config->groups[i].ss);
      free(io_config->groups[i].rs);
    }
    free(io_config->groups);
  }

  free(io_config);
}

/**
 * @brief Require a named child object or array under a JSON parent.
 *
 * @param[in] i_parent       Parent JSON object.
 * @param[in] i_key          Child key name.
 * @param[in] i_expect_array Non-zero if the child must be an array; else object.
 * @return Child node, or @c NULL (with an error message) on failure.
 */
static cJSON *require_child(cJSON *i_parent, const char *i_key, int i_expect_array) {
  cJSON *child;

  child = cJSON_GetObjectItemCaseSensitive(i_parent, i_key);
  if (child == NULL) {
    fprintf(stderr, "JSON config: missing '%s'\n", i_key);
    return NULL;
  }

  if (i_expect_array && !cJSON_IsArray(child)) {
    fprintf(stderr, "JSON config: '%s' must be an array\n", i_key);
    return NULL;
  }

  if (!i_expect_array && !cJSON_IsObject(child)) {
    fprintf(stderr, "JSON config: '%s' must be an object\n", i_key);
    return NULL;
  }

  return child;
}

/**
 * @brief Require a numeric field and store it as @c size_t.
 * @param[in]  i_obj Parent object.
 * @param[in]  i_key Field name.
 * @param[out] o_out Receives the value.
 * @return 1 on success, 0 on missing/invalid field.
 */
static int require_size_t(cJSON *i_obj, const char *i_key, size_t *o_out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(i_obj, i_key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", i_key);
    return 0;
  }

  *o_out = (size_t)cJSON_GetNumberValue(item);
  return 1;
}

/**
 * @brief Require a numeric field and store it as @c int.
 * @param[in]  i_obj Parent object.
 * @param[in]  i_key Field name.
 * @param[out] o_out Receives the value.
 * @return 1 on success, 0 on missing/invalid field.
 */
static int require_int(cJSON *i_obj, const char *i_key, int *o_out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(i_obj, i_key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", i_key);
    return 0;
  }

  *o_out = (int)cJSON_GetNumberValue(item);
  return 1;
}

/**
 * @brief Optionally read a numeric field as @c int.
 *
 * If @p i_key is absent, returns success without writing @p o_out (the
 * caller must have initialised the destination).
 *
 * @param[in]  i_obj Parent object.
 * @param[in]  i_key Field name.
 * @param[out] o_out Receives the value when the key is present.
 * @return 1 on success (missing or valid), 0 if present but not a number.
 */
static int optional_int(cJSON *i_obj, const char *i_key, int *o_out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(i_obj, i_key);
  if (item == NULL) {
    return 1;
  }

  if (!cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: invalid number '%s'\n", i_key);
    return 0;
  }

  *o_out = (int)cJSON_GetNumberValue(item);
  return 1;
}

/**
 * @brief Require a numeric field and store it as @c double.
 * @param[in]  i_obj Parent object.
 * @param[in]  i_key Field name.
 * @param[out] o_out Receives the value.
 * @return 1 on success, 0 on missing/invalid field.
 */
static int require_double(cJSON *i_obj, const char *i_key, double *o_out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(i_obj, i_key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", i_key);
    return 0;
  }

  *o_out = cJSON_GetNumberValue(item);
  return 1;
}

/**
 * @brief Copy a non-empty JSON integer array into a new C array.
 *
 * @param[in]  i_array   JSON array of numbers.
 * @param[out] o_out     Receives the allocated array (caller frees).
 * @param[out] o_out_len Receives the element count.
 * @return 1 on success, 0 on failure.
 */
static int copy_int_array(cJSON *i_array, int **o_out, size_t *o_out_len) {
  int i, count;
  cJSON *item;

  if (!cJSON_IsArray(i_array)) {
    fprintf(stderr, "JSON config: expected integer array\n");
    return 0;
  }

  count = cJSON_GetArraySize(i_array);
  if (count == 0) {
    fprintf(stderr, "JSON config: integer array cannot be empty\n");
    return 0;
  }

  *o_out = malloc((size_t)count * sizeof(int));
  if (*o_out == NULL) {
    fprintf(stderr, "JSON config: memory allocation failed\n");
    return 0;
  }

  for (i = 0; i < count; i++) {
    item = cJSON_GetArrayItem(i_array, i);
    if (item == NULL || !cJSON_IsNumber(item)) {
      fprintf(stderr, "JSON config: invalid integer at array index %d\n", i);
      free(*o_out);
      *o_out = NULL;
      return 0;
    }
    (*o_out)[i] = (int)cJSON_GetNumberValue(item);
  }

  *o_out_len = (size_t)count;
  return 1;
}

/**
 * @brief Parse the @c general object into global configuration fields.
 *
 * Sets @c Total_Resizes / @c Total_Phases and invokes allocation callbacks,
 * then reads @c SDR, @c ADR, @c Datasize, @c Rigid, and @c Capture_Method.
 *
 * @param[in]     i_general JSON @c general object.
 * @param[in,out] io_config Configuration being filled.
 * @param[in]     i_funcs   Allocation callbacks.
 * @return 1 on success, 0 on failure.
 */
static int parse_general(cJSON *i_general, configuration *io_config, ext_functions_t *i_funcs) {
  if (!require_size_t(i_general, "Total_Resizes", &io_config->n_resizes)) {
    return 0;
  }

  io_config->n_groups = io_config->n_resizes + 1;
  i_funcs->resizes_f(io_config);

  if (!require_size_t(i_general, "Total_Phases", &io_config->n_phases)) {
    return 0;
  }
  i_funcs->phases_f(io_config);

  if (!require_size_t(i_general, "SDR", &io_config->sdr)) {
    return 0;
  }
  if (!require_size_t(i_general, "ADR", &io_config->adr)) {
    return 0;
  }
  if (!require_size_t(i_general, "Datasize", &io_config->datasize)) {
    return 0;
  }
  if (!require_int(i_general, "Rigid", &io_config->rigid_times)) {
    return 0;
  }
  if (!require_int(i_general, "Capture_Method", &io_config->capture_method)) {
    return 0;
  }

  return 1;
}

/**
 * @brief Parse one stage object into a ::stage_t.
 *
 * Required: @c Stage_Type, @c Stage_Bytes, @c Stage_Time.
 * Optional: @c Granularity, @c Stage_Time_Capped, @c Stage_Identifier,
 * @c Stage_Involved_Procs (missing keys leave the field unchanged).
 *
 * @param[in]  i_stage_json Stage JSON object.
 * @param[out] o_stage      Stage structure to fill.
 * @return 1 on success, 0 on failure.
 */
static int parse_stage(cJSON *i_stage_json, stage_t *o_stage) {
  double double_value;

  if (!require_int(i_stage_json, "Stage_Type", &o_stage->pt)) {
    return 0;
  }
  if (!require_int(i_stage_json, "Stage_Bytes", &o_stage->bytes)) {
    return 0;
  }
  if (!require_double(i_stage_json, "Stage_Time", &double_value)) {
    return 0;
  }
  o_stage->t_stage = (float)double_value;

  if (!optional_int(i_stage_json, "Granularity", &o_stage->granularity)) {
    return 0;
  }
  if (!optional_int(i_stage_json, "Stage_Time_Capped", &o_stage->t_capped)) {
    return 0;
  }
  if (!optional_int(i_stage_json, "Stage_Identifier", &o_stage->id)) {
    return 0;
  }
  if (!optional_int(i_stage_json, "Stage_Involved_Procs", &o_stage->involved_procs)) {
    return 0;
  }

  return 1;
}

/**
 * @brief Parse the @c phases array into @p io_config.
 *
 * Array length must equal @c n_phases. For each phase, allocates stages via
 * @p i_funcs and parses nested @c stages.
 *
 * @param[in]     i_phases  JSON phases array.
 * @param[in,out] io_config Configuration being filled.
 * @param[in]     i_funcs   Allocation callbacks.
 * @return 1 on success, 0 on failure.
 */
static int parse_phases(cJSON *i_phases, configuration *io_config, ext_functions_t *i_funcs) {
  int phase_index, stage_index, phase_count, stage_count;
  cJSON *phase_json, *stages_json, *stage_json;
  phase_t *phase;

  if (!cJSON_IsArray(i_phases)) {
    fprintf(stderr, "JSON config: 'phases' must be an array\n");
    return 0;
  }

  phase_count = cJSON_GetArraySize(i_phases);
  if ((size_t)phase_count != io_config->n_phases) {
    fprintf(stderr, "JSON config: expected %zu phases, found %d\n", io_config->n_phases, phase_count);
    return 0;
  }

  for (phase_index = 0; phase_index < phase_count; phase_index++) {
    phase_json = cJSON_GetArrayItem(i_phases, phase_index);
    if (phase_json == NULL || !cJSON_IsObject(phase_json)) {
      fprintf(stderr, "JSON config: invalid phase at index %d\n", phase_index);
      return 0;
    }

    phase = io_config->phases + phase_index;
    if (!require_size_t(phase_json, "Total_Iters", &phase->qty_iters)) {
      return 0;
    }
    if (!require_size_t(phase_json, "Total_Stages", &phase->qty_stages)) {
      return 0;
    }

    i_funcs->stages_f(io_config, (size_t)phase_index);

    stages_json = require_child(phase_json, "stages", 1);
    if (stages_json == NULL) {
      return 0;
    }

    stage_count = cJSON_GetArraySize(stages_json);
    if ((size_t)stage_count != phase->qty_stages) {
      fprintf(stderr, "JSON config: phase %d expected %zu stages, found %d\n",
              phase_index, phase->qty_stages, stage_count);
      return 0;
    }

    for (stage_index = 0; stage_index < stage_count; stage_index++) {
      stage_json = cJSON_GetArrayItem(stages_json, stage_index);
      if (stage_json == NULL || !cJSON_IsObject(stage_json)) {
        fprintf(stderr, "JSON config: invalid stage at phase %d index %d\n",
                phase_index, stage_index);
        return 0;
      }

      if (!parse_stage(stage_json, phase->stages + stage_index)) {
        return 0;
      }
    }
  }

  return 1;
}

/**
 * @brief Parse the @c groups array into @p io_config.
 *
 * Array length must equal @c n_groups. Reads process counts, spawn/redistribution
 * methods and strategies, and physical distribution (@c compact / @c spread).
 *
 * @param[in]     i_groups  JSON groups array.
 * @param[in,out] io_config Configuration being filled.
 * @return 1 on success, 0 on failure.
 */
static int parse_groups(cJSON *i_groups, configuration *io_config) {
  int group_index, group_count;
  cJSON *group_json, *strategy_json;
  group_config_t *group;
  const char *dist_value;
  double factor_value;

  if (!cJSON_IsArray(i_groups)) {
    fprintf(stderr, "JSON config: 'groups' must be an array\n");
    return 0;
  }

  group_count = cJSON_GetArraySize(i_groups);
  if ((size_t)group_count != io_config->n_groups) {
    fprintf(stderr, "JSON config: expected %zu groups, found %d\n", io_config->n_groups, group_count);
    return 0;
  }

  for (group_index = 0; group_index < group_count; group_index++) {
    group_json = cJSON_GetArrayItem(i_groups, group_index);
    if (group_json == NULL || !cJSON_IsObject(group_json)) {
      fprintf(stderr, "JSON config: invalid group at index %d\n", group_index);
      return 0;
    }

    group = io_config->groups + group_index;
    if (!require_int(group_json, "Iters", &group->iters)) {
      return 0;
    }
    if (!require_int(group_json, "Procs", &group->procs)) {
      return 0;
    }
    if (!require_double(group_json, "FactorS", &factor_value)) {
      return 0;
    }
    group->factor = (float)factor_value;

    strategy_json = cJSON_GetObjectItemCaseSensitive(group_json, "Dist");
    if (strategy_json == NULL || !cJSON_IsString(strategy_json)) {
      fprintf(stderr, "JSON config: missing or invalid string 'Dist' in group %d\n", group_index);
      return 0;
    }

    dist_value = strategy_json->valuestring;
    group->phy_dist = MAM_PHY_DIST_COMPACT;
    if (strcmp(dist_value, "spread") == 0) {
      group->phy_dist = MAM_PHY_DIST_SPREAD;
    } else if (strcmp(dist_value, "compact") != 0) {
      fprintf(stderr, "JSON config: invalid Dist '%s' in group %d (use 'compact' or 'spread')\n",
              dist_value, group_index);
      return 0;
    }

    if (!require_int(group_json, "Redistribution_Method", &group->rm)) {
      return 0;
    }
    if (!require_int(group_json, "Spawn_Method", &group->sm)) {
      return 0;
    }

    strategy_json = cJSON_GetObjectItemCaseSensitive(group_json, "Redistribution_Strategy");
    if (!copy_int_array(strategy_json, &group->rs, &group->rs_len)) {
      return 0;
    }

    strategy_json = cJSON_GetObjectItemCaseSensitive(group_json, "Spawn_Strategy");
    if (!copy_int_array(strategy_json, &group->ss, &group->ss_len)) {
      return 0;
    }
  }

  return 1;
}

configuration *read_json_file(char *i_file_name, ext_functions_t i_init_functions) {
  configuration *config;
  char *file_contents;
  long file_length;
  cJSON *root, *general, *phases, *groups;
  const char *error_ptr;

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
  config->groups = NULL;
  config->phases = NULL;

  file_contents = read_file_contents(i_file_name, &file_length);
  if (file_contents == NULL) {
    printf("Can't load '%s'\n", i_file_name);
    free(config);
    return NULL;
  }

  root = cJSON_ParseWithLength(file_contents, (size_t)file_length);
  free(file_contents);
  if (root == NULL) {
    error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      fprintf(stderr, "JSON parse error before: %s\n", error_ptr);
    }
    printf("Can't parse '%s'\n", i_file_name);
    free(config);
    return NULL;
  }

  general = require_child(root, "general", 0);
  phases = require_child(root, "phases", 1);
  groups = require_child(root, "groups", 1);
  if (general == NULL || phases == NULL || groups == NULL ||
      !parse_general(general, config, &i_init_functions) ||
      !parse_phases(phases, config, &i_init_functions) ||
      !parse_groups(groups, config)) {
    cJSON_Delete(root);
    free_config_partial(config);
    return NULL;
  }

  cJSON_Delete(root);
  return config;
}
