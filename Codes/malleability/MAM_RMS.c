#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <mpi.h>
#include "MAM_RMS.h"
#include "MAM_DataStructures.h"


#if MAM_USE_SLURM
#include <slurm/slurm.h>
int MAM_I_slurm_getenv_hosts_info();
int MAM_I_slurm_getjob_hosts_info();
#endif

int MAM_I_get_hosts_info();
int GetCPUCount();

void MAM_check_hosts() {
  int not_filled = 1;
  
  #if MAM_USE_SLURM
    not_filled = MAM_I_slurm_getenv_hosts_info();
    if(not_filled) {
      if(mall->nodelist != NULL) {
        free(mall->nodelist);
	mall->nodelist = NULL;
      }

      not_filled = MAM_I_slurm_getjob_hosts_info();
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
      printf("NODELIST: %s\nNODE_COUNT: %d NUM_CPUS_PER_NODE: %d\n", mall->nodelist, mall->num_nodes, mall->num_cpus);
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
 * FIXME Does not consider heterogenous machines for num_cpus
 * FIXME Always returns 0... -- Perform error checking?
 */
int MAM_I_get_hosts_info() {
  int i, j, name_len, max_name_len, unique_count, *unique_hosts;
  char *my_host, *all_hosts, *confirmed_host, *tested_host;

  all_hosts = NULL;
  my_host = (char *) malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  MPI_Get_processor_name(my_host, &name_len);

  MPI_Allreduce(&name_len, &max_name_len, 1, MPI_INT, MPI_MAX, mall->comm);
  my_host[max_name_len] = '\0';
  max_name_len++; // Len does not consider terminating character
  if(mall->myId == mall->root) {
    all_hosts = (char *) malloc(mall->numP * max_name_len * sizeof(char));
    unique_hosts = (int *) malloc(mall->numP * sizeof(int));
    unique_hosts[0] = 0; //First host will always be unique
    unique_count = 1;
  }
  //FIXME Should be a Gatherv as each host could have unitialised chars between name_len and max_name_len
  MPI_Gather(my_host, max_name_len, MPI_CHAR, all_hosts, max_name_len, MPI_CHAR, mall->root, mall->comm);

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
    mall->num_cpus = GetCPUCount(); 
    mall->nodelist_len = unique_count*max_name_len;
    mall->nodelist = (char *) malloc(mall->nodelist_len * sizeof(char));

    strcpy(mall->nodelist, ""); //FIXME Strcat can be very inneficient...
    for (i = 0; i < unique_count; i++) {
      confirmed_host = all_hosts + (unique_hosts[i] * max_name_len);
      strcat(mall->nodelist, confirmed_host);
      if (i < unique_count - 1) {
        strcat(mall->nodelist, ",");
      }
    }

    free(all_hosts);
    free(unique_hosts);
  }

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

#if MAM_USE_SLURM
/*
 * TODO
 */
int MAM_I_slurm_getenv_hosts_info() {
  char *tmp = NULL, *tmp_copy, *token;
  int cpus, count;
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
  //TODO When MaM considers heteregenous allocations, these will be needed instead of num_cpus.
  //cpus_counts = (int *) malloc(mall->num_nodes * sizeof(int));
  //nodes_counts = (int *) malloc(mall->num_nodes * sizeof(int));
  //i = 0;
  mall->num_cpus = 0;

  while (token != NULL) {
    // If actual token contains only one node, the second portion
    // does not appear and sscanf does not modify "count"
    // First portion --> "%d"
    // Second portion -> "(x%d)"
    count = 1;
    if (sscanf(token, "%d(x%d)", &cpus, &count) >= 1) {
      mall->num_cpus = cpus; // num_cpus stores the amount of cores per cpu
      //cpus_per_node[i] = cpus;
      //nodes_count[i] = count;
      //i++;
    }
    token = strtok(NULL, ",");
  }
  /*
  if(i < mall->num_nodes) {
    aux = (int *) realloc(cpus_per_node, i * sizeof(int));
    if(cpus_per_node != aux && cpus_per_node != NULL) free(cpus_per_node);
    cpus_per_node = aux;

    aux = (int *) realloc(nodes_counts, i * sizeof(int));
    if(nodes_count != aux && nodes_count != NULL) free(nodes_count);
    nodes_count = aux;
  }
  */

  free(tmp_copy);
  return 0;
}

/*
 * TODO
 * FIXME Does not consider heterogenous machines
 */
int MAM_I_slurm_getjob_hosts_info() {
  int jobId, err;
  char *tmp = NULL;
  job_info_msg_t *j_info;
  slurm_job_info_t last_record;

  tmp = getenv("SLURM_JOB_ID");
  if(tmp == NULL) return 1;
  jobId = atoi(tmp);

  err = slurm_load_job(&j_info, jobId, 1);
  if(err) return err;
  last_record = j_info->job_array[j_info->record_count - 1];

  mall->num_nodes = last_record.num_nodes;
  mall->num_cpus = last_record.num_cpus;

  mall->nodelist_len = strlen(last_record.nodes)+1;
  mall->nodelist = (char *) malloc(mall->nodelist_len * sizeof(char));
  strcpy(mall->nodelist, last_record.nodes);

  slurm_free_job_info_msg(j_info);
  return 0;
}
#endif

//TODO REFACTOR PARA CUANDO SE COMUNIQUE CON RMS
    // Get Slurm job info
    //int jobId;
    //char *tmp;
    //job_info_msg_t *j_info;
    //slurm_job_info_t last_record;
    //tmp = getenv("SLURM_JOB_ID");
    //jobId = atoi(tmp);
    //slurm_load_job(&j_info, jobId, 1);
    //last_record = j_info->job_array[j_info->record_count - 1];
    // Free JOB INFO
    //slurm_free_job_info_msg(j_info);
