#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "linear_reg.h"
#include "Main_datatypes.h"
#include "process_stage.h"
#include "../malleability/malleabilityManager.h" //FIXME Refactor


void get_byte_dist(int qty, int id, int numP, int *result);

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
void init_stage(void *config_file_void, int stage, void *group_void, MPI_Comm comm, int compute) {
  double result, start_time, t_stage;
  int i, aux_bytes, qty = 20000;

  group_data group = *((group_data *) group_void);
  configuration *config_file = (configuration *) config_file_void;
  iter_stage_t *stage_data = &(config_file->iter_stage[stage]);
  stage_data->operations = qty;
  t_stage = stage_data->t_stage * config_file->factors[group.grp];

  if(stage_data->bytes == 0) {
    stage_data->bytes = (stage_data->t_stage - config_file->latency_m) * config_file->bw_m;
  }

  get_byte_dist(stage_data->bytes, group.myId, group.numP, &(stage_data->real_bytes) );

  start_time = MPI_Wtime();
  result = 0;
  switch(stage_data->pt) {
    //Computo
    case COMP_MATRIX:
      initMatrix(&(stage_data->double_array), config_file->matrix_tam);
    case COMP_PI:
      if(group.myId == ROOT && compute) {
        result+= process_stage(config_file_void, stage, group_void, comm);
      }
      break;

    //Comunicación
    case COMP_POINT:
      if(stage_data->array != NULL)
        free(stage_data->array);
      stage_data->array = malloc(sizeof(char) * stage_data->real_bytes);
      break;

    case COMP_BCAST:
      if(stage_data->array != NULL)
        free(stage_data->array);
      stage_data->real_bytes = stage_data->bytes; // Caso especial al usar Bcast
      stage_data->array = malloc(sizeof(char) * stage_data->real_bytes);
      break;

    case COMP_ALLGATHER:

      if(stage_data->counts != NULL)
        free(stage_data->counts);
      stage_data->counts = calloc(group.numP,sizeof(int));
      if(stage_data->displs != NULL)
        free(stage_data->displs);
      stage_data->displs = calloc(group.numP,sizeof(int));

      get_byte_dist(stage_data->bytes, 0, group.numP, &aux_bytes);
      stage_data->counts[0] = aux_bytes;
      stage_data->displs[0] = 0;

      for(i=1; i<group.numP; i++){
        get_byte_dist(stage_data->bytes, i, group.numP, &aux_bytes);
        stage_data->counts[i] = aux_bytes;
        stage_data->displs[i] = stage_data->displs[i-1] + stage_data->counts[i-1];
      }
      
      if(stage_data->array != NULL)
        free(stage_data->array);
      stage_data->array = malloc(sizeof(char) * stage_data->real_bytes);
      if(stage_data->full_array != NULL)
        free(stage_data->full_array);
      stage_data->full_array = malloc(sizeof(char) * stage_data->bytes);
      break;

    case COMP_REDUCE:
    case COMP_ALLREDUCE:
      stage_data->real_bytes = stage_data->bytes;
      if(stage_data->array != NULL)
        free(stage_data->array);
      stage_data->array = malloc(sizeof(char) * stage_data->real_bytes);
      //Full array para el reduce necesita el mismo tamanyo
      if(stage_data->full_array != NULL)
        free(stage_data->full_array);
      stage_data->full_array = malloc(sizeof(char) * stage_data->real_bytes);
      break;
  }
  if(compute) {
    stage_data->t_op = (MPI_Wtime() - start_time) / qty; //Tiempo de una operacion
    MPI_Bcast(&(stage_data->t_op), 1, MPI_DOUBLE, ROOT, comm);
  }
  stage_data->operations = t_stage / stage_data->t_op;
}

/*
 * Procesa una fase de la iteracion, concretando el tipo
 * de operacion a realizar y llamando a la funcion que
 * realizara la operacion.
 */
