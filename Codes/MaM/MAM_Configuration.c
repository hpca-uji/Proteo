#include "MAM_Configuration.h"
#include "MAM_Init_Configuration.h"
#include "MAM_DataStructures.h"
#include <limits.h>

/**
 * @file MAM_Configuration.c
 * @brief Implementation of the MaM configuration table (spawn/redistribution
 * methods and strategy bitmasks) and its environment/runtime setters.
 */

/**
 * @brief Row of the internal configuration table (::configSettings).
 *
 * Each entry describes one ::mam_key_values configuration key: where its
 * current value is stored, its default/limit, the setter used to validate
 * and apply new values, and the environment variable used to seed it at
 * start-up.
 */
typedef struct {
    unsigned int *value; /**< Pointer to the live configuration value (set by ::MAM_Init_configuration). */
    unsigned int default_value; /**< Default value used when the environment variable is unset. */
    int config_max_length; /**< Exclusive upper bound for valid values of this key. */
    union {
      int (*set_config_simple)(unsigned int, unsigned int *); /**< Setter for keys that write through @c value (method/strategy keys). */
      int (*set_config_complex)(unsigned int); /**< Setter for keys that do not use @c value directly (e.g. target number). */
    };
    char *env_name; /**< Name of the environment variable used to seed this key. */
} mam_config_setting_t;

/**
 * @brief Overwrite a method configuration value.
 *
 * @param[in]  i_new_method Method value to store.
 * @param[out] o_method     Receives @p i_new_method.
 * @return The stored value (@p i_new_method).
 */
int MAM_I_set_method(unsigned int i_new_method, unsigned int *o_method);

/**
 * @brief Apply a spawn strategy ordinal to a spawn strategy bitmask.
 *
 * Adds/clears the bit(s) corresponding to @p i_strategy in @p io_strategies,
 * removing any other bits that are incompatible with it.
 *
 * @param[in]     i_strategy    ::mam_spawn_strategies ordinal to apply.
 * @param[in,out] io_strategies Spawn strategy bitmask to update.
 * @return ::MAM_STRATS_ADDED, ::MAM_STRATS_MODIFIED, ::MAM_OK if already set,
 * or ::MAM_DENIED if @p i_strategy is not a recognised ordinal.
 */
int MAM_I_set_spawn_strat(unsigned int i_strategy, unsigned int *io_strategies);

/**
 * @brief Apply a redistribution strategy ordinal to a redistribution strategy bitmask.
 *
 * Adds/clears the bit(s) corresponding to @p i_strategy in @p io_strategies,
 * removing any other bits that are incompatible with it.
 *
 * @param[in]     i_strategy    ::mam_red_strategies ordinal to apply.
 * @param[in,out] io_strategies Redistribution strategy bitmask to update.
 * @return ::MAM_STRATS_ADDED, ::MAM_STRATS_MODIFIED, ::MAM_OK if already set,
 * or ::MAM_DENIED if @p i_strategy is not a recognised ordinal.
 */
int MAM_I_set_red_strat(unsigned int i_strategy, unsigned int *io_strategies);

/**
 * @brief Apply the target process count to the global process-state singleton.
 *
 * @param[in] i_new_numC Target process count; rejected if 0.
 * @return @p i_new_numC on success, or ::MAM_DENIED if malleability has
 * already started or @p i_new_numC is 0.
 */
int MAM_I_set_target_number(unsigned int i_new_numC);

/**
 * @brief Seed every configuration key from its environment variable, or its default.
 *
 * @return Always 0.
 */
int MAM_I_configuration_get_defaults(void);

/**
 * @brief Test whether a bitmask contains a given bit/mask.
 *
 * @param[in] i_comm_strategies Strategy bitmask to test.
 * @param[in] i_strategy        Bit/mask to look for.
 * @return Non-zero if @p i_strategy is set in @p i_comm_strategies, 0 otherwise.
 */
int MAM_I_contains_strat(unsigned int i_comm_strategies, unsigned int i_strategy);

/**
 * @brief Add a bit/mask to a strategy bitmask if not already present.
 *
 * @param[in,out] io_comm_strategies Strategy bitmask to update.
 * @param[in]     i_strategy         Bit/mask to add.
 * @return ::MAM_OK if @p i_strategy was already set, ::MAM_STRATS_ADDED otherwise.
 */
