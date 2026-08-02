#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"
#include "PortService.h"
#include "Strategy_Parallel.h"
#include "ProcessDist.h"
#include "SpawnUtils.h"
#include <math.h>

/**
 * @file Strategy_Parallel.c
 * @brief Implementation of the Parallel (cascading/recursive) spawn strategy.
 */

/**
 * @brief Hypercube++ cascading spawn step: compute and create this rank's own
 *        next subgroup(s) of children (homogeneous group sizes only).
 *
 * Used when ::check_homogenous_dist reports that every used node hosts the
 * same number of spawned processes (with a possible exception for the last
 * node). Both the original sources and every previously spawned rank call
 * this function once, each independently deriving -- from its own
 * @p i_group_id, @c mall->myId and @p i_init_step -- which further group ids
 * it is responsible for spawning next, so that the whole spawn tree fans out
 * with a branching factor of @c (1+num_cpus) per step (@c num_cpus being the
 * homogeneous number of processes spawned per node). No extra communication
 * between spawners is required: every rank can reproduce the same strict
 * spawn order locally.
 *
 * @warning FIXME: not thread-safe. Each spawn writes the new child's group
 *          id into @c mall->gid (a single global) right before calling
 *          ::mam_spawn, relying on the spawn completing before it could be
 *          overwritten; this function must not be called concurrently by
 *          multiple threads of the same rank.
 *
 * @param[in]  i_group_id  This rank's own group id (negative, offset by
 *                           @p i_init_nodes, for the original sources).
 * @param[in]  i_groups    Total number of groups once every requested
 *                           process has been spawned (sources included).
 * @param[in]  i_init_nodes Number of nodes already used by the sources.
 * @param[in]  i_init_step Hypercube step this rank/group belongs to
 *                           (@c 0 for the original sources).
 * @param[out] o_spawn_comm Receives a newly allocated array of
 *                           intercommunicators, one per group spawned by
 *                           this rank (allocated only if at least one
 *                           further group needs to be spawned).
 * @param[out] o_qty_comms Receives the number of entries filled in
 *                           @p o_spawn_comm.
 *
 * @note FIXME: @p o_qty_comms is not computed correctly for processes
 *       sharing the same group id in the last steps -- the worst-case
 *       estimate used to size @p o_spawn_comm can be wrong for those ranks.
 */
void hypercube_spawn(int i_group_id, int i_groups, int i_init_nodes, int i_init_step, MPI_Comm **o_spawn_comm, int *o_qty_comms);

/**
 * @brief Iterative Diffusive cascading spawn step: compute and create this
 *        rank's own next subgroup(s) of children (heterogeneous group sizes).
 *
 * Used when ::check_homogenous_dist reports that used nodes do not all host
 * the same number of spawned processes. Ranks are assigned a unique,
 * globally-ordered @p i_exp_id (by the caller); on each cascading "wave" the
 * number of active spawner slots grows to match the total number of
 * processes spawned in the previous wave (hence the name: the spawning
 * capacity diffuses through the newly created processes instead of doubling
 * uniformly like ::hypercube_spawn). Within a wave, the rank whose
 * @p i_exp_id equals the current slot index spawns the next node's group
 * (with that node's own, possibly different, process count).
 *
 * @param[in]  i_exp_id   This rank's unique, globally-ordered expansion id.
 * @param[in]  i_groups   Number of new groups (children groups) still to be
 *                          spawned in total.
 * @param[in]  i_init_procs Initial number of active spawner slots (sources'
 *                          process count).
 * @param[out] o_spawn_comm Receives a newly allocated array of
 *                          intercommunicators, one per group spawned by this
 *                          rank (allocated only if this rank may need to
 *                          spawn at least one group).
 * @param[out] o_qty_comms Receives the number of entries filled in
 *                          @p o_spawn_comm.
 */
void diffusive_iterative_spawn(int i_exp_id, int i_groups, int i_init_procs, MPI_Comm **o_spawn_comm, int *o_qty_comms);

