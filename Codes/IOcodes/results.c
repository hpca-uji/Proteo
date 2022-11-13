#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "results.h"

#define RESULTS_EXTRA_SIZE 100

void def_results_type(results_data *results, int resizes, MPI_Datatype *results_type);

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
void comm_results(results_data *results, int root, size_t resizes, MPI_Comm intercomm) {
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
  int blocklengths[] = {1, 1, 1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = types[4] = types[5] = MPI_DOUBLE;
  blocklengths[3] = blocklengths[4] = resizes;

  // Rellenar vector displs
  MPI_Get_address(results, &dir);

  MPI_Get_address(&(results->sync_start), &displs[0]);
  MPI_Get_address(&(results->async_start), &displs[1]);
  MPI_Get_address(&(results->exec_start), &displs[2]);
  MPI_Get_address(&(results->wasted_time), &displs[3]);
  MPI_Get_address(&(results->spawn_real_time[0]), &displs[4]);
  MPI_Get_address(&(results->spawn_time[0]), &displs[5]); //TODO Revisar si se puede simplificar //FIXME Si hay mas de un spawn error?

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
 * Guarda los resultados respecto a la redistribución de datos
 * tras una reconfiguración. A llamar por los hijos tras
 * terminar la redistribución y obtener la configuración.
 */
void set_results_post_reconfig(results_data *results, int grp, int sdr, int adr) {
  if(sdr) { // Si no hay datos sincronos, el tiempo es 0
    results->sync_time[grp]  = results->sync_end - results->sync_start;
  } else {
    results->sync_time[grp]  = 0;
  }
  if(adr) { // Si no hay datos asincronos, el tiempo es 0
    results->async_time[grp]  = results->async_end - results->async_start;
  } else {
    results->async_time[grp]  = 0;
  }
}

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
}

//=============================================================== FIXME BORRAR?
int compare(const void *_a, const void *_b) { 
        double *a, *b;
        a = (double *) _a;
        b = (double *) _b;
        return (*a - *b);
}
/*
 * Obtiene para cada iteracion, el tiempo maximo entre todos los procesos
 * que han participado.
 *
 * Es necesario obtener el maximo, pues es el que representa el tiempo real
 * que se ha utilizado.
 */
void compute_results_iter(results_data *results, int myId, int numP, int root, MPI_Comm comm) { //TODO Probar a quedarse la MEDIA en vez de MAX?
  if(myId == root) {
    /*MPI_Reduce(MPI_IN_PLACE, results->iters_time, results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
    for(size_t i=0; i<results->iter_index; i++) {
      results->iters_time[i] = results->iters_time[i] / numP;
    }*/
  } else {
    //MPI_Reduce(results->iters_time, NULL, results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
  }
  double *aux_all_iters, *aux_id_iters, median;
  if(myId == root) {
    aux_all_iters = malloc(numP *results->iter_index * sizeof(double));
  }
  MPI_Gather(results->iters_time, results->iter_index, MPI_DOUBLE, aux_all_iters, results->iter_index, MPI_DOUBLE, root, comm);
  if(myId == root) {
    aux_id_iters = malloc(numP * sizeof(double));
    for(size_t i=0; i<results->iter_index; i++) {
      for(int j=0; j<numP; j++) {
        aux_id_iters[j] = aux_all_iters[i+(results->iter_index*j)];
      }
      // Get Median
      qsort(aux_id_iters, numP, sizeof(double), &compare);
      median = aux_id_iters[numP/2];
      if (numP % 2 == 0) median = (aux_id_iters[numP/2 - 1] + aux_id_iters[numP/2]) / 2;
      results->iters_time[i] = median;
    }
    free(aux_all_iters);
    free(aux_id_iters);
  }
}


/*
 * Obtiene para cada stage de cada iteracion, el tiempo maximo entre todos los procesos
 * que han participado.
 *
 * Es necesario obtener el maximo, pues es el que representa el tiempo real
 * que se ha utilizado.
 */
void compute_results_stages(results_data *results, int myId, int numP, int root, int stages, MPI_Comm comm) { //TODO Probar a quedarse la MEDIA en vez de MAX?
  int i;
  if(myId == root) {
    for(i=0; i<stages; i++) {
      MPI_Reduce(MPI_IN_PLACE, results->stage_times[i], results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
      for(size_t j=0; j<results->iter_index; j++) {
        results->stage_times[i][j] = results->stage_times[i][j] / numP;
      }
    }
  }
  else {
    for(i=0; i<stages; i++) {
      MPI_Reduce(results->stage_times[i], NULL, results->iter_index, MPI_DOUBLE, MPI_SUM, root, comm);
    }
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

  printf("T_iter: ");
  for(i=0; i< results.iter_index; i++) {
    printf("%lf ", results.iters_time[i]);
  }

  printf("\nAsync_Iters: %ld\n", results.iters_async);
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
  for(i=0; i < resizes - 1; i++) {
    printf("%lf ", results.spawn_time[i]);
  }

  printf("\nT_spawn_real: ");
  for(i=0; i< resizes - 1; i++) {
    printf("%lf ", results.spawn_real_time[i]);
  }

  printf("\nT_SR: ");
  for(i=0; i < resizes - 1; i++) {
    printf("%lf ", results.sync_time[i]);
  }

  printf("\nT_AR: ");
  for(i=0; i < resizes - 1; i++) {
    printf("%lf ", results.async_time[i]);
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
  results->spawn_real_time = calloc(resizes, sizeof(double));
  results->sync_time = calloc(resizes, sizeof(double));
  results->async_time = calloc(resizes, sizeof(double));
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
  time_aux = (double *) realloc(results->iters_time, needed * sizeof(double));

  for(i=0; i<stages; i++) { //TODO Comprobar que no da error el realloc
    results->stage_times[i] = (double *) realloc(results->stage_times[i], needed * sizeof(double));
    if(results->stage_times[i] == NULL) error = 1;
  }

  if(time_aux == NULL) error = 1;
  if(error) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria de resultados\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  results->iters_time = time_aux;
}

/*
 * Libera toda la memoria asociada con una estructura de resultados.
 */
void free_results_data(results_data *results, size_t stages) {
  size_t i;
  if(results != NULL) {
    free(results->spawn_time);
    free(results->spawn_real_time);
    free(results->sync_time);
    free(results->async_time);

    free(results->iters_time);
    for(i=0; i<stages; i++) {
      free(results->stage_times[i]);
    }
    free(results->stage_times);
  }
}