int MAM_I_add_strat(unsigned int *io_comm_strategies, unsigned int i_strategy);

/**
 * @brief Remove a bit/mask from a strategy bitmask if present.
 *
 * @param[in,out] io_comm_strategies Strategy bitmask to update.
 * @param[in]     i_strategy         Bit/mask to remove.
 * @return ::MAM_OK if @p i_strategy was not set, ::MAM_STRATS_MODIFIED otherwise.
 */
int MAM_I_remove_strat(unsigned int *io_comm_strategies, unsigned int i_strategy);

/**
 * @brief Configuration table indexed by ::mam_key_values.
 *
 * The last entry (::MAM_NUM_TARGETS) is handled separately from the rest
 * since it does not write through @c value but instead updates
 * @c mall->numC directly via @c set_config_complex.
 */
mam_config_setting_t configSettings[] = { 
    {NULL, MAM_SPAWN_MERGE, MAM_METHODS_SPAWN_LEN, {.set_config_simple = MAM_I_set_method }, MAM_SPAWN_METHOD_ENV},
    {NULL, MAM_STRAT_SPAWN_CLEAR, MAM_STRATS_SPAWN_LEN, {.set_config_simple = MAM_I_set_spawn_strat }, MAM_SPAWN_STRATS_ENV},
    {NULL, MAM_PHY_DIST_COMPACT, MAM_METHODS_PHYSICAL_DISTRIBUTION_LEN, {.set_config_simple = MAM_I_set_method }, MAM_PHYSICAL_DISTRIBUTION_METHOD_ENV},
    {NULL, MAM_RED_BASELINE, MAM_METHODS_RED_LEN, {.set_config_simple = MAM_I_set_method }, MAM_RED_METHOD_ENV},
    {NULL, MAM_STRAT_RED_CLEAR, MAM_STRATS_RED_LEN, {.set_config_simple = MAM_I_set_red_strat }, MAM_RED_STRATS_ENV},

    {NULL, 1, INT_MAX, {.set_config_complex = MAM_I_set_target_number }, MAM_NUM_TARGETS_ENV}
};

/** @brief Bitmasks for ::mam_spawn_strategies ordinals, indexed by ::MAM_Contains_strat's @c i_strategy. */
unsigned int masks_spawn[] = {MAM_STRAT_CLEAR_VALUE, MAM_MASK_PTHREAD, MAM_MASK_SPAWN_SINGLE, MAM_MASK_SPAWN_INTERCOMM, MAM_MASK_SPAWN_MULTIPLE, MAM_MASK_SPAWN_PARALLEL};
/** @brief Bitmasks for ::mam_red_strategies ordinals, indexed by ::MAM_Contains_strat's @c i_strategy. */
unsigned int masks_red[] = {MAM_STRAT_CLEAR_VALUE, MAM_MASK_PTHREAD, MAM_MASK_RED_WAIT_SOURCES, MAM_MASK_RED_WAIT_TARGETS};

/**
 * @brief Set configuration parameters for MAM.
 *
 * This function allows setting various configuration parameters for MAM
 * such as spawn method, spawn strategies, spawn physical distribution, 
 * redistribution method, and red strategies.
 *
 * @param[in] i_spawn_method     The spawn method reconfiguration.
 * @param[in] i_spawn_strategies The spawn strategies reconfiguration.
 * @param[in] i_spawn_dist       The spawn physical distribution method reconfiguration.
 * @param[in] i_red_method       The redistribution method reconfiguration.
 * @param[in] i_red_strategies   The redesitribution strategy for reconfiguration.
 */
