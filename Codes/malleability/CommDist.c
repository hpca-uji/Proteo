#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "CommDist.h"

struct Dist_data {
  int ini; //Primer elemento a enviar
  int fin; //Ultimo elemento a enviar

  int tamBl; // Total de elementos
  int qty; // Total number of rows of the full disperse matrix

  int myId;
  int numP;
  MPI_Comm intercomm;
};

struct Counts {
  int *counts;
  int *displs;
  int *zero_arr;
};

void send_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_child, int idI,  int idE, struct Counts counts);
void recv_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts);

void send_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req);
void recv_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req);

// DIST FUNCTIONS
void get_dist(int qty, int id, int numP, struct Dist_data *dist_data);
void set_counts(int id, int numP, struct Dist_data data_dist, int *sendcounts);
void getIds_intercomm(struct Dist_data dist_data, int numP_other, int **idS);
void mallocCounts(struct Counts *counts, int numP);
void freeCounts(struct Counts *counts);

void print_counts(struct Dist_data data_dist, int *xcounts, int *xdispls, int size, const char* name);

/*
 * Reserva memoria para un vector de hasta "qty" elementos.
 * Los "qty" elementos se disitribuyen entre los "numP" procesos
 * que llaman a esta funcion.
 */
void malloc_comm_array(char **array, int qty, int myId, int numP) {
    struct Dist_data dist_data;

    get_dist(qty, myId, numP, &dist_data);
    if( (*array = malloc(dist_data.tamBl * sizeof(char))) == NULL) {
      printf("Memory Error (Malloc Arrays(%d))\n", dist_data.tamBl); 
      exit(1); 
    }

/*
        int i;
	for(i=0; i<dist_data.tamBl; i++) {
	  (*array)[i] = '!' + i + dist_data.ini;
	}
	
        printf("P%d Tam %d String: %s\n", myId, dist_data.tamBl, *array);
*/
}

//================================================================================
//================================================================================
//========================SINCHRONOUS FUNCTIONS===================================
//================================================================================
//================================================================================

/*
 * Realiza un envio síncrono del vector array desde este grupo de procesos al grupo
 * enlazado por el intercomunicador intercomm.
 *
 * El vector array no se modifica en esta funcion.
 */
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

    send_sync_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts);

    freeCounts(&counts);
    free(idS);

    return 1;
}


/*
 * Realiza una recepcion síncrona del vector array a este grupo de procesos desde el grupo
 * enlazado por el intercomunicador intercomm.
 *
 * El vector array se reserva dentro de la funcion y se devuelve en el mismo argumento.
 * Tiene que ser liberado posteriormente por el usuario.
 */
void recv_sync(char **array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_parents) {
    int *idS = NULL;
    struct Counts counts;
    struct Dist_data dist_data;

    // Obtener distribución para este hijo
    get_dist(qty, myId, numP, &dist_data);
    *array = malloc(dist_data.tamBl * sizeof(char));
    //(*array)[dist_data.tamBl] = '\0';
    dist_data.intercomm = intercomm;

    /* PREPARAR DATOS DE RECEPCION SOBRE VECTOR*/
    mallocCounts(&counts, numP_parents);

    getIds_intercomm(dist_data, numP_parents, &idS); // Obtener el rango de Ids de padres del que este proceso recibira datos

    recv_sync_arrays(dist_data, *array, root, numP_parents, idS[0], idS[1], counts);
    //printf("S%d Tam %d String: %s END\n", myId, dist_data.tamBl, *array);

    freeCounts(&counts);
    free(idS);
}

/*
 * Envia a los hijos un vector que es redistribuido a los procesos
 * hijos. Antes de realizar la comunicacion, cada proceso padre calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 */
void send_sync_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE, struct Counts counts) {

    int i;

    // PREPARAR ENVIO DEL VECTOR
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_child, "Padres");

    /* COMUNICACION DE DATOS */
    MPI_Alltoallv(array, counts.counts, counts.displs, MPI_CHAR, NULL, counts.zero_arr, counts.zero_arr, MPI_CHAR, dist_data.intercomm);
    
}

