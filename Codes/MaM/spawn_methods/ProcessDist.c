#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>
#include "ProcessDist.h"
#include "SpawnUtils.h"
#include "MAM_Constants.h"
#include "MAM_DataStructures.h"

/**
 * @file ProcessDist.c
 * @brief Implementation of MaM's physical distribution of newly spawned
 *        children, MPI_Info mapping generation (string/hostfile, with or
 *        without Slurm), and hostfile I/O.
 */

//--------------PRIVATE CONSTANTS------------------//
#define MAM_HOSTFILE_NAME1 "MAM_HF_ID"  // Constant size name (9) -- Part of SIZE1
#define MAM_HOSTFILE_NAME2 "_S"  // Constant size name (2) -- Part of SIZE1
#define MAM_HOSTFILE_NAME3 ".tmp"  // Constant size name (4) -- Part of SIZE2
#define MAM_HOSTFILE_SIZE1 11 // 11 Chars (Does not count slurm job id)
#define MAM_HOSTFILE_SIZE2 8 // 4 Chars + 3 Digits + \0
#define MAM_HOSTFILE_SIZE MAM_HOSTFILE_SIZE1 + MAM_HOSTFILE_SIZE2 //19 = 11 Chars + 3 Digits + \0
#define MAM_HOSTFILE_LINE_SIZE 32

//--------------PRIVATE DECLARATIONS---------------//

void node_dist(Spawn_data i_spawn_data, int *o_used_nodes, int *o_total_spawns);
void spread_dist(Spawn_data i_spawn_data, int *o_used_nodes);
void compact_dist(Spawn_data i_spawn_data, int *o_used_nodes);

void generate_info_string(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data);
void generate_multiple_info_string(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data);

void set_mapping_host(int i_qty, char *i_info_type, char *i_host, size_t i_index, Spawn_data *io_spawn_data);
void fill_str_hosts(char *i_nodelist, int *i_qty, size_t i_used_nodes, char **o_hostlist_str);
int write_str_node(char **io_hostlist_str, size_t i_len_og, size_t i_qty, char *i_node_name);
int write_hostfile_node(int i_file, int i_qty, char *i_node_name, char **io_line, size_t *io_len_og);
//--------------------------------SLURM USAGE-------------------------------------//
#if MAM_USE_SLURM
#include <slurm/slurm.h>
void generate_info_string_slurm(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data);
void generate_multiple_info_string_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, Spawn_data *io_spawn_data);
void fill_str_hosts_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, char **o_hostlist_str);

void generate_info_hostfile_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, Spawn_data *io_spawn_data);
void fill_hostfile_slurm(char* i_file_name, size_t i_used_nodes, int *i_qty, hostlist_t *io_hostlist);
void fill_multiple_hostfile_slurm(char* i_file_name, int *i_qty, size_t *io_index, hostlist_t *io_hostlist, char **io_line, size_t *io_len_line);
#endif
//--------------------------------SLURM USAGE-------------------------------------//

//--------------PUBLIC FUNCTIONS---------------//

/**
 * @brief Compute the physical distribution of children to spawn and build the
 *        MPI_Info mapping used by MPI_Comm_spawn.
 *
 * Determines how many nodes/cores each new spawn set will use, allocates the
 * array of spawn sets, and fills each set's command and host/hostfile
 * MPI_Info describing where the children will be created.
 *
 * @param[in,out] io_spawn_data Spawn configuration; @c total_spawns and
 *                               @c sets are computed/allocated here.
 */
