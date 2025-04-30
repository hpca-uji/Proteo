#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <mpi.h>
#include "MAM_RMS.h"
#include "MAM_DataStructures.h"

#define MAM_HASH_KEY 21787

#if MAM_USE_SLURM
#include <slurm/slurm.h>
int MAM_I_slurm_getenv_hosts_info();
int MAM_I_slurm_getjob_hosts_info();
void MAM_I_slurm_get_assigned_cpus();
#endif

int MAM_I_get_hosts_info();
int GetCPUCount();
unsigned long long hash64(const void *buf, size_t len, unsigned long long key);

void MAM_check_hosts() {
  int not_filled = 1;
  
  #if MAM_USE_SLURM
    not_filled = MAM_I_slurm_getjob_hosts_info();
    if(not_filled) {
      #if MAM_DEBUG >= 2
        DEBUG_FUNC("WARNING - RMS info retriever failed with slurm functions. Trying with ENV variables", mall->myId, mall->numP); 
      #endif
      if(mall->nodelist != NULL) {
        free(mall->nodelist);
	mall->nodelist = NULL;
      }

      not_filled = MAM_I_slurm_getenv_hosts_info();
    }
  #endif
  if(not_filled) {
    if(mall->nodelist != NULL) {
      free(mall->nodelist);
      mall->nodelist = NULL;
    }

    not_filled = MAM_I_get_hosts_info();
  }


  if(not_filled) {
    if(mall->myId == mall->root) printf("MAM FATAL ERROR: It has not been possible to obtain the nodelist\n");
    fflush(stdout);
    MPI_Abort(mall->comm, -50);
  }

  #if MAM_DEBUG >= 2
    if(mall->myId == mall->root) {
      DEBUG_FUNC("Obtained Nodelist", mall->myId, mall->numP); 
      printf("NODELIST: %s\nNODE_COUNT: %d\n", mall->nodelist, mall->num_nodes);
      printf("MAX_CPUS:       [");
      for(int i_debug=0; i_debug < mall->num_nodes; i_debug++) {
        printf("%d ", mall->max_cpus[i_debug]);
      }
      printf("]\nASSSIGNED_CPUS: [");
      for(int i_debug=0; i_debug < mall->num_nodes; i_debug++) {
        printf("%d ", mall->assigned_cpus[i_debug]);
      }
      printf("]\n");
      fflush(stdout); 
    }
  #endif
}

/*
 * @brief Get if a group of processes uses an internode comunicator
 *
 * This function checks the physical distribution of all ranks in the
 * original communicator passed to MaM. If all of them reside in the
 * same host, false is returned. True is returned otherwise.
 *
 * @return Integer indicating if more than one node is used by the
 * original communicator (>0) or only one (0).
 */
int MAM_Is_internode_group() {
  int i, name_len, max_name_len, unique_count;
  int myId, numP;
  char *my_host, *all_hosts, *tested_host;

  MPI_Comm_rank(mall->original_comm, &myId);
  MPI_Comm_size(mall->original_comm, &numP);

  unique_count = 0; //First node is not counted
  if(numP == 1) return unique_count;

  all_hosts = NULL;
  my_host = (char *) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  MPI_Get_processor_name(my_host, &name_len);

  MPI_Allreduce(&name_len, &max_name_len, 1, MPI_INT, MPI_MAX, mall->original_comm);
  my_host[max_name_len] = '\0';
  max_name_len++; // Len does not consider terminating character
  if(myId == MAM_ROOT) {
    all_hosts = (char *) malloc(numP * max_name_len * sizeof(char));
  }
  //FIXME Should be a Gatherv as each host could have unitialised chars between name_len and max_name_len
  MPI_Gather(my_host, max_name_len, MPI_CHAR, all_hosts, max_name_len, MPI_CHAR, MAM_ROOT, mall->original_comm);

  if(myId == MAM_ROOT) {
    for (i = 1; i < numP; i++) {
      tested_host = all_hosts + (i * max_name_len);
      if (strcmp(my_host, tested_host) != 0) {
        unique_count++;
        break;
      }
    }
    free(all_hosts);
  }
  MPI_Bcast(&unique_count, 1, MPI_INT, MAM_ROOT, mall->original_comm);
  free(my_host);
  return unique_count;
}

