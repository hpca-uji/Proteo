#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"
#include "PortService.h"
#include "Strategy_Parallel.h"
#include "ProcessDist.h"
#include "SpawnUtils.h"
#include <math.h>

void parallel_strat_parents_hypercube(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child);
void parallel_strat_children_hypercube(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *parents);

void hypercube_spawn(int group_id, int groups, int init_nodes, int init_step, MPI_Comm **spawn_comm, int *qty_comms);
void common_synch(Spawn_data spawn_data, int qty_comms, MPI_Comm intercomm, MPI_Comm *spawn_comm);
void binary_tree_connection(int groups, int group_id, Spawn_ports *spawn_port, MPI_Comm *newintracomm);
void binary_tree_reorder(MPI_Comm *newintracomm, int group_id);


//--------PUBLIC FUNCTIONS----------//
//The abstraction for the algorithm is to allow different algorithms depending
//on the circumstances of the spawn.

void parallel_strat_parents(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child) {
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel PA started", mall->myId, mall->numP); fflush(stdout);
  #endif
  parallel_strat_parents_hypercube(spawn_data, spawn_port, child);
    #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

void parallel_strat_children(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *parents) {
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel CH started", mall->myId, mall->numP); fflush(stdout);
  #endif
  parallel_strat_children_hypercube(spawn_data, spawn_port, parents);
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel CH completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

//--------PRIVATE FUNCTIONS----------//

/*=====================HYPERCUBE++ ALGORITHM=====================*/
//The following algorithm divides the spawning task across all available ranks.
//It starts with just the sources, and then all spawned processes help with further
//spawns until all the required processes have been created.  

//FIXME -- The amount of processes per spawned group must be homogenous among groups
//       - There is an exception for the last node, which could have less procs
//       - Yet, the first spawned group cannot have less procs than the rest

void parallel_strat_parents_hypercube(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *child) {
  int opening, qty_comms;
  int groups, init_nodes, actual_step, group_id;
  MPI_Comm *spawn_comm = NULL;

  MPI_Bcast(&spawn_data.total_spawns, 1, MPI_INT, mall->root, spawn_data.comm);
  
  actual_step = 0;
  qty_comms = 0;
  init_nodes = mall->numP / mall->num_cpus; //FIXME does not consider heterogenous machines
  groups = spawn_data.total_spawns + init_nodes;
  group_id = -init_nodes;

  opening = mall->myId == mall->root ? 1 : 0;
  open_port(spawn_port, opening, groups);

  hypercube_spawn(group_id, groups, init_nodes, actual_step, &spawn_comm, &qty_comms);
  common_synch(spawn_data, qty_comms, MPI_COMM_NULL, spawn_comm);

  for(int i=0; i<qty_comms; i++) { MPI_Comm_disconnect(&spawn_comm[i]); }
  if(spawn_comm != NULL) free(spawn_comm); 

  MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, MAM_ROOT, spawn_data.comm, child);
}

/*
  - MPI_Comm *parents: Initially is the intercommunicator with its parent
*/
void parallel_strat_children_hypercube(Spawn_data spawn_data, Spawn_ports *spawn_port, MPI_Comm *parents) {
  int group_id, opening, qty_comms;
  int actual_step;
  int groups, init_nodes;
  MPI_Comm newintracomm, *spawn_comm = NULL;
  // TODO Comprobar si entrar en spawn solo si groups < numSources

  qty_comms = 0;
  group_id = mall->gid;
  init_nodes = spawn_data.initial_qty / mall->num_cpus;
  groups = spawn_data.spawn_qty / mall->num_cpus + init_nodes;
  opening = (mall->myId == MAM_ROOT && group_id < (groups-init_nodes)/2) ? 1 : 0;
  open_port(spawn_port, opening, group_id);

  // Spawn more processes if required
  if(groups - init_nodes > spawn_data.initial_qty) { 
    actual_step = log((group_id + init_nodes) / init_nodes) / log(1 + mall->numP);
    actual_step = floor(actual_step) + 1;
    hypercube_spawn(group_id, groups, init_nodes, actual_step, &spawn_comm, &qty_comms); 
  }

  common_synch(spawn_data, qty_comms, *parents, spawn_comm);
  for(int i=0; i<qty_comms; i++) { MPI_Comm_disconnect(&spawn_comm[i]); }
  MPI_Comm_disconnect(parents);

  // Connect groups and ensure expected rank order
  binary_tree_connection(groups - init_nodes, group_id, spawn_port, &newintracomm);
  binary_tree_reorder(&newintracomm, group_id);
  
  // Create intercomm between sources and children
  opening = (mall->myId == mall->root && !group_id) ? groups : MAM_SERVICE_UNNEEDED;
  discover_remote_port(opening, spawn_port);
  MPI_Comm_connect(spawn_port->remote_port, MPI_INFO_NULL, MAM_ROOT, newintracomm, parents);

  // New group obtained -- Adjust ranks and comms
  MAM_comms_update(newintracomm);
  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);
  MPI_Comm_disconnect(&newintracomm);
}