void processes_dist(Spawn_data *io_spawn_data) {
  int used_nodes=0;

  // GET NEW DISTRIBUTION 
  node_dist(*io_spawn_data, &used_nodes, &io_spawn_data->total_spawns);
  io_spawn_data->sets = (Spawn_set *) malloc(io_spawn_data->total_spawns * sizeof(Spawn_set));
#if MAM_USE_SLURM
  switch(io_spawn_data->mapping_fill_method) {
    case MAM_PHY_TYPE_STRING:
      if(io_spawn_data->spawn_is_multiple || io_spawn_data->spawn_is_parallel) {
        generate_multiple_info_string_slurm(mall->nodelist, mall->spawned_cpus, used_nodes, io_spawn_data);
      } else {
        generate_info_string_slurm(mall->nodelist, mall->spawned_cpus, used_nodes, io_spawn_data);
      }
      break;
    case MAM_PHY_TYPE_HOSTFILE:
      generate_info_hostfile_slurm(mall->nodelist, mall->spawned_cpus, used_nodes, io_spawn_data);
      break;
  }
#else
  if(io_spawn_data->spawn_is_multiple || io_spawn_data->spawn_is_parallel) {
    generate_multiple_info_string(mall->nodelist, mall->spawned_cpus, used_nodes, io_spawn_data);
  } else {
    generate_info_string(mall->nodelist, mall->spawned_cpus, used_nodes, io_spawn_data);
  }
#endif
  char *aux_cmd = get_spawn_cmd();
  for(int index = 0; index<io_spawn_data->total_spawns; index++) {
    io_spawn_data->sets[index].cmd = aux_cmd;
  }

  #if MAM_DEBUG >= 2
    printf("SPAWN_CPUS:     [");
    for(int i_debug=0; i_debug < mall->num_nodes; i_debug++) {
      printf("%d ", mall->spawned_cpus[i_debug]);
    }
    printf("]\n");
    fflush(stdout); 
  #endif
}

/**
 * @brief Check whether all currently used nodes host the same number of
 *        spawned processes.
 *
 * @return Non-zero if the distribution is homogeneous (or no node is used),
 *         0 otherwise.
 */
int check_homogenous_dist(void) {
  int actual, last, i;

  i = 0;
  while(!mall->spawned_cpus[i]) { i++; }
  if(i >= mall->num_nodes) { return 1; }

  last = mall->spawned_cpus[i];
  for(i++; i < mall->num_nodes; i++) {
    actual = mall->spawned_cpus[i];
    if(actual != 0) { 
      if(last != actual) { return 0; }
      last = actual; 
    }
  }
  return 1;
}

/**
 * @brief Mark the ranks to be removed as free space in MaM's logical
 *        per-node occupancy state.
 *
 * @param[in] i_spawn_data Spawn configuration; @c initial_qty and
 *                          @c target_qty give the number of ranks to free.
 */
