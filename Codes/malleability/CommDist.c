#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>


struct Dist_data {
  int ini; //Primer elemento a enviar
  int fin; //Ultimo elemento a enviar

  int tamBl; // Total de elementos
  int qty; // Total number of rows of the full disperse matrix
  MPI_Comm intercomm;
};

struct Counts {
  int *counts;
  int *displs;
  int *zero_arr;
};


void send_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_child, int idI,  int idE,
                 int *sendcounts, int *recvcounts, int *sdispls, int *rdispls);
void recv_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE,
                 int *sendcounts, int *recvcounts,int *sdispls, int *rdispls);

// DIST FUNCTIONS
void get_dist(int qty, int id, int numP, struct Dist_data *dist_data);
void set_counts(int id, int numP, struct Dist_data data_dist, int *sendcounts);
void getIds_intercomm(struct Dist_data dist_data, int numP_other, int **idS);
void mallocCounts(struct Counts *counts, int numP);
void freeCounts(struct Counts *counts);

int send_sync(char *array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_child) {
    int rootBcast = MPI_PROC_NULL;
    int *idS = NULL;
    struct Counts counts;
    struct Dist_data dist_data;

    if(myId == root) rootBcast = MPI_ROOT;

    get_dist(qty, myId, numP, &dist_data); // Distribucion de este proceso en su grupo
    dist_data.intercomm = intercomm;

    // Create arrays which contains info about how many elements will be send to each created process
    mallocCounts(&counts, numP_child);

    getIds_intercomm(dist_data, numP_child, &idS); // Obtener rango de Id hijos a los que este proceso manda datos

	printf("-1!! -- Vector de padres realizan COUNTS\n");
	fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);
    send_sync_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts.counts, counts.zero_arr, counts.displs, counts.zero_arr);

    freeCounts(&counts);
    free(idS);

    return 1;
}


void recv_sync(char **array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_parents) {
    int *idS = NULL;
    struct Counts counts;
    struct Dist_data dist_data;

	printf("Vector de hijos mandan datos\n");
	fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);

    // Obtener distribución para este hijo
    get_dist(qty, myId, numP, &dist_data);
    //*array = malloc(dist_data.tamBl * sizeof(char));
    *array = malloc(qty * sizeof(char));
    dist_data.intercomm = intercomm;

    /* PREPARAR DATOS DE RECEPCION SOBRE VECTOR*/
    mallocCounts(&counts, numP_parents);

    getIds_intercomm(dist_data, numP_parents, &idS); // Obtener el rango de Ids de padres del que este proceso recibira datos

    recv_sync_arrays(dist_data, *array, root, idS[0], idS[1], numP_parents, counts.zero_arr, counts.counts, counts.zero_arr, counts.displs);

    freeCounts(&counts);
    free(idS);
}


void malloc_comm_array(char **array, int qty, int myId, int numP) {
    struct Dist_data dist_data;

    get_dist(qty, myId, numP, &dist_data);
    //*array = malloc(dist_data.tamBl * sizeof(char));
    *array = malloc(qty * sizeof(char));
}


/*
 * Send to children Compute_data arrays which change in each iteration
 */
void send_sync_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE,
                 int *sendcounts, int *recvcounts, int *sdispls, int *rdispls) {
    int i;

    // PREPARAR ENVIO DEL VECTOR
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, sendcounts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, sendcounts);
      sdispls[i] = sdispls[i-1] + sendcounts[i-1];
    }

    /* COMUNICACION DE DATOS */
//    MPI_Alltoallv(array, sendcounts, sdispls, MPI_CHAR, NULL, recvcounts, rdispls, MPI_CHAR, dist_data.intercomm);
}

void recv_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE,
                 int *sendcounts, int *recvcounts,int *sdispls, int *rdispls) {
    int i;
    char *aux;

	printf("A -- Vector de hijos realizan COUNTS\n");
	fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);
    // Ajustar los valores de recepcion
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, recvcounts);
      idI++;
    }
	printf("B -- Vector de hijos realizan COUNTS\n");
	fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, recvcounts);
      rdispls[i] = rdispls[i-1] + recvcounts[i-1];
    }
	printf("C -- Vector de hijos realizan COUNTS\n");
	fflush(stdout);
	MPI_Barrier(MPI_COMM_WORLD);
 //   print_counts(*dist_data, recvcounts, rdispls, numP_parents, "Recv");

    /* COMUNICACION DE DATOS */