void MAM_Set_configuration(int i_spawn_method, int i_spawn_strategies, int i_spawn_dist, int i_red_method, int i_red_strategies) {
  int i, aux;
  int aux_array[] = {i_spawn_method, i_spawn_strategies, i_spawn_dist, i_red_method, i_red_strategies};
  if(state > MAM_I_NOT_STARTED) return;

  mam_config_setting_t *config = NULL;
  for (i = 0; i < MAM_KEY_COUNT-1; i++) { //FIXME: Magic number to avoid changing num_targets
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

/**
 * @brief Set the configuration value for a specific key in MAM.
 *
 * Modifies the configuration value associated with the given key
 * to the specified "required" value. The final value set is returned in the
 * "provided" parameter.
 *
 * @param[in]  i_key      The key for which the configuration value is to be modified.
 * @param[in]  i_required The required value to set for the specified key.
 * @param[out] o_provided Optional; receives the final value set after modification.
 *                        For strategy keys the value is ::MAM_STRATS_ADDED if
 *                        @p i_required has been added, or ::MAM_STRATS_MODIFIED
 *                        if multiple strategies of the key have been modified.
 */
void MAM_Set_key_configuration(int i_key, int i_required, int *o_provided) {
  int i, aux;

  if(o_provided == NULL) o_provided = &aux;
  *o_provided = MAM_DENIED;
  if(i_required < 0 || state > MAM_I_NOT_STARTED) return;

  mam_config_setting_t *config = NULL;
  for (i = 0; i < MAM_KEY_COUNT; i++) { //FIXME: A for is not needed -- Check if key < MAM_KEY_COUNT and then just use key as index
    if (i_key == i) {
      config = &configSettings[i];
      break;
    }
  }

  if (config != NULL) {
    if (i_required < config->config_max_length) {
      if(i == MAM_NUM_TARGETS) {
        *o_provided = config->set_config_complex(i_required);
      } else {
        *o_provided = config->set_config_simple(i_required, config->value);
      }
    } else {*o_provided = *(config->value); }
  } else { printf("MAM: Key %d does not exist\n", i_key); }
}

/**
 * @brief Test whether a strategy ordinal is set in the current configuration.
 *
 * @param[in]  i_key      ::MAM_SPAWN_STRATEGIES or ::MAM_RED_STRATEGIES.
 * @param[in]  i_strategy Strategy enum ordinal (::mam_spawn_strategies / ::mam_red_strategies).
 * @param[out] o_result   Optional; receives non-zero if contained (may be @c NULL).
 * @return Non-zero if the strategy bit is set, 0 otherwise (also 0 if @p i_key is unknown).
 */
int MAM_Contains_strat(int i_key, unsigned int i_strategy, int *o_result) {
  int strategies, aux = MAM_OK;
  unsigned int len = 0, mask;

  switch(i_key) {
    case MAM_SPAWN_STRATEGIES:
      strategies = mall_conf->spawn_strategies;
      mask = masks_spawn[i_strategy];
      len = MAM_STRATS_SPAWN_LEN;
      break;
    case MAM_RED_STRATEGIES:
      strategies = mall_conf->red_strategies;
      mask = masks_red[i_strategy];
      len = MAM_STRATS_RED_LEN;
      break;
    default:
      aux = MAM_DENIED;
      break;
  }

  if(aux == MAM_OK && i_strategy < len) {
    aux = MAM_I_contains_strat(strategies, mask);
  } else {
    aux = 0;
  }

  if(o_result != NULL) *o_result = aux;
  return aux;
}

/**
 * @brief Set the desired number of target processes for the next reconfiguration.
 *
 * Indicates how many target processes the reconfiguration will use. Once
 * SLURM is taken into account, this will decide the total number of new
 * nodes to allocate or return. Must be called after setting the rest of
 * the configuration.
 *
 * @param[in] i_numC Target process count.
 * @return @c i_numC on success, or ::MAM_DENIED if malleability has already
 * started or @p i_numC is 0.
 */
int MAM_Set_target_number(unsigned int i_numC){
  return MAM_I_set_target_number(i_numC);
}


/**
 * @brief Enable/disable Valgrind wrapper for spawned processes.
 *
 * Indicates that newly created processes should start execution by first
 * calling Valgrind through a wrapper script. Must be called outside of
 * a reconfiguration.
 *
 * @param[in] i_flag Non-zero to enable.
 */
void MAM_Use_valgrind(int i_flag) {
  if(state > MAM_I_NOT_STARTED) return;

  mall_conf->external_usage = i_flag ? MAM_USE_VALGRIND: 0;
  #if MAM_DEBUG
    if(mall->myId == mall->root && i_flag) { DEBUG_FUNC("Settled Valgrind Wrapper", mall->myId, mall->numP); fflush(stdout); }
  #endif
}

/**
 * @brief Enable/disable Extrae wrapper for spawned processes.
 *
 * Indicates that newly created processes should start execution by first
 * calling Extrae through a wrapper script. Must be called outside of
 * a reconfiguration.
 *
 * @param[in] i_flag Non-zero to enable.
 */
void MAM_Use_extrae(int i_flag) {
  if(state > MAM_I_NOT_STARTED) return;

  mall_conf->external_usage = i_flag ? MAM_USE_EXTRAE: 0;
  #if MAM_DEBUG
    if(mall->myId == mall->root && i_flag) { DEBUG_FUNC("Settled Extrae Wrapper", mall->myId, mall->numP); fflush(stdout); }
  #endif
}

//======================================================||
//===============MAM_INIT FUNCTIONS=====================||
//======================================================||
//======================================================||

/**
 * @brief Allocate/default the configuration table used at MaM start-up.
 *
 * Resets the method/strategy fields of @c mall_conf and binds each
 * relevant ::configSettings entry's @c value pointer to its field in
 * @c mall_conf. Aborts the job (@c MPI_Abort) if @c mall or @c mall_conf
 * have not been allocated yet.
 */
void MAM_Init_configuration(void) {
  if(mall == NULL || mall_conf == NULL) {
    printf("MAM FATAL ERROR: Setting initial config without previous mallocs\n");
    fflush(stdout);
    MPI_Abort(MPI_COMM_WORLD, -50);
  }

  mall_conf->spawn_method = MAM_STRAT_CLEAR_VALUE;
  mall_conf->spawn_strategies = MAM_STRAT_CLEAR_VALUE;
  mall_conf->red_method = MAM_STRAT_CLEAR_VALUE;
  mall_conf->red_strategies = MAM_STRAT_CLEAR_VALUE;
  mall_conf->external_usage = 0;

  configSettings[MAM_SPAWN_METHOD].value = &mall_conf->spawn_method;
  configSettings[MAM_SPAWN_STRATEGIES].value = &mall_conf->spawn_strategies;
  configSettings[MAM_PHYSICAL_DISTRIBUTION].value = &mall_conf->spawn_dist;
  configSettings[MAM_RED_METHOD].value = &mall_conf->red_method;
  configSettings[MAM_RED_STRATEGIES].value = &mall_conf->red_strategies;
}

/**
 * @brief Apply environment defaults and initial method/strategy values.
 *
 * Seeds every configuration key from its environment variable (or default)
 * via ::MAM_I_configuration_get_defaults. Aborts the job (@c MPI_Abort) if
 * the starting configuration could not be filled.
 */
void MAM_Set_initial_configuration(void) {
  int not_filled = 1;
  
  not_filled = MAM_I_configuration_get_defaults();
  if(not_filled) {
    if(mall->myId == mall->root) printf("MAM WARNING: Starting configuration not set\n");
    fflush(stdout);
    MPI_Abort(mall->comm, -50);
  }

  #if MAM_DEBUG >= 2
    if(mall->myId == mall->root) {
      DEBUG_FUNC("Initial configuration settled", mall->myId, mall->numP); 
      fflush(stdout); 
    }
  #endif
}

/**
 * @brief Validate and normalise incompatible strategy combinations.
 *
 * Checks that the configuration follows the rules of MaM. If any rule is
 * violated, the configuration is modified: forces the baseline spawn
 * method on migration or when shrinking would otherwise leave no internode
 * WORLDS; drops incompatible spawn strategies for the merge spawn method;
 * and drops the intercomm spawn strategy (forcing a wait-targets
 * redistribution strategy if none is already set) for RMA-lock-based
 * redistribution methods.
 */
void MAM_Check_configuration(void) {
  int global_internodes;
  if(mall->numC == mall->numP) { // Migrate
    MAM_Set_key_configuration(MAM_SPAWN_METHOD, MAM_SPAWN_BASELINE, NULL);
  }

  MPI_Allreduce(&mall->internode_group, &global_internodes, 1, MPI_INT, MPI_MAX, mall->comm);
  if((MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL)
  || MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PARALLEL, NULL) )
  && global_internodes && mall->numP > mall->numC) { // Ensure when shrinking there are no internodes WORLDS left
    MAM_Set_key_configuration(MAM_SPAWN_METHOD, MAM_SPAWN_BASELINE, NULL);
  }

  if(mall_conf->spawn_method == MAM_SPAWN_MERGE) {
    if(MAM_I_contains_strat(mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM)) {
      MAM_I_remove_strat(&mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM);
    }
    // FIXME: This should not be required to be removed for that case...
    if(mall->numP > mall->numC && MAM_I_contains_strat(mall_conf->spawn_strategies, MAM_MASK_SPAWN_SINGLE)) {
      MAM_I_remove_strat(&mall_conf->spawn_strategies, MAM_MASK_SPAWN_SINGLE);
    }
  }
  if(mall_conf->red_method == MAM_RED_RMA_LOCK || mall_conf->red_method == MAM_RED_RMA_LOCKALL) {
    if(MAM_I_contains_strat(mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM)) {
      MAM_I_remove_strat(&mall_conf->spawn_strategies, MAM_MASK_SPAWN_INTERCOMM);
    }
    if(!MAM_I_contains_strat(mall_conf->red_strategies, MAM_MASK_RED_WAIT_TARGETS) &&
       !MAM_I_contains_strat(mall_conf->red_strategies, MAM_MASK_PTHREAD)) {
      MAM_I_set_red_strat(MAM_STRAT_RED_WAIT_TARGETS, &mall_conf->red_strategies);
    }
  }

  #if MAM_DEBUG >= 2
    if(mall->myId == mall->root) {
      DEBUG_FUNC("MaM configuration", mall->myId, mall->numP); 
      printf("Spawn M=%d S=%d D=%d Redist M=%d S=%d\n", 
            mall_conf->spawn_method, mall_conf->spawn_strategies, mall_conf->spawn_dist, mall_conf->red_method, mall_conf->red_strategies);
      fflush(stdout);
    }
  #endif
}

