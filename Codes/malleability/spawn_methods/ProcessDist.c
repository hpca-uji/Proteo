#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>
#include "ProcessDist.h"

//#define USE_SLURM

//--------------PRIVATE DECLARATIONS---------------//

void node_dist( struct physical_dist dist, int **qty, int *used_nodes);
void spread_dist(struct physical_dist dist, int *used_nodes, int *procs);
void compact_dist(struct physical_dist dist, int *used_nodes, int *procs);

void generate_info_string(int target_qty, MPI_Info *info);
//--------------------------------SLURM USAGE-------------------------------------//
#ifdef USE_SLURM
#include <slurm/slurm.h>
void generate_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, MPI_Info *info);
void fill_str_hosts_slurm(char *nodelist, int *qty, size_t used_nodes, char **hostfile_str);
//@deprecated functions
void generate_info_hostfile_slurm(char *nodelist, int *procs_array, int nodes, MPI_Info *info);
void fill_hostfile_slurm(char *nodelist, int ptr, int *qty, int used_nodes);
#endif
//--------------------------------SLURM USAGE-------------------------------------//

int write_str_node(char **hostfile_str, size_t len_og, size_t qty, char *node_name);
//@deprecated functions
int create_hostfile(char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);

//--------------PUBLIC FUNCTIONS---------------//
/*
 * Pone los datos para una estructura que guarda los parametros
 * para realizar un mappeado de los procesos.
 *
 * Si la memoria no esta reservada devuelve falso y no hace nada.
 * Si puede realizar los cambios devuelve verdadero.
 *
 * IN parameters -->
 * target_qty: Numero de procesos tras la reconfiguracion
 * alreadyCreated: Numero de procesos padre a considerar
 *   La resta de target_qty-alreadyCreated es el numero de hijos a crear
 * num_cpus: Numero de cpus totales (En uso o no)
 * num_nodes: Numero de nodos disponibles por esta aplicacion
 * info_type: Indica como realizar el mappeado, si indicarlo
 *   en una cadena (MALL_DIST_STRING) o en un hostfile
 *   (MALL_DIST_HOSTFILE)
 * dist_type: Indica como sera el mappeado, si intentar rellenar
 *   primero los nodos con cpus ya usados (CPUS/BEST/COMPACT) o
 *   que todos los nodos tengan el mismo numero de cpus usados
 *   (NODES/WORST/SPREAD)
 */
int physical_struct_create(int target_qty, int already_created, int num_cpus, int num_nodes, char *nodelist, int dist_type, int info_type, struct physical_dist *dist) {

  dist->target_qty = target_qty;
  dist->already_created = already_created;
  dist->num_cpus = num_cpus;
  dist->num_nodes = num_nodes;
  dist->nodelist = nodelist;
  dist->dist_type = dist_type;
  dist->info_type = info_type;

  return 1;
}

/*
 * Configura la creacion de un nuevo grupo de procesos, reservando la memoria
 * para una llamada a MPI_Comm_spawn, obteniendo una distribucion fisica
 * para los procesos y creando un fichero hostfile.
 *
 * OUT parameters -->
 * info_spawn: Objeto MPI_Info en el que se indica el mappeado
 *   a usar al crear los procesos.
 */
void processes_dist(struct physical_dist dist, MPI_Info *info_spawn) {
#ifdef USE_SLURM
  int used_nodes=0;
  int *procs_array;
  // GET NEW DISTRIBUTION 
  node_dist(dist, &procs_array, &used_nodes);
  switch(dist.info_type) {
    case MALL_DIST_STRING:
      generate_info_string_slurm(dist.nodelist, procs_array, used_nodes, info_spawn);
      break;
    case MALL_DIST_HOSTFILE:
      generate_info_hostfile_slurm(dist.nodelist, procs_array, used_nodes, info_spawn);
      break;
  }
  free(procs_array);
#else
  generate_info_string(dist.target_qty, info_spawn);
#endif
}


//--------------PRIVATE FUNCTIONS---------------//
//-----------------DISTRIBUTION-----------------//
/*
 * Obtiene la distribucion fisica del grupo de procesos a crear, devolviendo
 * cuantos nodos se van a utilizar y la cantidad de procesos que alojara cada
 * nodo.
 *
 * Se permiten dos tipos de distribuciones fisicas segun el valor de "dist_type":
 *
 *  COMM_PHY_NODES (1): Orientada a equilibrar el numero de procesos entre
 *                      todos los nodos disponibles.
 *  COMM_PHY_CPU   (2): Orientada a completar la capacidad de un nodo antes de
 *                      ocupar otro nodo.
 */
