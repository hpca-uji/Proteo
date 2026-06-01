#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "block_distribution.h"

void set_interblock_counts(int id, int numP, struct Dist_data data_dist, int offset_ids, MPI_Count *sendcounts);
void get_util_ids(struct Dist_data dist_data, int numP_other, int **idS);

/*
 * Prepares a communication from "numP" processes to "numP_other" processes
 * of "n" elements an returns an struct of counts with 3 arrays to perform the
 * communications.
 *
 * The struct should be freed with freeCounts
 */
void prepare_comm_alltoall(int myId, int numP, int numP_other, size_t n, int offset_ids, struct Counts *counts) {
  int i, *idS, first_id = 0;
  struct Dist_data dist_data, dist_target;
 
  if(counts == NULL) { 
    fprintf(stderr, "Counts is NULL for rank %d/%d ", myId, numP);
    MPI_Abort(MPI_COMM_WORLD, -3);
  } 

  get_block_dist(n, myId, numP, &dist_data);
  get_util_ids(dist_data, numP_other, &idS);

  counts->idI = idS[0] + offset_ids;
  counts->idE = idS[1] + offset_ids;
  get_block_dist(n, idS[0], numP_other, &dist_target); // RMA Specific operation -- uses idS[0], not idI
  counts->first_target_displs = dist_data.ini - dist_target.ini; // RMA Specific operation

  if(idS[0] == 0) { // Uses idS[0], not idI
    set_interblock_counts(counts->idI, numP_other, dist_data, offset_ids, counts->counts);
    first_id++;
  }
  for(i=counts->idI + first_id; i<counts->idE; i++) {
    set_interblock_counts(i, numP_other, dist_data, offset_ids, counts->counts);
    counts->displs[i] = counts->displs[i-1] + counts->counts[i-1];
  }
  free(idS);

  for(i=0; i<numP_other; i++) {
    if(counts->counts[i] < 0) {
      fprintf(stderr, "Counts value [i=%d/%d] is negative for rank %d/%d ", i, numP_other, myId, numP);
      MPI_Abort(MPI_COMM_WORLD, -3);
    }
    if(counts->displs[i] < 0) {
      fprintf(stderr, "Displs value [i=%d/%d] is negative for rank %d/%d ", i, numP_other, myId, numP);
      MPI_Abort(MPI_COMM_WORLD, -3);
    }
  }
}

/*
 * Prepares a communication of "numP" processes of "n" elements an 
 * returns an struct of counts with 3 arrays to perform the
 * communications.
 *
 * The struct should be freed with freeCounts
 */
void prepare_comm_allgatherv(int numP, int n, struct Counts *counts) {
  int i;
  struct Dist_data dist_data;

  mallocCounts(counts, numP);
  get_block_dist(n, 0, numP, &dist_data);

  counts->counts[0] = dist_data.tamBl;
  for(i=1; i<numP; i++){
    get_block_dist(n, i, numP, &dist_data);
    counts->counts[i] = dist_data.tamBl;
    counts->displs[i] = counts->displs[i-1] + counts->counts[i-1];
  }
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
 * elements per row will have process "Id"
 * and fills the results in a Dist_data struct
 */
void get_block_dist(size_t qty, int id, int numP, struct Dist_data *dist_data) {
  size_t rem;

  dist_data->myId = id;
  dist_data->numP = numP;
  dist_data->qty = qty;
  dist_data->tamBl = qty / numP;
  rem = qty % numP;

  if(((size_t) id) < rem) { // First subgroup
    dist_data->ini = id * dist_data->tamBl + id;
    dist_data->fin = (id+1) * dist_data->tamBl + (id+1);
  } else { // Second subgroup
    dist_data->ini = id * dist_data->tamBl + rem;
    dist_data->fin = (id+1) * dist_data->tamBl + rem;
  }
  
  if(dist_data->fin > qty) { dist_data->fin = qty; }
  if(dist_data->ini > dist_data->fin) { dist_data->ini = dist_data->fin; }

  dist_data->tamBl = dist_data->fin - dist_data->ini;
}


/*
 * Obtiene para el Id de un proceso dado, cuantos elementos
 * enviara o recibira desde el proceso indicado en Dist_data.
 */
void set_interblock_counts(int id, int numP, struct Dist_data data_dist, int offset_ids, MPI_Count *sendcounts) {
  struct Dist_data other;
  size_t biggest_ini, smallest_end;

  get_block_dist(data_dist.qty, id - offset_ids, numP, &other);

  // Si el rango de valores no coincide, se pasa al siguiente proceso
  if(data_dist.ini >= other.fin || data_dist.fin <= other.ini) {
    return;
  }

  // Obtiene el proceso con mayor ini entre los dos procesos
  biggest_ini = (data_dist.ini > other.ini) ? data_dist.ini : other.ini;
  // Obtiene el proceso con menor fin entre los dos procesos
  smallest_end = (data_dist.fin < other.fin) ? data_dist.fin : other.fin;

  sendcounts[id] = smallest_end - biggest_ini; // Numero de elementos a enviar/recibir del proceso Id
}


/*
 * Obtiene para un proceso de un grupo a que rango procesos de 
 * otro grupo tiene que enviar o recibir datos.
 *
 * Devuelve el primer identificador y el último (Excluido) con el que
 * comunicarse.
 */
void get_util_ids(struct Dist_data dist_data, int numP_other, int **idS) {
    int idI, idE;
    size_t tamOther = dist_data.qty / numP_other;
    size_t remOther = dist_data.qty % numP_other;
    // Indica el punto de corte del grupo de procesos externo que 
    // divide entre los procesos que tienen 
    // un tamaño tamOther + 1 y un tamaño tamOther
    size_t middle = (tamOther + 1) * remOther;

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
 * ========================================================================================
 * ========================================================================================
 * ==============================INIT/FREE/PRINT FUNCTIONS=================================
 * ========================================================================================
 * ========================================================================================
*/

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
 */
void mallocCounts(struct Counts *counts, size_t numP) {

    counts->counts = calloc(numP, sizeof(MPI_Count)); 
    if(counts->counts == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->displs = calloc(numP, sizeof(MPI_Aint));
    if(counts->displs == NULL) { MPI_Abort(MPI_COMM_WORLD, -2);}

    counts->len = numP;
    counts->idI = -1;
    counts->idE = -1;
    counts->first_target_displs = 0;
}


/*
 * Libera la memoria interna de una estructura Counts.
 *
 * No libera la memoria de la estructura counts si se ha alojado
 * de forma dinamica.
 */
void freeCounts(struct Counts *counts) {
    if(counts == NULL) {
      return;
    }

    if(counts->counts != NULL) {
      free(counts->counts);
      counts->counts = NULL;
    }
    if(counts->displs != NULL) {
      free(counts->displs);
      counts->displs = NULL;
    }
}

/*
 * Muestra la informacion de comunicaciones de un proceso
 * Si se activa la bandera "include_zero" a verdadero se mostraran para el vector
 * xcounts los valores a 0.
 *
 * En "name" se puede indicar un string con el fin de identificar mejor a que vectores
 * se refiere la llamada.
 */
void print_counts(struct Dist_data data_dist, MPI_Count *xcounts, MPI_Aint *xdispls, int size, int include_zero, const char* name) {
  int i;

  for(i=0; i < size; i++) {
    if(xcounts[i] != 0 || include_zero) {
      printf("P%d of %d | %scounts[%d]=%lld disp=%ld\n", data_dist.myId, data_dist.numP, name, i, xcounts[i], xdispls[i]);
    }
  }
}
