#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <math.h>
#include <mpi.h>
#include "MAM_RMS.h"
#include "MAM_DataStructures.h"

#define BINBASH "#!/bin/bash"

/**
 * @file MAM_RMS.c
 * @brief Implementation of physical resource (node/CPU) discovery for MaM.
 *
 * Discovers the current node list and per-node CPU counts, preferring the
 * SLURM API when available, falling back to SLURM ENV variables, and
 * finally to direct MPI-based discovery when SLURM is not available.
 */

#define MAM_HASH_KEY 21787

#if MAM_USE_SLURM
#include <slurm/slurm.h>
//Next function should be in the slurm.h, but is instead in src/common/slurm_protocol_defs.h
extern void slurm_free_will_run_response_msg(will_run_response_msg_t *msg);
/**
 * @brief Discover the current resource allocation using SLURM ENV variables.
 *
 * Obtains the node names and each process' physical placement using SLURM
 * environment variables.
 *
 * @return 0 on success; non-zero if a required SLURM ENV variable is missing.
 */
int MAM_I_slurm_getenv_hosts_info(void);

/**
 * @brief Discover the current resource allocation using the SLURM API.
 *
 * Obtains the node names and each process' physical placement using the
 * SLURM API.
 *
 * @return 0 on success; non-zero SLURM error code otherwise.
 */
int MAM_I_slurm_getjob_hosts_info(int jobId, int update);

/**
 * @brief Determine, per node, how many of this job's processes reside there.
 *
 * Determines where each process is located and fills the assigned CPUs
 * vectors accordingly.
 */
void MAM_I_slurm_get_assigned_cpus(void);

static int MAM_I_slurm_copy_environ(job_desc_msg_t *job_desc_msg, const char *port_name);

int MAM_I_slurm_request_job(job_desc_msg_t *job_desc_msg, int *new_job_id);

int MAM_I_slurm_prepare_job(int new_nodes, job_desc_msg_t *job_desc_msg);

int MAM_I_slurm_check_job_status(int new_job_id);
#endif

/**
 * @brief Discover the current resource allocation via direct MPI communication.
 *
 * Obtains the total number of processes per node available to the
 * application. It computes where all current processes are located and how
 * many cores each node has. Used when SLURM is not an option; the idea is
 * that the total number of processes may change without the total
 * resources changing.
 *
 * @return 0 always.
 *
 * @note FIXME: Always returns 0... -- Perform error checking?
 */
int MAM_I_get_hosts_info(void);

/**
 * @brief Get the total number of CPUs available to the calling process.
 * @return The total number of CPUs available to the process.
 */
int GetCPUCount(void);

/**
 * @brief Compute a 64-bit hash over a memory buffer.
 *
 * @param[in] i_buf Buffer whose bytes will be hashed.
 * @param[in] i_len Length in bytes of @p i_buf.
 * @param[in] i_key Seed key mixed into the hash.
 * @return The resulting 64-bit hash value.
 */
unsigned long long hash64(const void *i_buf, size_t i_len, unsigned long long i_key);

void MAM_I_generate_job_name(char **port_name);
void MAM_I_realloc_RMS_arrays(void);

/**
 * @brief Discover the node list and per-node CPU counts into ::mall fields.
 *
 * Checks the current resources (nodes) accessible to the application. It
 * prefers the SLURM API when available. SLURM ENV variables only work for
 * the first reconfiguration, since the total resources do not change, only
 * the total number of processes. If SLURM is not available, the processes
 * communicate with each other to discover which host they are running on.
 */