//======================================================||
//================PRIVATE FUNCTIONS=====================||
//======================================================||
//======================================================||

/**
 * @brief Seed every configuration key from its environment variable, or its default.
 *
 * Iterates over ::configSettings, reading each entry's environment
 * variable (falling back to its default value if unset), and applies it
 * through the entry's setter when it falls within the valid range.
 *
 * @return Always 0.
 */
int MAM_I_configuration_get_defaults(void) {
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


/**
 * @brief Overwrite a method configuration value.
 *
 * @param[in]  i_new_method Method value to store.
 * @param[out] o_method     Receives @p i_new_method.
 * @return The stored value (@p i_new_method).
 */
int MAM_I_set_method(unsigned int i_new_method, unsigned int *o_method) {
  *o_method = i_new_method;
  return *o_method;
}

//TODO: A pair of arrays/dicts could be used to obtain the mask without a switch
/**
 * @brief Apply a spawn strategy ordinal to a spawn strategy bitmask.
 *
 * Adds/clears the bit(s) corresponding to @p i_strategy in @p io_strategies,
 * removing any other bits that are incompatible with it.
 *
 * @param[in]     i_strategy    ::mam_spawn_strategies ordinal to apply.
 * @param[in,out] io_strategies Spawn strategy bitmask to update.
 * @return ::MAM_STRATS_ADDED, ::MAM_STRATS_MODIFIED, ::MAM_OK if already set,
 * or ::MAM_DENIED if @p i_strategy is not a recognised ordinal.
 */
int MAM_I_set_spawn_strat(unsigned int i_strategy, unsigned int *io_strategies) {
  int result = 0;
  int strat_removed = 0;

  switch(i_strategy) {
    case MAM_STRAT_SPAWN_CLEAR:
      *io_strategies = MAM_STRAT_CLEAR_VALUE;
      result = MAM_STRATS_MODIFIED;
      break;
    case MAM_STRAT_SPAWN_PTHREAD:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_PTHREAD);
      break;
    case MAM_STRAT_SPAWN_SINGLE:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_SPAWN_SINGLE);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_SPAWN_PARALLEL);
      }
      break;
    case MAM_STRAT_SPAWN_INTERCOMM:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_SPAWN_INTERCOMM);
      break;
    case MAM_STRAT_SPAWN_MULTIPLE:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_SPAWN_MULTIPLE);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_SPAWN_PARALLEL);
      }
      break;
    case MAM_STRAT_SPAWN_PARALLEL:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_SPAWN_PARALLEL);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_SPAWN_MULTIPLE);
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_SPAWN_SINGLE);
      }
      break;
    default:
      //Unkown strategy
      result = MAM_DENIED;
      break;
  }

  if(strat_removed) {
    result = MAM_STRATS_MODIFIED;
  }
  return result;
}

