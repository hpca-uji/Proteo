#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <mpi.h>
#include <string.h>
#include <slurm/slurm.h>
#include "ProcessDist.h"

int commSlurm = MAL_NOT_STARTED;
struct Slurm_data *slurm_data;  
pthread_t slurm_thread;
MPI_Comm *returned_comm;

struct Slurm_data {
  char *cmd; // Executable name
  int qty_procs, result_procs;
  MPI_Info info;
  int type_creation;
  int spawn_is_single;
};

typedef struct {
  char *argv;
  int numP_childs, myId, root, type_dist;
  int spawn_is_single;
  int spawn_method;
  MPI_Comm comm;
}Creation_data;


//--------------PRIVATE SPAWN TYPE DECLARATIONS---------------//
void* thread_work(void* creation_data_arg);

//--------------PRIVATE DECLARATIONS---------------//
void processes_dist(char *argv, int numP_childs, int type_dist);

void generic_spawn(int myId, int root, int is_single, MPI_Comm *child, MPI_Comm comm);
void single_spawn_connection(int myId, int root, MPI_Comm comm, MPI_Comm *child);
int create_processes(int myId, int root, MPI_Comm *child, MPI_Comm comm);

void node_dist(slurm_job_info_t job_record, int type, int total_procs, int **qty, int *used_nodes);

void fill_str_hostfile(slurm_job_info_t job_record, int *qty, int used_nodes, char **hostfile_str);
int write_str_node(char **hostfile_str, int len_og, int qty, char *node_name);

