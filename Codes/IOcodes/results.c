#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "results.h"

void def_results_type(results_data *results, MPI_Datatype *results_type);

//======================================================||
//======================================================||
//================MPI RESULTS FUNCTIONS=================||
//======================================================||
//======================================================||
void send_results(results_data *results, int root, MPI_Comm intercomm) {
  MPI_Datatype results_type;

  // Obtener un tipo derivado para enviar todos los
  // datos escalares con una sola comunicacion
  def_results_type(results, &results_type);
  MPI_Bcast(results, 1, results_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&results_type);
}

void recv_results(results_data *results, int root, MPI_Comm intercomm) {
  MPI_Datatype results_type;

  // Obtener un tipo derivado para enviar todos los
  // datos escalares con una sola comunicacion
  def_results_type(results, &results_type);
  MPI_Bcast(results, 1, results_type, root, intercomm);

  //Liberar tipos derivados
  MPI_Type_free(&results_type);
}

void def_results_type(results_data *results, MPI_Datatype *results_type) {
  int i, counts = 3;
  int blocklengths[3] = {1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = MPI_DOUBLE;

  // Rellenar vector displs
  MPI_Get_address(results, &dir);

  MPI_Get_address(&(results->sync_start), &displs[0]);
  MPI_Get_address(&(results->async_start), &displs[1]);
  MPI_Get_address(&(results->spawn_time), &displs[2]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, results_type);
  MPI_Type_commit(results_type);
}

//======================================================||
//======================================================||
//===============PRINT RESULTS FUNCTIONS================||
//======================================================||
//======================================================||
void print_iter_results(results_data *results, int last_normal_iter_index) {
  int i, aux;

  printf("Titer: ");
  for(i=0; i< results->iter_index; i++) {
    printf("%lf ", results->iters_time[i]);
  }

  printf("\nTtype: ");
  for(i=0; i< results->iter_index; i++) {
    printf("%d ", results->iters_type[i] == 0);
  }

  printf("\nTop: ");
  for(i=0; i< results->iter_index; i++) {
    aux = results->iters_type[i] == 0 ? results->iters_type[last_normal_iter_index] : results->iters_type[i];
    printf("%d ", aux);
  }
  printf("\n");
}

//======================================================||
//======================================================||
//=============INIT/FREE RESULTS FUNCTIONS==============||
//======================================================||
//======================================================||

void init_results_data(results_data **results, int resizes, int iters_size) {
  *results = malloc(1 * sizeof(results_data));

  (*results)->spawn_time = calloc(resizes, sizeof(double));
  (*results)->sync_time = calloc(resizes, sizeof(double));
  (*results)->async_time = calloc(resizes, sizeof(double));

  (*results)->iters_time = calloc(iters_size * 20, sizeof(double));
  (*results)->iters_type = calloc(iters_size * 20, sizeof(int));
  (*results)->iter_index = 0;
}

void free_results_data(results_data **results) {
    //free((*results)->spawn_time);
    //free((*results)->sync_time);
    //free((*results)->async_time);

    free((*results)->iters_time);
    free((*results)->iters_type);
    free(*results);
}