double process_stage(void *config_file_void, int stage, void *group_void, MPI_Comm comm) {
  int i;
  double result;
  group_data group = *((group_data *) group_void);
  configuration config_file = *((configuration *) config_file_void);
  iter_stage_t stage_data = config_file.iter_stage[stage];

  switch(stage_data.pt) {
    //Computo
    case COMP_PI:
      for(i=0; i < stage_data.operations; i++) {
        result += computePiSerial(config_file.matrix_tam);
      }
      break;
    case COMP_MATRIX:
      for(i=0; i < stage_data.operations; i++) {
        result += computeMatrix(stage_data.double_array, config_file.matrix_tam); //FIXME No da tiempos repetibles
      } 
      break;
    //Comunicaciones
    case COMP_POINT:
      point_to_point(group.myId, group.numP, ROOT, comm, stage_data.array, stage_data.real_bytes);
      break;
    case COMP_BCAST:
      MPI_Bcast(stage_data.array, stage_data.real_bytes, MPI_CHAR, ROOT, comm);
      break;
    case COMP_ALLGATHER:
      MPI_Allgatherv(stage_data.array, stage_data.real_bytes, MPI_CHAR, stage_data.full_array, stage_data.counts, stage_data.displs, MPI_CHAR, comm);
      break;
    case COMP_REDUCE:
      MPI_Reduce(stage_data.array, stage_data.full_array, stage_data.real_bytes, MPI_CHAR, MPI_MAX, ROOT, comm);
      break;
    case COMP_ALLREDUCE:
      MPI_Allreduce(stage_data.array, stage_data.full_array, stage_data.real_bytes, MPI_CHAR, MPI_MAX, comm);
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

  if(myId+1 != numP || (myId+1 == numP && numP % 2 == 0)) {
    MPI_Barrier(comm);
    start_time = MPI_Wtime();
    if(myId % 2 == 0){
      for(i=0; i<loop_count; i++){
        MPI_Ssend(&aux, 0, MPI_CHAR, myId+1, 99, comm);
      }
      MPI_Recv(&aux, 0, MPI_CHAR, myId+1, 99, comm, MPI_STATUS_IGNORE);
    } else {
      for(i=0; i<loop_count; i++){
        MPI_Recv(&aux, 0, MPI_CHAR, myId-1, 99, comm, MPI_STATUS_IGNORE);
      }
      MPI_Ssend(&aux, 0, MPI_CHAR, myId-1, 99, comm);
    }
    MPI_Barrier(comm);
    stop_time = MPI_Wtime();
    elapsed_time = (stop_time - start_time) / loop_count;
    
  }

  if(myId %2 != 0) {
    elapsed_time=0;
  }
  MPI_Allreduce(&elapsed_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, comm);
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

  if(myId+1 != numP || (myId+1 == numP && numP % 2 == 0)) {

    MPI_Barrier(comm);
    start_time = MPI_Wtime();
    if(myId % 2 == 0){
      for(i=0; i<loop_count; i++){
        MPI_Ssend(aux, n, MPI_CHAR, myId+1, 99, comm);
      }
      MPI_Recv(aux, 0, MPI_CHAR, myId+1, 99, comm, MPI_STATUS_IGNORE);
    } else {
      for(i=0; i<loop_count; i++){
        MPI_Recv(aux, n, MPI_CHAR, myId-1, 99, comm, MPI_STATUS_IGNORE);
      }
      MPI_Ssend(aux, 0, MPI_CHAR, myId-1, 99, comm);
    }
    MPI_Barrier(comm);
    stop_time = MPI_Wtime();
    elapsed_time = (stop_time - start_time) / loop_count;
  }

  if(myId %2 == 0) {
    time = elapsed_time - latency;
  }

  MPI_Allreduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, comm);
  bw = ((double)n_bytes) / max_time;
  free(aux);
  return bw;
}

/*
 *
 */
void linear_regression_stage(void *stage_void, void *group_void, MPI_Comm comm) {

  group_data group = *((group_data *) group_void);
  iter_stage_t *stage = (iter_stage_t *) stage_void;

  double *times = NULL;
  if(group.myId == ROOT) {
    times = malloc(LR_ARRAY_TAM * sizeof(double));
  }

  switch(stage->pt) {
    //Comunicaciones
    case COMP_BCAST:
      lr_times_bcast(group.myId, group.numP, ROOT, comm, times);
      if(group.myId == ROOT) {
        lr_compute(times, &(stage->slope), &(stage->intercept));
      }
      MPI_Bcast(&(stage->slope), 1, MPI_DOUBLE, ROOT, comm);
      MPI_Bcast(&(stage->intercept), 1, MPI_DOUBLE, ROOT, comm);
      break;
    case COMP_ALLGATHER:
      break;
    case COMP_REDUCE:
      break;
    case COMP_ALLREDUCE:
      break;
    default:
      break;
  }

  free(times);
}

/* 
 * Obatains for "Id" and "numP", how many
 * bytes will have process "Id" and returns
 * that quantity.
 *
 * Processes under "rem" will have more data
 * than those with ranks higher or equal to "rem".
 *
 * TODO Refactor: Ya existe esta funcion en malleability/CommDist.c
 */
void get_byte_dist(int qty, int id, int numP, int *result) {
  int rem, ini, fin, tamBl;

  tamBl = qty / numP;
  rem = qty % numP;

  if(id < rem) { // First subgroup
    ini = id * tamBl + id;
    fin = (id+1) * tamBl + (id+1);
  } else { // Second subgroup
    ini = id * tamBl + rem;
    fin = (id+1) * tamBl + rem;
  }
  
  if(fin > qty) {
    fin = qty;
  }
  if(ini > fin) {
    ini = fin;
  }

  *result= fin - ini;
}
