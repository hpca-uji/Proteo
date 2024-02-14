#include "MAM_Configuration.h"
#include "MAM_Init_Configuration.h"
#include "malleabilityDataStructures.h"
#include <limits.h>

typedef struct {
    unsigned int *value, default_value;
    int config_max_length;
    union {
      int (*set_config_simple)(unsigned int, unsigned int *);
      int (*set_config_complex)(unsigned int);
    };
    char *env_name;
} mam_config_setting_t;

int MAM_I_set_method(unsigned int new_method, unsigned int *method);
int MAM_I_set_spawn_strat(unsigned int strategy, unsigned int *strategies);
int MAM_I_set_red_strat(unsigned int strategy, unsigned int *strategies);
int MAM_I_set_target_number(unsigned int new_numC);

int MAM_I_configuration_get_defaults();

int MAM_I_contains_strat(unsigned int comm_strategies, unsigned int strategy);
int MAM_I_add_strat(unsigned int *comm_strategies, unsigned int strategy);
int MAM_I_remove_strat(unsigned int *comm_strategies, unsigned int strategy);

mam_config_setting_t configSettings[] = { 
    {NULL, MALL_SPAWN_MERGE, MAM_METHODS_SPAWN_LEN, {.set_config_simple = MAM_I_set_method }, MAM_SPAWN_METHOD_ENV},
    {NULL, MAM_STRAT_SPAWN_CLEAR, MAM_STRATS_SPAWN_LEN, {.set_config_simple = MAM_I_set_spawn_strat }, MAM_SPAWN_STRATS_ENV},
    {NULL, MALL_DIST_COMPACT, MAM_METHODS_PHYSICAL_DISTRIBUTION_LEN, {.set_config_simple = MAM_I_set_method }, MAM_PHYSICAL_DISTRIBUTION_METHOD_ENV},
    {NULL, MALL_RED_BASELINE, MAM_METHODS_RED_LEN, {.set_config_simple = MAM_I_set_method }, MAM_RED_METHOD_ENV},
    {NULL, MAM_STRAT_RED_CLEAR, MAM_STRATS_RED_LEN, {.set_config_simple = MAM_I_set_red_strat }, MAM_RED_STRATS_ENV},

    {NULL, 1, INT_MAX, {.set_config_complex = MAM_I_set_target_number }, MAM_NUM_TARGETS_ENV}
};

unsigned int masks_spawn[] = {MAM_STRAT_CLEAR_VALUE, MAM_MASK_PTHREAD, MAM_MASK_SPAWN_SINGLE, MAM_MASK_SPAWN_INTERCOMM};
unsigned int masks_red[] = {MAM_STRAT_CLEAR_VALUE, MAM_MASK_PTHREAD, MAM_MASK_RED_WAIT_SOURCES, MAM_MASK_RED_WAIT_TARGETS};

/**
 * @brief Set configuration parameters for MAM.
 *
 * This function allows setting various configuration parameters for MAM
 * such as spawn method, spawn strategies, spawn physical distribution, 
 * redistribution method, and red strategies.
 *
 * @param spawn_method The spawn method reconfiguration.
 * @param spawn_strategies The spawn strategies reconfiguration.
 * @param spawn_dist The spawn physical distribution method reconfiguration.
 * @param red_method The redistribution method reconfiguration.
 * @param red_strategies The redesitribution strategy for reconfiguration.
 */
void MAM_Set_configuration(int spawn_method, int spawn_strategies, int spawn_dist, int red_method, int red_strategies) {
  int i, aux;
  int aux_array[] = {spawn_method, spawn_strategies, spawn_dist, red_method, red_strategies};
  if(state > MALL_NOT_STARTED) return;

  mam_config_setting_t *config = NULL;
  for (i = 0; i < MAM_KEY_COUNT-1; i++) { //FIXME Numero magico para no cambiar num_targets
    aux = aux_array[i];
    config = &configSettings[i];
    if (0 <= aux && aux < config->config_max_length) {
      if(i == MAM_NUM_TARGETS) {
        config->set_config_complex(aux);
      } else {
        config->set_config_simple(aux, config->value);
      }
    } 
  }
}

/*
 * @brief Set the configuration value for a specific key in MAM.
 *
 * Modifies the configuration value associated with the given key
 * to the specified "required" value. The final value set is returned in the
 * "provided" parameter.
 *
 * @param key       The key for which the configuration value is to be modified.
 * @param required  The required value to set for the specified key.
 * @param provided  Pointer to an integer where the final value set will be stored.
 *                  This parameter is updated with the actual value after modification.
 *                  For strategy keys the value is "MAM_STRATS_ADDED" if "required" has 
 *                  been added, or "MAM_STRATS_MODIFIED" if multiple strategies of the
 *                  key have been modified.
 */
