#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "PortService.h"

/**
 * @file PortService.c
 * @brief Implementation of MPI port and service name helpers for MaM spawn strategies.
 */

#define MAM_SERVICE_CONSTANT_NAME 22  /**< Fixed prefix length of a MaM service name. */
#define MAM_SERVICE_VARIABLE_NAME 4   /**< Variable suffix length (group id digits + NUL). */
#define MAM_SERVICE_NAME_SIZE MAM_SERVICE_CONSTANT_NAME + MAM_SERVICE_VARIABLE_NAME
/* Example: "mam_service_jid0010_gr001\0" — constant prefix | variable group suffix */

/**
 * @brief Zero-initialise a ::Spawn_ports structure.
 * @param[out] o_spawn_port Ports structure to initialise.
 */
void init_ports(Spawn_ports *o_spawn_port) {
  o_spawn_port->opened_port = 0;
  o_spawn_port->port_name = NULL;
  o_spawn_port->service_name = NULL;
  o_spawn_port->remote_port = NULL;
  o_spawn_port->remote_service = NULL;
}

/**
 * @brief Open an MPI port and optionally publish it as a named service.
 *
 * Allows MaM to find spawned groups that are not yet connected. Opens only once
 * (@c port_name already non-NULL is a no-op). Non-openers allocate a 1-byte
 * placeholder so @c port_name is never NULL. Under Slurm, the job id (mod 1000)
 * is embedded in the service name.
 *
 * @param[in,out] io_spawn_port  Ports structure (filled with names).
 * @param[in]     i_open_port    Non-zero to open; zero only allocates a placeholder name.
 * @param[in]     i_open_service Group id to publish, or @c MAM_SERVICE_UNNEEDED to skip publish.
 */
void open_port(Spawn_ports *io_spawn_port, int i_open_port, int i_open_service)
{
    int job_id = 0;
    if (io_spawn_port->port_name != NULL)
        return;

    if (i_open_port) {
        io_spawn_port->opened_port = 1;
        io_spawn_port->port_name = (char *)malloc(MPI_MAX_PORT_NAME * sizeof(char));
        MPI_Open_port(MPI_INFO_NULL, io_spawn_port->port_name);
        if (i_open_service != MAM_SERVICE_UNNEEDED) {
          io_spawn_port->service_name = (char *)malloc((MAM_SERVICE_NAME_SIZE) * sizeof(char));
#if MAM_USE_SLURM
          char *tmp = getenv("SLURM_JOB_ID");
          if (tmp != NULL) { job_id = atoi(tmp) % 1000; }
#endif
          snprintf(io_spawn_port->service_name, MAM_SERVICE_NAME_SIZE, "mam_service_jid%04d_gr%03d", job_id, i_open_service);
          MPI_Publish_name(io_spawn_port->service_name, MPI_INFO_NULL, io_spawn_port->port_name);
        }
  } else {
    io_spawn_port->port_name = malloc(1);
    io_spawn_port->port_name[0] = '\0';
  }
}

/**
 * @brief Unpublish (if needed) and close the local MPI port.
 *
 * Frees @c service_name and @c port_name and clears the pointers.
 *
 * @param[in,out] io_spawn_port Ports structure whose local port is closed.
 */
void close_port(Spawn_ports *io_spawn_port) {
  if (io_spawn_port->port_name != NULL) {
    if (io_spawn_port->service_name != NULL) {
      MPI_Unpublish_name(io_spawn_port->service_name, MPI_INFO_NULL, io_spawn_port->port_name);
      free(io_spawn_port->service_name);
      io_spawn_port->service_name = NULL;
    }
    if (io_spawn_port->opened_port) MPI_Close_port(io_spawn_port->port_name);
    free(io_spawn_port->port_name);
    io_spawn_port->port_name = NULL;
  }
}

/**
 * @brief Look up a remote group's published port by group id.
 *
 * Must be called at least by the root that will @c MPI_Comm_connect. Retries
 * lookup up to 5 times (1 s sleep) then aborts. If @p i_id_group is
 * @c MAM_SERVICE_UNNEEDED, allocates an empty remote port and returns.
 *
 * @param[in]     i_id_group     Target group id, or @c MAM_SERVICE_UNNEEDED to skip lookup.
 * @param[in,out] io_spawn_port  Receives @c remote_service / @c remote_port.
 */
void discover_remote_port(int i_id_group, Spawn_ports *io_spawn_port) {
  int error_tries = 0, job_id = 0;

  if (io_spawn_port->remote_port == NULL) {
    io_spawn_port->remote_port = (char *)malloc(MPI_MAX_PORT_NAME * sizeof(char));
    if (i_id_group == MAM_SERVICE_UNNEEDED) { io_spawn_port->remote_port[0] = '\0'; }
  }
  if (i_id_group == MAM_SERVICE_UNNEEDED) { return; }

  if (io_spawn_port->remote_service == NULL) { // First discover
    io_spawn_port->remote_service = (char *)malloc(MAM_SERVICE_NAME_SIZE * sizeof(char));
#if MAM_USE_SLURM
    char *tmp = getenv("SLURM_JOB_ID");
    if (tmp != NULL) { job_id = atoi(tmp) % 1000; }
#endif
    snprintf(io_spawn_port->remote_service, MAM_SERVICE_NAME_SIZE, "mam_service_jid%04d_gr%03d", job_id, i_id_group);
  } else { // Subsequent lookups: update only the variable group-id suffix
    snprintf(io_spawn_port->remote_service + MAM_SERVICE_CONSTANT_NAME, MAM_SERVICE_VARIABLE_NAME, "%03d", i_id_group);
  }

  snprintf(io_spawn_port->remote_port, 5, "NULL");

  MPI_Lookup_name(io_spawn_port->remote_service, MPI_INFO_NULL, io_spawn_port->remote_port);
  while (strncmp(io_spawn_port->remote_port, "NULL", 4) == 0) {
    sleep(1);
    MPI_Lookup_name(io_spawn_port->remote_service, MPI_INFO_NULL, io_spawn_port->remote_port);
    if (++error_tries > 5) MPI_Abort(MPI_COMM_WORLD, -1);
  }
}


/**
 * @brief Close the local port and free remote name buffers.
 * @param[in,out] io_spawn_port Ports structure to free.
 */
void free_ports(Spawn_ports *io_spawn_port) {
  close_port(io_spawn_port);
  if (io_spawn_port->remote_port != NULL) {
    free(io_spawn_port->remote_port);
    io_spawn_port->remote_port = NULL;
  }

  if (io_spawn_port->remote_service != NULL) {
    free(io_spawn_port->remote_service);
    io_spawn_port->remote_service = NULL;
  }
}