/**
 * @brief Relay upside/downside synchronisation tokens through the spawn tree
 *        so every group learns the whole cascading spawn has finished.
 *
 * Splits the calling group into a subcommunicator (@c involved_procs) made
 * of the root plus every rank that itself spawned further groups
 * (@p i_qty_comms @c > @c 0). Upside: each spawner waits (tag @c 130) for a
 * token from every group it spawned (arriving on @p i_spawn_comm), then all
 * spawners of this group barrier together once their whole subtree is done.
 * If this call represents a non-top-level group (@p i_intercomm @c !=
 * @c MPI_COMM_NULL), its root then forwards a token upward to its own
 * spawning parent on @p i_intercomm and waits for the downside "go ahead"
 * token back. Downside: once available, the spawners barrier again and each
 * one forwards a token (tag @c 130) down to every group it spawned on
 * @p i_spawn_comm, letting the whole subtree proceed.
 *
 * @param[in] i_spawn_data Spawn configuration; @c comm is used (only for the
 *                           very first/root call) as the communicator to
 *                           split among the sources.
 * @param[in] i_qty_comms  Number of further groups this rank itself spawned
 *                           (and thus number of valid entries in
 *                           @p i_spawn_comm).
 * @param[in] i_intercomm  Intercommunicator to this group's own spawning
 *                           parent, or @c MPI_COMM_NULL for the very first
 *                           call made by the original sources (the true
 *                           root of the recursion, which has no parent to
 *                           relay to).
 * @param[in] i_spawn_comm Array of intercommunicators (size
 *                           @p i_qty_comms) to the groups this rank spawned,
 *                           used to relay the upside/downside tokens.
 */
void common_synch(Spawn_data i_spawn_data, int i_qty_comms, MPI_Comm i_intercomm, MPI_Comm *i_spawn_comm);

/**
 * @brief One binary-tree pairwise-merge round, repeated until every spawned
 *        group has been folded into a single intracommunicator.
 *
 * Iteratively halves the number of independent groups (@p i_groups) by
 * pairing group id @c g with its mirror @c i_groups-g-1: the lower half
 * (@c group_id @c < @c groups/2) accepts an already-open port (opened
 * earlier by ::open_port using this same group id) while the upper half
 * looks up and connects to its mirror's port (::discover_remote_port); both
 * sides then merge the resulting intercommunicator with
 * @c MPI_Intercomm_merge (ranks that pass @c 0 come first). A group only
 * reachable when @p i_groups is odd (its id already falls below the next
 * round's group count) simply carries its communicator over unchanged for
 * that round. The loop repeats with the halved group count until a single
 * merged intracommunicator remains.
 *
 * @warning FIXME: assumes @c MPI_COMM_WORLD is still a valid representation
 *          of "everyone in this group" and that no prior group changes have
 *          happened before this point; if there had been any, they should
 *          be reflected in @c mall->comm and a duplicate of it should be
 *          used here instead. Kept as @c MPI_COMM_WORLD for simplicity.
 *
 * @param[in]     i_groups      Number of independent groups still to merge.
 * @param[in]     i_group_id    This rank's group id within @p i_groups.
 * @param[in,out] io_spawn_port Ports used to accept/connect/discover each
 *                                round's merge intercommunicator.
 * @param[out]    o_newintracomm Receives the final, fully merged
 *                                intracommunicator once only one group
 *                                remains.
 */
void binary_tree_connection(int i_groups, int i_group_id, Spawn_ports *io_spawn_port, MPI_Comm *o_newintracomm);

/**
 * @brief Reorder ranks inside the merged intracommunicator to match each
 *        rank's expected (logical) id.
 *
 * @c MPI_Intercomm_merge only guarantees a first-group/second-group
 * ordering, not the exact rank each process should end up with. Splitting
 * the whole communicator with a single color and @p i_expected_rank as the
 * ordering key produces an equivalent intracommunicator where ranks are
 * ordered by @p i_expected_rank instead.
 *
 * @param[in,out] io_newintracomm Communicator to reorder; replaced in place
 *                                 with the reordered one (the original is
 *                                 disconnected unless it is
 *                                 @c MPI_COMM_WORLD or @c MPI_COMM_NULL).
 * @param[in]     i_expected_rank Logical rank (e.g. the @c exp_id assigned
 *                                 during the Hypercube/Diffusive spawn) this
 *                                 process should have in the reordered comm.
 */
void binary_tree_reorder(MPI_Comm *io_newintracomm, int i_expected_rank);


//--------PUBLIC FUNCTIONS----------//
// Dispatch Hypercube or Iterative Diffusive, then synchronise, merge children,
// reorder ranks, and connect the merged children group to the parents.

