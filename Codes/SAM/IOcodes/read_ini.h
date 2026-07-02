#ifndef READ_INI_H
#define READ_INI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Main_datatypes.h"

typedef void (*Malloc_conf)(configuration* user_config);
typedef void (*Malloc_conf_index)(configuration* user_config, size_t index);
typedef struct {
  Malloc_conf resizes_f, phases_f;
  Malloc_conf_index stages_f;
} ext_functions_t;

configuration *read_ini_file(char *file_name, ext_functions_t init_functions);

#endif
