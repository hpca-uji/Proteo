#ifndef READ_INI_H
#define READ_INI_H

/**
 * @file read_ini.h
 * @brief Load a Proteo ::configuration from an INI file.
 */

#include "read_config.h"

/**
 * @brief Create and fill a configuration from an INI file.
 *
 * Allocates a ::configuration, parses @p i_file_name with the inih library,
 * and uses @p i_init_functions to allocate groups, phases, and stages as
 * counts become known. On parse failure returns @c NULL.
 *
 * The returned structure should be released with the application's
 * configuration free routine (e.g. @c free_config()).
 *
 * @param[in] i_file_name       Path to the INI configuration file.
 * @param[in] i_init_functions  Allocation callbacks for groups/phases/stages.
 * @return Pointer to the filled configuration, or @c NULL on error.
 */
configuration *read_ini_file(char *i_file_name, ext_functions_t i_init_functions);

#endif
