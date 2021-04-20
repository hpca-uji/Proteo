#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <mpi.h>
#include <string.h>
#include <slurm/slurm.h>
#include "ProcessDist.h"

#define ROOT 0

int commSlurm = COMM_UNRESERVED;
struct Slurm_data *slurm_data;  
pthread_t slurm_thread;

struct Slurm_data {
  char *cmd; // Executable name
  int qty_procs;
  MPI_Info info;
  int type_creation;
};

struct Creation_data {
  char **argv;
  int numP_childs, type_dist;
};

//--------------PRIVATE SPAWN TYPE DECLARATIONS---------------//
void* thread_work(void* creation_data_arg);

//--------------PRIVATE DECLARATIONS---------------//

void processes_dist(char *argv[], int numP_childs, int type_dist);
int create_processes(int myId, int root, MPI_Comm *child, MPI_Comm comm);
void node_dist(slurm_job_info_t job_record, int type, int total_procs, int **qty, int *used_nodes);

int create_hostfile(char *jobId, char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);
void fill_hostfile(slurm_job_info_t job_record, int ptr, int *qty, int used_nodes);

void print_Info(MPI_Info info);

//--------------PUBLIC FUNCTIONS---------------//

int init_slurm_comm(char **argv, int myId, int numP, int root, int type_dist, int type_creation) {

  slurm_data = malloc(sizeof(struct Slurm_data));

  if(myId == root) {
    slurm_data->type_creation = type_creation;
    if(type_creation == COMM_SPAWN_SERIAL) {

      processes_dist(argv, numP, type_dist);
      commSlurm = COMM_FINISHED;

    } else if(type_creation == COMM_SPAWN_PTHREAD) {
      commSlurm = COMM_IN_PROGRESS;

      struct Creation_data *creation_data = malloc(sizeof(struct Creation_Data*));
      creation_data->argv = argv;
      creation_data->numP_childs = numP;
      creation_data->type_dist = type_dist;

      if(pthread_create(&slurm_thread, NULL, thread_work, creation_data)) {
        printf("Error al crear el hilo de contacto con SLURM\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -1;
      }

    }
  }
    
  return 0;
}

int check_slurm_comm(int myId, int root, MPI_Comm comm, MPI_Comm *child) {
  int spawn_err = COMM_IN_PROGRESS;

  if(myId == root && commSlurm == COMM_FINISHED && slurm_data->type_creation == COMM_SPAWN_PTHREAD) {
    if(pthread_join(slurm_thread, NULL)) {
      printf("Error al esperar al hilo\n");
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -2;
    }  
  }

  MPI_Bcast(&commSlurm, 1, MPI_INT, root, comm);

  if(commSlurm == COMM_FINISHED) {
    spawn_err = create_processes(myId, root, child, comm);
    free(slurm_data);
  }

  return spawn_err;
}

//--------------PRIVATE SPAWN TYPE FUNCTIONS---------------//
void* thread_work(void* creation_data_arg) {
  struct Creation_data *creation_data = (struct Creation_data*) creation_data_arg;
 
  processes_dist(creation_data->argv, creation_data->numP_childs, creation_data->type_dist);
  commSlurm = COMM_FINISHED;

  free(creation_data);
  pthread_exit(NULL);
}

//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

void processes_dist(char *argv[], int numP_childs, int type) {
    int jobId, ptr;
    char *tmp;
    job_info_msg_t *j_info;
    slurm_job_info_t last_record;

    int used_nodes=0;
    int *procs_array;
    char *hostfile_name;

    // Get Slurm job info
    tmp = getenv("SLURM_JOB_ID");
    jobId = atoi(tmp);
    slurm_load_job(&j_info, jobId, 1);
    last_record = j_info->job_array[j_info->record_count - 1];

    //COPY PROGRAM NAME
    slurm_data->cmd = malloc(strlen(argv[0]) * sizeof(char));
    strcpy(slurm_data->cmd, argv[0]);

    // GET NEW DISTRIBUTION 
    node_dist(last_record, type, numP_childs, &procs_array, &used_nodes);
    slurm_data->qty_procs = numP_childs;

    // CREATE/UPDATE HOSTFILE
    ptr = create_hostfile(tmp, &hostfile_name);
    MPI_Info_create(&(slurm_data->info));
    MPI_Info_set(slurm_data->info, "hostfile", hostfile_name);
    free(hostfile_name);

    // SET NEW DISTRIBUTION 
    fill_hostfile(last_record, ptr, procs_array, used_nodes);
    close(ptr);

    // Free JOB INFO
    slurm_free_job_info_msg(j_info); 
}


int create_processes(int myId, int root, MPI_Comm *child, MPI_Comm comm) {
  int spawn_err = MPI_Comm_spawn(slurm_data->cmd, MPI_ARGV_NULL, slurm_data->qty_procs, slurm_data->info, root, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", slurm_data->qty_procs);
  }

  if(myId == root) {
    MPI_Info_free(&(slurm_data->info));
    free(slurm_data->cmd);
  }

  return spawn_err;
}

void node_dist(slurm_job_info_t job_record, int type, int total_procs, int **qty, int *used_nodes) {
  int i, asigCores;
  int tamBl, remainder;
  int *procs;

  procs = calloc(job_record.num_nodes, sizeof(int)); // Numero de procesos por nodo

  /* GET NEW DISTRIBUTION  */
  if(type == 1) { // DIST NODES
    *used_nodes = job_record.num_nodes;
    tamBl = total_procs / job_record.num_nodes;
    remainder = total_procs % job_record.num_nodes;
    for(i=0; i<remainder; i++) {
      procs[i] = tamBl + 1; 
    }
    for(i=remainder; i<job_record.num_nodes; i++) {
      procs[i] = tamBl; 
    }
  } else if (type == 2) { // DIST CPUs
    tamBl = job_record.num_cpus / job_record.num_nodes;
    asigCores = 0;
    i = 0;
    *used_nodes = 0;

    while(asigCores+tamBl <= total_procs) {
      asigCores += tamBl;
      procs[i] += tamBl;
      i = (i+1) % job_record.num_nodes;
      (*used_nodes)++;
    }
    if(asigCores < total_procs) {
      procs[i] += total_procs - asigCores;
      (*used_nodes)++;
    }
    if(*used_nodes > job_record.num_nodes) *used_nodes = job_record.num_nodes;
  }

  *qty = calloc(*used_nodes, sizeof(int)); // Numero de procesos por nodo
  for(i=0; i< *used_nodes; i++) {
    (*qty)[i] = procs[i];
  }
  free(procs);
}

int create_hostfile(char *jobId, char **file_name) {
  int ptr, err, len;

  len = strlen(jobId) + 11;

  *file_name = NULL;
  *file_name = malloc( len * sizeof(char));
  if(*file_name == NULL) return -1; // No ha sido posible alojar la memoria
  err = snprintf(*file_name, len, "hostfile.o%s", jobId);
  if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero

  ptr = open(*file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(ptr < 0) return -3; // No ha sido posible crear el fichero

  return ptr; // Devolver puntero a fichero
}

void fill_hostfile(slurm_job_info_t job_record, int ptr, int *qty, int used_nodes) {
  int i=0;
  char *host;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(job_record.nodes);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < used_nodes) {
    write_hostfile_node(ptr, qty[i], host);
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);

}

int write_hostfile_node(int ptr, int qty, char *node_name) {
  int err, len_node, len_int, len;
  char *line;

  len_node = strlen(node_name);
  len_int = snprintf(NULL, 0, "%d", qty);

  len = len_node + len_int + 3;
  line = malloc(len * sizeof(char));
  if(line == NULL) return -1; // No ha sido posible alojar la memoria
  err = snprintf(line, len, "%s:%d\n", node_name, qty);

  if(err < 0) return -2; // No ha sido posible escribir en el fichero

  write(ptr, line, len-1);
  free(line);

  return 0;
}