/**
 * @brief Apply a redistribution strategy ordinal to a redistribution strategy bitmask.
 *
 * Adds/clears the bit(s) corresponding to @p i_strategy in @p io_strategies,
 * removing any other bits that are incompatible with it.
 *
 * @param[in]     i_strategy    ::mam_red_strategies ordinal to apply.
 * @param[in,out] io_strategies Redistribution strategy bitmask to update.
 * @return ::MAM_STRATS_ADDED, ::MAM_STRATS_MODIFIED, ::MAM_OK if already set,
 * or ::MAM_DENIED if @p i_strategy is not a recognised ordinal.
 */
int MAM_I_set_red_strat(unsigned int i_strategy, unsigned int *io_strategies) {
  int result = 0;
  int strat_removed = 0;

  switch(i_strategy) {
    case MAM_STRAT_RED_CLEAR:
      *io_strategies = MAM_STRAT_CLEAR_VALUE;
      result = MAM_STRATS_MODIFIED;
      break;
    case MAM_STRAT_RED_PTHREAD: //TODO: IMPROVEMENT - This could be done with a single operation instead of 3.
      result = MAM_I_add_strat(io_strategies, MAM_MASK_PTHREAD);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_RED_WAIT_SOURCES);
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_RED_WAIT_TARGETS);
      }
      break;
    case MAM_STRAT_RED_WAIT_SOURCES:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_RED_WAIT_SOURCES);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_RED_WAIT_TARGETS);
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_PTHREAD);
      }
      break;
    case MAM_STRAT_RED_WAIT_TARGETS:
      result = MAM_I_add_strat(io_strategies, MAM_MASK_RED_WAIT_TARGETS);
      if(result == MAM_STRATS_ADDED) {
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_RED_WAIT_SOURCES);
        strat_removed += MAM_I_remove_strat(io_strategies, MAM_MASK_PTHREAD);
      }
      break;
    default:
      //Unkown strategy
      result = MAM_DENIED;
      break;
  }

  if(strat_removed) {
    result = MAM_STRATS_MODIFIED;
  }
  return result;
}

