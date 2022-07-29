#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <string.h>
#include "distribution_methods/block_distribution.h"
#include "CommDist.h"

void send_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_child, int idI,  int idE, struct Counts counts);
void recv_sync_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts);

void send_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req);
void recv_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req);

void send_async_point_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req);
void recv_async_point_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req);

void getIds_intercomm(struct Dist_data dist_data, int numP_other, int **idS);
/*
 * Reserva memoria para un vector de hasta "qty" elementos.
 * Los "qty" elementos se disitribuyen entre los "numP" procesos
 * que llaman a esta funcion.
 */
void malloc_comm_array(char **array, int qty, int myId, int numP) {
    struct Dist_data dist_data;

    get_block_dist(qty, myId, numP, &dist_data);
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
//========================SYNCHRONOUS FUNCTIONS===================================
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

    get_block_dist(qty, myId, numP, &dist_data); // Distribucion de este proceso en su grupo
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
    get_block_dist(qty, myId, numP, &dist_data);
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

    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_child, dist_data.qty, &counts);
    /*int i;
    // PREPARAR ENVIO DEL VECTOR
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }*/

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
	
    char aux;

    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_parents, dist_data.qty, &counts);
    // Ajustar los valores de recepcion
    /*
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }*/
    //print_counts(dist_data, counts.counts, counts.displs, numP_parents, "Hijos");

    /* COMUNICACION DE DATOS */
    MPI_Alltoallv(&aux, counts.zero_arr, counts.zero_arr, MPI_CHAR, array, counts.counts, counts.displs, MPI_CHAR, dist_data.intercomm);
}


//================================================================================
//================================================================================
//========================ASYNCHRONOUS FUNCTIONS==================================
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
    int i, rootBcast = MPI_PROC_NULL;
    int *idS = NULL;
    struct Counts counts;
    struct Dist_data dist_data;

    if(myId == root) rootBcast = MPI_ROOT;

    get_block_dist(qty, myId, numP, &dist_data); // Distribucion de este proceso en su grupo
    dist_data.intercomm = intercomm;

    // Create arrays which contains info about how many elements will be send to each created process
    mallocCounts(&counts, numP_child);

    getIds_intercomm(dist_data, numP_child, &idS); // Obtener rango de Id hijos a los que este proceso manda datos

    // MAL_USE_THREAD sigue el camino sincrono
    if(parents_wait == MAL_USE_NORMAL) {
      //*comm_req = (MPI_Request *) malloc(sizeof(MPI_Request));
      *comm_req[0] = MPI_REQUEST_NULL;
      send_async_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts, &(*comm_req[0])); 

    } else if (parents_wait == MAL_USE_IBARRIER){
      //*comm_req = (MPI_Request *) malloc(2 * sizeof(MPI_Request));
      *comm_req[0] = MPI_REQUEST_NULL;
      *comm_req[1] = MPI_REQUEST_NULL;
      send_async_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts, &((*comm_req)[1])); 
      MPI_Ibarrier(intercomm, &((*comm_req)[0]) );
    } else if (parents_wait == MAL_USE_POINT){
      //*comm_req = (MPI_Request *) malloc(numP_child * sizeof(MPI_Request));
      for(i=0; i<numP_child; i++){
        (*comm_req)[i] = MPI_REQUEST_NULL;
      }
      send_async_point_arrays(dist_data, array, rootBcast, numP_child, idS[0], idS[1], counts, *comm_req); 
    } else if (parents_wait == MAL_USE_THREAD) { //TODO 
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
    int wait_err, i;
    struct Counts counts;
    struct Dist_data dist_data;
    MPI_Request *comm_req, aux;

    // Obtener distribución para este hijo
    get_block_dist(qty, myId, numP, &dist_data);
    *array = malloc(dist_data.tamBl * sizeof(char));
    dist_data.intercomm = intercomm;

    /* PREPARAR DATOS DE RECEPCION SOBRE VECTOR*/
    mallocCounts(&counts, numP_parents);

    getIds_intercomm(dist_data, numP_parents, &idS); // Obtener el rango de Ids de padres del que este proceso recibira datos

    // MAL_USE_THREAD sigue el camino sincrono
    if(parents_wait == MAL_USE_POINT) {
      comm_req = (MPI_Request *) malloc(numP_parents * sizeof(MPI_Request));
      for(i=0; i<numP_parents; i++){
        comm_req[i] = MPI_REQUEST_NULL;
      }
      recv_async_point_arrays(dist_data, *array, root, numP_parents, idS[0], idS[1], counts, comm_req);
      wait_err = MPI_Waitall(numP_parents, comm_req, MPI_STATUSES_IGNORE);

    } else if (parents_wait == MAL_USE_NORMAL || parents_wait == MAL_USE_IBARRIER) {
      comm_req = (MPI_Request *) malloc(sizeof(MPI_Request));
      *comm_req = MPI_REQUEST_NULL;
      recv_async_arrays(dist_data, *array, root, numP_parents, idS[0], idS[1], counts, comm_req);
      wait_err = MPI_Wait(comm_req, MPI_STATUS_IGNORE);
    } else if (parents_wait == MAL_USE_THREAD) { //TODO
    }

    if(wait_err != MPI_SUCCESS) {
      MPI_Abort(MPI_COMM_WORLD, wait_err);
    }

    if(parents_wait == MAL_USE_IBARRIER) { //MAL USE IBARRIER END
      MPI_Ibarrier(intercomm, &aux);
      MPI_Wait(&aux, MPI_STATUS_IGNORE); //Es necesario comprobar que la comunicación ha terminado para desconectar los grupos de procesos
    }

    //printf("S%d Tam %d String: %s END\n", myId, dist_data.tamBl, *array);
    freeCounts(&counts);
    free(idS);
    free(comm_req);
}