//@deprecated functions
int create_hostfile(char *jobId, char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);
void fill_hostfile(slurm_job_info_t job_record, int ptr, int *qty, int used_nodes);

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
int init_slurm_comm(char *argv, int myId, int numP, int numC, int root, int type_dist, int type_creation, int spawn_is_single, MPI_Comm comm, MPI_Comm *child) {
  int spawn_qty;
  slurm_data = malloc(sizeof(struct Slurm_data));

  slurm_data->type_creation = type_creation;
  slurm_data->spawn_is_single = spawn_is_single;
  slurm_data->result_procs = numC;
  spawn_qty = numC;
  if(type_creation == COMM_SPAWN_MERGE || type_creation == COMM_SPAWN_MERGE_PTHREAD) {
    if (numP < slurm_data->result_procs) {
      spawn_qty = slurm_data->result_procs - numP;
    }
  }

  if(type_creation == COMM_SPAWN_SERIAL || slurm_data->type_creation == COMM_SPAWN_MERGE) {

    if(myId == root) {
      processes_dist(argv, numP, type_dist);
    } else {
      slurm_data->cmd = malloc(1 * sizeof(char));
      slurm_data->info = MPI_INFO_NULL;
    }

    // WORK
    generic_spawn(myId, root, slurm_data->spawn_is_single, child, comm);
    if(slurm_data->type_creation == COMM_SPAWN_MERGE) {
      int numParents;
      MPI_Comm_size(comm, &numParents);
      if(numParents < numP) { //Expand
      //proc_adapt_expand(numParents, numP, child, comm, MALLEABILITY_NOT_CHILDREN);
      }
    }
    // END WORK

    if(myId == root && slurm_data->info != MPI_INFO_NULL) {
      MPI_Info_free(&(slurm_data->info));
    }
    free(slurm_data->cmd);
    free(slurm_data);

    commSlurm = MAL_SPAWN_COMPLETED;

  } else if(type_creation == COMM_SPAWN_PTHREAD || slurm_data->type_creation == COMM_SPAWN_MERGE_PTHREAD) {
    commSlurm = MAL_SPAWN_PENDING;
    
    if((spawn_is_single && myId == root) || !spawn_is_single || (slurm_data->type_creation == COMM_SPAWN_MERGE_PTHREAD && numP > slurm_data->result_procs)) {
      Creation_data *creation_data = (Creation_data *) malloc(sizeof(Creation_data));
      creation_data->argv = argv;
      creation_data->numP_childs = spawn_qty;
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
  }
    
  return commSlurm;
}


/*
 * Comprueba si una configuracion para crear un nuevo grupo de procesos esta lista,
 * y en caso de que lo este, se devuelve el communicador a estos nuevos procesos.
 */
int check_slurm_comm(int myId, int root, int numP, MPI_Comm *child, MPI_Comm comm, MPI_Comm comm_thread) {
  int state=-10;
  
  if(slurm_data->type_creation == COMM_SPAWN_PTHREAD || slurm_data->type_creation == COMM_SPAWN_MERGE_PTHREAD) {
    if(!slurm_data->spawn_is_single || (slurm_data->type_creation == COMM_SPAWN_MERGE_PTHREAD && numP > slurm_data->result_procs)) {

      MPI_Allreduce(&commSlurm, &state, 1, MPI_INT, MPI_MIN, comm);
      if(state != MAL_SPAWN_COMPLETED) return state; // Continue only if asynchronous process creation has ended 

      if(pthread_join(slurm_thread, NULL)) {
        printf("Error al esperar al hilo\n");
        MPI_Abort(MPI_COMM_WORLD, -1);
        return -10;
      }  
      *child = *returned_comm;

    } else if (slurm_data->spawn_is_single) {

      MPI_Bcast(&commSlurm, 1, MPI_INT, root, comm);
      state = commSlurm;
      if(state == MAL_SPAWN_PENDING) return state; // Continue only if asynchronous process creation has ended 

      if(myId == root) {
        if(pthread_join(slurm_thread, NULL)) {
          printf("Error al esperar al hilo\n");
          MPI_Abort(MPI_COMM_WORLD, -1);
          return -10;
        }  
        *child = *returned_comm;
      } else {
        slurm_data->cmd = malloc(1 * sizeof(char));
        slurm_data->info = MPI_INFO_NULL;
        generic_spawn(myId, root, slurm_data->spawn_is_single, child, comm_thread);
      }

    } else {
      printf("Error Check spawn: Configuracion invalida\n");
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -10;
    }
  } else {  
    return commSlurm;
  }

  //Free memory
  if(myId == root && slurm_data->info != MPI_INFO_NULL) {
    MPI_Info_free(&(slurm_data->info));
  }
  free(slurm_data->cmd);
  free(slurm_data);

  return commSlurm;
}

/*
 * Conectar grupo de hijos con grupo de padres
 * Devuelve un intercomunicador para hablar con los padres
 *
 * Solo se utiliza cuando la creación de los procesos ha sido
 * realizada por un solo proceso padre
 */
void malleability_establish_connection(int myId, int root, MPI_Comm *intercomm) {
  char *port_name;
  MPI_Comm newintercomm;

  if(myId == root) {
    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Open_port(MPI_INFO_NULL, port_name);
    MPI_Send(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, root, 130, *intercomm);
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_accept(port_name, MPI_INFO_NULL, root, MPI_COMM_WORLD, &newintercomm);

  if(myId == root) {
    MPI_Close_port(port_name);
  }
  free(port_name);
  MPI_Comm_free(intercomm);
  *intercomm = newintercomm;
}

//--------------TODO PRIVATE SPAWN TYPE FUNCTIONS---------------//

/*
 * Se encarga de que el grupo de procesos resultante se 
 * encuentren todos en un intra comunicador, uniendo a
 * padres e hijos en un solo comunicador.
 *
 * Se llama antes de la redistribución de datos.
 *
 * TODO REFACTOR
 */
void proc_adapt_expand(int *numP, int numC, MPI_Comm intercomm, MPI_Comm *comm, int is_children_group) {
  MPI_Comm new_comm = MPI_COMM_NULL;

  MPI_Intercomm_merge(intercomm, is_children_group, &new_comm); //El que pone 0 va primero
  //MPI_Comm_free(intercomm); TODO Nueva redistribucion para estos casos y liberar aqui
  // *intercomm = MPI_COMM_NULL;

  *numP = numC;
  if(*comm != MPI_COMM_WORLD && *comm != MPI_COMM_NULL) {
    MPI_Comm_free(comm);
  }
  *comm=new_comm;
}


/*
 * Se encarga de que el grupo de procesos resultante se
 * eliminen aquellos procesos que ya no son necesarios.
 * Los procesos eliminados se quedaran como zombies.
 *
 * Se llama una vez ha terminado la redistribución de datos.
 */
void proc_adapt_shrink(int numC, MPI_Comm *comm, int myId) {
  int color = MPI_UNDEFINED;
  MPI_Comm new_comm = MPI_COMM_NULL;

  if(myId < numC) {
      color = 1;  
  }
  MPI_Comm_split(*comm, color, myId, &new_comm);

  if(*comm != MPI_COMM_WORLD && *comm != MPI_COMM_NULL)
    //MPI_Comm_free(comm); FIXME
  *comm=new_comm;
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
  int numP;
  MPI_Comm aux_comm;
  Creation_data *creation_data = (Creation_data*) creation_data_arg;
  returned_comm = (MPI_Comm *) malloc(sizeof(MPI_Comm));
 
  if(creation_data->myId == creation_data->root) {
    processes_dist(creation_data->argv, creation_data->numP_childs, creation_data->type_dist);
  } else {
    slurm_data->cmd = malloc(1 * sizeof(char));
    slurm_data->info = MPI_INFO_NULL;
  }
  commSlurm = MAL_SPAWN_COMPLETED; // TODO REFACTOR?
  
  generic_spawn(creation_data->myId, creation_data->root, slurm_data->spawn_is_single, returned_comm, creation_data->comm);

  if(slurm_data->type_creation == COMM_SPAWN_MERGE_PTHREAD) {
    //MPI_Comm_size(creation_data->comm, &numP);
    numP= 1; //FIXME BORRAR
    if(numP < creation_data->numP_childs) { //Expand
      //TODO Crear nueva redistribucion y descomentar esto
      //proc_adapt_expand(numP, creation_data->numP_childs, returned_comm, &aux_comm, MALLEABILITY_NOT_CHILDREN);
      //*returned_comm = aux_comm;
    }
  }

  free(creation_data);
  pthread_exit(NULL);
}

//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

/*
 * Configura la creacion de un nuevo grupo de procesos, reservando la memoria
 * para una llamada a MPI_Comm_spawn, obteniendo una distribucion fisica
 * para los procesos y creando un fichero hostfile.
 */
void processes_dist(char *argv, int numP_childs, int type) {
    int jobId;
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
    slurm_data->cmd = malloc(strlen(argv) * sizeof(char));
    strcpy(slurm_data->cmd, argv);

    // GET NEW DISTRIBUTION 
    node_dist(last_record, type, numP_childs, &procs_array, &used_nodes);
    slurm_data->qty_procs = numP_childs;

    /*
    // CREATE/UPDATE HOSTFILE 
    int ptr;
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
    free(procs_array);
   

    // Free JOB INFO
    slurm_free_job_info_msg(j_info); 
}

/*
TODO
*/
void generic_spawn(int myId, int root, int spawn_is_single, MPI_Comm *child, MPI_Comm comm) {
  if(spawn_is_single) {
    single_spawn_connection(myId, root, comm, child);
  } else {
    int rootBcast = MPI_PROC_NULL;
    if(myId == root) rootBcast = MPI_ROOT;
    create_processes(myId, root, child, comm);
    MPI_Bcast(&spawn_is_single, 1, MPI_INT, rootBcast, *child);
    if(*child == MPI_COMM_NULL) {printf("P%d tiene un error --\n", myId); fflush(stdout);} else {printf("P%d guay\n", myId);}
  }
  commSlurm = MAL_SPAWN_COMPLETED; 
}


/* 
 * Si la variable "type" es 1, la creación es con la participación de todo el grupo de padres
 * Si el valor es diferente, la creación es solo con la participación del proceso root
 */
void single_spawn_connection(int myId, int root, MPI_Comm comm, MPI_Comm *child){
  char *port_name;
  int auxiliar_conf = COMM_SPAWN_SINGLE;
  MPI_Comm newintercomm;

  if (myId == root) {
    create_processes(myId, root, child, MPI_COMM_SELF);
    MPI_Bcast(&auxiliar_conf, 1, MPI_INT, MPI_ROOT, *child);

    port_name = (char *) malloc(MPI_MAX_PORT_NAME * sizeof(char));
    MPI_Recv(port_name, MPI_MAX_PORT_NAME, MPI_CHAR, root, 130, *child, MPI_STATUS_IGNORE);
  } else {
    port_name = malloc(1);
  }

  MPI_Comm_connect(port_name, MPI_INFO_NULL, root, comm, &newintercomm);

  if(myId == root)
    MPI_Comm_free(child);
  free(port_name);
  *child = newintercomm;
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
 * Crea y devuelve una cadena para ser utilizada por la llave "hosts"
 * al crear procesos e indicar donde tienen que ser creados.
 */
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

/*
 * Añade en una cadena "qty" entradas de "node_name".
 * Realiza la reserva de memoria y la realoja si es necesario.
 */
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
  for(; i<qty; i++){ // Las siguientes se conctanenan
    strcat(*hostfile_str, ocurrence);
  }

  
  free(ocurrence);
  return len+len_og;
}

//====================================================
//====================================================
//============DEPRECATED FUNCTIONS====================
//====================================================
//====================================================


/*
 * @deprecated
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
 * @deprecated
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
 * @deprecated
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
