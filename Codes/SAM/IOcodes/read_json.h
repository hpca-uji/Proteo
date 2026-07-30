#ifndef READ_JSON_H
#define READ_JSON_H

/**
 * @file read_json.h
 * @brief Load a Proteo ::configuration from a JSON file.
 */

#include "read_config.h"

/**
 * @brief Create and fill a configuration from a JSON file.
 *
 * Allocates a ::configuration, parses the file (root keys @c general,
 * @c phases, @c groups), and uses @p i_init_functions to allocate nested
 * arrays as counts become known. On failure frees partial state and
 * returns @c NULL.
 *
 * @param[in] i_file_name       Path to the JSON configuration file.
 * @param[in] i_init_functions  Allocation callbacks for groups/phases/stages.
 * @return Pointer to the filled configuration, or @c NULL on error.
 */
configuration *read_json_file(char *i_file_name, ext_functions_t i_init_functions);

#endif
