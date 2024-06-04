#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <mpi.h>
#include "ProcessDist.h"
#include "../MAM_Constants.h"
#include "../MAM_DataStructures.h"

//--------------PRIVATE DECLARATIONS---------------//

void node_dist(Spawn_data spawn_data, int **qty, int *used_nodes, int *total_spawns);
void spread_dist(Spawn_data spawn_data, int *used_nodes, int *procs);
void compact_dist(Spawn_data spawn_data, int *used_nodes, int *procs);

void generate_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void generate_multiple_info_string(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);

void set_mapping_host(int qty, char *host, size_t index, Spawn_data *spawn_data);
void fill_str_hosts(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str);
int write_str_node(char **hostlist_str, size_t len_og, size_t qty, char *node_name);
//--------------------------------SLURM USAGE-------------------------------------//
#if USE_MAL_SLURM
#include <slurm/slurm.h>
void generate_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void generate_multiple_info_string_slurm(char *nodelist, int *procs_array, size_t nodes, Spawn_data *spawn_data);
void fill_str_hosts_slurm(char *nodelist, int *qty, size_t used_nodes, char **hostlist_str);
//@deprecated functions
void generate_info_hostfile_slurm(char *nodelist, int *procs_array, int nodes, Spawn_data *spawn_data); 
void fill_hostfile_slurm(char *nodelist, int ptr, int *qty, int used_nodes);
#endif
//--------------------------------SLURM USAGE-------------------------------------//

//@deprecated functions
int create_hostfile(char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);

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
#if USE_MAL_SLURM
  switch(spawn_data->mapping_fill_method) {
    case MALL_DIST_STRING:
//      if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL) ) {
      if(spawn_data->spawn_is_multiple) {
        generate_multiple_info_string_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      } else {
        generate_info_string_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      }
      break;
    case MALL_DIST_HOSTFILE: // FIXME Does not consider multiple spawn strat
      generate_info_hostfile_slurm(mall->nodelist, procs_array, used_nodes, spawn_data);
      break;
  }
#else
//  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL) ) {
  if(spawn_data->spawn_is_multiple) {
    generate_multiple_info_string(mall->nodelist, procs_array, used_nodes, spawn_data);
  } else {
    generate_info_string(mall->nodelist, procs_array, used_nodes, spawn_data);
  }
#endif
  free(procs_array);
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
    case MALL_DIST_SPREAD: // DIST NODES @deprecated
      spread_dist(spawn_data, used_nodes, procs);
      break;
    case MALL_DIST_COMPACT: // DIST CPUs
      compact_dist(spawn_data, used_nodes, procs);
      break;
  }

  //Copy results to output vector qty
  *qty = calloc(*used_nodes, sizeof(int)); // Numero de procesos por nodo

//  if(MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_MULTIPLE, NULL) ) {
  if(spawn_data.spawn_is_multiple) {
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
  set_mapping_host(spawn_data->spawn_qty, host_str, 0, spawn_data);
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
      set_mapping_host(procs_array[i], hostlist_str, j, spawn_data);
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
void set_mapping_host(int qty, char *host, size_t index, Spawn_data *spawn_data) {
  MPI_Info *info;

  spawn_data->sets[index].spawn_qty = qty;
  info = &(spawn_data->sets[index].mapping);
  MPI_Info_create(info);
  MPI_Info_set(*info, "hosts", host);
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

//--------------------------------SLURM USAGE-------------------------------------//
#if USE_MAL_SLURM
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
  set_mapping_host(spawn_data->spawn_qty, hoststring, 0, spawn_data);
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
      set_mapping_host(qty[i], hostlist_str, j, spawn_data);
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

//====================================================
//====================================================
//============DEPRECATED FUNCTIONS====================
//====================================================
//====================================================

/* FIXME Por revisar
 * @deprecated
 * Genera un fichero hostfile y lo anyade a un objeto
 * MPI_Info para ser utilizado.
 */
void generate_info_hostfile_slurm(char *nodelist, int *procs_array, int nodes, Spawn_data *spawn_data){
    char *hostfile;
    int ptr;
    MPI_Info *info;

    spawn_data->sets[0].spawn_qty = spawn_data->spawn_qty;
    info = &(spawn_data->sets[0].mapping);

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
