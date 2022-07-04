#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "linear_reg.h"
#include "Main_datatypes.h"
#include "process_stage.h"
//#include "../malleability/malleabilityManager.h" //FIXME Refactor
#include "../malleability/distribution_methods/block_distribution.h"

void linear_regression_stage(iter_stage_t *stage, group_data group, MPI_Comm comm);


double init_matrix_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_pi_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
void init_comm_ptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);
double init_comm_bcast_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);
double init_comm_allgatherv_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);
double init_comm_reduce_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);

/*
 * Calcula el tiempo por operacion o total de bytes a enviar
 * de cada fase de iteración para despues realizar correctamente
 * las iteraciones.
 *
 * Solo es calculado por el proceso ROOT que tras ello lo envia al
 * resto de procesos.
 *
 * Si la bandera "compute" esta activada, se realizaran las operaciones
 * para recalcular los tiempos desde 0. Si esta en falso solo se reservara
 * la memoria necesaria y utilizara los valores obtenidos en anteriores 
 * llamadas. Todos los procesos tienen que indicar el mismo valor en
 * la bandera.
 *
 * TODO Que el trabajo se divida entre los procesos.
 * TODO No tiene en cuenta cambios entre maquinas heterogeneas.
 */
double init_stage(configuration *config_file, int stage_i, group_data group, MPI_Comm comm, int compute) {
  double result = 0;
  int qty = 20000;

  iter_stage_t *stage = &(config_file->stages[stage_i]);
  stage->operations = qty;

  switch(stage->pt) {
    //Computo
    case COMP_MATRIX:
      result = init_matrix_pt(group, config_file, stage, comm, compute);
    case COMP_PI:
      result = init_pi_pt(group, config_file, stage, comm, compute);
      break;

    //Comunicación
    case COMP_POINT:
      init_comm_ptop_pt(group, config_file, stage, comm);
      break;
    case COMP_BCAST:
      result = init_comm_bcast_pt(group, config_file, stage, comm);
      break;
    case COMP_ALLGATHER:
      result = init_comm_allgatherv_pt(group, config_file, stage, comm);
      break;
    case COMP_REDUCE:
    case COMP_ALLREDUCE:
      result = init_comm_reduce_pt(group, config_file, stage, comm);
      break;
  }
  return result;
}

/*
 * Procesa una fase de la iteracion, concretando el tipo
 * de operacion a realizar y llamando a la funcion que
 * realizara la operacion.
 */
double process_stage(configuration config_file, iter_stage_t stage, group_data group, MPI_Comm comm) {
  int i;
  double result;

  switch(stage.pt) {
    //Computo
    case COMP_PI:
      for(i=0; i < stage.operations; i++) {
        result += computePiSerial(config_file.granularity);
      }
      break;
    case COMP_MATRIX:
      for(i=0; i < stage.operations; i++) {
        result += computeMatrix(stage.double_array, config_file.granularity); //FIXME No da tiempos repetibles
      } 
      break;
    //Comunicaciones
    case COMP_POINT:
      point_to_point(group.myId, group.numP, ROOT, comm, stage.array, stage.real_bytes);
      break;
    case COMP_BCAST:
      MPI_Bcast(stage.array, stage.real_bytes, MPI_CHAR, ROOT, comm);
      break;
    case COMP_ALLGATHER:
      MPI_Allgatherv(stage.array, stage.my_bytes, MPI_CHAR, stage.full_array, stage.counts.counts, stage.counts.displs, MPI_CHAR, comm);
      break;
    case COMP_REDUCE:
      MPI_Reduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, ROOT, comm);
      break;
    case COMP_ALLREDUCE:
      MPI_Allreduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, comm);
      break;
  }
  return result;
}




// Se realizan varios tests de latencia al 
// mandar un único dato de tipo CHAR a los procesos impares
// desde el par inmediatamente anterior. Tras esto, los impares
// vuelven a enviar el dato al proceso par.
//
// Devuelve la latencia del sistema.
double latency(int myId, int numP, MPI_Comm comm) {
  int i, loop_count = 100;
  double start_time, stop_time, elapsed_time, max_time;
  char aux;

  aux = '0';
  elapsed_time = 0;

  //if(myId+1 != numP || (myId+1 == numP && numP % 2 == 0)) {
    MPI_Barrier(comm);
    start_time = MPI_Wtime();
    //if(myId % 2 == 0){
    if(myId == 0) {
      for(i=0; i<loop_count; i++){
        MPI_Ssend(&aux, 0, MPI_CHAR, numP-1, 99, comm);
      }
      MPI_Recv(&aux, 0, MPI_CHAR, numP-1, 99, comm, MPI_STATUS_IGNORE);
    } else if(myId+1 == numP) {
      for(i=0; i<loop_count; i++){
        MPI_Recv(&aux, 0, MPI_CHAR, 0, 99, comm, MPI_STATUS_IGNORE);
      }
      MPI_Ssend(&aux, 0, MPI_CHAR, 0, 99, comm);
    }

    MPI_Barrier(comm);
    stop_time = MPI_Wtime();
    max_time = (stop_time - start_time) / loop_count;
  //}

  //MPI_Allreduce(&elapsed_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, comm);
  MPI_Bcast(&max_time, 1, MPI_DOUBLE, ROOT, comm);
  return max_time;
}


