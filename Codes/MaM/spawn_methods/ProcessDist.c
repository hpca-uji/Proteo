#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>
#include "ProcessDist.h"
#include "SpawnUtils.h"
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"

//--------------PRIVATE CONSTANTS------------------//
#define MAM_HOSTFILE_NAME1 "MAM_HF_ID"  // Constant size name (9) -- Part of SIZE1
#define MAM_HOSTFILE_NAME2 "_S"  // Constant size name (2) -- Part of SIZE1
#define MAM_HOSTFILE_NAME3 ".tmp"  // Constant size name (4) -- Part of SIZE2
#define MAM_HOSTFILE_SIZE1 15 // 11 Chars + 4 Digits 
#define MAM_HOSTFILE_SIZE2 8 // 4 Chars + 3 Digits + \0
#define MAM_HOSTFILE_SIZE MAM_HOSTFILE_SIZE1 + MAM_HOSTFILE_SIZE2 //23 = 15 Chars + 7 Digits + \0
#define MAM_HOSTFILE_LINE_SIZE 32

//--------------PRIVATE DECLARATIONS---------------//

void node_dist(Spawn_data spawn_data, int **qty, int *used_nodes, int *total_spawns);
void spread_dist(Spawn_data spawn_data, int *used_nodes, int *procs);
void compact_dist(Spawn_data spawn_data, int *used_nodes, int *procs);

void generate_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void generate_multiple_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);

void set_mapping_host(int qty, char *info_type, char *host, size_t index, Spawn_data *spawn_data);
void fill_str_hosts(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str);
int write_str_node(char **hostlist_str, size_t len_og, size_t qty, char *node_name);
int write_hostfile_node(int file, int qty, char *node_name, char **line, size_t *len_og);
//--------------------------------SLURM USAGE-------------------------------------//
#if MAM_USE_SLURM
#include <slurm/slurm.h>
void generate_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void generate_multiple_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void fill_str_hosts_slurm(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str);

void generate_info_hostfile_slurm(char *nodelist, int *qty, size_t used_nodes, Spawn_data *spawn_data);
void fill_hostfile_slurm(char* file_name, size_t used_nodes, int *qty, hostlist_t *hostlist);
size_t fill_multiple_hostfile_slurm(char* file_name, int *qty, hostlist_t *hostlist, char **line, size_t *len_line);
#endif
//--------------------------------SLURM USAGE-------------------------------------//

//--------------PUBLIC FUNCTIONS---------------//

/*
 * Configura la creacion de un nuevo grupo de procesos, reservando la memoria
 * para una llamada a MPI_Comm_spawn, obteniendo una distribucion fisica
 * para los procesos y creando un fichero hostfile.
 *
 */
void processes_dist(Spawn_data *spawn_data) {
  int used_nodes=0;
  int *procs_array;

  // GET NEW DISTRIBUTION 
  node_dist(*spawn_data, &procs_array, &used_nodes, &spawn_data->total_spawns);
  spawn_data->sets = (Spawn_set *) malloc(spawn_data->total_spawns * sizeof(Spawn_set));
#if MAM_USE_SLURM
  switch(spawn_data->mapping_fill_method) {
    case MAM_PHY_TYPE_STRING:
      if(spawn_data->spawn_is_multiple || spawn_data->spawn_is_parallel) {
        generate_multiple_info_string_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      } else {
        generate_info_string_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      }
      break;
    case MAM_PHY_TYPE_HOSTFILE:
      generate_info_hostfile_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      break;
  }
#else
  if(spawn_data->spawn_is_multiple || spawn_data->spawn_is_parallel) {
    generate_multiple_info_string(mall->nodelist, procs_array, used_nodes, spawn_data);
  } else {
    generate_info_string(mall->nodelist, procs_array, used_nodes, spawn_data);
  }
#endif
  char *aux_cmd = get_spawn_cmd();
  for(int index = 0; index<spawn_data->total_spawns; index++) {
    spawn_data->sets[index].cmd = aux_cmd;
  }
  free(procs_array);
}