/**
 * @brief Apply the target process count to the global process-state singleton.
 *
 * @param[in] i_new_numC Target process count; rejected if 0.
 * @return @p i_new_numC on success, or ::MAM_DENIED if malleability has
 * already started or @p i_new_numC is 0.
 */
int MAM_I_set_target_number(unsigned int i_new_numC) {
  if(state > MAM_I_NOT_STARTED || i_new_numC == 0) return MAM_DENIED;

  mall->numC = (int) i_new_numC;
  return i_new_numC;
}


/**
 * @brief Test whether a bitmask contains a given bit/mask.
 *
 * @param[in] i_comm_strategies Strategy bitmask to test.
 * @param[in] i_strategy        Bit/mask to look for.
 * @return Non-zero if @p i_strategy is set in @p i_comm_strategies, 0 otherwise.
 */
int MAM_I_contains_strat(unsigned int i_comm_strategies, unsigned int i_strategy) {
  return i_comm_strategies & i_strategy;
}


/**
 * @brief Add a bit/mask to a strategy bitmask if not already present.
 *
 * @param[in,out] io_comm_strategies Strategy bitmask to update.
 * @param[in]     i_strategy         Bit/mask to add.
 * @return ::MAM_OK if @p i_strategy was already set, ::MAM_STRATS_ADDED otherwise.
 */
int MAM_I_add_strat(unsigned int *io_comm_strategies, unsigned int i_strategy) {
  if(MAM_I_contains_strat(*io_comm_strategies, i_strategy)) return MAM_OK;
  *io_comm_strategies |= i_strategy;
  return MAM_STRATS_ADDED;
}

/**
 * @brief Remove a bit/mask from a strategy bitmask if present.
 *
 * @param[in,out] io_comm_strategies Strategy bitmask to update.
 * @param[in]     i_strategy         Bit/mask to remove.
 * @return ::MAM_OK if @p i_strategy was not set, ::MAM_STRATS_MODIFIED otherwise.
 */
int MAM_I_remove_strat(unsigned int *io_comm_strategies, unsigned int i_strategy) {
  if(!MAM_I_contains_strat(*io_comm_strategies, i_strategy)) return MAM_OK;
  *io_comm_strategies &= ~i_strategy;
  return MAM_STRATS_MODIFIED;
}
