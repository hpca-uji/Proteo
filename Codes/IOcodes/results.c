#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "results.h"

#define RESULTS_EXTRA_SIZE 100

void def_results_type(results_data *results, int resizes, MPI_Datatype *results_type);

void compute_max(results_data *results, double *computed_array, int myId, int root, MPI_Comm comm);
void compute_mean(results_data *results, double *computed_array, int myId, int numP, int root, MPI_Comm comm);
void compute_median(results_data *results, double *computed_array, size_t *used_ids, int myId, int numP, int root, MPI_Comm comm);
void match_median(results_data *results, double *computed_array, size_t *used_ids, int myId, int numP, int root, MPI_Comm comm);

//======================================================||
//======================================================||
//================MPI RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||

/*
 * Comunica una estructura de resultados a todos los procesos del comunicador
 * a traves de un tipo derivado.
 *
 * Si se llama con un intercommunicador, el grupo de procesos que envia los datos
 * tiene que indicar en el proceso raiz el valor "MPI_ROOT" para "root" y el resto
 * de ese grupo el valor "MPI_PROC_NULL". Los procesos del otro grupo tienen que
 * indicar el Id del proceso raiz que ha puesto "MPI_ROOT".
 */
void results_comm(results_data *results, int root, size_t resizes, MPI_Comm intercomm) {
  MPI_Datatype results_type;

  // Obtener un tipo derivado para enviar todos los
  // datos escalares con una sola comunicacion
  def_results_type(results, resizes, &results_type);
  MPI_Bcast(results, 1, results_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&results_type);
}

/*
 * Define un tipo derivado de MPI para mandar los tiempos
 * con una sola comunicacion.
 *
 * En concreto son tres escalares y dos vectores de tamaño "resizes"
 */