/*
 * Recibe de los padres un vector que es redistribuido a los procesos
 * de este grupo. Antes de realizar la comunicacion cada hijo calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 */
void recv_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts) {
	
    int i;
    char *aux = malloc(1);

    // Ajustar los valores de recepcion
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_parents, "Hijos");

    /* COMUNICACION DE DATOS */
    MPI_Alltoallv(aux, counts.zero_arr, counts.zero_arr, MPI_CHAR, array, counts.counts, counts.displs, MPI_CHAR, dist_data.intercomm);
    free(aux);
}


//================================================================================
//================================================================================
//========================ASINCHRONOUS FUNCTIONS==================================
//================================================================================
//================================================================================

/*
 * Realiza un envio asincrono del vector array desde este grupo de procesos al grupo
 * enlazado por el intercomunicador intercomm.
 *
 * El objeto MPI_Request se devuelve con el manejador para comprobar si la comunicacion
 * ha terminado.
 *
 * El vector array no se modifica en esta funcion.
 */
int send_async(char *array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_child, MPI_Request **comm_req, int parents_wait) {
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

    if(parents_wait == MAL_USE_NORMAL) {
      *comm_req = (MPI_Request *) malloc(sizeof(MPI_Request));
      *comm_req[0] = MPI_REQUEST_NULL;
      send_async_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts, &(*comm_req[0])); 

    } else {
      *comm_req = (MPI_Request *) malloc(2 * sizeof(MPI_Request));
      (*comm_req)[0] = MPI_REQUEST_NULL;
      (*comm_req)[1] = MPI_REQUEST_NULL;
      send_async_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts, &((*comm_req)[1])); 
      MPI_Ibarrier(intercomm, &((*comm_req)[0]) );
    }

    freeCounts(&counts);
    free(idS);

    return 1;
}

/*
 * Realiza una recepcion asincrona del vector array a este grupo de procesos desde el grupo
 * enlazado por el intercomunicador intercomm.
 *
 * El vector array se reserva dentro de la funcion y se devuelve en el mismo argumento.
 * Tiene que ser liberado posteriormente por el usuario.
 *
 * El argumento "parents_wait" sirve para indicar si se usará la versión en la los padres 
 * espera a que terminen de enviar, o en la que esperan a que los hijos acaben de recibir.
 */
void recv_async(char **array, int qty, int myId, int numP, int root, MPI_Comm intercomm, int numP_parents, int parents_wait) {
    int *idS = NULL;
    int wait_err;
    struct Counts counts;
    struct Dist_data dist_data;
    MPI_Request comm_req, aux;

    // Obtener distribución para este hijo
    get_dist(qty, myId, numP, &dist_data);
    *array = malloc(dist_data.tamBl * sizeof(char));
    //(*array)[dist_data.tamBl] = '\0';
    dist_data.intercomm = intercomm;

    /* PREPARAR DATOS DE RECEPCION SOBRE VECTOR*/
    mallocCounts(&counts, numP_parents);

    getIds_intercomm(dist_data, numP_parents, &idS); // Obtener el rango de Ids de padres del que este proceso recibira datos

    recv_async_arrays(dist_data, *array, root, numP_parents, idS[0], idS[1], counts, &comm_req);

    wait_err = MPI_Wait(&comm_req, MPI_STATUS_IGNORE);
    if(wait_err != MPI_SUCCESS) {
      MPI_Abort(MPI_COMM_WORLD, wait_err);
    }

    if(parents_wait == MAL_USE_IBARRIER) {
      MPI_Ibarrier(intercomm, &aux);
      MPI_Wait(&aux, MPI_STATUS_IGNORE); //Es necesario comprobar que la comunicación ha terminado para desconectar los grupos de procesos
    }

    //printf("S%d Tam %d String: %s END\n", myId, dist_data.tamBl, *array);
    freeCounts(&counts);
    free(idS);
}

/*
 * Envia a los hijos un vector que es redistribuido a los procesos
 * hijos. Antes de realizar la comunicacion, cada proceso padre calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 */