//    MPI_Alltoallv(aux, sendcounts, sdispls, MPI_CHAR, array, recvcounts, rdispls, MPI_CHAR, dist_data->intercomm);
}

/*
 * ========================================================================================
 * ========================================================================================
 * ================================DISTRIBUTION FUNCTIONS==================================
 * ========================================================================================
 * ========================================================================================
*/

/* 
 * Obatains for "Id" and "numP", how many
 * rows and elements per row will have process "Id"
 * and fills the results in a Dist_data struct
 */
void get_dist(int qty, int id, int numP, struct Dist_data *dist_data) {
  int rem;

  dist_data->qty = qty;
  dist_data->tamBl = qty / numP;
  rem = qty % numP;

  if(id < rem) { // First subgroup
    dist_data->ini = id * dist_data->tamBl + id;
    dist_data->fin = (id+1) * dist_data->tamBl + (id+1);
  } else { // Second subgroup
    dist_data->ini = id * dist_data->tamBl + rem;
    dist_data->fin = (id+1) * dist_data->tamBl + rem;
  }
  
  if(dist_data->fin > qty) {
    dist_data->fin = qty;
  }
  if(dist_data->ini > dist_data->fin) {
    dist_data->ini = dist_data->fin;
  }

  dist_data->tamBl = dist_data->fin - dist_data->ini;
}


/*
 * Obtains for a given process Id, how many elements will
 * send or recieve from the process indicated in Dist_data
 */
void set_counts(int id, int numP, struct Dist_data data_dist, int *sendcounts) {
  struct Dist_data other;
  int biggest_ini, smallest_end;

  get_dist(data_dist.qty, id, numP, &other);

  // Si el rango de valores no coincide, se pasa al siguiente proceso
  if(data_dist.ini >= other.fin || data_dist.fin <= other.ini) {
    return;
  }

  // Obtiene el proceso con mayor ini entre los dos procesos
  if(data_dist.ini > other.ini) { 
    biggest_ini = data_dist.ini;
  } else {
    biggest_ini = other.ini;
  }

  // Obtiene el proceso con menor fin entre los dos procesos
  if(data_dist.fin < other.fin) {
    smallest_end = data_dist.fin;
  } else {
    smallest_end = other.fin;
  }
  sendcounts[id] = smallest_end - biggest_ini; // Numero de elementos a enviar/recibir del proceso Id
}


/*
 * Obtiene para un proceso de un grupo a que rango procesos de 
 * otro grupo tiene que enviar o recibir datos.
 *
 * Devuelve el primer identificador y el último (Excluido) con el que
 * comunicarse.
 */
void getIds_intercomm(struct Dist_data dist_data, int numP_other, int **idS) {
    int idI, idE;
    int tamOther = dist_data.qty / numP_other;
    int remOther = dist_data.qty % numP_other;
    int middle = (tamOther + 1) * remOther;

    if(middle > dist_data.ini) { // First subgroup
      idI = dist_data.ini / (tamOther + 1);
    } else { // Second subgroup
      idI = ((dist_data.ini - middle) / tamOther) + remOther;
    }

    if(middle >= dist_data.fin) { // First subgroup
      idE = dist_data.fin / (tamOther + 1);
      idE = (dist_data.fin % (tamOther + 1) > 0 && idE+1 <= numP_other) ? idE+1 : idE;
    } else { // Second subgroup
      idE = ((dist_data.fin - middle) / tamOther) + remOther;
      idE = ((dist_data.fin - middle) % tamOther > 0 && idE+1 <= numP_other) ? idE+1 : idE;
    }

    //free(*idS);
    idS = malloc(2 * sizeof(int));
    (*idS)[0] = idI;
    (*idS)[1] = idE;
}


void mallocCounts(struct Counts *counts, int numP) {
    counts->counts = calloc(numP, sizeof(int)); 
    if(counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->displs = calloc(numP, sizeof(int));
    if(counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->zero_arr = calloc(numP, sizeof(int));
    if(counts->zero_arr == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}
}

void freeCounts(struct Counts *counts) {
    free(counts->counts);
    free(counts->displs);
    free(counts->zero_arr);
}