/*
 * TODO
 * FIXME Always returns 0... -- Perform error checking?
 */
int MAM_I_get_hosts_info() {
  int i, j, name_len, max_name_len, unique_count, *unique_hosts;
  int hash, *procs_hashes, *hashes;
  int detected_cpus, *procs_cpus;
  char *my_host, *all_hosts, *confirmed_host, *tested_host;

  all_hosts = NULL;
  my_host = (char *) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  MPI_Get_processor_name(my_host, &name_len);

  hash = hash64(my_host, strlen(my_host), MAM_HASH_KEY);

  MPI_Allreduce(&name_len, &max_name_len, 1, MPI_INT, MPI_MAX, mall->comm);
  my_host[max_name_len] = '\0';
  max_name_len++; // Len does not consider terminating character
  if(mall->myId == mall->root) {
    procs_hashes = malloc(mall->numP * sizeof *procs_hashes);
    all_hosts = (char *) malloc(mall->numP * max_name_len * sizeof(char));
    unique_hosts = (int *) malloc(mall->numP * sizeof(int));
    unique_hosts[0] = 0; //First host will always be unique
    unique_count = 1;
  }

  MPI_Gather(my_host, max_name_len, MPI_CHAR, all_hosts, max_name_len, MPI_CHAR, mall->root, mall->comm);
  MPI_Gather(&hash, 1, MPI_INT, procs_hashes, 1, MPI_INT, mall->root, mall->comm);

  if(mall->myId == mall->root) {
    for (i = 1; i < mall->numP; i++) {
      for (j = 0; j < unique_count; j++) {
	      tested_host = all_hosts + (i * max_name_len);
	      confirmed_host = all_hosts + (unique_hosts[j] * max_name_len);
        if (strcmp(tested_host, confirmed_host) != 0) {
	        unique_hosts[unique_count] = i;
          unique_count++;
          break;
        }
      }
    }

    mall->num_nodes = unique_count;
    mall->nodelist_len = unique_count*max_name_len;
    mall->nodelist = (char *) malloc(mall->nodelist_len * sizeof(char));
    procs_cpus = malloc(mall->numP * sizeof *procs_cpus);
    hashes = malloc(mall->num_nodes * sizeof *hashes);

  }
  detected_cpus = GetCPUCount();
  MPI_Bcast(&mall->num_nodes, 1, MPI_INT, mall->root, mall->comm);
  MPI_Bcast(&mall->nodelist_len, 1, MPI_INT, mall->root, mall->comm);
  MPI_Gather(&detected_cpus, 1, MPI_INT, procs_cpus, 1, MPI_INT, mall->root, mall->comm);

  if(NULL != mall->max_cpus) { free(mall->max_cpus); }
  mall->max_cpus = malloc(mall->num_nodes * sizeof *mall->max_cpus);
  if(NULL != mall->assigned_cpus) { free(mall->assigned_cpus); }
  mall->assigned_cpus = calloc(mall->num_nodes, sizeof *mall->assigned_cpus);
  if(NULL != mall->spawned_cpus) { free(mall->spawned_cpus); }
  mall->spawned_cpus = calloc(mall->num_nodes, sizeof *mall->spawned_cpus);

  if(mall->myId == mall->root) {

    mall->nodelist[0] = '\0';
    for (i = 0; i < unique_count; i++) { // Create nodelist
      mall->max_cpus[i] = procs_cpus[unique_hosts[i]];
      confirmed_host = all_hosts + (unique_hosts[i] * max_name_len);
      hashes[i] = hash64(confirmed_host, strlen(confirmed_host), MAM_HASH_KEY);

      strcat(mall->nodelist, confirmed_host);
      if (i < unique_count - 1) {
        strcat(mall->nodelist, ",");
      }
    }

    for(i = 0; i < mall->num_nodes; i++) {
      for(j = 0; j < mall->numP; j++) {
        if(hashes[i] == procs_hashes[j]) {
          mall->assigned_cpus[i] +=1;
        }
      }
    }

    free(all_hosts);
    free(unique_hosts);
    free(hashes);
    free(procs_hashes);
    free(procs_cpus);
  } else {
    mall->nodelist = malloc(mall->nodelist_len * sizeof *mall->nodelist);
  }
  MPI_Bcast(mall->max_cpus, mall->num_nodes, MPI_INT, mall->root, mall->comm);
  MPI_Bcast(mall->assigned_cpus, mall->num_nodes, MPI_INT, mall->root, mall->comm);
  MPI_Bcast(mall->nodelist, mall->nodelist_len, MPI_CHAR, mall->root, mall->comm);

  free(my_host);
  return 0;
}