void MAM_check_hosts(void) {
  int not_filled = 1;
  int update = 0;
  
  #if MAM_USE_SLURM
    char *tmp = NULL;
    tmp = getenv("SLURM_JOB_ID");
    if(tmp != NULL) {
      int jobId = atoi(tmp);
      not_filled = MAM_I_slurm_getjob_hosts_info(jobId, update);
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

// TODO: Does not consider how to ask for heterogeneous nodes (Diff number of cores per node)
// MAM_DENIED if error, MAM_RMS_JOB_PENDING/MAM_RMS_JOB_STARTED if accepted, MAM_RMS_JOB_DENIED otherwise
int MAM_Request_job(void) {
  int i, sum, new_nodes, res;

  sum = 0;
  // Check if more cpus are required
  for(i = 0; i < mall->num_nodes; i++) { sum += mall->max_cpus[i]; }
  new_nodes = ceil((mall->numC - sum) / mall->max_cpus[0]);
  if(new_nodes <= 0) { return MAM_I_RMS_COMPLETED; }

#if MAM_USE_SLURM
  //MAM_I_generate_service_name(&(mall->service_name));
  if(MAM_ROOT == mall->myId) {
    job_desc_msg_t *job_desc_msg = malloc(sizeof *job_desc_msg); 
    res = MAM_I_slurm_prepare_job(new_nodes, job_desc_msg);
    if (res == MAM_OK) {
      res = (MAM_I_slurm_request_job(job_desc_msg, &(mall->new_job_id)) == MAM_OK ? MAM_I_RMS_PENDING : MAM_DENIED);
    }
    free(job_desc_msg);
  }
  MPI_Bcast(&mall->new_job_id, 1, MPI_INT, MAM_ROOT, mall->comm);
  MPI_Bcast(&res, 1, MPI_INT, MAM_ROOT, mall->comm);
#else
  res = MAM_I_RMS_COMPLETED; // Without an RMS cannot ask for more resources
#endif
  return res;
}

// Check if new job is available
int MAM_Check_pending_job(void) {
  int res = MAM_DENIED; // Without an RMS cannot ask for more resources

#if MAM_USE_SLURM
  if(MAM_ROOT == mall->myId) {
    res = MAM_I_slurm_check_job_status(mall->new_job_id);
  }
  MPI_Bcast(&res, 1, MPI_INT, MAM_ROOT, mall->comm);
#endif
  if (res == MAM_I_RMS_COMPLETED) { mall->num_expands += 1; }
  return res;
}

void MAM_check_new_hosts(void) {
  int not_filled = 1;
  int update = 1;

  if(mall->new_job_id == MAM_DENIED ) { return; }
  
#if MAM_USE_SLURM
    not_filled = MAM_I_slurm_getjob_hosts_info(mall->new_job_id, update);
    if(not_filled) {
      #if MAM_DEBUG >= 2
        DEBUG_FUNC("WARNING - RMS info retriever failed with slurm functions. Trying with ENV variables", mall->myId, mall->numP); 
      #endif
    }
#endif

  if(not_filled) {
    if(mall->myId == mall->root) printf("MAM FATAL ERROR: It has not been possible to update the nodelist\n");
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
      printf("]\nSPAWNED_CPUS: [");
      for(int i_debug=0; i_debug < mall->num_nodes; i_debug++) {
        printf("%d ", mall->spawned_cpus[i_debug]);
      }
      printf("]\n");
      fflush(stdout); 
    }
  #endif
}

/**
 * @brief Report whether the current job spans more than one node for at least one of their MPI_COMM_WORLD.
 *
 * Checks the physical distribution of all ranks in the original
 * communicator passed to MaM. If all of them reside on the same host,
 * false is returned. True is returned otherwise.
 *
 * @return Non-zero indicating that more than one node is used by the
 * original communicator, 0 if only one node is used.
 */
int MAM_Is_internode_group(void) {
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
  // FIXME: Should be a Gatherv as each host could have unitialised chars between name_len and max_name_len
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

/**
 * @brief Discover the current resource allocation via direct MPI communication.
 *
 * Obtains the total number of processes per node available to the
 * application. It computes where all current processes are located and how
 * many cores each node has. Used when SLURM is not an option; the idea is
 * that the total number of processes may change without the total
 * resources changing.
 *
 * @return 0 always.
 *
 * @note FIXME: Always returns 0... -- Perform error checking?
 */
int MAM_I_get_hosts_info(void) {
  int i, j, name_len, max_name_len, unique_count, *unique_hosts;
  int hash, *procs_hashes, *hashes;
  int detected_cpus, *procs_cpus;
  char *my_host, *all_hosts, *confirmed_host, *tested_host;

  unique_hosts = NULL;
  procs_hashes = NULL; hashes = NULL; procs_cpus = NULL; 
  my_host = NULL; all_hosts = NULL; confirmed_host = NULL; tested_host = NULL;
  
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

/**
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
int GetCPUCount(void) {
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

/**
 * @brief Compute a 64-bit hash over a memory buffer.
 *
 * Hash function extracted from:
 * https://www.reddit.com/r/C_Programming/comments/i1oj5w/hash_functions/
 *
 * @param[in] i_buf Buffer whose bytes will be hashed.
 * @param[in] i_len Length in bytes of @p i_buf.
 * @param[in] i_key Seed key mixed into the hash.
 * @return The resulting 64-bit hash value.
 */
unsigned long long hash64(const void *i_buf, size_t i_len, unsigned long long i_key) {
    unsigned long long h = i_key;
    const unsigned char *p = i_buf;
    for (size_t i = 0; i < i_len; i++) {
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

void MAM_I_generate_job_name(char **port_name) {
  int jid_count = 1;
  int exp_count = 1;
  int total;
  int jid = 0;
  char *name = NULL;

  char *constant_name = "MAM_Expansion_J";
  int constant_count = strlen(constant_name) + 2; //Addition of '_' and '\0'
  exp_count = snprintf(NULL, 0, "%d", mall->num_expands);

#if MAM_USE_SLURM
  char *tmp = getenv("SLURM_JOB_ID");
  if(tmp == NULL) return;
  jid = atoi(tmp);
  jid_count = snprintf(NULL, 0, "%d", jid);
#endif

  total = jid_count + exp_count + constant_count;
  name = malloc(total * sizeof *name);
  snprintf(name, total, "MAM_Expansion_J%d_%d", jid, mall->num_expands);

  if(name != NULL) { *port_name = name; }
}

void MAM_I_realloc_RMS_arrays(void) {
  char *nodelist_aux = NULL;
  int *max_cpus_aux = NULL;
  int *assigned_cpus_aux = NULL;
  int *spawned_cpus_aux = NULL;

  max_cpus_aux = realloc(mall->max_cpus, mall->num_nodes * sizeof *mall->max_cpus);
  assigned_cpus_aux = realloc(mall->assigned_cpus, mall->num_nodes * sizeof *mall->assigned_cpus);
  spawned_cpus_aux = realloc(mall->spawned_cpus, mall->num_nodes * sizeof *mall->spawned_cpus);
  nodelist_aux = realloc(mall->nodelist, mall->nodelist_len * sizeof *mall->nodelist);

  if(max_cpus_aux == NULL || assigned_cpus_aux == NULL || spawned_cpus_aux == NULL || nodelist_aux == NULL) {
    fprintf(stderr, "Fatal error - The new allocation update has to reallocate memory\n");
    MPI_Abort(MPI_COMM_WORLD, -50);
  }

  if(mall->max_cpus != max_cpus_aux && mall->max_cpus != NULL) free(mall->max_cpus);
  if(mall->assigned_cpus != assigned_cpus_aux && mall->assigned_cpus != NULL) free(mall->assigned_cpus);
  if(mall->spawned_cpus != spawned_cpus_aux && mall->spawned_cpus != NULL) free(mall->spawned_cpus);
  if(mall->nodelist != nodelist_aux && mall->nodelist != NULL) free(mall->nodelist);

  mall->max_cpus = max_cpus_aux;
  mall->assigned_cpus = assigned_cpus_aux;
  mall->spawned_cpus = spawned_cpus_aux;
  mall->nodelist = nodelist_aux;
}

#if MAM_USE_SLURM
/**
 * @brief Discover the current resource allocation using SLURM ENV variables.
 *
 * Obtains the node names and each process' physical placement using SLURM
 * environment variables.
 *
 * @return 0 on success; non-zero if a required SLURM ENV variable is missing.
 */
int MAM_I_slurm_getenv_hosts_info(void) {
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

/**
 * @brief Discover the current resource allocation using the SLURM API.
 *
 * Obtains the node names and each process' physical placement using the
 * SLURM API.
 *
 * @return 0 on success; non-zero SLURM error code otherwise.
 */
int MAM_I_slurm_getjob_hosts_info(int jobId, int update) {
  int err;
  size_t prev_nodes, prev_nodelist_len;
  size_t i, j, t;
  job_info_msg_t *j_info;
  resource_allocation_response_msg_t *alloc_msg;

  err = slurm_load_job(&j_info, jobId, 1); // FIXME: Valgrind Not freed
  if(err) return err;
  err = slurm_allocation_lookup(jobId, &alloc_msg);
  if(err) { return err; }

  prev_nodes = mall->num_nodes;
  mall->num_nodes += alloc_msg->node_cnt;
  prev_nodelist_len = mall->nodelist_len;
  mall->nodelist_len += strlen(alloc_msg->node_list)+1;

  if(!update) { 
    if(NULL != mall->max_cpus) { 
      free(mall->max_cpus);
    }
    mall->max_cpus = malloc(mall->num_nodes * sizeof *mall->max_cpus);
    mall->nodelist = malloc(mall->nodelist_len * sizeof *mall->nodelist); 
    strcpy(mall->nodelist, alloc_msg->node_list);

    MAM_I_slurm_get_assigned_cpus();
  } else {
    MAM_I_realloc_RMS_arrays();

    mall->nodelist[prev_nodelist_len-1] = ',';
    mall->nodelist[prev_nodelist_len] = '\0';
    strcat(mall->nodelist, alloc_msg->node_list);
  }

  t = prev_nodes;
  for(i = 0; i < alloc_msg->num_cpu_groups; i++) {
    for(j = 0; j < alloc_msg->cpu_count_reps[i]; j++) {
      mall->max_cpus[t] = alloc_msg->cpus_per_node[i];
      t++;
    }
  }

  if(update) {  //FIXME: It is assumed when creating a new job, all cores spawn at least 1 rank
    for(t = prev_nodes; t < (size_t) mall->num_nodes ; t++) {
      mall->spawned_cpus[t] = mall->max_cpus[t];
    }
  }

  slurm_free_job_info_msg(j_info);
  slurm_free_resource_allocation_response_msg(alloc_msg);
  return 0;
}

/**
 * @brief Determine, per node, how many of this job's processes reside there.
 *
 * Determines where each process is located and fills the assigned CPUs
 * vectors accordingly.
 */
void MAM_I_slurm_get_assigned_cpus(void) {
  int host_len, hash, *hashes, *procs_hashes;
  int i, j;
  char *my_host, *host;
  hostlist_t hostlist;

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
    while( (host = slurm_hostlist_shift(hostlist)) ) {
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
  free(procs_hashes);
}

// TODO: Does not consider how to ask for heterogeneous nodes (Diff number of cores per node)
// MAM_DENIED if error, MAM_RMS_JOB_PENDING/MAM_RMS_JOB_STARTED if accepted, MAM_RMS_JOB_DENIED otherwise
int MAM_I_slurm_prepare_job(int new_nodes, job_desc_msg_t *job_desc_msg) {
  int err, jobId, count;
  char *tmp = NULL;
  char *proteo_home = NULL;
  job_info_msg_t *prev_j_info;
  slurm_job_info_t *prev_j_chars;
  resource_allocation_response_msg_t *prev_alloc;

  // Get data from this job
  proteo_home = getenv("PROTEO_HOME");
  if(proteo_home == NULL) return MAM_DENIED;
  tmp = getenv("SLURM_JOB_ID");
  if(tmp == NULL) return MAM_DENIED;
  jobId = atoi(tmp);
  err = slurm_load_job(&prev_j_info, jobId, 1); // FIXME: Valgrind Not freed
  if(err) { return err; }
  prev_j_chars = prev_j_info->job_array+(prev_j_info->record_count - 1);
  err = slurm_allocation_lookup(jobId, &prev_alloc);
  if(err) { return err; }
  
  // Populate new job characteristics
  slurm_init_job_desc_msg(job_desc_msg);

  //strdup is required to copy strings as the current allocation info is freed at the end of this function
  //job_desc_msg->name = strdup(mall->service_name);
  MAM_I_generate_job_name(&(job_desc_msg->name));
  job_desc_msg->user_id = getuid();
  job_desc_msg->group_id = getgid();
  job_desc_msg->work_dir = strdup(prev_j_chars->work_dir);
  job_desc_msg->partition = strdup(prev_alloc->partition);
  job_desc_msg->shared = prev_j_chars->shared;
  job_desc_msg->min_nodes = new_nodes;
  job_desc_msg->max_nodes = new_nodes;
  job_desc_msg->end_time = prev_j_chars->end_time;
  job_desc_msg->time_limit = prev_j_chars->time_limit;

  // Set the script to execute the new job (Does not matter, is just to show as info)
  count = strlen(BINBASH) + strlen(proteo_home) + strlen(MAM_EXEC_SCRIPT_DIR) + strlen(MAM_EXEC_SCRIPT) + 5; //+5 considers '\0', two '\n' and two '/'
  job_desc_msg->script = malloc(count * sizeof(char)); //FIXME: ARGV[1-infinity] must be supllied here too
  snprintf(job_desc_msg->script, count, "%s\n%s/%s/%s\n", BINBASH, proteo_home, MAM_EXEC_SCRIPT_DIR, MAM_EXEC_SCRIPT);

  job_desc_msg->argc = 2;
  job_desc_msg->argv = malloc(job_desc_msg->argc * sizeof *job_desc_msg->argv);
  count = strlen(proteo_home) + strlen(MAM_EXEC_SCRIPT_DIR) + strlen(MAM_EXEC_SCRIPT) + 3; //+3 considers '\0' and two '/'
  job_desc_msg->argv[0] = malloc(count * sizeof(char));
  snprintf(job_desc_msg->argv[0], count, "%s/%s/%s", proteo_home, MAM_EXEC_SCRIPT_DIR, MAM_EXEC_SCRIPT);

  //Set the number of ranks for new job
  count = snprintf(NULL, 0, "%d", new_nodes*mall->max_cpus[0]) + strlen(MAM_SCRIPT_ARGV1) + 1;
  job_desc_msg->argv[1] = malloc(count * sizeof(char));
  snprintf(job_desc_msg->argv[1], count, "%s%d", MAM_SCRIPT_ARGV1, new_nodes*mall->max_cpus[0]);

  //Job environments may not be in the previous one. Copy manually. Add the service name also.
  if (MAM_I_slurm_copy_environ(job_desc_msg, mall->port_name) != MAM_OK) {
    fprintf(stderr, "MAM Expand Error: Environment has not been found.\n");
    free(job_desc_msg);
    MPI_Abort(mall->comm, 1);
    exit(1);
  }

#if MAM_DEBUG > 2
  printf("Script length = %zu\n", strlen(job_desc_msg->script));
  printf("----------\n%s\n----------\n", job_desc_msg->script);

  printf("env_size = %u\n", job_desc_msg->env_size);
  printf("job_desc_msg->name=%s\n",job_desc_msg->name);
  //printf("job_desc_msg->work_dir=%s\n",job_desc_msg->work_dir);
  //printf("job_desc_msg->partition=%s\n",job_desc_msg->partition);
  //printf("COMP partition with P1=%d\n", strcmp(job_desc_msg->partition, "P1"));
  //printf("job_desc_msg->shared=%d\n", job_desc_msg->shared);
  //printf("job_desc_msg->min_nodes=%d \n",job_desc_msg->min_nodes);
  //printf("job_desc_msg->max_nodes=%d \n",job_desc_msg->max_nodes );
  //printf("job_desc_msg->end_time=%ld \n",job_desc_msg->end_time );
  //printf("job_desc_msg->time_limit=%d \n",job_desc_msg->time_limit);
  //printf("job_desc_msg->argc=%d\n",job_desc_msg->argc);
  //printf("job_desc_msg->argv[0]=%s\n",job_desc_msg->argv[0]);
  //printf("job_desc_msg->argv[1]=%s\n",job_desc_msg->argv[1]);
#endif

  slurm_free_job_info_msg(prev_j_info);
  slurm_free_resource_allocation_response_msg(prev_alloc);

  return MAM_OK;
}

//Fills a Slurm job request with current environment plus the service name to connect
static int MAM_I_slurm_copy_environ(job_desc_msg_t *job_desc_msg, const char *port_name) {
  int count = 0, count_env, count_service;
  int i, j;

  if (!environ) { return -1; }

  for (char **e = environ; *e; e++) { count++; }
  if (port_name != NULL) { count++; }

  job_desc_msg->environment = calloc(count + 1, sizeof(char *));
  if (!job_desc_msg->environment) { return -1; }

  job_desc_msg->env_size = count;
  count_env = count - 1;
  for (i = 0; i < count_env; i++) {
    job_desc_msg->environment[i] = strdup(environ[i]);
    if (!job_desc_msg->environment[i]) {
      for (j = 0; j < i; j++)
        free(job_desc_msg->environment[j]);
      free(job_desc_msg->environment);
      job_desc_msg->environment = NULL;
      job_desc_msg->env_size = 0;
      return -1;
    }
  }

  if (port_name != NULL) {
    count_service = strlen(MAM_CONNECT_PORT) + strlen(port_name) + 2;
    job_desc_msg->environment[i] = malloc(count_service * sizeof(char));
    snprintf(job_desc_msg->environment[i], count_service, "%s=%s", MAM_CONNECT_PORT, port_name);
  }

  return MAM_OK;
}

// Given a new job info, check it could be reasonably run.
int MAM_I_slurm_request_job(job_desc_msg_t *job_desc_msg, int *new_job_id) {
  int res = MAM_OK;
  will_run_response_msg_t *resp = NULL;
  submit_response_msg_t *res_alloc = NULL;

  int rc = slurm_job_will_run2(job_desc_msg, &resp);
  if(rc != SLURM_SUCCESS) {
    // Job cannot run
#if MAM_DEBUG
    DEBUG_FUNC("MaM Expand. Job characteristics denied by SLURM", mall->myId, mall->numP);
    printf("Errno=%d\n", rc);
    fflush (stdout);
#endif
    res = MAM_DENIED;
  }

  // Deny job if it cannot start before maximum time limit of current
  if(res != MAM_DENIED && job_desc_msg->end_time < resp->start_time && job_desc_msg->time_limit != INFINITE) { 
#if MAM_DEBUG
    DEBUG_FUNC("MaM Expand. Job would have started after current job time limit", mall->myId, mall->numP);
#endif
    res = MAM_DENIED;
  }

  //Launch job
  if(res != MAM_DENIED) {
    rc = slurm_submit_batch_job(job_desc_msg, &res_alloc);
    if(rc != SLURM_SUCCESS) {
#if MAM_DEBUG
      DEBUG_FUNC("MaM Expand. Job characteristics denied by SLURM", mall->myId, mall->numP);
      fflush (stdout);
#endif
      res = MAM_DENIED;
    } else { *new_job_id = res_alloc->job_id; }
    slurm_free_submit_response_response_msg(res_alloc);
  }

  return res;
}

int MAM_I_slurm_check_job_status(int new_job_id) {
  int err, res = MAM_I_RMS_PENDING;
  job_info_msg_t *job_info;
  slurm_job_info_t *job_data;


  err = slurm_load_job(&job_info, new_job_id, 1); // FIXME: Valgrind Not freed
  if(err) { 
    return MAM_DENIED; 
  }
  job_data = job_info->job_array+(job_info->record_count - 1);

  if (job_data->job_state == JOB_RUNNING) { res = MAM_I_RMS_COMPLETED; }

  slurm_free_job_info_msg(job_info);
  return res;
}

#endif
