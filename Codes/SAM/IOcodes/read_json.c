#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "read_json.h"
#include "cJSON.h"
#include "MAM.h"

static char *read_file_contents(const char *file_name, long *out_len);
static void free_config_partial(configuration *config);
static cJSON *require_child(cJSON *parent, const char *key, int expect_array);
static int require_size_t(cJSON *obj, const char *key, size_t *out);
static int require_int(cJSON *obj, const char *key, int *out);
static int optional_int(cJSON *obj, const char *key, int *out);
static int require_double(cJSON *obj, const char *key, double *out);
static int copy_int_array(cJSON *array, int **out, size_t *out_len);
static int parse_general(cJSON *general, configuration *config, ext_functions_t *funcs);
static int parse_phases(cJSON *phases, configuration *config, ext_functions_t *funcs);
static int parse_groups(cJSON *groups, configuration *config);
static int parse_stage(cJSON *stage_json, stage_t *stage);

static char *read_file_contents(const char *file_name, long *out_len) {
  FILE *file;
  char *buffer;
  long length;

  file = fopen(file_name, "rb");
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
  *out_len = length;
  return buffer;
}

static void free_config_partial(configuration *config) {
  size_t i;

  if (config == NULL) {
    return;
  }

  if (config->phases != NULL) {
    for (i = 0; i < config->n_phases; i++) {
      free(config->phases[i].stages);
    }
    free(config->phases);
  }

  if (config->groups != NULL) {
    for (i = 0; i < config->n_groups; i++) {
      free(config->groups[i].ss);
      free(config->groups[i].rs);
    }
    free(config->groups);
  }

  free(config);
}

static cJSON *require_child(cJSON *parent, const char *key, int expect_array) {
  cJSON *child;

  child = cJSON_GetObjectItemCaseSensitive(parent, key);
  if (child == NULL) {
    fprintf(stderr, "JSON config: missing '%s'\n", key);
    return NULL;
  }

  if (expect_array && !cJSON_IsArray(child)) {
    fprintf(stderr, "JSON config: '%s' must be an array\n", key);
    return NULL;
  }

  if (!expect_array && !cJSON_IsObject(child)) {
    fprintf(stderr, "JSON config: '%s' must be an object\n", key);
    return NULL;
  }

  return child;
}

static int require_size_t(cJSON *obj, const char *key, size_t *out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", key);
    return 0;
  }

  *out = (size_t)cJSON_GetNumberValue(item);
  return 1;
}

static int require_int(cJSON *obj, const char *key, int *out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", key);
    return 0;
  }

  *out = (int)cJSON_GetNumberValue(item);
  return 1;
}

static int optional_int(cJSON *obj, const char *key, int *out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (item == NULL) {
    return 1;
  }

  if (!cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: invalid number '%s'\n", key);
    return 0;
  }

  *out = (int)cJSON_GetNumberValue(item);
  return 1;
}

static int require_double(cJSON *obj, const char *key, double *out) {
  cJSON *item;

  item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (item == NULL || !cJSON_IsNumber(item)) {
    fprintf(stderr, "JSON config: missing or invalid number '%s'\n", key);
    return 0;
  }

  *out = cJSON_GetNumberValue(item);
  return 1;
}

static int copy_int_array(cJSON *array, int **out, size_t *out_len) {
  int i, count;
  cJSON *item;

  if (!cJSON_IsArray(array)) {
    fprintf(stderr, "JSON config: expected integer array\n");
    return 0;
  }

  count = cJSON_GetArraySize(array);
  if (count == 0) {
    fprintf(stderr, "JSON config: integer array cannot be empty\n");
    return 0;
  }

  *out = malloc((size_t)count * sizeof(int));
  if (*out == NULL) {
    fprintf(stderr, "JSON config: memory allocation failed\n");
    return 0;
  }

  for (i = 0; i < count; i++) {
    item = cJSON_GetArrayItem(array, i);
    if (item == NULL || !cJSON_IsNumber(item)) {
      fprintf(stderr, "JSON config: invalid integer at array index %d\n", i);
      free(*out);
      *out = NULL;
      return 0;
    }
    (*out)[i] = (int)cJSON_GetNumberValue(item);
  }

  *out_len = (size_t)count;
  return 1;
}

