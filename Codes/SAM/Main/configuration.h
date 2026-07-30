#ifndef CONFIGURATION_H
#define CONFIGURATION_H

/**
 * @file configuration.h
 * @brief Load, free, print, and MPI-broadcast Proteo ::configuration objects.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "Main_datatypes.h"

/**
 * @brief Initialise a configuration from a file, or allocate an empty shell.
 *
 * If @p i_file_name ends in @c .json or @c .ini, loads via the matching reader.
 * If @p i_file_name is @c NULL, allocates a minimal empty configuration for
 * the caller to fill. Aborts on unsupported extension or parse failure.
 *
 * @param[in]  i_file_name    Path to config file, or @c NULL.
 * @param[out] o_user_config  Receives the allocated configuration pointer.
 */
void init_config(char *i_file_name, configuration **o_user_config);

/**
 * @brief Free all memory and MPI datatypes owned by a configuration.
 * @param[in,out] io_user_config Configuration to free (may be @c NULL).
 */
void free_config(configuration *io_user_config);

/**
 * @brief Print the full configuration to stdout.
 * @param[in] i_user_config Configuration to print.
 */
void print_config(configuration *i_user_config);

/**
 * @brief Print global settings and one process-group entry to stdout.
 * @param[in] i_user_config Configuration to print from.
 * @param[in] i_grp         Group index.
 */
void print_config_group(configuration *i_user_config, size_t i_grp);

/**
 * @brief Allocate @c counts and @c displs arrays of length @p i_numP.
 * @param[in,out] io_counts Counts structure to initialise.
 * @param[in]     i_numP    Communicator size / array length.
 */
void malloc_counts(struct Counts *io_counts, size_t i_numP);

/**
 * @brief Free the internal arrays of a ::Counts structure (not the struct itself).
 * @param[in,out] io_counts Counts structure whose arrays are freed.
 */
void free_counts(struct Counts *io_counts);

/**
 * @brief Broadcast a configuration over an intracommunicator (root → all).
 *
 * Must be called by every rank in @p i_comm. @p i_root is a normal intracomm rank.
 *
 * @param[in] i_config_file Configuration to send (root holds valid data).
 * @param[in] i_root        Broadcast root rank.
 * @param[in] i_comm        Intracommunicator.
 */
void send_config_file(configuration *i_config_file, int i_root, MPI_Comm i_comm);

/**
 * @brief Receive a configuration over an intracommunicator (allocates on all ranks).
 *
 * Must be called by every rank in @p i_comm. Free the result with ::free_config.
 *
 * @param[in]  i_root              Broadcast root rank.
 * @param[in]  i_comm              Intracommunicator.
 * @param[out] o_config_file_out   Receives the allocated configuration.
 */
void recv_config_file(int i_root, MPI_Comm i_comm, configuration **o_config_file_out);

#endif