void def_results_type(results_data *results, int resizes, MPI_Datatype *results_type) {
  int i, counts = 6;
  int blocklengths[] = {1, 1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = MPI_DOUBLE;
  blocklengths[2] = blocklengths[3] = blocklengths[4] = blocklengths[5] = resizes;

  // Rellenar vector displs
  MPI_Get_address(results, &dir);

  MPI_Get_address(&(results->exec_start), &displs[0]);
  MPI_Get_address(&(results->wasted_time), &displs[1]);
  MPI_Get_address(results->sync_time, &displs[2]);
  MPI_Get_address(results->async_time, &displs[3]);
  MPI_Get_address(results->spawn_time, &displs[4]);
  MPI_Get_address(results->malleability_time, &displs[5]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, results_type);
  MPI_Type_commit(results_type);
}

//======================================================||
//======================================================||
//================SET RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||

/*
 * Pone el indice del siguiente elemento a escribir a 0 para los vectores
 * que tengan que ver con las iteraciones.
 * Por tanto, todos los anteriores valores de esos vectores pasan a ser invalidos
 * si se intentan acceder desde un código externo.
 *
 * Solo es necesario llamar a esta funcion cuando se ha realizado una
 * expansion con el metodo MERGE
 */
void reset_results_index(results_data *results) {
  results->iter_index = 0;
  results->iters_async = 0;
}

/*
 * Obtiene para cada iteracion, el tiempo maximo entre todos los procesos
 * que han participado.
 *
 * Es necesario obtener el maximo, pues es el que representa el tiempo real
 * que se ha utilizado.
 */
void compute_results_iter(results_data *results, int myId, int numP, int root, size_t stages, int capture_method, MPI_Comm comm) {
  size_t i, *used_ids;
  switch(capture_method) {
    case RESULTS_MAX:
      compute_max(results, results->iters_time, myId, root, comm);
      for(i=0; i<stages; i++) {
        compute_max(results, results->stage_times[i], myId, root, comm);
      }
      break;
    case RESULTS_MEAN:
      compute_mean(results, results->iters_time, myId, numP, root, comm);
      for(i=0; i<stages; i++) {
        compute_mean(results, results->stage_times[i], myId, numP, root, comm);
      }
      break;
    case RESULTS_MEDIAN:
      used_ids = malloc(results->iter_index * sizeof(size_t));
      compute_median(results, results->iters_time, used_ids, myId, numP, root, comm);
      for(i=0; i<stages; i++) {
        //compute_median(results, results->stage_times[i], myId, numP, root, comm);
        match_median(results, results->stage_times[i], used_ids, myId, numP, root, comm);
      }
      free(used_ids);
      break;
  }
}

void compute_max(results_data *results, double *computed_array, int myId, int root, MPI_Comm comm) {
  if(myId == root) {
    MPI_Reduce(MPI_IN_PLACE, computed_array, results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
  } else {
    MPI_Reduce(computed_array, NULL, results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
  }
}

void compute_mean(results_data *results, double *computed_array, int myId, int numP, int root, MPI_Comm comm) {
  if(myId == root) {
    MPI_Reduce(MPI_IN_PLACE, computed_array, results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
    for(size_t i=0; i<results->iter_index; i++) {
      computed_array[i] = results->iters_time[i] / numP;
    }
  } else {
    MPI_Reduce(computed_array, NULL, results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
  }
}



struct TimeWithIndex {
    double time;
    size_t index;
};

int compare(const void *a, const void *b) {
  return ((struct TimeWithIndex *)a)->time - ((struct TimeWithIndex *)b)->time;
}

/*
 * Calcula la mediana de un vector de tiempos replicado entre "numP" procesos.
 * Se calcula la mediana para cada elemento del vector final y se devuelve este.
 *
 * Además se devuelve en el vector "used_ids" de que proceso se ha obtenido la mediana de cada elemento.
 */
void compute_median(results_data *results, double *computed_array, size_t *used_ids, int myId, int numP, int root, MPI_Comm comm) {
  double *aux_all_iters, median;
  struct TimeWithIndex *aux_id_iters;
  if(myId == root) {
    aux_all_iters = malloc(numP *results->iter_index * sizeof(double));
    aux_id_iters = malloc(numP * sizeof(struct TimeWithIndex));
  }
  MPI_Gather(computed_array, results->iter_index, MPI_DOUBLE, aux_all_iters, results->iter_index, MPI_DOUBLE, root, comm);
  if(myId == root) {
    for(size_t i=0; i<results->iter_index; i++) {
      for(int j=0; j<numP; j++) {
        aux_id_iters[j].time = aux_all_iters[i+(results->iter_index*j)];
        aux_id_iters[j].index = (size_t) j;
      }
      // Get Median
      qsort(aux_id_iters, numP, sizeof(struct TimeWithIndex), &compare);
      median = aux_id_iters[numP/2].time;
      if (numP % 2 == 0) median = (aux_id_iters[numP/2 - 1].time + aux_id_iters[numP/2].time) / 2;
      computed_array[i] = median;
      used_ids[i] = aux_id_iters[numP/2].index; //FIXME What should be the index when numP is even?
    }
    free(aux_all_iters);
    free(aux_id_iters);
  }
}

/*
 * Obtiene las medianas de un vector de tiempos replicado entre "numP" procesos.
 * La mediana de cada elemento se obtiene consultando el vector "used_ids", que contiene
 * que proceso tiene la mediana.
 *
 * Como resultado devuelve un vector con la mediana calculada.
 */
void match_median(results_data *results, double *computed_array, size_t *used_ids, int myId, int numP, int root, MPI_Comm comm) {
  double *aux_all_iters;
  size_t matched_id;
  if(myId == root) {
    aux_all_iters = malloc(numP * results->iter_index * sizeof(double));
  }
  MPI_Gather(computed_array, results->iter_index, MPI_DOUBLE, aux_all_iters, results->iter_index, MPI_DOUBLE, root, comm);
  if(myId == root) {
    for(size_t i=0; i<results->iter_index; i++) {
      matched_id = used_ids[i];
      computed_array[i] = aux_all_iters[i+(results->iter_index*matched_id)];
    }
    free(aux_all_iters);
  }
}

//======================================================||
//======================================================||
//===============PRINT RESULTS FUNCTIONS================||
//======================================================||
//======================================================||

/*
 * Imprime por pantalla los resultados locales.
 * Estos son los relacionados con las iteraciones, que son el tiempo
 * por iteracion, el tipo (Normal o durante communicacion asincrona).
 */
void print_iter_results(results_data results) {
  size_t i;

  printf("Async_Iters: %ld\n", results.iters_async);
  printf("T_iter: ");
  for(i=0; i< results.iter_index; i++) {
    printf("%lf ", results.iters_time[i]);
  }
  printf("\n");
}

/*
 * Imprime por pantalla los resultados locales de un stage.
 */
void print_stage_results(results_data results, size_t n_stages) {
  size_t i, j;

  for(i=0; i < n_stages; i++) {
    printf("T_stage %ld: ", i);
    for(j=0; j < results.iter_index; j++) {
      printf("%lf ", results.stage_times[i][j]);
    }
    printf("\n");
  }
}

/*
 * Imprime por pantalla los resultados globales.
 * Estos son el tiempo de creacion de procesos, los de comunicacion
 * asincrona y sincrona y el tiempo total de ejecucion.
 */
void print_global_results(results_data results, size_t resizes) {
  size_t i;

  printf("T_spawn: ");
  for(i=0; i < resizes; i++) {
    printf("%lf ", results.spawn_time[i]);
  }

  printf("\nT_SR: ");
  for(i=0; i < resizes; i++) {
    printf("%lf ", results.sync_time[i]);
  }

  printf("\nT_AR: ");
  for(i=0; i < resizes; i++) {
    printf("%lf ", results.async_time[i]);
  }

  printf("\nT_Malleability: ");
  for(i=0; i < resizes; i++) {
    printf("%lf ", results.malleability_time[i]);
  }

  printf("\nT_total: %lf\n", results.exec_time);
}

//======================================================||
//======================================================||
//=============INIT/FREE RESULTS FUNCTIONS==============||
//======================================================||
//======================================================||

/*
 * Inicializa los datos relacionados con una estructura de resultados.
 *
 * Los argumentos "resizes" y "iters_size" se necesitan para obtener el tamaño
 * de los vectores de resultados.
 */
void init_results_data(results_data *results, size_t resizes, size_t stages, size_t iters_size) {
  size_t i;

  results->spawn_time = calloc(resizes, sizeof(double));
  results->sync_time = calloc(resizes, sizeof(double));
  results->async_time = calloc(resizes, sizeof(double));
  results->malleability_time = calloc(resizes, sizeof(double));
  results->wasted_time = 0;

  results->iters_size = iters_size + RESULTS_EXTRA_SIZE;
  results->iters_time = calloc(results->iters_size, sizeof(double));
  results->stage_times = malloc(stages * sizeof(double*));
  for(i=0; i<stages; i++) {
    results->stage_times[i] = calloc(results->iters_size, sizeof(double));
  }

  results->iters_async = 0;
  results->iter_index = 0;

}

void realloc_results_iters(results_data *results, size_t stages, size_t needed) {
  int error = 0;
  double *time_aux;
  size_t i;

  if(results->iters_size >= needed) return;

  time_aux = (double *) realloc(results->iters_time, needed * sizeof(double));
  if(time_aux == NULL) error = 1;

  for(i=0; i<stages; i++) { //TODO Comprobar que no da error el realloc
    results->stage_times[i] = (double *) realloc(results->stage_times[i], needed * sizeof(double));
    if(results->stage_times[i] == NULL) error = 1;
  }

  if(error) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria de resultados\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  results->iters_time = time_aux;
  results->iters_size = needed;
}

/*
 * Libera toda la memoria asociada con una estructura de resultados.
 */
void free_results_data(results_data *results, size_t stages) {
  size_t i;
  if(results != NULL) {
    if(results->spawn_time != NULL) {
      free(results->spawn_time);
      results->spawn_time = NULL;
    }
    if(results->sync_time != NULL) {
      free(results->sync_time);
      results->sync_time = NULL;
    }
    if(results->async_time != NULL) {
      free(results->async_time);
      results->async_time = NULL;
    }
    if(results->malleability_time != NULL) {
      free(results->malleability_time);
      results->malleability_time = NULL;
    }

    if(results->iters_time != NULL) {
      free(results->iters_time);
      results->iters_time = NULL;
    }
    for(i=0; i<stages; i++) {
      if(results->stage_times[i] != NULL) {
        free(results->stage_times[i]);
        results->stage_times[i] = NULL;
      }
    }
    if(results->stage_times != NULL) {
      free(results->stage_times);
      results->stage_times = NULL;
    }
  }
}