// This function does not allow the same process to have multiple threads executing it
void hypercube_spawn(int group_id, int groups, int init_nodes, int init_step, 
                  MPI_Comm **spawn_comm, int *qty_comms) {
  int i,  aux_sum, actual_step;
  int next_group_id, actual_nodes;
  int jid=0, n=0;
  char *file_name = NULL;
  Spawn_set set;
 
  actual_step = init_step;
  actual_nodes = pow(1+mall->num_cpus, actual_step)*init_nodes - init_nodes;
  aux_sum = mall->num_cpus*(init_nodes + group_id) + mall->myId; //Constant sum for next line
  next_group_id = actual_nodes + aux_sum;
  if(next_group_id < groups - init_nodes) { //FIXME qty_comms no se calcula bien para procesos del mismo group_id en los ultimos pasos
    int max_steps = ceil(log(groups / init_nodes) / log(1 + mall->num_cpus));
    *qty_comms = max_steps - actual_step;
    *spawn_comm = (MPI_Comm *) malloc(*qty_comms * sizeof(MPI_Comm));
  }
  //if(mall->myId == 0)printf("T1 P%d+%d step=%d next_id=%d aux_sum=%d actual_nodes=%d comms=%d\n", mall->myId, group_id, actual_step, next_group_id, aux_sum, actual_nodes, *qty_comms);

#if MAM_USE_SLURM
  char *tmp = getenv("SLURM_JOB_ID");
  if(tmp != NULL) { jid = atoi(tmp); }
#endif
  set.cmd = get_spawn_cmd();
  i = 0;
  while(next_group_id < groups - init_nodes) {
    set_hostfile_name(&file_name, &n, jid, next_group_id);
    //read_hostfile_procs(file_name, &set.spawn_qty);
    set.spawn_qty = mall->num_cpus;
    MPI_Info_create(&set.mapping);
	  MPI_Info_set(set.mapping, "hostfile", file_name);
    mall->gid = next_group_id; // Used to pass the group id to the spawned process // Not thread safe
    mam_spawn(set, MPI_COMM_SELF, &(*spawn_comm)[i]);
    MPI_Info_free(&set.mapping);

    actual_step++; i++;
    actual_nodes = pow(1+mall->num_cpus, actual_step)*init_nodes - init_nodes;
    next_group_id = actual_nodes + aux_sum;
  }
  *qty_comms = i;
  if(file_name != NULL) free(file_name); 
}

