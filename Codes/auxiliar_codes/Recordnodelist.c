#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <slurm/slurm.h>

//--------------PRIVATE DECLARATIONS---------------//
void node_dist(slurm_job_info_t job_record, int type, int total_procs, int **qty, int *used_nodes);

int create_hostfile(char *jobId, char **file_name);
int write_hostfile_node(int ptr, int qty, char *node_name);
void fill_hostfile(slurm_job_info_t job_record, int ptr, int *qty, int used_nodes);


int main(int argc, char *argv[]) {
    int jobId, ptr, numP, dist;
    char *tmp;
    job_info_msg_t *j_info;
    slurm_job_info_t last_record;

    int used_nodes=0;
    int *procs_array;
    char *hostfile_name;

    if(argc < 3) {
      printf("Uso ./a.out numP physical_dist");
      exit(-1);
    }

    numP = atoi(argv[1]);
    dist = atoi(argv[2]);

    // Get Slurm job info
    tmp = getenv("SLURM_JOB_ID");
    jobId = atoi(tmp);
    slurm_load_job(&j_info, jobId, 1);
    last_record = j_info->job_array[j_info->record_count - 1];

    // GET NEW DISTRIBUTION 
    node_dist(last_record, dist, numP, &procs_array, &used_nodes);

    // CREATE/UPDATE HOSTFILE
    ptr = create_hostfile(tmp, &hostfile_name);
    free(hostfile_name);

    // SET NEW DISTRIBUTION 
    fill_hostfile(last_record, ptr, procs_array, used_nodes);
    close(ptr);

    // Free JOB INFO
    slurm_free_job_info_msg(j_info); 

  return 0;
}

/*
 * Obtiene la distribucion fisica del grupo de procesos a crear, devolviendo
 * cuantos nodos se van a utilizar y la cantidad de procesos que alojara cada
 * nodo.
 *
 * Se permiten dos tipos de distribuciones fisicas segun el valor de "type":
 *
 *  (1): Orientada a equilibrar el numero de procesos entre
 *                      todos los nodos disponibles.
 *  (2): Orientada a completar la capacidad de un nodo antes de
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
    if(*used_nodes > job_record.num_nodes) *used_nodes = job_record.num_nodes; //FIXME Si ocurre esto no es un error?
  }

  *used_nodes=job_record.num_nodes;
  // Antes se ponia aqui todos los nodos sin cpus a 1
  *qty = procs;
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
    if(qty[i] != 0)
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