void MAM_Set_key_configuration(int key, int required, int *provided) {
  int i, aux;

  if(provided == NULL) provided = &aux;
  *provided = MALL_DENIED;
  if(required < 0 || state > MALL_NOT_STARTED) return;

  mam_config_setting_t *config = NULL;
  for (i = 0; i < MAM_KEY_COUNT; i++) {
    if (key == i) {
      config = &configSettings[i];
      break;
    }
  }

  if (config != NULL) {
    if (required < config->config_max_length) {
      if(i == MAM_NUM_TARGETS) {
        *provided = config->set_config_complex(required);
      } else {
        *provided = config->set_config_simple(required, config->value);
      }
    } else {*provided = *(config->value); }
  } else { printf("MAM: Key %d does not exist\n", key); }
}

/*
 * Retorna si una estrategia aparece o no
 */
int MAM_Contains_strat(int key, unsigned int strategy, int *result) {
  int strategies, aux = MAM_OK;
  unsigned int len = 0, mask;

  switch(key) {
    case MAM_SPAWN_STRATEGIES:
      strategies = mall_conf->spawn_strategies;
      mask = masks_spawn[strategy];
      len = MAM_STRATS_SPAWN_LEN;
      break;
    case MAM_RED_STRATEGIES:
      strategies = mall_conf->red_strategies;
      mask = masks_red[strategy];
      len = MAM_STRATS_RED_LEN;
      break;
    default:
      aux = MALL_DENIED;
      break;
  }

  if(aux == MAM_OK && strategy < len) {
    aux = MAM_I_contains_strat(strategies, mask);
  } else {
    aux = 0;
  }

  if(result != NULL) *result = aux;
  return aux;
}

/*
 * //TODO
 * Tiene que ser llamado despues de setear la config
 */
int MAM_Set_target_number(unsigned int numC){
  return MAM_I_set_target_number(numC);
}

//======================================================||
//===============MAM_INIT FUNCTIONS=====================||
//======================================================||
//======================================================||

void MAM_Init_configuration() {
  if(mall == NULL || mall_conf == NULL) {
    printf("MAM FATAL ERROR: Setting initial config without previous mallocs\n");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, -50);
  }

  configSettings[MAM_SPAWN_METHOD].value = &mall_conf->spawn_method;
  configSettings[MAM_SPAWN_STRATEGIES].value = &mall_conf->spawn_strategies;
  configSettings[MAM_PHYSICAL_DISTRIBUTION].value = &mall_conf->spawn_dist;
  configSettings[MAM_RED_METHOD].value = &mall_conf->red_method;
  configSettings[MAM_RED_STRATEGIES].value = &mall_conf->red_strategies;
}

void MAM_Set_initial_configuration() {
  int not_filled = 1;
  
  not_filled = MAM_I_configuration_get_defaults();
  if(not_filled) {
    if(mall->myId == mall->root) printf("MAM WARNING: Starting configuration not set\n");
    fflush(stdout);
    MPI_Abort(mall->comm, -50);
  }

  #if USE_MAL_DEBUG >= 2
    if(mall->myId == mall->root) {
      DEBUG_FUNC("Initial configuration settled", mall->myId, mall->numP); 
      fflush(stdout); 
    }
  #endif
}