void common_synch(Spawn_data spawn_data, int qty_comms, MPI_Comm intercomm, MPI_Comm *spawn_comm) {
  int i, color;
  char aux;
  MPI_Request *requests = NULL;
  MPI_Comm involved_procs, aux_comm;
  
  requests = (MPI_Request *) malloc(qty_comms * sizeof(MPI_Request));

  aux_comm = intercomm == MPI_COMM_NULL ? spawn_data.comm : mall->comm;
  color = qty_comms ? 1 : MPI_UNDEFINED;
  MPI_Comm_split(aux_comm, color, mall->myId, &involved_procs);

  // Upside synchronization starts
  for(i=0; i<qty_comms; i++) {
    MPI_Irecv(&aux, 1, MPI_CHAR, MAM_ROOT, 130, spawn_comm[i], &requests[i]);
  }
  if(qty_comms) { 
    MPI_Waitall(qty_comms, requests, MPI_STATUSES_IGNORE);
    MPI_Barrier(involved_procs);
  }
  // Sources are the only synchronized procs at this point
  if(intercomm != MPI_COMM_NULL && mall->myId == MAM_ROOT) { 
    MPI_Send(&aux, 1, MPI_CHAR, MAM_ROOT, 130, intercomm); 
  // Upside synchronization ends
  // Downside synchronization starts
    MPI_Recv(&aux, 1, MPI_CHAR, MAM_ROOT, 130, intercomm, MPI_STATUS_IGNORE);
  }

  if(intercomm != MPI_COMM_NULL && qty_comms) { MPI_Barrier(involved_procs); }
  for(i=0; i<qty_comms; i++) {
    MPI_Isend(&aux, 1, MPI_CHAR, MAM_ROOT, 130, spawn_comm[i], &requests[i]);
  }
  if(qty_comms) { MPI_Waitall(qty_comms, requests, MPI_STATUSES_IGNORE); }
  
  if(requests != NULL) { free(requests); }
  if(involved_procs != MPI_COMM_NULL) { MPI_Comm_disconnect(&involved_procs); }
}



void binary_tree_connection(int groups, int group_id, Spawn_ports *spawn_port, MPI_Comm *newintracomm) {
  int service_id;
  int middle, new_groups, new_group_id, new_rank;
  MPI_Comm merge_comm, aux_comm, new_intercomm;

  // FIXME -- Supposes there is no changes in each group before this point
  //        - If there are any, they should be reflected in mall->comm
  //          and here should be used a duplicated of mall->comm.
  //          As of now is not used for simplicity
  merge_comm = aux_comm = MPI_COMM_WORLD;
  new_intercomm = MPI_COMM_NULL;
  new_rank = mall->myId;

  while(groups > 1) {
    middle = groups / 2;
    new_groups = ceil(groups / 2.0);
    if(group_id < middle) {
      //Accept work
      MPI_Comm_accept(spawn_port->port_name, MPI_INFO_NULL, MAM_ROOT, merge_comm, &new_intercomm);
      MPI_Intercomm_merge(new_intercomm, 0, &aux_comm); //El que pone 0 va primero
      if(merge_comm != MPI_COMM_WORLD && merge_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&merge_comm);
      if(new_intercomm != MPI_COMM_WORLD && new_intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&new_intercomm);
      merge_comm = aux_comm;
      MPI_Bcast(&new_groups, 1, MPI_INT, MAM_ROOT, aux_comm);

    } else if(group_id >= new_groups) {
      new_group_id = groups - group_id - 1;
      service_id = new_rank == MAM_ROOT ? new_group_id : MAM_SERVICE_UNNEEDED;
      discover_remote_port(service_id, spawn_port);

      // Connect work
      MPI_Comm_connect(spawn_port->remote_port, MPI_INFO_NULL, MAM_ROOT, merge_comm, &new_intercomm);
      MPI_Intercomm_merge(new_intercomm, 1, &aux_comm); //El que pone 0 va primero
      if(merge_comm != MPI_COMM_WORLD && merge_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&merge_comm);
      if(new_intercomm != MPI_COMM_WORLD && new_intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&new_intercomm);
      merge_comm = aux_comm;

      // Get new id
      group_id = new_group_id;
      new_rank = -1;
      MPI_Bcast(&new_groups, 1, MPI_INT, MAM_ROOT, aux_comm);
    }
    groups = new_groups;
  }

  *newintracomm =  merge_comm;
}

void binary_tree_reorder(MPI_Comm *newintracomm, int group_id) {
  int expected_rank;
  MPI_Comm aux_comm;

  // FIXME Expects all groups having the same size
  expected_rank = mall->numP * group_id + mall->myId;
  MPI_Comm_split(*newintracomm, 0, expected_rank, &aux_comm);

  //int merge_rank, new_rank;
  //MPI_Comm_rank(*newintracomm, &merge_rank);
  //MPI_Comm_rank(aux_comm, &new_rank);
  //printf("Grupo %d -- Merge rank = %d - New rank = %d\n", group_id, merge_rank, new_rank);

  if(*newintracomm != MPI_COMM_WORLD && *newintracomm != MPI_COMM_NULL) MPI_Comm_disconnect(newintracomm);
  *newintracomm = aux_comm;
}
