#include <mpi.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "GenericConnect.h"
#include "Spawn_DataStructure.h"
#include "PortService.h"

void MAM_Prepare_job_comms(int is_children) {
  int is_inter; 
  MPI_Comm *aux_comm;

  if (mall->intercomm != MPI_COMM_NULL) { // Children were spawned
  MPI_Comm_test_inter(mall->intercomm, &is_inter);
  if(is_inter) { // For connecting to job it is required an intracomm
    MPI_Intercomm_merge(mall->intercomm, is_children, aux_comm); //ranks that pass 0 come first
    if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&mall->intercomm);
    mall->intercomm = aux_comm;
    }
  }
}

void MAM_Repair_job_comms(int is_children, int children_type) {
  int numP, myId, remote_leader;
  MPI_Comm *aux_comm, *aux_intercomm;
  
  *aux_comm = MPI_COMM_NULL;

  MPI_Comm_size(mall->intercomm, &numP);
  myId = mall->myId + numP * children_type;
  MPI_Comm_split(mall->intercomm, is_children, myId, aux_comm);

  //FIXME: This is not needed if only a new job is created. How to detect that?
  if(is_children == MAM_TARGETS) { MAM_comms_update(*aux_comm); }

  // The following strategy is detrimental for performance, but if used,
  // some work done in other functions of this file is redone
  if (MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_INTERCOMM, NULL)) {
    remote_leader = is_children == MAM_SOURCES ? mall->numP : MAM_ROOT;
    MPI_Intercomm_create(*aux_comm, MAM_ROOT, mall->intercomm, remote_leader, 100, aux_intercomm);
    if(*aux_comm != MPI_COMM_NULL) MPI_Comm_disconnect(aux_comm);
    if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&mall->intercomm);
    mall->intercomm = *aux_intercomm;
  }
}

int MAM_Connect_jobs_as_source(void) {
  int err, res;
  MPI_Errhandler *errhandler;
  MPI_Comm aux_comm, aux_inter_comm;

  aux_comm = mall->intercomm != MPI_COMM_NULL ? mall->intercomm : mall->comm;

  res = MAM_I_JOB_CONNECTED;
  if(mall->port_name != NULL) { 
    mall->port_name = malloc(MPI_MAX_PORT_NAME * sizeof *mall->port_name);
    if(mall->myId == MAM_ROOT) { snprintf(mall->port_name, 5, "NULL"); } 
    else { mall->port_name[0] = '\0'; }
  } 

  // Try to detect if targets are ready
  if(mall->myId == MAM_ROOT) {
    MPI_Comm_get_errhandler(MPI_COMM_SELF, errhandler);
    MPI_Comm_set_errhandler(MPI_COMM_SELF, MPI_ERRORS_RETURN);
    err = MPI_Lookup_name(mall->service_name, MPI_INFO_NULL, mall->port_name);
    MPI_Comm_set_errhandler(MPI_COMM_SELF, errhandler);

    if (err == MPI_ERR_NAME || strncmp(mall->port_name, "NULL", 4) == 0) {
      res = MAM_I_JOB_CONNECTING; // Try again later
    } else if (err != MPI_SUCCESS) { // Fatal error
      DEBUG_FUNC("MaM Connection with new job failed in Lookup", mall->myId, mall->numP); fflush(stdout);
      fflush(stdout);
      MPI_Abort(aux_comm, -50);
    }
  }
  MPI_Bcast(&res, 1, MPI_INT, MAM_ROOT, mall->comm);
  if (res == MAM_I_JOB_CONNECTING) { return res; }

  // Join jobs into a single intercommunicator
  MPI_Comm_connect(mall->port_name, MPI_INFO_NULL, MAM_ROOT, aux_comm, &aux_inter_comm);
  if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&mall->intercomm);
  MPI_Intercomm_merge(aux_inter_comm, 0, &mall->intercomm); //ranks that pass 0 come first
  if(aux_inter_comm != MPI_COMM_WORLD && aux_inter_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&aux_inter_comm);

  if(mall->port_name != NULL) { free(mall->port_name); mall->port_name = NULL; }
  if(mall->service_name != NULL) { free(mall->service_name); mall->service_name = NULL; }

  return res;
}

void MAM_Connect_jobs_as_target_job(void) {
  int open_p;
  Spawn_ports *spawn_port = NULL;
  MPI_Comm aux_inter_comm = MPI_COMM_NULL;

  open_p = mall->myId == mall->root ? 1 : 0;
  init_ports(spawn_port);
  open_port(spawn_port, open_p, MAM_SERVICE_UNNEEDED);
  spawn_port->service_name = mall->service_name;

  MPI_Publish_name(spawn_port->service_name, MPI_INFO_NULL, spawn_port->port_name);

  MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, MAM_ROOT, mall->comm, &aux_inter_comm);
  if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&mall->intercomm); //TODO: I dont think it could have a value
  MPI_Intercomm_merge(aux_inter_comm, 1, &mall->intercomm); //ranks that pass 0 come first
  if(aux_inter_comm != MPI_COMM_WORLD && aux_inter_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&aux_inter_comm);

  free_ports(spawn_port);
  mall->service_name = NULL; // Service name is already freed in freed ports.
}

void MAM_Connect_jobs_as_children(void) {
  int err, res;
  MPI_Errhandler *errhandler;
  MPI_Comm aux_inter_comm = MPI_COMM_NULL;
  
  if(mall->port_name != NULL) { 
    mall->port_name = malloc(1 * sizeof *mall->port_name);
    mall->port_name[0] = '\0';
  } 

  do {
    MPI_Bcast(&res, 1, MPI_INT, MAM_ROOT, mall->intercomm);
  } while (res != MAM_I_JOB_CONNECTING);

  // Join jobs into a single intercommunicator
  MPI_Comm_connect(mall->port_name, MPI_INFO_NULL, MAM_ROOT, mall->intercomm, &aux_inter_comm);
  if(mall->intercomm != MPI_COMM_WORLD && mall->intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&mall->intercomm);
  MPI_Intercomm_merge(aux_inter_comm, 0, &mall->intercomm); //ranks that pass 0 come first
  if(aux_inter_comm != MPI_COMM_WORLD && aux_inter_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&aux_inter_comm);

  if(mall->port_name != NULL) { free(mall->port_name); mall->port_name = NULL; }
  if(mall->service_name != NULL) { free(mall->service_name); mall->service_name = NULL; }
}