void set_hostfile_name(char **file_name, int *n, int jid, int index) {
  if(*file_name == NULL) {
    *file_name = (char *) malloc(MAM_HOSTFILE_SIZE * sizeof(char));
  }

  if(*n == 0) {
    jid = jid % 1000;
    snprintf(*file_name, MAM_HOSTFILE_SIZE , "%s%04d%s%03d%s", MAM_HOSTFILE_NAME1, jid, MAM_HOSTFILE_NAME2, index, MAM_HOSTFILE_NAME3);
  } else {
    snprintf((*file_name)+MAM_HOSTFILE_SIZE1, MAM_HOSTFILE_SIZE2 , "%03d%s", index, MAM_HOSTFILE_NAME3);
  }
  *n=1;
}

int read_hostfile_procs(char *file_name, int *qty) {
  char *line = NULL, *ptr;
  FILE *file = NULL;

  file = fopen(file_name, "r");
  if(file == NULL) {
    perror("Could not open hostfile to read");
    MPI_Abort(MPI_COMM_WORLD, -1);
  }

  *qty = 0;
  line = (char *) malloc(MAM_HOSTFILE_LINE_SIZE * sizeof(char));
  while (fgets(line, MAM_HOSTFILE_LINE_SIZE, file) != NULL) {
    size_t len = strlen(line);
    ptr = line + len - 1;
    // Search delimiter
    while (ptr != line && *ptr != ':') { ptr--; }
    if (*ptr == ':') { *qty  += atoi(ptr + 1); }
  }
  return 0;
}


//--------------PRIVATE FUNCTIONS---------------//
//-----------------DISTRIBUTION-----------------//
/*
 * Obtiene la distribucion fisica del grupo de procesos a crear, devolviendo
 * cuantos nodos se van a utilizar, la cantidad de procesos que alojara cada
 * nodo y cuantas creaciones de procesos seran necesarias.
 *
 * Se permiten dos tipos de distribuciones fisicas segun el valor de "spawn_dist":
 *
 *  COMM_PHY_NODES (1): Orientada a equilibrar el numero de procesos entre
 *                      todos los nodos disponibles.
 *  COMM_PHY_CPU   (2): Orientada a completar la capacidad de un nodo antes de
 *                      ocupar otro nodo.
 */
void node_dist(Spawn_data spawn_data, int **qty, int *used_nodes, int *total_spawns) {
  int i, *procs;
  procs = calloc(mall->num_nodes, sizeof(int)); // Numero de procesos por nodo

  /* GET NEW DISTRIBUTION  */
  switch(mall_conf->spawn_dist) {
    case MAM_PHY_DIST_SPREAD: // DIST NODES
      spread_dist(spawn_data, used_nodes, procs);
      break;
    case MAM_PHY_DIST_COMPACT: // DIST CPUs
      compact_dist(spawn_data, used_nodes, procs);
      break;
  }

  //Copy results to output vector qty
  *qty = calloc(*used_nodes, sizeof(int)); // Numero de procesos por nodo

//  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL) ) {
  if(spawn_data.spawn_is_multiple || spawn_data.spawn_is_parallel) {
    for(i=0; i< *used_nodes; i++) {
      (*qty)[i] = procs[i];
      if(procs[i]) (*total_spawns)++;
    }
  } else {
    *total_spawns = 1;
    for(i=0; i< *used_nodes; i++) {
      (*qty)[i] = procs[i];
    }
  }
  free(procs);
}

/*
 * Distribucion basada en equilibrar el numero de procesos en cada nodo
 * para que todos los nodos tengan el mismo numero. Devuelve el total de
 * nodos utilizados y el numero de procesos a crear en cada nodo.
 *
 * Asume que los procesos que ya existen estan en los nodos mas bajos
 * con el mismo tamBl. //FIXME No deberia asumir el tamBl.
 *
 * FIXME Tener en cuenta localizacion de procesos ya creados (already_created)
 */
