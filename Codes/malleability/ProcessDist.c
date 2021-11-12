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
MPI_Comm *returned_comm;

struct Slurm_data {
  char *cmd; // Executable name
  int qty_procs;
  MPI_Info info;
  int type_creation;
};

typedef struct {
  char **argv;
  int numP_childs, myId, root, type_dist;
  MPI_Comm comm;
}Creation_data;


//--------------PRIVATE SPAWN TYPE DECLARATIONS---------------//
void* thread_work(void* creation_data_arg);

//--------------PRIVATE DECLARATIONS---------------//

void processes_dist(char *argv[], int numP_childs, int type_dist);
int create_processes(int myId, int root, MPI_Comm *child, MPI_Comm comm);
void node_dist(slurm_job_info_t job_record, int type, int total_procs, int **qty, int *used_nodes);

int create_hostfile(char *jobId, char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);
void fill_hostfile(slurm_job_info_t job_record, int ptr, int *qty, int used_nodes);

//TESTS
void fill_str_hostfile(slurm_job_info_t job_record, int *qty, int used_nodes, char **hostfile_str);
int write_str_node(char **hostfile_str, int len_og, int qty, char *node_name);
//

void print_Info(MPI_Info info);

//--------------PUBLIC FUNCTIONS---------------//

/*
 * Se solicita la creacion de un nuevo grupo de "numP" procesos con una distribucion
 * fisica "type_dist".
 *
 * Se puede solicitar en primer plano, encargandose por tanto el proceso que llama a esta funcion,
 * o en segundo plano, donde un hilo se encarga de configurar esta creacion.
 *
 * Si se pide en primer plano, al terminarla es posible llamar a "check_slurm_comm()" para crear
 * los procesos.
 *
 * Si se pide en segundo plano, llamar a "check_slurm_comm()" comprobara si la configuracion para
 * crearlos esta lista, y si es asi, los crea.
 *
 * Devuelve el estado de el procedimiento. Si no devuelve "COMM_FINISHED", es necesario llamar a
 * "check_slurm_comm()".
 */
int init_slurm_comm(char **argv, int myId, int numP, int root, int type_dist, int type_creation, MPI_Comm comm, MPI_Comm *child) {

  slurm_data = malloc(sizeof(struct Slurm_data));

  slurm_data->type_creation = type_creation;
  if(type_creation == COMM_SPAWN_SERIAL) {

    if(myId == root) {
      processes_dist(argv, numP, type_dist);
    } else {
      slurm_data->cmd = malloc(1 * sizeof(char));
      slurm_data->info = MPI_INFO_NULL;
    }
    create_processes(myId, root, child, comm);

    if(myId == root && slurm_data->info != MPI_INFO_NULL) {
      MPI_Info_free(&(slurm_data->info));
    }
    free(slurm_data->cmd);
    free(slurm_data);

    commSlurm = COMM_FINISHED;

  } else if(type_creation == COMM_SPAWN_PTHREAD) {
    commSlurm = COMM_IN_PROGRESS;

    Creation_data *creation_data = (Creation_data *) malloc(sizeof(Creation_data));
    creation_data->argv = argv;
    creation_data->numP_childs = numP;
    creation_data->myId = myId;
    creation_data->root = root;
    creation_data->type_dist = type_dist;
    creation_data->comm = comm;

    if(pthread_create(&slurm_thread, NULL, thread_work, (void *)creation_data)) {
      printf("Error al crear el hilo de contacto con SLURM\n");
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -1;
    }
  }
    
  return commSlurm;
}

/*
 * Comprueba si una configuracion para crear un nuevo grupo de procesos esta lista,
 * y en caso de que lo este, se devuelve el communicador a estos nuevos procesos.
 */