void node_dist(struct physical_dist dist, int **qty, int *used_nodes) {
  int i, *procs;

  procs = calloc(dist.num_nodes, sizeof(int)); // Numero de procesos por nodo

  /* GET NEW DISTRIBUTION  */
  switch(dist.dist_type) {
    case MALL_DIST_SPREAD: // DIST NODES @deprecated
      spread_dist(dist, used_nodes, procs);
      break;
    case MALL_DIST_COMPACT: // DIST CPUs
      compact_dist(dist, used_nodes, procs);
      break;
  }

  //Copy results to output vector qty
  *qty = calloc(*used_nodes, sizeof(int)); // Numero de procesos por nodo
  for(i=0; i< *used_nodes; i++) {
    (*qty)[i] = procs[i];
  }
  free(procs);
}

/*
 * Distribucion basada en equilibrar el numero de procesos en cada nodo
 * para que todos los nodos tengan el mismo numero. Devuelve el total de
 * nodos utilizados y el numero de procesos a crear en cada nodo.
 *
 * TODO Tener en cuenta procesos ya creados (already_created)
 */
void spread_dist(struct physical_dist dist, int *used_nodes, int *procs) {
  int i, tamBl, remainder;

  *used_nodes = dist.num_nodes;
  tamBl = dist.target_qty / dist.num_nodes;
  remainder = dist.target_qty % dist.num_nodes;
  for(i=0; i<remainder; i++) {
    procs[i] = tamBl + 1; 
  }
  for(i=remainder; i<dist.num_nodes; i++) {
    procs[i] = tamBl; 
  }
}

/*
 * Distribucion basada en llenar un nodo de procesos antes de pasar al
 * siguiente nodo. Devuelve el total de nodos utilizados y el numero 
 * de procesos a crear en cada nodo.
 *
 * Tiene en cuenta los procesos ya existentes para el mappeado de 
 * los procesos a crear.
 */
void compact_dist(struct physical_dist dist, int *used_nodes, int *procs) {
  int i, asigCores;
  int tamBl, remainder;

  tamBl = dist.num_cpus / dist.num_nodes;
  asigCores = dist.already_created;
  i = *used_nodes = dist.already_created / tamBl;
  remainder = dist.already_created % tamBl;

  //FIXME REFACTOR Que pasa si los nodos 1 y 2 tienen espacios libres
  //First nodes could already have existing procs
  //Start from the first with free spaces
  if (remainder) {
    procs[i] = tamBl - remainder;
    asigCores += procs[i];
    i = (i+1) % dist.num_nodes;
    (*used_nodes)++;
  }

  //Assign tamBl to each node
  while(asigCores+tamBl <= dist.target_qty) {
    asigCores += tamBl;
    procs[i] += tamBl;
    i = (i+1) % dist.num_nodes;
    (*used_nodes)++;
  }

  //Last node could have less procs than tamBl
  if(asigCores < dist.target_qty) { 
    procs[i] += dist.target_qty - asigCores;
    (*used_nodes)++;
  }
  if(*used_nodes > dist.num_nodes) *used_nodes = dist.num_nodes;  //FIXME Si ocurre esto no es un error?
}


//--------------PRIVATE FUNCTIONS---------------//
//-------------------INFO SET-------------------//

/*
 * Crea y devuelve un objeto MPI_Info con un par hosts/mapping
 * en el que se indica el mappeado a utilizar en los nuevos
 * procesos.
 *
 * Actualmente no considera que puedan haber varios nodos
 * y lleva todos al mismo. Las funciones "generate_info_string_slurm"
 * o "generate_info_hostfile_slurm" permiten utilizar varios
 * nodos, pero es necesario activar Slurm.
 */
void generate_info_string(int target_qty, MPI_Info *info){
  char *host_string, *host;
  int len, err;

  host = "localhost";
  //host = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  //MPI_Get_processor_name(host, &len);
  // CREATE AND SET STRING HOSTS
  err = write_str_node(&host_string, 0, target_qty, host);
  if (err<0) {printf("Error when generating mapping: %d\n", err); MPI_Abort(MPI_COMM_WORLD, err);}
  // SET MAPPING
  MPI_Info_create(info);
  MPI_Info_set(*info, "hosts", host_string);
  //free(host);
  free(host_string);
}