// FIXME: Assumes the library can freely choose which ranks/nodes should be returned.
// TODO: Should consider removing full nodes if possible, with preference to Intercomm nodes.
void remove_dist(Spawn_data i_spawn_data) {
  if(i_spawn_data.initial_qty <= i_spawn_data.target_qty) return;

  int i;
  int to_remove_ranks=i_spawn_data.initial_qty - i_spawn_data.target_qty;
  for(i=mall->num_nodes-1; 0 <= i && 0 < to_remove_ranks; i--) {
    if(mall->assigned_cpus[i]) {
      to_remove_ranks -= mall->assigned_cpus[i];
      mall->spawned_cpus[i] -= mall->assigned_cpus[i];
    }
  }
  //Last node could try to remove more than needed
  mall->spawned_cpus[i+1] -= to_remove_ranks;

  if(0 < to_remove_ranks) {
    perror("ProcesDist Shrink Error. Target amount of cores cannot be reached\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(-1);
  }

#if MAM_DEBUG >= 2
  if(mall->myId == mall->root) {
    printf("SPAWN_CPUS:     [");
    for(int i_debug=0; i_debug < mall->num_nodes; i_debug++) {
      printf("%d ", mall->spawned_cpus[i_debug]);
    }
    printf("]\n");
    fflush(stdout); 
  }
#endif
}

/**
 * @brief Build (or extend) the hostfile name for a given job id and spawn
 *        index.
 *
 * On the first call for a given file (@p io_n == 0) allocates and writes the
 * full name; subsequent calls only patch the numeric index suffix.
 *
 * @param[in,out] io_file_name Pointer to the hostfile name buffer; allocated
 *                              on first use.
 * @param[in,out] io_n         Pointer to a flag; 0 on first call, set to 1
 *                              afterwards.
 * @param[in]     i_jid        Job id string used to build the file name.
 * @param[in]     i_index      Spawn index used to build the numeric suffix
 *                              of the file name.
 */
void set_hostfile_name(char **io_file_name, int *io_n, const char *i_jid, int i_index) {
  int jid_count, count;

  jid_count = snprintf(NULL, 0, "%s", i_jid);
  count = MAM_HOSTFILE_SIZE + jid_count;
  if(*io_file_name == NULL) {
    *io_file_name = malloc(count * sizeof *io_file_name);
  }

  if(*io_n == 0) {
    snprintf(*io_file_name, count, "%s%s%s%03d%s", MAM_HOSTFILE_NAME1, i_jid, MAM_HOSTFILE_NAME2, i_index, MAM_HOSTFILE_NAME3);
  } else {
    count = MAM_HOSTFILE_SIZE1 + jid_count;
    snprintf((*io_file_name)+count, MAM_HOSTFILE_SIZE2, "%03d%s", i_index, MAM_HOSTFILE_NAME3);
  }
  *io_n=1;
}


/**
 * @brief Read a hostfile and accumulate the total number of processes
 *        assigned across all of its lines.
 *
 * @param[in]  i_file_name Path of the hostfile to read.
 * @param[out] o_qty       Receives the total process count read from the
 *                          file.
 * @return Always 0.
 */
int read_hostfile_procs(char *i_file_name, int *o_qty) {
  char *line = NULL, *ptr;
  FILE *file = NULL;

  file = fopen(i_file_name, "r");
  if(file == NULL) {
    perror("Could not open hostfile to read");
    MPI_Abort(MPI_COMM_WORLD, -1);
  }

  *o_qty = 0;
  line = (char *) malloc(MAM_HOSTFILE_LINE_SIZE * sizeof(char));
  while (fgets(line, MAM_HOSTFILE_LINE_SIZE, file) != NULL) {
    size_t len = strlen(line);
    ptr = line + len - 1;
    // Search delimiter
    while (ptr != line && *ptr != ':') { ptr--; }
    if (*ptr == ':') { *o_qty  += atoi(ptr + 1); }
  }
  return 0;
}


//--------------PRIVATE FUNCTIONS---------------//
//-----------------DISTRIBUTION-----------------//
/**
 * @brief Compute the physical distribution (nodes and spawn sets) for the
 *        children to create.
 *
 * Dispatches to ::spread_dist or ::compact_dist depending on
 * @c mall_conf->spawn_dist, then derives how many separate spawn calls
 * (spawn sets) are needed.
 *
 * @param[in]  i_spawn_data   Spawn configuration (spawn quantities and
 *                              strategy flags).
 * @param[out] o_used_nodes   Number of nodes used by the computed
 *                              distribution.
 * @param[out] o_total_spawns Number of spawn sets required to create the
 *                              children.
 */
void node_dist(Spawn_data i_spawn_data, int *o_used_nodes, int *o_total_spawns) {
  int i;

  /* GET NEW DISTRIBUTION  */
  switch(mall_conf->spawn_dist) {
    case MAM_PHY_DIST_SPREAD: // DIST NODES
      spread_dist(i_spawn_data, o_used_nodes);
      break;
    case MAM_PHY_DIST_COMPACT: // DIST CPUs
      compact_dist(i_spawn_data, o_used_nodes);
      break;
  }

  *o_total_spawns = 1;
  if(i_spawn_data.spawn_is_multiple || i_spawn_data.spawn_is_parallel) {
    *o_total_spawns = 0;
    for(i=0; i< *o_used_nodes; i++) {
      if(mall->spawned_cpus[i]) (*o_total_spawns)++;
    }
  }
}

#define OCCUPIED_CPUS(i) ((i_spawn_data.already_created) ? (mall->assigned_cpus[i] + mall->spawned_cpus[i]) : (mall->spawned_cpus[i]))
/**
 * @brief Distribute children evenly (spread) so every node ends with a
 *        similar process count.
 *
 * @param[in]  i_spawn_data Spawn configuration; uses @c spawn_qty as the
 *                            amount to distribute.
 * @param[out] o_used_nodes Number of nodes that received at least one
 *                            process.
 */
void spread_dist(Spawn_data i_spawn_data, int *o_used_nodes) {
  int i, tam_bl, diff, not_full_nodes, to_assig_cores;

  not_full_nodes = 0;
  to_assig_cores = i_spawn_data.spawn_qty;
  for(i = 0; i<mall->num_nodes; i++) {
    if(mall->max_cpus[i] > OCCUPIED_CPUS(i)) {
      not_full_nodes++; 
    }
  }

  while(0 < to_assig_cores && to_assig_cores > not_full_nodes && not_full_nodes) {
    tam_bl = to_assig_cores / not_full_nodes;
    for(i = 0; i < mall->num_nodes; i++) {
      diff = mall->max_cpus[i] - OCCUPIED_CPUS(i);
      if(0 < (diff - tam_bl)) {
        mall->spawned_cpus[i] += tam_bl;
        to_assig_cores -= tam_bl;
      } else if(0 < diff) {
        mall->spawned_cpus[i] += diff;
        to_assig_cores -= diff;
        not_full_nodes--;
      }
    }
  }

  if(!not_full_nodes && 0 < to_assig_cores) {
    perror("ProcesDist SPREAD Error. Target amount of cores cannot be reached\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(-1);
  }
  
  
  if(0 < to_assig_cores) {
    for(i = 0; i<mall->num_nodes && to_assig_cores; i++) {
      if(mall->max_cpus[i] > OCCUPIED_CPUS(i)) {
        mall->spawned_cpus[i] +=1;
        to_assig_cores--;
      }
    }
  }
  
  *o_used_nodes = 0;
  for(i=0; i<mall->num_nodes; i++) {
    if(mall->spawned_cpus[i]) (*o_used_nodes)++;
  }
}

/**
 * @brief Distribute children compactly, filling each node's capacity before
 *        using the next one.
 *
 * Accounts for processes already present on a node
 * (@c i_spawn_data.already_created) when computing the remaining capacity to
 * fill.
 *
 * @param[in]  i_spawn_data Spawn configuration; uses @c already_created and
 *                            @c target_qty.
 * @param[out] o_used_nodes Index of the last node used by the distribution.
 */
void compact_dist(Spawn_data i_spawn_data, int *o_used_nodes) {
  int i, diff, asigCores;

  asigCores = i_spawn_data.already_created;

  if(i_spawn_data.already_created) {
    for(i=0; i < mall->num_nodes && asigCores < i_spawn_data.target_qty; i++) {
      diff = mall->max_cpus[i] - mall->assigned_cpus[i];
      if(0 < diff) {
        if(asigCores+diff > i_spawn_data.target_qty) {
          diff -= (asigCores + diff) - i_spawn_data.target_qty;
        }
        asigCores += diff;
        mall->spawned_cpus[i] = diff;
      }
    }
  } else {
    for(i=0; i < mall->num_nodes && asigCores < i_spawn_data.target_qty; i++) {
      diff = mall->max_cpus[i];
      if(0 < diff) {
        if(asigCores+diff > i_spawn_data.target_qty) {
          diff -= (asigCores + diff) - i_spawn_data.target_qty;
        }
        asigCores += diff;
        mall->spawned_cpus[i] = diff;
      }
    }
  }

  if(asigCores < i_spawn_data.target_qty ) {
    perror("ProcesDist COMPACT Error. Target amount of cores cannot be reached\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    exit(-1);
  }

  *o_used_nodes = i;
}

//--------------PRIVATE FUNCTIONS---------------//
//-------------------INFO SET-------------------//

/**
 * @brief Build a single MPI_Info host mapping for a non-multiple/non-parallel
 *        spawn.
 *
 * @param[in]     i_nodelist    Comma-separated list of candidate node names.
 * @param[in]     i_procs_array Per-node process counts (indexed like
 *                                @p i_nodelist).
 * @param[in]     i_nodes       Number of entries to consider in
 *                                @p i_procs_array.
 * @param[in,out] io_spawn_data Spawn configuration; the resulting mapping is
 *                                stored in @c sets[0].
 */
void generate_info_string(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data){
  char *host_str;

  fill_str_hosts(i_nodelist, i_procs_array, i_nodes, &host_str);
  // SET MAPPING
  set_mapping_host(io_spawn_data->spawn_qty, "hosts", host_str, 0, io_spawn_data);
  free(host_str);
}

/**
 * @brief Build one MPI_Info host mapping per used node, for multiple/parallel
 *        spawns.
 *
 * @param[in]     i_nodelist    Comma-separated list of candidate node names.
 * @param[in]     i_procs_array Per-node process counts (indexed like
 *                                @p i_nodelist).
 * @param[in]     i_nodes       Number of entries to consider in
 *                                @p i_procs_array.
 * @param[in,out] io_spawn_data Spawn configuration; one mapping per used
 *                                node is stored in @c sets.
 */
void generate_multiple_info_string(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data){
  char *host, *aux, *token, *hostlist_str;
  size_t i=0,j=0,len=0;

  aux = (char *) malloc((strlen(i_nodelist)+1) * sizeof(char));
  strcpy(aux, i_nodelist);
  token = strtok(aux, ",");
  while (token != NULL && i < i_nodes) {
    host = strdup(token);
    if (i_procs_array[i] != 0) {
      write_str_node(&hostlist_str, len, i_procs_array[i], host);
      set_mapping_host(i_procs_array[i], "hosts", hostlist_str, j, io_spawn_data);
      free(hostlist_str); hostlist_str = NULL;
      j++;
    }
    i++;
    free(host);
    token = strtok(NULL, ",");
  }
  free(aux);
  if(hostlist_str != NULL) { free(hostlist_str); }
}


//--------------PRIVATE FUNCTIONS---------------//
//---------------MAPPING UTILITY----------------//
//----------------------------------------------//

/**
 * @brief Store one host/mapping MPI_Info pair and its process count into a
 *        spawn set.
 *
 * @param[in]     i_qty         Number of processes assigned to this spawn
 *                                set.
 * @param[in]     i_info_type   MPI_Info key to set (e.g. "hosts" or
 *                                "hostfile").
 * @param[in]     i_host        MPI_Info value to set (host list string or
 *                                hostfile name).
 * @param[in]     i_index       Index of the target entry in
 *                                @c io_spawn_data->sets.
 * @param[in,out] io_spawn_data Spawn configuration; @c sets[i_index] is
 *                                filled.
 */
void set_mapping_host(int i_qty, char *i_info_type, char *i_host, size_t i_index, Spawn_data *io_spawn_data) {
  MPI_Info *info;

  io_spawn_data->sets[i_index].spawn_qty = i_qty;
  info = &(io_spawn_data->sets[i_index].mapping);
  MPI_Info_create(info);
  MPI_Info_set(*info, i_info_type, i_host);
}

/**
 * @brief Build the comma-separated "hosts" string (node repeated per
 *        assigned process).
 *
 * @param[in]  i_nodelist     Comma-separated list of candidate node names.
 * @param[in]  i_qty          Per-node process counts (indexed like
 *                               @p i_nodelist).
 * @param[in]  i_used_nodes   Number of entries to consider in @p i_qty.
 * @param[out] o_hostlist_str Newly allocated "hosts" string.
 */
void fill_str_hosts(char *i_nodelist, int *i_qty, size_t i_used_nodes, char **o_hostlist_str) {
  char *host, *aux, *token;
  size_t i=0,len=0;

  aux = (char *) malloc((strlen(i_nodelist)+1) * sizeof(char));
  strcpy(aux, i_nodelist);
  token = strtok(aux, ",");
  while (token != NULL && i < i_used_nodes) {
    host = strdup(token);
    if (i_qty[i] != 0) {
      len = write_str_node(o_hostlist_str, len, i_qty[i], host);
    }
    i++;
    free(host);
    token = strtok(NULL, ",");
  }
  free(aux);
}

/**
 * @brief Append @p i_qty repetitions of @p i_node_name to a comma-separated
 *        host string.
 *
 * Allocates the string on first use (@p i_len_og == 0) and reallocates it
 * otherwise.
 * TODO: Should be like hostfile_node, where is written as node:qty
 *
 * @param[in,out] io_hostlist_str Host string to append to; (re)allocated as
 *                                  needed.
 * @param[in]     i_len_og        Current length of @p io_hostlist_str (0 if
 *                                  not yet allocated).
 * @param[in]     i_qty           Number of times to append @p i_node_name.
 * @param[in]     i_node_name     Node name to append.
 * @return New length of @p io_hostlist_str, or a negative error code on
 *         allocation/format failure.
 */
int write_str_node(char **io_hostlist_str, size_t i_len_og, size_t i_qty, char *i_node_name) {
  int err;
  char *ocurrence;
  size_t i, len, len_node;

  len_node = strlen(i_node_name) + 1; // Str length + ','
  len = i_qty * len_node; // Number of times the node is used

  if(i_len_og == 0) { // Memory not yet allocated
    *io_hostlist_str = (char *) malloc((len+1) * sizeof(char));
  } else { // String already has data
    *io_hostlist_str = (char *) realloc(*io_hostlist_str, (i_len_og + len + 1) * sizeof(char));
  }
  if(io_hostlist_str == NULL) return -1; // Memory could not be allocated

  ocurrence = (char *) malloc((len_node+1) * sizeof(char));
  if(ocurrence == NULL) return -2; // Memory could not be allocated
  err = snprintf(ocurrence, len_node+1, ",%s", i_node_name);
  if(err < 0) return -3; // Could not write to the auxiliary variable

  i=0;
  if(i_len_og == 0) { // If initializing, the first entry is a plain copy
    i++;
    strcpy(*io_hostlist_str, i_node_name);
  }
  for(; i<i_qty; i++){ // The following entries are concatenated
    strcat(*io_hostlist_str, ocurrence);
  }

  
  free(ocurrence);
  return len+i_len_og;
}

/**
 * @brief Append one "node:qty" line to an open hostfile.
 *
 * Reuses and grows the @p io_line buffer across calls to avoid repeated
 * allocations.
 *
 * @param[in]     i_file      Open file descriptor of the hostfile.
 * @param[in]     i_qty       Number of processes to assign to
 *                              @p i_node_name.
 * @param[in]     i_node_name Node name to write.
 * @param[in,out] io_line     Reusable line buffer.
 * @param[in,out] io_len_og   Current capacity of @p io_line.
 * @return 0 on success; negative on formatting failure (process aborts on
 *         write failure).
 */
int write_hostfile_node(int i_file, int i_qty, char *i_node_name, char **io_line, size_t *io_len_og) {
  int err;
  size_t len, len_node, len_int;

  if(*io_line == NULL) {
    *io_len_og = MAM_HOSTFILE_LINE_SIZE;
    *io_line = (char *) malloc(*io_len_og * sizeof(char));
  }

  len_node = strlen(i_node_name);
  err = snprintf(NULL, 0, "%d", i_qty);
  if(err < 0) return -1;
  len_int = err;

  len = len_node + len_int + 3;
  if(*io_len_og < len) {
    *io_len_og = len+MAM_HOSTFILE_LINE_SIZE;
    *io_line = (char *) realloc(*io_line, *io_len_og * sizeof(char));
  }

  err = snprintf(*io_line, len, "%s:%d\n", i_node_name, i_qty);
  err = write(i_file, *io_line, len-1);
  if(err < 0) {
    perror("Error writing to the host file");
    close(i_file);
    exit(EXIT_FAILURE);
  }
  return 0;
}

//--------------------------------SLURM USAGE-------------------------------------//
#if MAM_USE_SLURM
/**
 * @brief Build a single MPI_Info host mapping for a non-multiple/non-parallel
 *        spawn (Slurm hostlist).
 *
 * Requires Slurm.
 *
 * @param[in]     i_nodelist    Slurm hostlist string of candidate node
 *                                names.
 * @param[in]     i_procs_array Per-node process counts (indexed like
 *                                @p i_nodelist).
 * @param[in]     i_nodes       Number of entries to consider in
 *                                @p i_procs_array.
 * @param[in,out] io_spawn_data Spawn configuration; the resulting mapping is
 *                                stored in @c sets[0].
 */
void generate_info_string_slurm(char *i_nodelist, int *i_procs_array, size_t i_nodes, Spawn_data *io_spawn_data){
  char *hoststring;

  // CREATE AND SET STRING HOSTS
  fill_str_hosts_slurm(i_nodelist, i_procs_array, i_nodes, &hoststring);
  set_mapping_host(io_spawn_data->spawn_qty, "hosts", hoststring, 0, io_spawn_data);
  free(hoststring);
}

/**
 * @brief Build one MPI_Info host mapping per used node, for multiple/parallel
 *        spawns (Slurm hostlist).
 *
 * Requires Slurm.
 *
 * @param[in]     i_nodelist    Slurm hostlist string of candidate node
 *                                names.
 * @param[in]     i_qty         Per-node process counts (indexed by hostlist
 *                                order).
 * @param[in]     i_used_nodes  Number of entries to consider in @p i_qty.
 * @param[in,out] io_spawn_data Spawn configuration; one mapping per used
 *                                node is stored in @c sets.
 */
void generate_multiple_info_string_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, Spawn_data *io_spawn_data) {
  char *host, *hostlist_str;
  size_t i=0,j=0,len=0;
  hostlist_t hostlist;
  
  hostlist_str = NULL;
  hostlist = slurm_hostlist_create(i_nodelist);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < i_used_nodes) {
    if(i_qty[i] != 0) {
      write_str_node(&hostlist_str, len, i_qty[i], host);
      set_mapping_host(i_qty[i], "hosts", hostlist_str, j, io_spawn_data);
      free(hostlist_str); hostlist_str = NULL;
      j++;
    }
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);
  if(hostlist_str != NULL) { free(hostlist_str); }
}


/**
 * @brief Build the comma-separated "hosts" string using a Slurm hostlist
 *        (node repeated per assigned process).
 *
 * Requires Slurm.
 *
 * @param[in]  i_nodelist     Slurm hostlist string of candidate node names.
 * @param[in]  i_qty          Per-node process counts (indexed by hostlist
 *                               order).
 * @param[in]  i_used_nodes   Number of entries to consider in @p i_qty.
 * @param[out] o_hostlist_str Newly allocated "hosts" string.
 */
void fill_str_hosts_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, char **o_hostlist_str) {
  char *host;
  size_t i=0,len=0;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(i_nodelist);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < i_used_nodes) {
    if(i_qty[i] != 0) {
      len = write_str_node(o_hostlist_str, len, i_qty[i], host);
    }
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);
}

