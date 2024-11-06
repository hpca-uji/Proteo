#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "PortService.h"

#define MAM_SERVICE_CONSTANT_NAME 22  // Constant size name
#define MAM_SERVICE_VARIABLE_NAME 4  // Variable size name + '\0'
#define MAM_SERVICE_NAME_SIZE MAM_SERVICE_CONSTANT_NAME + MAM_SERVICE_VARIABLE_NAME
// Example of mam service name --> "mam_service_jid0010_gr001\0"
//                                  constant part        |variable part
//
void init_ports(Spawn_ports *spawn_port) {
  spawn_port->opened_port = 0;
  spawn_port->port_name = NULL;
  spawn_port->service_name = NULL;
  spawn_port->remote_port = NULL;
  spawn_port->remote_service = NULL;
}

/*
 * Opens an MPI port for inter-process communication and optionally publishes it as a service.
 * Allows MaM to find other spawned groups which are not connected.
 *
 * Parameters:
 *   spawn_data: A structure containing information related to the port and service names.
 *   open_port : A flag that indicates if this process should Open (1) or only malloc(0).
 *   open_service: A flag that indicates if the service should be published.
 *                 If it is not MAM_SERVICE_UNNEEDED, a service name is generated and published with the chosen number.
 *
 * Functionality:
 *   - Ensures that a port is only opened if it hasn't been opened already.
 *   - The process with the root rank opens the port and, if required, publishes a service name for it.
 *     - If SLURM is being used, it attempts to get the SLURM job ID from the environment.
 *   - For non-root ranks, it simply allocates 1 byte of memory for the port_name to avoid it being NULL (a placeholder operation).
 *
 * Notes:
 *   - SLURM is conditionally used to obtain job-specific information.
 *   - Error handling is not included in this function (e.g., failed memory allocation, failed MPI calls).
 */
void open_port(Spawn_ports *spawn_port, int open_port, int open_service)
{
    int job_id = 0;
    if (spawn_port->port_name != NULL)
        return;

    if (open_port) {
        spawn_port->opened_port = 1;
        spawn_port->port_name = (char *)malloc(MPI_MAX_PORT_NAME * sizeof(char));
        MPI_Open_port(MPI_INFO_NULL, spawn_port->port_name);
        if (open_service != MAM_SERVICE_UNNEEDED) {
            spawn_port->service_name = (char *)malloc((MAM_SERVICE_NAME_SIZE) * sizeof(char));
#if MAM_USE_SLURM
      char *tmp = getenv("SLURM_JOB_ID");
      if(tmp != NULL) { job_id = atoi(tmp)%1000; }
#endif
      snprintf(spawn_port->service_name, MAM_SERVICE_NAME_SIZE, "mam_service_jid%04d_gr%03d", job_id, open_service);
      MPI_Publish_name(spawn_port->service_name, MPI_INFO_NULL, spawn_port->port_name);
    }
  } else {
    spawn_port->port_name = malloc(1);
    spawn_port->port_name[0] = '\0';
  }
}

/*
 * Function: close_port
 * --------------------
 * Closes an open MPI local port and cleans up associated resources.
 *
 * Parameters:
 *   spawn_data: A structure containing information related to the port and service names.
 *
 * Functionality:
 *   - The root process is the only one responsible for closing the MPI port and service.
 *   - Frees the memory allocated for the port and service and sets the pointer to NULL.
 *
 * Notes:
 *   - This function assumes that MPI resources were successfully allocated and opened in the corresponding `open_port` function.
 *   - No explicit error handling is present (e.g., checking the return value of MPI functions).
 */
void close_port(Spawn_ports *spawn_port) {
  if(spawn_port->port_name != NULL) {
    if(spawn_port->service_name != NULL) {
      MPI_Unpublish_name(spawn_port->service_name, MPI_INFO_NULL, spawn_port->port_name);
      free(spawn_port->service_name);
      spawn_port->service_name = NULL;
    }
    if(spawn_port->opened_port) MPI_Close_port(spawn_port->port_name);
    free(spawn_port->port_name);
    spawn_port->port_name = NULL;
  }
}

/*
 * Function: discover_remote_port
 * ------------------------------
 * Discovers the MPI port associated with a remote service using its service name. 
 * If the port cannot be found, it retries a set number of times before aborting the MPI execution.
 * This function must at least be called by the root process which will call MPI_Comm_connect, altough
 * it could be called by all processes without any issues.
 *
 * Parameters:
 *   remote_service: A pointer to a string that will hold the remote service name.
 *                   If this is the first time discovering the service, memory will be allocated and the name will be generated.
 *   id_group: An integer representing the group ID, used to identify the service.
 *   remote_port: A string where the discovered remote port name will be stored.
 *
 * Notes:
 *   - This function assumes that the service name follows a fixed pattern (`mam_service_jid%04d_gr%03d`).
 *   - If id_group is MAM_SERVICE_UNNEEDED, it is assumed the process is not the root and does not require
 *     to discover the real port.
 *   - SLURM is conditionally used to retrieve the job ID from the environment.
 *   - The number of retry attempts before aborting is limited to 5.
 *   - No explicit error handling is present (e.g., checking the return value of MPI functions).
 */
void discover_remote_port(int id_group, Spawn_ports *spawn_port) {
  int error_tries = 0, job_id = 0;

  if(spawn_port->remote_port == NULL) { 
    spawn_port->remote_port = (char*) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    if(id_group == MAM_SERVICE_UNNEEDED) { spawn_port->remote_port[0] = '\0'; }
  }
  if(id_group == MAM_SERVICE_UNNEEDED) { return; }

  if(spawn_port->remote_service == NULL) { //First discover
    spawn_port->remote_service = (char*) malloc(MAM_SERVICE_NAME_SIZE * sizeof(char));
#if MAM_USE_SLURM
    char *tmp = getenv("SLURM_JOB_ID");
    if(tmp != NULL) { job_id = atoi(tmp)%1000; }
#endif
    snprintf(spawn_port->remote_service, MAM_SERVICE_NAME_SIZE, "mam_service_jid%04d_gr%03d", job_id, id_group);
  } else { // For subsequent lookups, only update the variable part (group ID) of the service name.
    snprintf(spawn_port->remote_service + MAM_SERVICE_CONSTANT_NAME, MAM_SERVICE_VARIABLE_NAME, "%03d", id_group);
  }

  snprintf(spawn_port->remote_port, 5, "NULL");

  MPI_Lookup_name(spawn_port->remote_service, MPI_INFO_NULL, spawn_port->remote_port);
  while(strncmp(spawn_port->remote_port, "NULL", 4) == 0) {
    sleep(1);
    MPI_Lookup_name(spawn_port->remote_service, MPI_INFO_NULL, spawn_port->remote_port);
    if(++error_tries > 5) MPI_Abort(MPI_COMM_WORLD, -1);
  }
}


void free_ports(Spawn_ports *spawn_port) {
  close_port(spawn_port);
  if(spawn_port->remote_port != NULL) {
    free(spawn_port->remote_port);
    spawn_port->remote_port = NULL;
  }

  if(spawn_port->remote_service != NULL) {
    free(spawn_port->remote_service);
    spawn_port->remote_service = NULL;
  }
}