/*
 * @brief Get the total number of CPUs available to the process.
 *
 * This function uses sched_getaffinity to obtain the CPU affinity of the current process
 * and counts the number of CPUs in the affinity set. It adjusts the loop based on the
 * maximum number of CPUs allowed on the system.
 *
 * @return The total number of CPUs available to the process.
 *
 * Code obtained from: https://stackoverflow.com/questions/4586405/how-to-get-the-number-of-cpus-in-linux-using-c
 * The code has been slightly modified.
 */
int GetCPUCount() {
  cpu_set_t cs;
  CPU_ZERO(&cs);
  sched_getaffinity(0, sizeof(cs), &cs);

  int count = 0;
  int max_cpus = sysconf(_SC_NPROCESSORS_ONLN);

  for (int i = 0; i < max_cpus; i++) {
      if (CPU_ISSET(i, &cs)) {
          count++;
      } else {
          break;
      }
  }
  return count;
}

/*
 * Hash function extracted from:
 * https://www.reddit.com/r/C_Programming/comments/i1oj5w/hash_functions/
 */
unsigned long long hash64(const void *buf, size_t len, unsigned long long key) {
    unsigned long long h = key;
    const unsigned char *p = buf;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0xaea8d4541a5831df;
    }
    h &= 0xffffffffffffffff;
    h ^= h >> 32;
    h *= 0x95b1374e649ef59f;
    h &= 0xffffffffffffffff;
    h ^= h >> 32;
    return h;
}

#if MAM_USE_SLURM
/*
 * TODO
 */
int MAM_I_slurm_getenv_hosts_info() {
  char *tmp = NULL, *tmp_copy, *token;
  int cpus, count, i, j;
  //int i, *cpus_counts, *nodes_counts, *aux;
  
  tmp = getenv("SLURM_JOB_NUM_NODES");
  if(tmp == NULL) return 1;
  mall->num_nodes = atoi(tmp);
  tmp = NULL;

  tmp = getenv("SLURM_JOB_NODELIST");
  if(tmp == NULL) return 1;
  mall->nodelist_len = strlen(tmp)+1;
  mall->nodelist = (char *) malloc(mall->nodelist_len * sizeof(char));
  strcpy(mall->nodelist, tmp);
  tmp = NULL;


  //EXAMPLE - SLURM_JOB_CPUS_PER_NODE='72(x2),36'
  //It indicates two nodes have 72 CPUs each and third has 36 cpus
  tmp = getenv("SLURM_JOB_CPUS_PER_NODE");
  if(tmp == NULL) return 1;

  tmp_copy = (char *) malloc((strlen(tmp)+1) * sizeof(char));
  strcpy(tmp_copy, tmp);
  token = strtok(tmp_copy, ",");

  if(NULL != mall->max_cpus) { free(mall->max_cpus); }
  mall->max_cpus = malloc(mall->num_nodes * sizeof *mall->max_cpus);
  i = 0;
  count = 1;

  while (token != NULL) {
    // If actual token contains only one node, the second portion
    // does not appear and sscanf does not modify "count"
    // First portion --> "%d"
    // Second portion -> "(x%d)"
    count = 1;
    if (sscanf(token, "%d(x%d)", &cpus, &count) >= 1) {
      for(j = 0; j < count; j++) {
        mall->max_cpus[i] = cpus;
        i++;
      }
      count = 1;
    }
    token = strtok(NULL, ",");
  }

  MAM_I_slurm_get_assigned_cpus();

  free(tmp_copy);
  return 0;
}