/**
 * @brief Build one or several hostfiles describing the mapping for the
 *        children to spawn (Slurm).
 *
 * For multiple/parallel spawns, writes one hostfile per spawn set via
 * ::fill_multiple_hostfile_slurm; otherwise writes a single hostfile via
 * ::fill_hostfile_slurm.
 *
 * Requires Slurm.
 *
 * @param[in]     i_nodelist    Slurm hostlist string of candidate node
 *                                names.
 * @param[in]     i_qty         Per-node process counts (indexed by hostlist
 *                                order).
 * @param[in]     i_used_nodes  Number of entries to consider in @p i_qty.
 * @param[in,out] io_spawn_data Spawn configuration; the resulting hostfile
 *                                mapping(s) are stored in @c sets.
 */
void generate_info_hostfile_slurm(char *i_nodelist, int *i_qty, size_t i_used_nodes, Spawn_data *io_spawn_data){
  int index = 0, jid, count, err_sn;
  size_t qty_index = 0, len_line = 0;
  char *hostfile_name, *line;
  hostlist_t hostlist;

  char *tmp_job_id = getenv("SLURM_JOB_ID");
  jid = snprintf(NULL, 0, "%s", tmp_job_id);
  count = MAM_HOSTFILE_SIZE + jid;

  line = NULL;
  hostlist = slurm_hostlist_create(i_nodelist);
  hostfile_name = (char *) malloc(count * sizeof *hostfile_name);
  err_sn = snprintf(hostfile_name, count, "%s%s%s%03d%s", MAM_HOSTFILE_NAME1, tmp_job_id, MAM_HOSTFILE_NAME2, index, MAM_HOSTFILE_NAME3);
  if(err_sn < 0) { 
    perror("Process_Dist snprintf error"); 
    MPI_Abort(MPI_COMM_WORLD, -1); 
  }
  count = MAM_HOSTFILE_SIZE1+jid;

  if(io_spawn_data->spawn_is_multiple || io_spawn_data->spawn_is_parallel) { // MULTIPLE
    for(; index<io_spawn_data->total_spawns; index++) {
      // This strat creates 1 hostfile per spawn
      fill_multiple_hostfile_slurm(hostfile_name, i_qty, &qty_index, &hostlist, &line, &len_line);
      set_mapping_host(i_qty[qty_index-1], "hostfile", hostfile_name, index, io_spawn_data); 
      err_sn = snprintf(hostfile_name+count, MAM_HOSTFILE_SIZE2 , "%03d%s", index+1, MAM_HOSTFILE_NAME3);
      if(err_sn < 0) { 
        perror("Process_Dist snprintf error"); 
        MPI_Abort(MPI_COMM_WORLD, -1); 
      }
    }
    free(line);

  } else { // NOT MULTIPLE
    fill_hostfile_slurm(hostfile_name, i_used_nodes, i_qty, &hostlist);
    set_mapping_host(io_spawn_data->spawn_qty, "hostfile", hostfile_name, index, io_spawn_data);
  }

  free(hostfile_name);
  slurm_hostlist_destroy(hostlist);
}

