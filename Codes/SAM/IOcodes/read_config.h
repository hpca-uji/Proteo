#ifndef READ_CONFIG_H
#define READ_CONFIG_H

#include "Main_datatypes.h"

typedef void (*Malloc_conf)(configuration *user_config);
typedef void (*Malloc_conf_index)(configuration *user_config, size_t index);
typedef struct {
  Malloc_conf resizes_f, phases_f;
  Malloc_conf_index stages_f;
} ext_functions_t;

#endif