static int parse_general(cJSON *general, configuration *config, ext_functions_t *funcs) {
  if (!require_size_t(general, "Total_Resizes", &config->n_resizes)) {
    return 0;
  }

  config->n_groups = config->n_resizes + 1;
  funcs->resizes_f(config);

  if (!require_size_t(general, "Total_Phases", &config->n_phases)) {
    return 0;
  }
  funcs->phases_f(config);

  if (!require_size_t(general, "SDR", &config->sdr)) {
    return 0;
  }
  if (!require_size_t(general, "ADR", &config->adr)) {
    return 0;
  }
  if (!require_size_t(general, "Datasize", &config->datasize)) {
    return 0;
  }
  if (!require_int(general, "Rigid", &config->rigid_times)) {
    return 0;
  }
  if (!require_int(general, "Capture_Method", &config->capture_method)) {
    return 0;
  }

  return 1;
}

static int parse_stage(cJSON *stage_json, stage_t *stage) {
  double double_value;

  if (!require_int(stage_json, "Stage_Type", &stage->pt)) {
    return 0;
  }
  if (!require_int(stage_json, "Stage_Bytes", &stage->bytes)) {
    return 0;
  }
  if (!require_double(stage_json, "Stage_Time", &double_value)) {
    return 0;
  }
  stage->t_stage = (float)double_value;

  if (!optional_int(stage_json, "Granularity", &stage->granularity)) {
    return 0;
  }
  if (!optional_int(stage_json, "Stage_Time_Capped", &stage->t_capped)) {
    return 0;
  }
  if (!optional_int(stage_json, "Stage_Identifier", &stage->id)) {
    return 0;
  }
  if (!optional_int(stage_json, "Stage_Involved_Procs", &stage->involved_procs)) {
    return 0;
  }

  return 1;
}

static int parse_phases(cJSON *phases, configuration *config, ext_functions_t *funcs) {
  int phase_index, stage_index, phase_count, stage_count;
  cJSON *phase_json, *stages_json, *stage_json;
  phase_t *phase;

  if (!cJSON_IsArray(phases)) {
    fprintf(stderr, "JSON config: 'phases' must be an array\n");
    return 0;
  }

  phase_count = cJSON_GetArraySize(phases);
  if ((size_t)phase_count != config->n_phases) {
    fprintf(stderr, "JSON config: expected %zu phases, found %d\n", config->n_phases, phase_count);
    return 0;
  }

  for (phase_index = 0; phase_index < phase_count; phase_index++) {
    phase_json = cJSON_GetArrayItem(phases, phase_index);
    if (phase_json == NULL || !cJSON_IsObject(phase_json)) {
      fprintf(stderr, "JSON config: invalid phase at index %d\n", phase_index);
      return 0;
    }

    phase = config->phases + phase_index;
    if (!require_size_t(phase_json, "Total_Iters", &phase->qty_iters)) {
      return 0;
    }
    if (!require_size_t(phase_json, "Total_Stages", &phase->qty_stages)) {
      return 0;
    }

    funcs->stages_f(config, (size_t)phase_index);

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

static int parse_groups(cJSON *groups, configuration *config) {
  int group_index, group_count;
  cJSON *group_json, *strategy_json;
  group_config_t *group;
  const char *dist_value;
  double factor_value;

  if (!cJSON_IsArray(groups)) {
    fprintf(stderr, "JSON config: 'groups' must be an array\n");
    return 0;
  }

  group_count = cJSON_GetArraySize(groups);
  if ((size_t)group_count != config->n_groups) {
    fprintf(stderr, "JSON config: expected %zu groups, found %d\n", config->n_groups, group_count);
    return 0;
  }

  for (group_index = 0; group_index < group_count; group_index++) {
    group_json = cJSON_GetArrayItem(groups, group_index);
    if (group_json == NULL || !cJSON_IsObject(group_json)) {
      fprintf(stderr, "JSON config: invalid group at index %d\n", group_index);
      return 0;
    }

    group = config->groups + group_index;
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

configuration *read_json_file(char *file_name, ext_functions_t init_functions) {
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

  file_contents = read_file_contents(file_name, &file_length);
  if (file_contents == NULL) {
    printf("Can't load '%s'\n", file_name);
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
    printf("Can't parse '%s'\n", file_name);
    free(config);
    return NULL;
  }

  general = require_child(root, "general", 0);
  phases = require_child(root, "phases", 1);
  groups = require_child(root, "groups", 1);
  if (general == NULL || phases == NULL || groups == NULL ||
      !parse_general(general, config, &init_functions) ||
      !parse_phases(phases, config, &init_functions) ||
      !parse_groups(groups, config)) {
    cJSON_Delete(root);
    free_config_partial(config);
    return NULL;
  }

  cJSON_Delete(root);
  return config;
}