void send_async_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req) {
    int i;

    // PREPARAR ENVIO DEL VECTOR
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_child, "Padres");

    /* COMUNICACION DE DATOS */
    MPI_Ialltoallv(array, counts.counts, counts.displs, MPI_CHAR, NULL, counts.zero_arr, counts.zero_arr, MPI_CHAR, dist_data.intercomm, comm_req);
}

/*
 * Recibe de los padres un vector que es redistribuido a los procesos
 * de este grupo. Antes de realizar la comunicacion cada hijo calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 */
void recv_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req) {
    int i;
    char *aux = malloc(1);

    // Ajustar los valores de recepcion
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_parents, "Hijos");

    /* COMUNICACION DE DATOS */
    MPI_Ialltoallv(aux, counts.zero_arr, counts.zero_arr, MPI_CHAR, array, counts.counts, counts.displs, MPI_CHAR, dist_data.intercomm, comm_req);
    free(aux);
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

  dist_data->myId = id;
  dist_data->numP = numP;
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
 * Obtiene para el Id de un proceso dado, cuantos elementos
 * enviara o recibira desde el proceso indicado en Dist_data.
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
    // Indica el punto de corte del grupo de procesos externo que 
    // divide entre los procesos que tienen 
    // un tamaño tamOther + 1 y un tamaño tamOther
    int middle = (tamOther + 1) * remOther;

    // Calcular idI teniendo en cuenta si se comunica con un
    // proceso con tamano tamOther o tamOther+1
    if(middle > dist_data.ini) { // First subgroup (tamOther+1)
      idI = dist_data.ini / (tamOther + 1);
    } else { // Second subgroup (tamOther)
      idI = ((dist_data.ini - middle) / tamOther) + remOther;
    }

    // Calcular idR teniendo en cuenta si se comunica con un
    // proceso con tamano tamOther o tamOther+1
    if(middle >= dist_data.fin) { // First subgroup (tamOther +1)
      idE = dist_data.fin / (tamOther + 1);
      idE = (dist_data.fin % (tamOther + 1) > 0 && idE+1 <= numP_other) ? idE+1 : idE;
    } else { // Second subgroup (tamOther)
      idE = ((dist_data.fin - middle) / tamOther) + remOther;
      idE = ((dist_data.fin - middle) % tamOther > 0 && idE+1 <= numP_other) ? idE+1 : idE;
    }

    *idS = malloc(2 * sizeof(int));
    (*idS)[0] = idI;
    (*idS)[1] = idE;
}

/*
 * Reserva memoria para los vectores de counts/displs de la funcion
 * MPI_Alltoallv. Todos los vectores tienen un tamaño de numP, que es la
 * cantidad de procesos en el otro grupo de procesos.
 *
 * El vector counts indica cuantos elementos se comunican desde este proceso
 * al proceso "i" del otro grupo.
 *
 * El vector displs indica los desplazamientos necesarios para cada comunicacion
 * con el proceso "i" del otro grupo.
 *
 * El vector zero_arr se utiliza cuando se quiere indicar un vector incializado
 * a 0 en todos sus elementos. Sirve para indicar que no hay comunicacion.
 */
void mallocCounts(struct Counts *counts, int numP) {
    counts->counts = calloc(numP, sizeof(int)); 
    if(counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->displs = calloc(numP, sizeof(int));
    if(counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->zero_arr = calloc(numP, sizeof(int));
    if(counts->zero_arr == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}
}

/*
 * Libera la memoria interna de una estructura Counts.
 *
 * No libera la memoria de la estructura counts si se ha alojado
 * de forma dinamica.
 */
void freeCounts(struct Counts *counts) {
    free(counts->counts);
    free(counts->displs);
    free(counts->zero_arr);
}


void print_counts(struct Dist_data data_dist, int *xcounts, int *xdispls, int size, const char* name) {
  int i;

  for(i=0; i < size; i++) {
    if(xcounts[i] != 0) {
      printf("P%d of %d | %scounts[%d]=%d disp=%d\n", data_dist.myId, data_dist.numP, name, i, xcounts[i], xdispls[i]);
    }
  }
}