/**
 * @brief Parents (sources) side of Parallel: cascading spawn, synchronise, then accept children.
 *
 * Broadcasts @c total_spawns to the sources, opens the top-level port that
 * the merged children will eventually connect to, then dispatches to either
 * ::hypercube_spawn or ::diffusive_iterative_spawn depending on
 * ::check_homogenous_dist to cascade-spawn the first level of children.
 * Runs ::common_synch (as the root of the recursion, with no outer parent)
 * to wait until every spawned subtree has finished spawning, disconnects the
 * temporary spawn-tree communicators, and finally @c MPI_Comm_accept's the
 * connection from the fully merged group of children.
 *
 * @param[in]     i_spawn_data Spawn configuration (@c total_spawns,
 *                               @c initial_qty, @c comm).
 * @param[in,out] io_spawn_port Ports structure used to open the top-level
 *                               port and later accept the children.
 * @param[out]    o_child      Receives the parent-children intercommunicator
 *                               once the merged children connect back.
 */
void parallel_strat_parents(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *o_child) {
  int opening, qty_comms;
  int groups, init_nodes;
  MPI_Comm *spawn_comm = NULL;

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel PA started", mall->myId, mall->numP); fflush(stdout);
  #endif

  MPI_Bcast(&i_spawn_data.total_spawns, 1, MPI_INT, mall->root, i_spawn_data.comm);
  qty_comms = 0;
  init_nodes = 0;
  for(int i = 0; i < mall->num_nodes; i++) { if(mall->assigned_cpus[i]) { init_nodes++; } }
  groups = i_spawn_data.total_spawns + init_nodes;

  opening = mall->myId == mall->root ? 1 : 0;
  open_port(io_spawn_port, opening, groups);

  // Choose specific algorithm
  if(check_homogenous_dist()) {
    #if MAM_DEBUG >= 4
      DEBUG_FUNC("Additional spawn action - Parallel PA uses Hypercube", mall->myId, mall->numP); fflush(stdout);
    #endif
    int group_id = -init_nodes;
    int actual_step = 0;
    hypercube_spawn(group_id, groups, init_nodes, actual_step, &spawn_comm, &qty_comms);
  } else {
    #if MAM_DEBUG >= 4
      DEBUG_FUNC("Additional spawn action - Parallel PA uses Diffusive Iterative", mall->myId, mall->numP); fflush(stdout);
    #endif
    diffusive_iterative_spawn(mall->myId, groups-init_nodes, i_spawn_data.initial_qty, &spawn_comm, &qty_comms);
  }

  common_synch(i_spawn_data, qty_comms, MPI_COMM_NULL, spawn_comm);

  for(int i=0; i<qty_comms; i++) { MPI_Comm_disconnect(&spawn_comm[i]); }
  if(spawn_comm != NULL) free(spawn_comm); 
  MPI_Comm_accept(io_spawn_port->port_name, MPI_INFO_NULL, MAM_ROOT, i_spawn_data.comm, o_child);

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel PA completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

/**
 * @brief Children side of Parallel: continue the cascading spawn, synchronise,
 *        merge with sibling groups, and reconnect to the sources.
 *
 * Recomputes this group's id/group count from @c mall, opens this group's
 * port if it belongs to the lower half of the binary tree, and -- depending
 * on ::check_homogenous_dist -- possibly cascades further spawns of its own
 * (::hypercube_spawn / ::diffusive_iterative_spawn) before running
 * ::common_synch against its own immediate parent group. After
 * disconnecting the temporary spawn-tree and parent communicators, merges
 * with every sibling group into one intracommunicator
 * (::binary_tree_connection), reorders ranks to their expected logical id
 * (::binary_tree_reorder), discovers the sources' published port and
 * @c MPI_Comm_connect's back to them, then updates MaM's communicators to
 * the newly merged group.
 *
 * @param[in]     i_spawn_data Spawn configuration (@c initial_qty).
 * @param[in,out] io_spawn_port Ports structure used for this group's own
 *                               port and for discovering sibling/parent ports.
 * @param[in,out] io_parents   Intercommunicator to this group's immediate
 *                               spawning parent on entry; replaced by the
 *                               new intercommunicator to the sources on exit.
 */
void parallel_strat_children(Spawn_data i_spawn_data, Spawn_ports *io_spawn_port, MPI_Comm *io_parents) {
  int i, exp_id, group_id, opening, qty_comms;
  int groups, init_nodes;
  MPI_Comm newintracomm, *spawn_comm = NULL;
  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel CH started", mall->myId, mall->numP); fflush(stdout);
  #endif

  qty_comms = 0;
  group_id = mall->gid;
  init_nodes = 0;
  groups = 0;
  for(i = 0; i < mall->num_nodes; i++) { 
    if(mall->assigned_cpus[i]) { init_nodes++; } 
    if(mall->spawned_cpus[i]) { groups++; }
  }
  groups += init_nodes;

  opening = (mall->myId == MAM_ROOT && group_id < (groups-init_nodes)/2) ? 1 : 0;
  open_port(io_spawn_port, opening, group_id);

  if(check_homogenous_dist()) {
    #if MAM_DEBUG >= 4
      DEBUG_FUNC("Additional spawn action - Parallel CH uses Hypercube", mall->myId, mall->numP); fflush(stdout);
    #endif
    if(groups - init_nodes > i_spawn_data.initial_qty) { 
      int actual_step = log((group_id + init_nodes) / init_nodes) / log(1 + mall->numP);
      actual_step = floor(actual_step) + 1;
      hypercube_spawn(group_id, groups, init_nodes, actual_step, &spawn_comm, &qty_comms); 
    }
    exp_id = mall->numP * group_id + mall->myId;
  } else {
    #if MAM_DEBUG >= 4
      DEBUG_FUNC("Additional spawn action - Parallel CH uses Diffusive Iterative", mall->myId, mall->numP); fflush(stdout);
    #endif
    int i_real = 0;
    exp_id = i_spawn_data.initial_qty + mall->myId;
    for(i = 0; i < group_id; i_real++) { 
      if(mall->spawned_cpus[i_real]) { 
        exp_id += mall->spawned_cpus[i_real]; 
        i++; 
      } 
    }
    diffusive_iterative_spawn(exp_id, groups-init_nodes, i_spawn_data.initial_qty, &spawn_comm, &qty_comms);
  }

  common_synch(i_spawn_data, qty_comms, *io_parents, spawn_comm);
  for(i=0; i<qty_comms; i++) { MPI_Comm_disconnect(&spawn_comm[i]); }
  MPI_Comm_disconnect(io_parents);

  // Connect groups and ensure expected rank order
  binary_tree_connection(groups - init_nodes, group_id, io_spawn_port, &newintracomm);
  binary_tree_reorder(&newintracomm, exp_id);

  // Create intercomm between sources and children
  opening = (mall->myId == mall->root && !group_id) ? groups : MAM_SERVICE_UNNEEDED;
  discover_remote_port(opening, io_spawn_port);
  MPI_Comm_connect(io_spawn_port->remote_port, MPI_INFO_NULL, MAM_ROOT, newintracomm, io_parents);

  // New group obtained -- Adjust ranks and comms
  MAM_comms_update(newintracomm);
  MPI_Comm_rank(mall->comm, &mall->myId);
  MPI_Comm_size(mall->comm, &mall->numP);
  MPI_Comm_disconnect(&newintracomm);

  #if MAM_DEBUG >= 4
    DEBUG_FUNC("Additional spawn action - Parallel CH completed", mall->myId, mall->numP); fflush(stdout);
  #endif
}

//--------PRIVATE FUNCTIONS----------//

/*=====================HYPERCUBE++ ALGORITHM=====================*/
//The following algorithm divides the spawning task across all available ranks.
//It starts with just the sources, and then all spawned processes help with further
//spawns until all the required processes have been created.  

//       - The amount of processes per spawned group must be homogenous among groups
//       - There is an exception for the last node, which could have less procs
// This function does not allow the same process to have multiple threads executing it.
// This function only works when array spawned_cpus has the same values 
// for all indexes(ignoring 0 values) and the spawn follows a strict order.
/**
 * @brief Hypercube++ cascading spawn step: compute and create this rank's own
 *        next subgroup(s) of children (homogeneous group sizes only).
 *
 * Used when ::check_homogenous_dist reports that every used node hosts the
 * same number of spawned processes (with a possible exception for the last
 * node). Both the original sources and every previously spawned rank call
 * this function once, each independently deriving -- from its own
 * @p i_group_id, @c mall->myId and @p i_init_step -- which further group ids
 * it is responsible for spawning next, so that the whole spawn tree fans out
 * with a branching factor of @c (1+num_cpus) per step (@c num_cpus being the
 * homogeneous number of processes spawned per node). No extra communication
 * between spawners is required: every rank can reproduce the same strict
 * spawn order locally.
 *
 * @warning FIXME: not thread-safe. Each spawn writes the new child's group
 *          id into @c mall->gid (a single global) right before calling
 *          ::mam_spawn, relying on the spawn completing before it could be
 *          overwritten; this function must not be called concurrently by
 *          multiple threads of the same rank.
 *
 * @param[in]  i_group_id  This rank's own group id (negative, offset by
 *                           @p i_init_nodes, for the original sources).
 * @param[in]  i_groups    Total number of groups once every requested
 *                           process has been spawned (sources included).
 * @param[in]  i_init_nodes Number of nodes already used by the sources.
 * @param[in]  i_init_step Hypercube step this rank/group belongs to
 *                           (@c 0 for the original sources).
 * @param[out] o_spawn_comm Receives a newly allocated array of
 *                           intercommunicators, one per group spawned by
 *                           this rank (allocated only if at least one
 *                           further group needs to be spawned).
 * @param[out] o_qty_comms Receives the number of entries filled in
 *                           @p o_spawn_comm.
 *
 * @note FIXME: @p o_qty_comms is not computed correctly for processes
 *       sharing the same group id in the last steps -- the worst-case
 *       estimate used to size @p o_spawn_comm can be wrong for those ranks.
 */
void hypercube_spawn(int i_group_id, int i_groups, int i_init_nodes, int i_init_step, 
                  MPI_Comm **o_spawn_comm, int *o_qty_comms) {
  int i,  aux_sum, actual_step, num_cpus;
  int next_group_id, actual_nodes, tmp_job_id_used;
  int n=0;
  char *file_name = NULL;
  char *tmp_job_id = NULL;
  Spawn_set set;
 
  i=0;
  tmp_job_id_used = 0;
  while(!mall->spawned_cpus[i]) { i++; }
  num_cpus = mall->spawned_cpus[i];
  
  actual_step = i_init_step;
  actual_nodes = pow(1+num_cpus, actual_step)*i_init_nodes - i_init_nodes;
  aux_sum = num_cpus*(i_init_nodes + i_group_id) + mall->myId; //Constant sum for next line
  next_group_id = actual_nodes + aux_sum;
  if(next_group_id < i_groups - i_init_nodes) { //FIXME: qty_comms is not computed correctly for processes with the same group_id in the last steps
    int max_steps = ceil(log(i_groups / i_init_nodes) / log(1 + num_cpus));
    *o_qty_comms = max_steps - actual_step;
    *o_spawn_comm = (MPI_Comm *) malloc(*o_qty_comms * sizeof(MPI_Comm));
  }
  //if(mall->myId == 0)printf("T1 P%d+%d step=%d next_id=%d aux_sum=%d actual_nodes=%d comms=%d\n", mall->myId, i_group_id, actual_step, next_group_id, aux_sum, actual_nodes, *o_qty_comms);

#if MAM_USE_SLURM
  tmp_job_id = getenv("SLURM_JOB_ID");
#endif
  if(tmp_job_id == NULL) { 
    tmp_job_id = malloc(2 * sizeof *tmp_job_id);
    snprintf(tmp_job_id, 2, "0"); 
    tmp_job_id_used = 1;
  }

  set.cmd = get_spawn_cmd();
  i = 0;
  while(next_group_id < i_groups - i_init_nodes) {
    set_hostfile_name(&file_name, &n, tmp_job_id, next_group_id);
    set.spawn_qty = num_cpus;
    MPI_Info_create(&set.mapping);
	  MPI_Info_set(set.mapping, "hostfile", file_name);
    mall->gid = next_group_id; // Used to pass the group id to the spawned process // Not thread safe
    mam_spawn(set, MPI_COMM_SELF, &(*o_spawn_comm)[i]);
    MPI_Info_free(&set.mapping);

    actual_step++; i++;
    actual_nodes = pow(1+num_cpus, actual_step)*i_init_nodes - i_init_nodes;
    next_group_id = actual_nodes + aux_sum;
  }
  *o_qty_comms = i;
  if(file_name != NULL) free(file_name); 
  if(tmp_job_id_used) free(tmp_job_id);
}

/*=====================Iterative Diffusive ALGORITHM=====================*/
//The following algorithm divides the spawning task across all available ranks.
//It starts with just the sources, and then all spawned processes help with further
//spawns until all the required processes have been created.
//The main difference against the Hypercube is that it allows to have differents amount
//of ranks in each spawned group.
/**
 * @brief Iterative Diffusive cascading spawn step: compute and create this
 *        rank's own next subgroup(s) of children (heterogeneous group sizes).
 *
 * Used when ::check_homogenous_dist reports that used nodes do not all host
 * the same number of spawned processes. Ranks are assigned a unique,
 * globally-ordered @p i_exp_id (by the caller); on each cascading "wave" the
 * number of active spawner slots grows to match the total number of
 * processes spawned in the previous wave (hence the name: the spawning
 * capacity diffuses through the newly created processes instead of doubling
 * uniformly like ::hypercube_spawn). Within a wave, the rank whose
 * @p i_exp_id equals the current slot index spawns the next node's group
 * (with that node's own, possibly different, process count).
 *
 * @param[in]  i_exp_id   This rank's unique, globally-ordered expansion id.
 * @param[in]  i_groups   Number of new groups (children groups) still to be
 *                          spawned in total.
 * @param[in]  i_init_procs Initial number of active spawner slots (sources'
 *                          process count).
 * @param[out] o_spawn_comm Receives a newly allocated array of
 *                          intercommunicators, one per group spawned by this
 *                          rank (allocated only if this rank may need to
 *                          spawn at least one group).
 * @param[out] o_qty_comms Receives the number of entries filled in
 *                          @p o_spawn_comm.
 */
void diffusive_iterative_spawn(int i_exp_id, int i_groups, int i_init_procs, MPI_Comm **o_spawn_comm, int *o_qty_comms) {
  int i = 0, i_comm = 0;
  int n=0, tmp_job_id_used = 0;
  char *file_name = NULL;
  char *tmp_job_id = NULL;
  Spawn_set set;

  int actual_procs, new_procs, spawned_nodes;
  actual_procs = new_procs = i_init_procs;
  spawned_nodes = 0;
  set.cmd = get_spawn_cmd();
#if MAM_USE_SLURM
  tmp_job_id = getenv("SLURM_JOB_ID");
#endif
  if(tmp_job_id == NULL) { 
    tmp_job_id = malloc(2 * sizeof *tmp_job_id);
    snprintf(tmp_job_id, 2, "0");
    tmp_job_id_used = 1;
  }

  *o_spawn_comm = NULL;
  if(i_exp_id < i_groups) {  // Overexpect the worst case for this array
    *o_qty_comms = ceil(i_groups/2.0);
    *o_spawn_comm = (MPI_Comm *) malloc(*o_qty_comms * sizeof(MPI_Comm));
  }
  
  while(i < mall->num_nodes) {
    for(int j = 0; j < actual_procs && i < mall->num_nodes; j++) {

      // Ignore nodes that do not need to spawn anything
      while(i < mall->num_nodes && !mall->spawned_cpus[i]) {i++;}
      if(i >= mall->num_nodes) { break; }

      if(i_exp_id == j) {
        //printf("P%d is expanding to node %d\n", i_exp_id, spawned_nodes); fflush(stdout);
        set_hostfile_name(&file_name, &n, tmp_job_id, spawned_nodes);
        set.spawn_qty = mall->spawned_cpus[i]; 
        MPI_Info_create(&set.mapping);
        MPI_Info_set(set.mapping, "hostfile", file_name);
        mall->gid = spawned_nodes; // Used to pass the group id to the spawned process // Not thread safe
        mam_spawn(set, MPI_COMM_SELF, &(*o_spawn_comm)[i_comm]);
        MPI_Info_free(&set.mapping);
        i_comm++;
      }
      new_procs += mall->spawned_cpus[i];
      i++; spawned_nodes++;
    }
    actual_procs = new_procs;
  }

  *o_qty_comms = i_comm;
  if(file_name != NULL) free(file_name); 
  if(!i_comm && *o_spawn_comm != NULL) free(*o_spawn_comm);
  if(tmp_job_id_used) free(tmp_job_id);
}

/*=====================Parallel private functions=====================*/

/**
 * @brief Relay upside/downside synchronisation tokens through the spawn tree
 *        so every group learns the whole cascading spawn has finished.
 *
 * Splits the calling group into a subcommunicator (@c involved_procs) made
 * of the root plus every rank that itself spawned further groups
 * (@p i_qty_comms @c > @c 0). Upside: each spawner waits (tag @c 130) for a
 * token from every group it spawned (arriving on @p i_spawn_comm), then all
 * spawners of this group barrier together once their whole subtree is done.
 * If this call represents a non-top-level group (@p i_intercomm @c !=
 * @c MPI_COMM_NULL), its root then forwards a token upward to its own
 * spawning parent on @p i_intercomm and waits for the downside "go ahead"
 * token back. Downside: once available, the spawners barrier again and each
 * one forwards a token (tag @c 130) down to every group it spawned on
 * @p i_spawn_comm, letting the whole subtree proceed.
 *
 * @param[in] i_spawn_data Spawn configuration; @c comm is used (only for the
 *                           very first/root call) as the communicator to
 *                           split among the sources.
 * @param[in] i_qty_comms  Number of further groups this rank itself spawned
 *                           (and thus number of valid entries in
 *                           @p i_spawn_comm).
 * @param[in] i_intercomm  Intercommunicator to this group's own spawning
 *                           parent, or @c MPI_COMM_NULL for the very first
 *                           call made by the original sources (the true
 *                           root of the recursion, which has no parent to
 *                           relay to).
 * @param[in] i_spawn_comm Array of intercommunicators (size
 *                           @p i_qty_comms) to the groups this rank spawned,
 *                           used to relay the upside/downside tokens.
 */
void common_synch(Spawn_data i_spawn_data, int i_qty_comms, MPI_Comm i_intercomm, MPI_Comm *i_spawn_comm) {
  int i, color;
  char aux;
  MPI_Request *requests = NULL;
  MPI_Comm involved_procs, aux_comm;
  
  requests = (MPI_Request *) malloc(i_qty_comms * sizeof(MPI_Request));

  aux_comm = i_intercomm == MPI_COMM_NULL ? i_spawn_data.comm : mall->comm;
  color = i_qty_comms ? 1 : MPI_UNDEFINED;
  MPI_Comm_split(aux_comm, color, mall->myId, &involved_procs);

  // Upside synchronization starts
  for(i=0; i<i_qty_comms; i++) {
    MPI_Irecv(&aux, 1, MPI_CHAR, MAM_ROOT, 130, i_spawn_comm[i], &requests[i]);
  }
  if(i_qty_comms) { 
    MPI_Waitall(i_qty_comms, requests, MPI_STATUSES_IGNORE);
    MPI_Barrier(involved_procs);
  }
  // Sources are the only synchronized procs at this point
  if(i_intercomm != MPI_COMM_NULL && mall->myId == MAM_ROOT) { 
    MPI_Send(&aux, 1, MPI_CHAR, MAM_ROOT, 130, i_intercomm); 
  // Upside synchronization ends
  // Downside synchronization starts
    MPI_Recv(&aux, 1, MPI_CHAR, MAM_ROOT, 130, i_intercomm, MPI_STATUS_IGNORE);
  }

  if(i_intercomm != MPI_COMM_NULL && i_qty_comms) { MPI_Barrier(involved_procs); }
  for(i=0; i<i_qty_comms; i++) {
    MPI_Isend(&aux, 1, MPI_CHAR, MAM_ROOT, 130, i_spawn_comm[i], &requests[i]);
  }
  if(i_qty_comms) { MPI_Waitall(i_qty_comms, requests, MPI_STATUSES_IGNORE); }
  
  if(requests != NULL) { free(requests); }
  if(involved_procs != MPI_COMM_NULL) { MPI_Comm_disconnect(&involved_procs); }
}



/**
 * @brief One binary-tree pairwise-merge round, repeated until every spawned
 *        group has been folded into a single intracommunicator.
 *
 * Iteratively halves the number of independent groups (@p i_groups) by
 * pairing group id @c g with its mirror @c i_groups-g-1: the lower half
 * (@c group_id @c < @c groups/2) accepts an already-open port (opened
 * earlier by ::open_port using this same group id) while the upper half
 * looks up and connects to its mirror's port (::discover_remote_port); both
 * sides then merge the resulting intercommunicator with
 * @c MPI_Intercomm_merge (ranks that pass @c 0 come first). A group only
 * reachable when @p i_groups is odd (its id already falls below the next
 * round's group count) simply carries its communicator over unchanged for
 * that round. The loop repeats with the halved group count until a single
 * merged intracommunicator remains.
 *
 * @warning FIXME: assumes @c MPI_COMM_WORLD is still a valid representation
 *          of "everyone in this group" and that no prior group changes have
 *          happened before this point; if there had been any, they should
 *          be reflected in @c mall->comm and a duplicate of it should be
 *          used here instead. Kept as @c MPI_COMM_WORLD for simplicity.
 *
 * @param[in]     i_groups      Number of independent groups still to merge.
 * @param[in]     i_group_id    This rank's group id within @p i_groups.
 * @param[in,out] io_spawn_port Ports used to accept/connect/discover each
 *                                round's merge intercommunicator.
 * @param[out]    o_newintracomm Receives the final, fully merged
 *                                intracommunicator once only one group
 *                                remains.
 */
void binary_tree_connection(int i_groups, int i_group_id, Spawn_ports *io_spawn_port, MPI_Comm *o_newintracomm) {
  int service_id;
  int middle, new_groups, new_group_id, new_rank;
  MPI_Comm merge_comm, aux_comm, new_intercomm;

  // FIXME: Assumes there are no changes in each group before this point.
  //        - If there are any, they should be reflected in mall->comm
  //          and here should be used a duplicated of mall->comm.
  //          As of now is not used for simplicity
  merge_comm = aux_comm = MPI_COMM_WORLD;
  new_intercomm = MPI_COMM_NULL;
  new_rank = mall->myId;

  while(i_groups > 1) {
    middle = i_groups / 2;
    new_groups = ceil(i_groups / 2.0);
    if(i_group_id < middle) {
      //Accept work
      MPI_Comm_accept(io_spawn_port->port_name, MPI_INFO_NULL, MAM_ROOT, merge_comm, &new_intercomm);
      MPI_Intercomm_merge(new_intercomm, 0, &aux_comm); //ranks that pass 0 come first
      if(merge_comm != MPI_COMM_WORLD && merge_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&merge_comm);
      if(new_intercomm != MPI_COMM_WORLD && new_intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&new_intercomm);
      merge_comm = aux_comm;
      MPI_Bcast(&new_groups, 1, MPI_INT, MAM_ROOT, aux_comm);

    } else if(i_group_id >= new_groups) {
      new_group_id = i_groups - i_group_id - 1;
      service_id = new_rank == MAM_ROOT ? new_group_id : MAM_SERVICE_UNNEEDED;
      discover_remote_port(service_id, io_spawn_port);

      // Connect work
      MPI_Comm_connect(io_spawn_port->remote_port, MPI_INFO_NULL, MAM_ROOT, merge_comm, &new_intercomm);
      MPI_Intercomm_merge(new_intercomm, 1, &aux_comm); //ranks that pass 0 come first
      if(merge_comm != MPI_COMM_WORLD && merge_comm != MPI_COMM_NULL) MPI_Comm_disconnect(&merge_comm);
      if(new_intercomm != MPI_COMM_WORLD && new_intercomm != MPI_COMM_NULL) MPI_Comm_disconnect(&new_intercomm);
      merge_comm = aux_comm;

      // Get new id
      i_group_id = new_group_id;
      new_rank = -1;
      MPI_Bcast(&new_groups, 1, MPI_INT, MAM_ROOT, aux_comm);
    }
    i_groups = new_groups;
  }

  *o_newintracomm =  merge_comm;
}

/**
 * @brief Reorder ranks inside the merged intracommunicator to match each
 *        rank's expected (logical) id.
 *
 * @c MPI_Intercomm_merge only guarantees a first-group/second-group
 * ordering, not the exact rank each process should end up with. Splitting
 * the whole communicator with a single color and @p i_expected_rank as the
 * ordering key produces an equivalent intracommunicator where ranks are
 * ordered by @p i_expected_rank instead.
 *
 * @param[in,out] io_newintracomm Communicator to reorder; replaced in place
 *                                 with the reordered one (the original is
 *                                 disconnected unless it is
 *                                 @c MPI_COMM_WORLD or @c MPI_COMM_NULL).
 * @param[in]     i_expected_rank Logical rank (e.g. the @c exp_id assigned
 *                                 during the Hypercube/Diffusive spawn) this
 *                                 process should have in the reordered comm.
 */
void binary_tree_reorder(MPI_Comm *io_newintracomm, int i_expected_rank) {
  MPI_Comm aux_comm;

  MPI_Comm_split(*io_newintracomm, 0, i_expected_rank, &aux_comm);

  if(*io_newintracomm != MPI_COMM_WORLD && *io_newintracomm != MPI_COMM_NULL) MPI_Comm_disconnect(io_newintracomm);
  *io_newintracomm = aux_comm;
}