/**
 * @brief Write a single hostfile with one "node:qty" line per used node.
 *
 * @param[in]     i_file_name  Path of the hostfile to (re)create.
 * @param[in]     i_used_nodes Number of nodes to write.
 * @param[in]     i_qty        Per-node process counts (indexed by hostlist
 *                                order).
 * @param[in,out] io_hostlist  Slurm hostlist to consume node names from.
 */
void fill_hostfile_slurm(char* i_file_name, size_t i_used_nodes, int *i_qty, hostlist_t *io_hostlist) {
  char *host, *line;
  size_t i=0, i_used=0, len_line=0;

  line = NULL;
  int file = open(i_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file < 0) {
    perror("Error opening the host file");
    exit(EXIT_FAILURE);
  }

  while ( (host = slurm_hostlist_shift(*io_hostlist)) && i_used < i_used_nodes) {
    if(i_qty[i] != 0) {
      write_hostfile_node(file, i_qty[i], host, &line, &len_line);
      i_used++;
    }
    i++;
    free(host);
  }

  close(file);
  free(line);
}

/**
 * @brief Write a single hostfile entry for the next non-empty node in the
 *        hostlist.
 *
 * Continues iterating the Slurm hostlist from @p io_index so consecutive
 * calls each produce one hostfile covering a different node.
 *
 * @param[in]     i_file_name Path of the hostfile to (re)create.
 * @param[in]     i_qty       Per-node process counts (indexed by hostlist
 *                               order).
 * @param[in,out] io_index    Hostlist position to resume from; updated to
 *                               the next position.
 * @param[in,out] io_hostlist Slurm hostlist to consume node names from.
 * @param[in,out] io_line     Reusable line buffer passed through to
 *                               ::write_hostfile_node.
 * @param[in,out] io_len_line Current capacity of @p io_line.
 */
void fill_multiple_hostfile_slurm(char* i_file_name, int *i_qty, size_t *io_index, hostlist_t *io_hostlist, char **io_line, size_t *io_len_line) {
  char *host;
  size_t i=*io_index;

  int file = open(i_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file < 0) {
    perror("Error opening the host file");
    exit(EXIT_FAILURE);
  }

  // The value of i must continue from where it was left off
  while( (host = slurm_hostlist_shift(*io_hostlist)) ) {
    if(i_qty[i] != 0) {
      write_hostfile_node(file, i_qty[i], host, io_line, io_len_line);
      i++;
      break;
    }
    i++;
    free(host); host = NULL;
  }

  if(host != NULL) free(host);
  close(file);
  *io_index = i;
}
#endif
//--------------------------------SLURM USAGE-------------------------------------//
