#ifndef MAM_SPAWN_PORTSERVICE_H
#define MAM_SPAWN_PORTSERVICE_H

/**
 * @file PortService.h
 * @brief MPI port open/publish/lookup helpers for discovering spawned groups.
 */

#include <mpi.h>
#include "Spawn_DataStructure.h"

/** @brief Sentinel for @c open_service / @c id_group when no service is needed. */
#define MAM_SERVICE_UNNEEDED -1

/**
 * @brief Zero-initialise a ::Spawn_ports structure.
 * @param[out] o_spawn_port Ports structure to initialise.
 */
void init_ports(Spawn_ports *o_spawn_port);

/**
 * @brief Open an MPI port and optionally publish it as a named service.
 *
 * @param[in,out] io_spawn_port  Ports structure (filled with names).
 * @param[in]     i_open_port    Non-zero to open; zero only allocates a placeholder name.
 * @param[in]     i_open_service Group id to publish, or @c MAM_SERVICE_UNNEEDED to skip publish.
 */
void open_port(Spawn_ports *io_spawn_port, int i_open_port, int i_open_service);

/**
 * @brief Unpublish (if needed) and close the local MPI port.
 * @param[in,out] io_spawn_port Ports structure whose local port is closed.
 */
void close_port(Spawn_ports *io_spawn_port);

/**
 * @brief Look up a remote group's published port by group id (retries then abort).
 *
 * @param[in]     i_id_group     Target group id, or @c MAM_SERVICE_UNNEEDED to skip lookup.
 * @param[in,out] io_spawn_port  Receives @c remote_service / @c remote_port.
 */
void discover_remote_port(int i_id_group, Spawn_ports *io_spawn_port);

/**
 * @brief Close the local port and free remote name buffers.
 * @param[in,out] io_spawn_port Ports structure to free.
 */
void free_ports(Spawn_ports *io_spawn_port);
#endif