// Se realizan varios tests de ancho de banda
// al mandar N datos a los procesos impares desde el
// par inmediatamente anterior. Tras esto, los impares
// vuelven a enviar los N datos al proceso par.
//
// Devuelve el tiempo necesario para realizar las pruebas
double bandwidth(int myId, int numP, MPI_Comm comm, double latency, int n) {
  int i, loop_count = 100, n_bytes;
  double start_time, stop_time, elapsed_time, bw, time, max_time;
  char *aux;

  n_bytes = n * sizeof(char);
  aux = malloc(n_bytes);
  elapsed_time = 0;
  time = 0;

//  if(myId+1 != numP || (myId+1 == numP && numP % 2 == 0)) {

    MPI_Barrier(comm);
    start_time = MPI_Wtime();
    //if(myId % 2 == 0){
    if(myId == 0) {
      for(i=0; i<loop_count; i++){
        MPI_Ssend(aux, n, MPI_CHAR, numP-1, 99, comm);
      }
      MPI_Recv(aux, 0, MPI_CHAR, numP-1, 99, comm, MPI_STATUS_IGNORE);
    } else if(myId+1 == numP) {
      for(i=0; i<loop_count; i++){
        MPI_Recv(aux, n, MPI_CHAR, 0, 99, comm, MPI_STATUS_IGNORE);
      }
      MPI_Ssend(aux, 0, MPI_CHAR, 0, 99, comm);
    }
    MPI_Barrier(comm);
    stop_time = MPI_Wtime();
    elapsed_time = (stop_time - start_time) / loop_count;
  //}

  if(myId %2 == 0) {
    time = elapsed_time - latency;
  }

  MPI_Allreduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, comm);
  //TODO Cambiar a Bcast si solo se realiza por Root
  bw = ((double)n_bytes) / max_time;
  free(aux);
  return bw;
}

/*
 * Creates a linear regression model to predict
 * the number of bytes needed to perform a collective
 * communication.
 */
void linear_regression_stage(iter_stage_t *stage, group_data group, MPI_Comm comm) {
  int i, j, tam, loop_iters = 100;

  tam = LR_ARRAY_TAM * loop_iters;
  double *bytes = malloc(tam * sizeof(double));
  double *times = malloc(tam * sizeof(double));
  
  for(i=0; i<LR_ARRAY_TAM; i++) {
    for(j=0; j<loop_iters; j++) {
      bytes[i*loop_iters + j] = LR_bytes_array[i];
    }
  }

  switch(stage->pt) {
    //Comunicaciones
    case COMP_BCAST:
      lr_times_bcast(group.myId, group.numP, ROOT, comm, loop_iters, times);
      break;
    case COMP_ALLGATHER:
      lr_times_allgatherv(group.myId, group.numP, ROOT, comm, loop_iters, times);
      break;
    case COMP_REDUCE:
      lr_times_reduce(group.myId, group.numP, ROOT, comm, loop_iters, times);
      break;
    case COMP_ALLREDUCE:
      lr_times_allreduce(group.myId, group.numP, ROOT, comm, loop_iters, times);
      break;
    default:
      return;
      break;
  }

  if(group.myId == ROOT) {
    MPI_Reduce(MPI_IN_PLACE, times, LR_ARRAY_TAM * loop_iters, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    /*
    printf("PT=%d ", stage->pt);
    for(i=0; i<tam; i++) {
      printf("%lf, ", times[i]);
    }
    printf("\n");
    printf("BYTES ");
    for(i=0; i<tam; i++) {
      printf("%lf, ", bytes[i]);
    }
    printf("\n");
    */
    lr_compute(tam, bytes, times, &(stage->slope), &(stage->intercept));
  } else {
    MPI_Reduce(times, NULL, LR_ARRAY_TAM * loop_iters, MPI_DOUBLE, MPI_MAX, ROOT, comm);
  }

  MPI_Bcast(&(stage->slope), 1, MPI_DOUBLE, ROOT, comm);
  MPI_Bcast(&(stage->intercept), 1, MPI_DOUBLE, ROOT, comm);

  free(times);
  free(bytes);
}


/*
 * ========================================================================================
 * ========================================================================================
 * =================================INIT STAGE FUNCTIONS===================================
 * ========================================================================================
 * ========================================================================================
*/

double init_matrix_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double result, t_stage;

  result = 0;
  t_stage = stage->t_stage * config_file->factors[group.grp];
  initMatrix(&(stage->double_array), config_file->granularity);

  double start_time = MPI_Wtime();
  if(group.myId == ROOT && compute) {
    result+= process_stage(*config_file, *stage, group, comm);
  }

  if(compute) {
    stage->t_op = (MPI_Wtime() - start_time) / stage->operations; //Tiempo de una operacion
    MPI_Bcast(&(stage->t_op), 1, MPI_DOUBLE, ROOT, comm);
  }
  stage->operations = t_stage / stage->t_op;

  return result;
}