/*
 * TODO
 */
int MAM_I_slurm_getjob_hosts_info() {
  int jobId, err, i, j, t;
  char *tmp = NULL;
  job_info_msg_t *j_info;
  slurm_job_info_t last_record;
  resource_allocation_response_msg_t *alloc_msg;

  tmp = getenv("SLURM_JOB_ID");
  if(tmp == NULL) return 1;
  jobId = atoi(tmp);

  err = slurm_load_job(&j_info, jobId, 1); // FIXME Valgrind Not freed
  if(err) return err;
  err = slurm_allocation_lookup(jobId, &alloc_msg);
  if(err) { return err; }

  last_record = j_info->job_array[j_info->record_count - 1];

  mall->num_nodes = alloc_msg->node_cnt;
  if(NULL != mall->max_cpus) { free(mall->max_cpus); }
  mall->max_cpus = malloc(mall->num_nodes * sizeof *mall->max_cpus);

  t = 0;
  for(i = 0; i < alloc_msg->num_cpu_groups; i++) {
    for(j = 0; j < alloc_msg->cpu_count_reps[i]; j++) {
      mall->max_cpus[t] = alloc_msg->cpus_per_node[i];
      t++;
    }
  }

  mall->nodelist_len = strlen(alloc_msg->node_list)+1;
  mall->nodelist = (char *) malloc(mall->nodelist_len * sizeof(char));
  strcpy(mall->nodelist, alloc_msg->node_list);

  MAM_I_slurm_get_assigned_cpus();

  slurm_free_job_info_msg(j_info);
  slurm_free_resource_allocation_response_msg(alloc_msg);
  return 0;
}

/*
 * TODO
 */
void MAM_I_slurm_get_assigned_cpus() {
  int host_len, hash, *hashes, *procs_hashes;
  size_t i, j;
  char *my_host, *host;
  hostlist_t *hostlist;

  if(NULL != mall->assigned_cpus) { free(mall->assigned_cpus); }
  mall->assigned_cpus = calloc(mall->num_nodes, sizeof *mall->assigned_cpus);

  if(NULL != mall->spawned_cpus) { free(mall->spawned_cpus); }
  mall->spawned_cpus = calloc(mall->num_nodes, sizeof *mall->spawned_cpus);

  my_host = malloc(MPI_MAX_PROCESSOR_NAME * sizeof *my_host);
  MPI_Get_processor_name(my_host, &host_len);

  procs_hashes = malloc(mall->numP * sizeof *procs_hashes);
  hash = hash64(my_host, strlen(my_host), MAM_HASH_KEY);
  MPI_Gather(&hash, 1, MPI_INT, procs_hashes, 1, MPI_INT, mall->root, mall->comm);

  if(mall->myId == mall->root) {
    hashes = malloc(mall->num_nodes * sizeof *hashes);
    hostlist = slurm_hostlist_create(mall->nodelist);
    i = 0;
    while( (host = slurm_hostlist_shift(*hostlist)) ) {
      hashes[i] = hash64(host, strlen(host), MAM_HASH_KEY);
      i++;
      free(host); host = NULL;
    }
    slurm_hostlist_destroy(hostlist);
    

    for(i = 0; i < mall->num_nodes; i++) {
      for(j = 0; j < mall->numP; j++) {
        if(hashes[i] == procs_hashes[j]) {
          mall->assigned_cpus[i] +=1;
        }
      }
    }
    free(hashes);
  }
  MPI_Bcast(mall->assigned_cpus, mall->num_nodes, MPI_INT, MAM_ROOT, mall->comm);

  free(my_host);
}
#endif
