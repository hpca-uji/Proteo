#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "results.h"

void def_results_type(results_data *results, int resizes, MPI_Datatype *results_type);

//======================================================||
//======================================================||
//================MPI RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||

//TODO Generalizar ambas funciones en una sola
/*
 * Envia una estructura de resultados al grupo de procesos al que se 
 * enlaza este grupo a traves del intercomunicador pasado como argumento.
 *
 * Esta funcion tiene que ser llamada por todos los procesos del mismo grupo
 * e indicar cual es el proceso raiz que se encargara de enviar los
 * resultados al otro grupo.
 */
void send_results(results_data *results, int root, int resizes, MPI_Comm intercomm) {
  MPI_Datatype results_type;

  // Obtener un tipo derivado para enviar todos los
  // datos escalares con una sola comunicacion
  def_results_type(results, resizes, &results_type);
  MPI_Bcast(results, 1, results_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&results_type);
}

/*
 * Recibe una estructura de resultados desde otro grupo de procesos
 * y la devuelve. La memoria de la estructura se tiene que reservar con antelacion
 * con la función "init_results_data".
 *
 * Esta funcion tiene que ser llamada por todos los procesos del mismo grupo
 * e indicar cual es el proceso raiz del otro grupo que se encarga de enviar
 * los resultados a este grupo.
 */
void recv_results(results_data *results, int root, int resizes, MPI_Comm intercomm) {
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


/*
 * Obtiene para cada iteracion, el tiempo maximo entre todos los procesos
 * que han participado.
 *
 * Es necesario obtener el maximo, pues es el que representa el tiempo real
 * que se ha utilizado.
 */
void compute_results_iter(results_data *results, int myId, int root, MPI_Comm comm) {
  if(myId == root)
    MPI_Reduce(MPI_IN_PLACE, results->iters_time, results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
  else
    MPI_Reduce(results->iters_time, NULL, results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
}


/*
 * Obtiene para cada stage de cada iteracion, el tiempo maximo entre todos los procesos
 * que han participado.
 *
 * Es necesario obtener el maximo, pues es el que representa el tiempo real
 * que se ha utilizado.
 */
void compute_results_stages(results_data *results, int myId, int root, int stages, MPI_Comm comm) {
  int i;
  if(myId == root) {
    for(i=0; i<stages; i++) {
      MPI_Reduce(MPI_IN_PLACE, results->stage_times[i], results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
    }
  }
  else {
    for(i=0; i<stages; i++) {
      MPI_Reduce(results->stage_times[i], NULL, results->iter_index, MPI_DOUBLE, MPI_MAX, root, comm);
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
  int i;

  printf("T_iter: ");
  for(i=0; i< results.iter_index; i++) {
    printf("%lf ", results.iters_time[i]);
  }

  printf("\nAsync_Iters: %d\n", results.iters_async);
}

/*
 * Imprime por pantalla los resultados locales de un stage.
 */
void print_stage_results(results_data results, int n_stages) {
  int i, j;

  for(i=0; i<n_stages; i++) {
    printf("T_stage %d: ", i);
    for(j=0; j< results.iter_index; j++) {
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
void print_global_results(results_data results, int resizes) {
  int i;

  printf("T_spawn: ");  // FIXME REFACTOR Cambiar nombre a T_resize_real
  for(i=0; i< resizes - 1; i++) {
    printf("%lf ", results.spawn_time[i]);
  }

  printf("\nT_spawn_real: "); // FIXME REFACTOR Cambiar nombre a T_resize
  for(i=0; i< resizes - 1; i++) {
    printf("%lf ", results.spawn_real_time[i]);
  }

  printf("\nT_SR: ");
  for(i=1; i < resizes; i++) {
    printf("%lf ", results.sync_time[i]);
  }

  printf("\nT_AR: ");
  for(i=1; i < resizes; i++) {
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
void init_results_data(results_data *results, int resizes, int stages, int iters_size) {
  int i;

  results->spawn_time = calloc(resizes, sizeof(double));
  results->spawn_real_time = calloc(resizes, sizeof(double));
  results->sync_time = calloc(resizes, sizeof(double));
  results->async_time = calloc(resizes, sizeof(double));
  results->wasted_time = 0;

  results->iters_size = iters_size + 100;
  results->iters_time = calloc(iters_size + 100, sizeof(double)); //FIXME Numero magico
  results->stage_times = malloc(stages * sizeof(double*)); //FIXME Numero magico
  for(i=0; i<stages; i++) {
    results->stage_times[i] = calloc(iters_size + 100, sizeof(double)); //FIXME Numero magico
  }

  results->iters_async = 0;
  results->iter_index = 0;

}

void realloc_results_iters(results_data *results, int stages, int needed) {
  int i;
  double *time_aux;
  time_aux = (double *) realloc(results->iters_time, needed * sizeof(double));

  for(i=0; i<stages; i++) { //TODO Comprobar que no da error el realloc
    results->stage_times[i] = (double *) realloc(results->stage_times[i], needed * sizeof(double)); 
  }

  if(time_aux == NULL) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria de resultados\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  results->iters_time = time_aux;
}

/*
 * Libera toda la memoria asociada con una estructura de resultados.
 */
void free_results_data(results_data *results, int stages) {
    int i;
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