void spread_dist(Spawn_data spawn_data, int *used_nodes, int *procs) {
  int i, tamBl, remainder;

  *used_nodes = mall->num_nodes;
  tamBl = spawn_data.target_qty / *used_nodes;
  i = spawn_data.already_created / tamBl;
  remainder = spawn_data.already_created % tamBl;
  if(remainder) {
    procs[i++] = tamBl - remainder;
  }
  for(; i<*used_nodes; i++) {
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
void compact_dist(Spawn_data spawn_data, int *used_nodes, int *procs) {
  int i, asigCores;
  int tamBl, remainder;

  tamBl = mall->num_cpus;
  asigCores = spawn_data.already_created;
  i = *used_nodes = spawn_data.already_created / tamBl;
  remainder = spawn_data.already_created % tamBl;

  //FIXME REFACTOR Que pasa si los nodos 1 y 2 tienen espacios libres
  //First nodes could already have existing procs
  //Start from the first with free spaces
  if (remainder && asigCores + (tamBl - remainder) < spawn_data.target_qty) {
    procs[i] = tamBl - remainder;
    asigCores += procs[i];
    i = (i+1) % mall->num_nodes;
    (*used_nodes)++;
  }

  //Assign tamBl to each node
  while(asigCores+tamBl <= spawn_data.target_qty) {
    asigCores += tamBl;
    procs[i] += tamBl;
    i = (i+1) % mall->num_nodes;
    (*used_nodes)++;
  }

  //Last node could have less procs than tamBl
  if(asigCores < spawn_data.target_qty) { 
    procs[i] += spawn_data.target_qty - asigCores;
    (*used_nodes)++;
  }
  if(*used_nodes > mall->num_nodes) *used_nodes = mall->num_nodes;  //FIXME Si ocurre esto no es un error?
}

//--------------PRIVATE FUNCTIONS---------------//
//-------------------INFO SET-------------------//

/*
 * Crea y devuelve un objeto MPI_Info con un par hosts/mapping
 * en el que se indica el mappeado a utilizar en los nuevos
 * procesos.
 *
 *
 */
void generate_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data){
  char *host_str;

  fill_str_hosts(nodelist, procs_array, nodes, &host_str);
  // SET MAPPING
  set_mapping_host(spawn_data->spawn_qty, "hosts", host_str, 0, spawn_data);
  free(host_str);
}

/*
 * Crea y devuelve un objeto MPI_Info con un par hosts/mapping
 * en el que se indica el mappeado a utilizar en los nuevos
 * procesos.
 *
 *
 */
void generate_multiple_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data){
  char *host, *aux, *token, *hostlist_str;
  size_t i=0,j=0,len=0;

  aux = (char *) malloc((strlen(nodelist)+1) * sizeof(char));
  strcpy(aux, nodelist);
  token = strtok(aux, ",");
  while (token != NULL && i < nodes) {
    host = strdup(token);
    if (procs_array[i] != 0) {
      write_str_node(&hostlist_str, len, procs_array[i], host);
      set_mapping_host(procs_array[i], "hosts", hostlist_str, j, spawn_data);
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

/*
 * Anyade en la siguiente entrada de spawns la
 * distribucion fisica a utilizar con un par 
 * host/mapping y el total de procesos.
 */
void set_mapping_host(int qty, char *info_type, char *host, size_t index, Spawn_data *spawn_data) {
  MPI_Info *info;

  spawn_data->sets[index].spawn_qty = qty;
  info = &(spawn_data->sets[index].mapping);
  MPI_Info_create(info);
  MPI_Info_set(*info, info_type, host);
}

/*
 * Crea y devuelve una cadena para ser utilizada por la llave "hosts"
 * al crear procesos e indicar donde tienen que ser creados.
 */
void fill_str_hosts(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str) {
  char *host, *aux, *token;
  size_t i=0,len=0;

  aux = (char *) malloc((strlen(nodelist)+1) * sizeof(char));
  strcpy(aux, nodelist);
  token = strtok(aux, ",");
  while (token != NULL && i < used_nodes) {
    host = strdup(token);
    if (qty[i] != 0) {
      len = write_str_node(hostlist_str, len, qty[i], host);
    }
    i++;
    free(host);
    token = strtok(NULL, ",");
  }
  free(aux);
}

/*
 * Añade en una cadena "qty" entradas de "node_name".
 * Realiza la reserva de memoria y la realoja si es necesario.
 */
int write_str_node(char **hostlist_str, size_t len_og, size_t qty, char *node_name) {
  int err;
  char *ocurrence;
  size_t i, len, len_node;

  len_node = strlen(node_name) + 1; // Str length + ','
  len = qty * len_node; // Number of times the node is used

  if(len_og == 0) { // Memoria no reservada
    *hostlist_str = (char *) malloc((len+1) * sizeof(char));
  } else { // Cadena ya tiene datos
    *hostlist_str = (char *) realloc(*hostlist_str, (len_og + len + 1) * sizeof(char));
  }
  if(hostlist_str == NULL) return -1; // No ha sido posible alojar la memoria

  ocurrence = (char *) malloc((len_node+1) * sizeof(char));
  if(ocurrence == NULL) return -2; // No ha sido posible alojar la memoria
  err = snprintf(ocurrence, len_node+1, ",%s", node_name);
  if(err < 0) return -3; // No ha sido posible escribir sobre la variable auxiliar

  i=0;
  if(len_og == 0) { // Si se inicializa, la primera es una copia
    i++;
    strcpy(*hostlist_str, node_name);
  }
  for(; i<qty; i++){ // Las siguientes se conctanenan
    strcat(*hostlist_str, ocurrence);
  }

  
  free(ocurrence);
  return len+len_og;
}

/*
 * Escribe en el fichero hostfile indicado por ptr una nueva linea.
 *
 * Esta linea indica el nombre de un nodo y la cantidad de procesos a
 * alojar en ese nodo.
 */
int write_hostfile_node(int file, int qty, char *node_name, char **line, size_t *len_og) {
  int err;
  size_t len, len_node, len_int;

  if(*line == NULL) {
    *len_og = MAM_HOSTFILE_LINE_SIZE;
    *line = (char *) malloc(*len_og * sizeof(char));
  }

  len_node = strlen(node_name);
  err = snprintf(NULL, 0, "%d", qty);
  if(err < 0) return -1;
  len_int = err;

  len = len_node + len_int + 3;
  if(*len_og < len) {
    *len_og = len+MAM_HOSTFILE_LINE_SIZE;
    *line = (char *) realloc(*line, *len_og * sizeof(char));
  }

  err = snprintf(*line, len, "%s:%d\n", node_name, qty);
  err = write(file, *line, len-1);
  if(err < 0) {
    perror("Error writing to the host file");
    close(file);
    exit(EXIT_FAILURE);
  }
  return 0;
}

//--------------------------------SLURM USAGE-------------------------------------//
#if MAM_USE_SLURM
/*
 * Crea y devuelve un objeto MPI_Info con un par hosts/mapping
 * en el que se indica el mappeado a utilizar en los nuevos
 * procesos.
 * Es necesario usar Slurm para usarlo.
 */
void generate_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data){
  char *hoststring;

  // CREATE AND SET STRING HOSTS
  fill_str_hosts_slurm(nodelist, procs_array, nodes, &hoststring);
  set_mapping_host(spawn_data->spawn_qty, "hosts", hoststring, 0, spawn_data);
  free(hoststring);
}

/*
 * Crea y devuelve un conjunto de objetos MPI_Info con 
 * un par host/mapping en el que se indica el mappeado 
 * a utilizar en los nuevos procesos dividido por nodos.
 * Es necesario Slurm para usarlo.
 */
void generate_multiple_info_string_slurm(char *nodelist, int *qty, size_t used_nodes, Spawn_data *spawn_data) {
  char *host, *hostlist_str;
  size_t i=0,j=0,len=0;
  hostlist_t hostlist;
  
  hostlist_str = NULL;
  hostlist = slurm_hostlist_create(nodelist);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < used_nodes) {
    if(qty[i] != 0) {
      write_str_node(&hostlist_str, len, qty[i], host);
      set_mapping_host(qty[i], "hosts", hostlist_str, j, spawn_data);
      free(hostlist_str); hostlist_str = NULL;
      j++;
    }
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);
  if(hostlist_str != NULL) { free(hostlist_str); }
}


/*
 * Crea y devuelve una cadena para ser utilizada por la llave "hosts"
 * al crear procesos e indicar donde tienen que ser creados.
 */
void fill_str_hosts_slurm(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str) {
  char *host;
  size_t i=0,len=0;
  hostlist_t hostlist;
  
  hostlist = slurm_hostlist_create(nodelist);
  while ( (host = slurm_hostlist_shift(hostlist)) && i < used_nodes) {
    if(qty[i] != 0) {
      len = write_str_node(hostlist_str, len, qty[i], host);
    }
    i++;
    free(host);
  }
  slurm_hostlist_destroy(hostlist);
}

void generate_info_hostfile_slurm(char *nodelist, int *qty, size_t used_nodes, Spawn_data *spawn_data){
  int index = 0, jid;
  size_t qty_index = 0, len_line = 0;
  char *hostfile_name, *line;
  hostlist_t hostlist;

  jid = mall_conf->slurm_jid%1000; //Modified for DMR

  line = NULL;
  hostlist = slurm_hostlist_create(nodelist);
  hostfile_name = (char *) malloc(MAM_HOSTFILE_SIZE * sizeof(char));
  snprintf(hostfile_name, MAM_HOSTFILE_SIZE , "%s%04d%s%03d%s", MAM_HOSTFILE_NAME1, jid, MAM_HOSTFILE_NAME2, index, MAM_HOSTFILE_NAME3);

  if(spawn_data->spawn_is_multiple || spawn_data->spawn_is_parallel) { // MULTIPLE
    for(; index<spawn_data->total_spawns; index++) {
      // This strat creates 1 hostfile per spawn
      qty_index = fill_multiple_hostfile_slurm(hostfile_name, qty+qty_index, &hostlist, &line, &len_line);
      set_mapping_host(qty[qty_index-1], "hostfile", hostfile_name, index, spawn_data); 
      snprintf(hostfile_name+MAM_HOSTFILE_SIZE1, MAM_HOSTFILE_SIZE2 , "%03d%s", index+1, MAM_HOSTFILE_NAME3);
    }
    free(line);

  } else { // NOT MULTIPLE
    fill_hostfile_slurm(hostfile_name, used_nodes, qty, &hostlist);
    set_mapping_host(spawn_data->spawn_qty, "hostfile", hostfile_name, index, spawn_data);
  }

  free(hostfile_name);
  slurm_hostlist_destroy(hostlist);
}

// Function to generate the configuration file
void fill_hostfile_slurm(char* file_name, size_t used_nodes, int *qty, hostlist_t *hostlist) {
  char *host, *line;
  size_t i=0, len_line=0;

  line = NULL;
  int file = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file < 0) {
    perror("Error opening the host file");
    exit(EXIT_FAILURE);
  }

  while ( (host = slurm_hostlist_shift(*hostlist)) && i < used_nodes) {
    if(qty[i] != 0) {
      write_hostfile_node(file, qty[i], host, &line, &len_line);
    }
    i++;
    free(host);
  }

  close(file);
  free(line);
}

size_t fill_multiple_hostfile_slurm(char* file_name, int *qty, hostlist_t *hostlist, char **line, size_t *len_line) {
  char *host;
  size_t i=0;

  int file = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (file < 0) {
    perror("Error opening the host file");
    exit(EXIT_FAILURE);
  }

  while( (host = slurm_hostlist_shift(*hostlist)) ) {
    if(qty[i] != 0) {
      write_hostfile_node(file, qty[i], host, line, len_line);
      i++;
      break;
    }
    i++;
    free(host); host = NULL;
  }

  if(host != NULL) free(host);
  close(file);
  return i;
}
#endif
//--------------------------------SLURM USAGE-------------------------------------//