double init_pi_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double result, t_stage, start_time;

  result = 0;
  t_stage = stage->t_stage * config_file->factors[group.grp];	 
  start_time = MPI_Wtime();
  if(group.myId == ROOT && compute) {
    result+= process_stage(*config_file, *stage, group, comm);
  }

  if(compute) {
    stage->t_op = (MPI_Wtime() - start_time) / stage->operations; //Tiempo de una operacion
    MPI_Bcast(&(stage->t_op), 1, MPI_DOUBLE, ROOT, comm);
  }
  stage->operations = t_stage / stage->t_op;

  return result;
}

void init_comm_ptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  struct Dist_data dist_data;

  if(stage->array != NULL)
    free(stage->array);
  if(stage->bytes == 0) {
    stage->bytes = (stage->t_stage - config_file->latency_m) * config_file->bw_m;
  }
  get_block_dist(stage->bytes, group.myId, group.numP, &dist_data);
  stage->real_bytes = dist_data.tamBl;
  stage->array = malloc(sizeof(char) * stage->real_bytes);
}

double init_comm_bcast_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  double start_time, time = 0;
  stage->real_bytes = stage->bytes;
  if(stage->bytes == 0) {
    start_time = MPI_Wtime();
    linear_regression_stage(stage, group, comm);
    lr_calc_Y(stage->slope, stage->intercept, stage->t_stage, &(stage->real_bytes));

    time = MPI_Wtime() - start_time;
    if(group.myId == ROOT) {
      MPI_Reduce(MPI_IN_PLACE, &time, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    } else {
      MPI_Reduce(&time, NULL, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    }
  }

  if(stage->array != NULL)
    free(stage->array);
  stage->array = malloc(sizeof(char) * stage->real_bytes);

  return time;
}


double init_comm_allgatherv_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  double start_time, time = 0;
  struct Dist_data dist_data;

  stage->real_bytes = stage->bytes;
  if(stage->bytes == 0) {
    start_time = MPI_Wtime();
    linear_regression_stage(stage, group, comm);
    lr_calc_Y(stage->slope, stage->intercept, stage->t_stage, &(stage->real_bytes));

    time = MPI_Wtime() - start_time;
    if(group.myId == ROOT) {
      MPI_Reduce(MPI_IN_PLACE, &time, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    } else {
      MPI_Reduce(&time, NULL, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    }
  }

  if(stage->counts.counts != NULL)
    freeCounts(&(stage->counts));
  prepare_comm_allgatherv(group.numP, stage->real_bytes, &(stage->counts));
      
  get_block_dist(stage->real_bytes, group.myId, group.numP, &dist_data);
  stage->my_bytes = dist_data.tamBl;
  if(stage->array != NULL)
    free(stage->array);
  stage->array = malloc(sizeof(char) * stage->my_bytes);
  if(stage->full_array != NULL)
    free(stage->full_array);
  stage->full_array = malloc(sizeof(char) * stage->real_bytes);

  return time;
}

double init_comm_reduce_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  double start_time, time = 0;

  stage->real_bytes = stage->bytes;
  if(stage->bytes == 0) {
    start_time = MPI_Wtime();
    linear_regression_stage(stage, group, comm);
    lr_calc_Y(stage->slope, stage->intercept, stage->t_stage, &(stage->real_bytes));

    time = MPI_Wtime() - start_time;
    if(group.myId == ROOT) {
      MPI_Reduce(MPI_IN_PLACE, &time, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    } else {
      MPI_Reduce(&time, NULL, 1, MPI_DOUBLE, MPI_MAX, ROOT, comm);
    }
  }

  if(stage->array != NULL)
    free(stage->array);
  stage->array = malloc(sizeof(char) * stage->real_bytes);
  //Full array para el reduce necesita el mismo tamanyo
  if(stage->full_array != NULL)
    free(stage->full_array);
  stage->full_array = malloc(sizeof(char) * stage->real_bytes);

  return time;
}