//--------------------------------SLURM USAGE-------------------------------------//
#ifdef USE_SLURM
/*
 * Crea y devuelve un objeto MPI_Info con un par hosts/mapping
 * en el que se indica el mappeado a utilizar en los nuevos
 * procesos.
 * Es necesario usar Slurm para usarlo.
 */
void generate_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, MPI_Info *info){
  // CREATE AND SET STRING HOSTS
  char *hoststring;
  fill_str_hosts_slurm(nodelist, procs_array, nodes, &hoststring);
  MPI_Info_create(info);
  MPI_Info_set(*info, "hosts", hoststring);
  free(hoststring);
}


/*
 * Crea y devuelve una cadena para ser utilizada por la llave "hosts"
 * al crear procesos e indicar donde tienen que ser creados.
 */
void fill_str_hosts_slurm(char *nodelist, int *qty, size_t used_nodes, char **hostfile_str) {
  char *host;
  size_t i=0,len=0;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(nodelist);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < used_nodes) {
    if(qty[i] != 0) {
      len = write_str_node(hostfile_str, len, qty[i], host);
    }
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);
}

#endif
//--------------------------------SLURM USAGE-------------------------------------//
/*
 * Añade en una cadena "qty" entradas de "node_name".
 * Realiza la reserva de memoria y la realoja si es necesario.
 */
int write_str_node(char **hostfile_str, size_t len_og, size_t qty, char *node_name) {
  int err;
  char *ocurrence;
  size_t i, len, len_node;

  len_node = strlen(node_name) + 1; // Str length + ','
  len = qty * len_node; // Number of times the node is used

  if(len_og == 0) { // Memoria no reservada
    *hostfile_str = (char *) malloc((len+1) * sizeof(char));
  } else { // Cadena ya tiene datos
    *hostfile_str = (char *) realloc(*hostfile_str, (len_og + len + 1) * sizeof(char));
  }
  if(hostfile_str == NULL) return -1; // No ha sido posible alojar la memoria

  ocurrence = (char *) malloc((len_node+1) * sizeof(char));
  if(ocurrence == NULL) return -2; // No ha sido posible alojar la memoria
  err = snprintf(ocurrence, len_node+1, ",%s", node_name);
  if(err < 0) return -3; // No ha sido posible escribir sobre la variable auxiliar

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

//--------------------------------SLURM USAGE-------------------------------------//
#ifdef USE_SLURM
/* FIXME Por revisar
 * @deprecated
 * Genera un fichero hostfile y lo anyade a un objeto
 * MPI_Info para ser utilizado.
 */
void generate_info_hostfile_slurm(char *nodelist, int *procs_array, int nodes, MPI_Info *info){
    char *hostfile;
    int ptr;

    // CREATE/UPDATE HOSTFILE 
    ptr = create_hostfile(&hostfile);
    MPI_Info_create(info);
    MPI_Info_set(*info, "hostfile", hostfile);
    free(hostfile);

    // SET NEW DISTRIBUTION 
    fill_hostfile_slurm(nodelist, ptr, procs_array, nodes);
    close(ptr);
}

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
int create_hostfile(char **file_name) {
  int ptr, err;
  size_t len = 11; //FIXME Numero mágico

  *file_name = NULL;
  *file_name = malloc(len * sizeof(char));
  if(*file_name == NULL) return -1; // No ha sido posible alojar la memoria
  err = snprintf(*file_name, len, "hostfile.o");
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
void fill_hostfile_slurm(char *nodelist, int ptr, int *qty, int nodes) {
  int i=0;
  char *host;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(nodelist);
  while ((host = slurm_hostlist_shift(hostlist)) && i < nodes) {
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
  int err;
  char *line;
  size_t len, len_node, len_int;

  len_node = strlen(node_name);
  err = snprintf(NULL, 0, "%d", qty);
  if(err < 0) return -1;
  len_int = err;

  len = len_node + len_int + 3;
  line = malloc(len * sizeof(char));
  if(line == NULL) return -2; // No ha sido posible alojar la memoria
  err = snprintf(line, len, "%s:%d\n", node_name, qty);

  if(err < 0) return -3; // No ha sido posible escribir en el fichero

  write(ptr, line, len-1);
  free(line);

  return 0;
}
#endif
//--------------------------------SLURM USAGE-------------------------------------//


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