void MAM_Check_configuration() {
  if(mall_conf->spawn_method == MALL_SPAWN_MERGE && MAM_I_contains_strat(mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM)) {
    MAM_I_remove_strat(&mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM);
  }
  if(mall_conf->red_method == MALL_RED_RMA_LOCK || mall_conf->red_method == MALL_RED_RMA_LOCKALL) {
    if(MAM_I_contains_strat(mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM)) {
      MAM_I_remove_strat(&mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM);
    }
    if(!MAM_I_contains_strat(mall_conf->red_strategies, MAM_MASK_RED_WAIT_TARGETS) &&
       !MAM_I_contains_strat(mall_conf->red_strategies, MAM_MASK_PTHREAD)) {
      MAM_I_set_red_strat(MAM_STRAT_RED_WAIT_TARGETS, &mall_conf->red_strategies);
    }
  }

  if(mall->numC == mall->numP) { // Migrate
    MAM_Set_key_configuration(MAM_SPAWN_METHOD, MALL_SPAWN_BASELINE, NULL);
  }
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//======================================================||
//======================================================||


int MAM_I_configuration_get_defaults() {
  size_t i;
  int set_value;
  char *tmp = NULL;
  
  mam_config_setting_t *config = NULL;
  for (i = 0; i < MAM_KEY_COUNT; i++) {
    config = &configSettings[i];
    tmp = getenv(config->env_name);

    if(tmp != NULL) {
      set_value = atoi(tmp);
    } else {
      set_value = config->default_value;
    }

    if (0 <= set_value && set_value < config->config_max_length) {
      if(i == MAM_NUM_TARGETS) {
        config->set_config_complex(set_value);
      } else {
        config->set_config_simple(set_value, config->value);
      }
    }
    tmp = NULL;
  }
  return 0;
}


int MAM_I_set_method(unsigned int new_method, unsigned int *method) {
  *method = new_method;
  return *method;
}

int MAM_I_set_spawn_strat(unsigned int strategy, unsigned int *strategies) {
  int result = 0;
  int strat_removed = 0;

  switch(strategy) {
    case MAM_STRAT_SPAWN_CLEAR:
      *strategies = MAM_STRAT_CLEAR_VALUE;
      result = MAM_STRATS_MODIFIED;
      break;
    case MAM_STRAT_SPAWN_PTHREAD:
      result = MAM_I_add_strat(strategies, MAM_MASK_PTHREAD);
      break;
    case MAM_STRAT_SPAWN_SINGLE:
      result = MAM_I_add_strat(strategies, MAM_MASK_SPAWN_SINGLE);
      break;
    case MAM_STRAT_SPAWN_INTERCOMM:
      result = MAM_I_add_strat(strategies, MAM_MASK_SPAWN_INTERCOMM);
      break;
    default:
      //Unkown strategy
      result = MALL_DENIED;
      break;
  }

  if(strat_removed) {
    result = MAM_STRATS_MODIFIED;
  }
  return result;
}

int MAM_I_set_red_strat(unsigned int strategy, unsigned int *strategies) {
  int result = 0;
  int strat_removed = 0;

  switch(strategy) {
    case MAM_STRAT_RED_CLEAR:
      *strategies = MAM_STRAT_CLEAR_VALUE;
      result = MAM_STRATS_MODIFIED;
      break;
    case MAM_STRAT_RED_PTHREAD: //TODO - IMPROVEMENT - This could be done with a single operation instead of 3.
      result = MAM_I_add_strat(strategies, MAM_MASK_PTHREAD);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_RED_WAIT_SOURCES);
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_RED_WAIT_TARGETS);
      }
      break;
    case MAM_STRAT_RED_WAIT_SOURCES:
      result = MAM_I_add_strat(strategies, MAM_MASK_RED_WAIT_SOURCES);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_RED_WAIT_TARGETS);
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_PTHREAD);
      }
      break;
    case MAM_STRAT_RED_WAIT_TARGETS:
      result = MAM_I_add_strat(strategies, MAM_MASK_RED_WAIT_TARGETS);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_RED_WAIT_SOURCES);
        strat_removed += MAM_I_remove_strat(strategies, MAM_MASK_PTHREAD);
      }
      break;
    default:
      //Unkown strategy
      result = MALL_DENIED;
      break;
  }

  if(strat_removed) {
    result = MAM_STRATS_MODIFIED;
  }
  return result;
}

int MAM_I_set_target_number(unsigned int new_numC) {
  if(state > MALL_NOT_STARTED || new_numC == 0) return MALL_DENIED;

  mall->numC = (int) new_numC;
  return new_numC;
}


/*
 * Returns 1 if strategy is applied, 0 otherwise
 */
int MAM_I_contains_strat(unsigned int comm_strategies, unsigned int strategy) {
  return comm_strategies & strategy;
}

int MAM_I_add_strat(unsigned int *comm_strategies, unsigned int strategy) {
  if(MAM_I_contains_strat(*comm_strategies, strategy)) return MAM_OK;
  *comm_strategies |= strategy;
  return MAM_STRATS_ADDED;
}

int MAM_I_remove_strat(unsigned int *comm_strategies, unsigned int strategy) {
  if(!MAM_I_contains_strat(*comm_strategies, strategy)) return MAM_OK;
  *comm_strategies &= ~strategy;
  return MAM_STRATS_MODIFIED;
}