/*
 * Envia a los hijos un vector que es redistribuido a los procesos
 * hijos. Antes de realizar la comunicacion, cada proceso padre calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 *
 * El envio se realiza a partir de una comunicación colectiva.
 */
void send_async_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req) {

    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_child, dist_data.qty, &counts);
    /*
    // PREPARAR ENVIO DEL VECTOR
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }
    */
    //print_counts(dist_data, counts.counts, counts.displs, numP_child, "Padres");

    /* COMUNICACION DE DATOS */
    MPI_Ialltoallv(array, counts.counts, counts.displs, MPI_CHAR, NULL, counts.zero_arr, counts.zero_arr, MPI_CHAR, dist_data.intercomm, comm_req);
}

/*
 * Envia a los hijos un vector que es redistribuido a los procesos
 * hijos. Antes de realizar la comunicacion, cada proceso padre calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 *
 * El envio se realiza a partir de varias comunicaciones punto a punto.
 */
void send_async_point_arrays(struct Dist_data dist_data, char *array, int rootBcast, int numP_child, int idI,  int idE, struct Counts counts, MPI_Request *comm_req) {
    int i;
    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_child, dist_data.qty, &counts);
    // PREPARAR ENVIO DEL VECTOR
    /*
    if(idI == 0) {
      set_counts(0, numP_child, dist_data, counts.counts);
      idI++;
      MPI_Isend(array, counts.counts[0], MPI_CHAR, 0, 99, dist_data.intercomm, &(comm_req[0]));
    }	
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_child, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
      MPI_Isend(array+counts.displs[i], counts.counts[i], MPI_CHAR, i, 99, dist_data.intercomm, &(comm_req[i]));
    }*/
    for(i=0; i<numP_child; i++) { //TODO Esta propuesta ya no usa el IdI y Ide
      if(counts.counts[0] != 0) {
        MPI_Isend(array+counts.displs[i], counts.counts[i], MPI_CHAR, i, 99, dist_data.intercomm, &(comm_req[i]));
      }
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_child, "Padres");
}

/*
 * Recibe de los padres un vector que es redistribuido a los procesos
 * de este grupo. Antes de realizar la comunicacion cada hijo calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 *
 * La recepcion se realiza a partir de una comunicacion colectiva.
 */
void recv_async_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req) {
    char *aux = malloc(1);

    // Ajustar los valores de recepcion
    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_parents, dist_data.qty, &counts);
    /*
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, counts.counts);
      idI++;
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
    }*/
    //print_counts(dist_data, counts.counts, counts.displs, numP_parents, "Hijos");

    /* COMUNICACION DE DATOS */
    MPI_Ialltoallv(aux, counts.zero_arr, counts.zero_arr, MPI_CHAR, array, counts.counts, counts.displs, MPI_CHAR, dist_data.intercomm, comm_req);
    free(aux);
}

/*
 * Recibe de los padres un vector que es redistribuido a los procesos
 * de este grupo. Antes de realizar la comunicacion cada hijo calcula sobre que procesos
 * del otro grupo se transmiten elementos.
 *
 * La recepcion se realiza a partir de varias comunicaciones punto a punto.
 */
void recv_async_point_arrays(struct Dist_data dist_data, char *array, int root, int numP_parents, int idI, int idE, struct Counts counts, MPI_Request *comm_req) {
    int i;

    // Ajustar los valores de recepcion
    prepare_comm_alltoall(dist_data.myId, dist_data.numP, numP_parents, dist_data.qty, &counts);
    /*
    if(idI == 0) {
      set_counts(0, numP_parents, dist_data, counts.counts);
      idI++;
      MPI_Irecv(array, counts.counts[0], MPI_CHAR, 0, 99, dist_data.intercomm, &(comm_req[0])); //FIXME BUffer recv
    }
    for(i=idI; i<idE; i++) {
      set_counts(i, numP_parents, dist_data, counts.counts);
      counts.displs[i] = counts.displs[i-1] + counts.counts[i-1];
      MPI_Irecv(array+counts.displs[i], counts.counts[i], MPI_CHAR, i, 99, dist_data.intercomm, &(comm_req[i])); //FIXME BUffer recv
    }*/
    for(i=0; i<numP_parents; i++) { //TODO Esta propuesta ya no usa el IdI y Ide
      if(counts.counts[0] != 0) {
        MPI_Irecv(array+counts.displs[i], counts.counts[i], MPI_CHAR, i, 99, dist_data.intercomm, &(comm_req[i])); //FIXME BUffer recv
      }
    }
    //print_counts(dist_data, counts.counts, counts.displs, numP_parents, "Hijos");
}

/*
 * ========================================================================================
 * ========================================================================================
 * ================================DISTRIBUTION FUNCTIONS==================================
 * ========================================================================================
 * ========================================================================================
*/

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