int check_slurm_comm(int myId, int root, int numP, MPI_Comm *child) { // TODO Borrar numP si no se usa
  int state=-10;

  if(slurm_data->type_creation == COMM_SPAWN_PTHREAD) {

    //MPI_Allreduce(&commSlurm, &state, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    
    if(myId == root) {
      int i, recv_state;
      state = commSlurm;
      for(i=0; i<numP; i++) { //Recv states
	if(i != myId) {
          MPI_Recv(&recv_state, 1, MPI_INT, i, 120, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
          if(recv_state == COMM_IN_PROGRESS) {
	    state = recv_state;
	  }
	}
      }
      for(i=0; i<numP; i++) { //Send state
	if(i != myId) {
          MPI_Send(&state, 1, MPI_INT, i, 120, MPI_COMM_WORLD);
	}
      }
    } else {
      MPI_Send(&commSlurm, 1, MPI_INT, root, 120, MPI_COMM_WORLD);
      MPI_Recv(&state, 1, MPI_INT, root, 120, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    if(state != COMM_FINISHED) return state; // Continue only if asynchronous process creation has ended 

  } else { 
    return commSlurm;
  }

  if(pthread_join(slurm_thread, NULL)) {
    printf("Error al esperar al hilo\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -10;
  }  

  *child = *returned_comm;

  if(myId == root && slurm_data->info != MPI_INFO_NULL) {
    MPI_Info_free(&(slurm_data->info));
  }
  free(slurm_data->cmd);
  free(slurm_data);

  return commSlurm;
}

//--------------PRIVATE SPAWN TYPE FUNCTIONS---------------//

/*
 * Funcion llamada por un hilo para que este se encarge
 * de configurar la creacion de un nuevo grupo de procesos.
 *
 * Una vez esta lista la configuracion y es posible crear los procesos
 * se avisa al hilo maestro.
 */
void* thread_work(void* creation_data_arg) {
  Creation_data *creation_data = (Creation_data*) creation_data_arg;
  returned_comm = (MPI_Comm *) malloc(sizeof(MPI_Comm));
 
  if(creation_data->myId == creation_data->root) {
    processes_dist(creation_data->argv, creation_data->numP_childs, creation_data->type_dist);
  } else {
    slurm_data->cmd = malloc(1 * sizeof(char));
    slurm_data->info = MPI_INFO_NULL;
  }

  create_processes(creation_data->myId, creation_data->root, returned_comm, creation_data->comm);
  commSlurm = COMM_FINISHED;

  free(creation_data);
  pthread_exit(NULL);
}

//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

/*
 * Configura la creacion de un nuevo grupo de procesos, reservando la memoria
 * para una llamada a MPI_Comm_spawn, obteniendo una distribucion fisica
 * para los procesos y creando un fichero hostfile.
 */
void processes_dist(char *argv[], int numP_childs, int type) {
    int jobId, ptr;
    char *tmp;
    job_info_msg_t *j_info;
    slurm_job_info_t last_record;

    int used_nodes=0;
    int *procs_array;
    char *hostfile;

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

    /*
    // CREATE/UPDATE HOSTFILE 
    ptr = create_hostfile(tmp, &hostfile);
    MPI_Info_create(&(slurm_data->info));
    MPI_Info_set(slurm_data->info, "hostfile", hostfile);
    free(hostfile);

    // SET NEW DISTRIBUTION 
    fill_hostfile(last_record, ptr, procs_array, used_nodes);
    close(ptr);
    */
    

    // TEST 
    fill_str_hostfile(last_record, procs_array, used_nodes, &hostfile);
    MPI_Info_create(&(slurm_data->info));
    MPI_Info_set(slurm_data->info, "hosts", hostfile);
    free(hostfile);
   

    // Free JOB INFO
    slurm_free_job_info_msg(j_info); 
}

/*
 * Crea un grupo de procesos segun la configuracion indicada por la funcion
 * "processes_dist()".
 */
int create_processes(int myId, int root, MPI_Comm *child, MPI_Comm comm) {
  int spawn_err = MPI_Comm_spawn(slurm_data->cmd, MPI_ARGV_NULL, slurm_data->qty_procs, slurm_data->info, root, comm, child, MPI_ERRCODES_IGNORE); 

  if(spawn_err != MPI_SUCCESS) {
    printf("Error creating new set of %d procs.\n", slurm_data->qty_procs);
  }

  return spawn_err;
}

/*
 * Obtiene la distribucion fisica del grupo de procesos a crear, devolviendo
 * cuantos nodos se van a utilizar y la cantidad de procesos que alojara cada
 * nodo.
 *
 * Se permiten dos tipos de distribuciones fisicas segun el valor de "type":
 *
 *  COMM_PHY_NODES (1): Orientada a equilibrar el numero de procesos entre
 *                      todos los nodos disponibles.
 *  COMM_PHY_CPU   (2): Orientada a completar la capacidad de un nodo antes de
 *                      ocupar otro nodo.
 */
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
    if(*used_nodes > job_record.num_nodes) *used_nodes = job_record.num_nodes;  //FIXME Si ocurre esto no es un error?
  }

  *qty = calloc(*used_nodes, sizeof(int)); // Numero de procesos por nodo
  for(i=0; i< *used_nodes; i++) {
    (*qty)[i] = procs[i];
  }
  free(procs);
}

/*
 * Crea un fichero que se utilizara como hostfile
 * para un nuevo grupo de procesos. 
 *
 * El nombre es devuelto en el argumento "file_name",
 * que tiene que ser un puntero vacio.
 *
 * Ademas se devuelve un descriptor de fichero para 
 * modificar el fichero.
 */
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

/*
 * Rellena un fichero hostfile indicado por ptr con los nombres
 * de los nodos a utilizar indicados por "job_record" y la cantidad 
 * de procesos que alojara cada nodo indicado por "qty".
 */
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

/*
 * Escribe en el fichero hostfile indicado por ptr una nueva linea.
 *
 * Esta linea indica el nombre de un nodo y la cantidad de procesos a
 * alojar en ese nodo.
 */
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



void fill_str_hostfile(slurm_job_info_t job_record, int *qty, int used_nodes, char **hostfile_str) {
  int i=0, len=0;
  char *host;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(job_record.nodes);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < used_nodes) {
    len = write_str_node(hostfile_str, len, qty[i], host);
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);

}

int write_str_node(char **hostfile_str, int len_og, int qty, char *node_name) {
  int err, len_node, len, i;
  char *ocurrence;

  len_node = strlen(node_name);
  len = qty * (len_node + 1);

  if(len_og == 0) { // Memoria no reservada
    *hostfile_str = (char *) malloc(len * sizeof(char) - (1 * sizeof(char)));
  } else { // Cadena ya tiene datos
    *hostfile_str = (char *) realloc(*hostfile_str, (len_og + len) * sizeof(char) - (1 * sizeof(char)));
  }
  if(hostfile_str == NULL) return -1; // No ha sido posible alojar la memoria

  ocurrence = (char *) malloc((len_node+1) * sizeof(char));
  if(ocurrence == NULL) return -1; // No ha sido posible alojar la memoria
  err = sprintf(ocurrence, ",%s", node_name);
  if(err < 0) return -2; // No ha sido posible escribir sobre la variable auxiliar

  i=0;
  if(len_og == 0) { // Si se inicializa, la primera es una copia
    i++;
    strcpy(*hostfile_str, node_name);
  }
  for(; i<qty; i++){ // Las siguientes se realizan con concatenan
    strcat(*hostfile_str, ocurrence);
  }

  
  free(ocurrence);
  return len+len_og;
}
