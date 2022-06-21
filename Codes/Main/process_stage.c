#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "Main_datatypes.h"
#include "process_stage.h"
#include "../malleability/malleabilityManager.h" //FIXME Refactor

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
 */
void init_stage(void *config_file_void, int stage, void *group_void, MPI_Comm comm, int compute) {
  double result, start_time, t_stage;
  int qty = 20000;

  group_data group = *((group_data *) group_void);
  configuration config_file = *((configuration *) config_file_void);
  config_file.iter_stage[stage].operations = qty;
  t_stage = config_file.iter_stage[stage].t_stage * config_file.factors[group.grp];


  if(config_file.iter_stage[stage].bytes == 0) {
    config_file.iter_stage[stage].bytes = (t_stage - config_file.latency_m) * config_file.bw_m;
  } else {
    //config_file.iter_stage[stage].bytes = config_file.iter_stage[stage].bytes;
  }

  start_time = MPI_Wtime();
  result = 0;
  switch(config_file.iter_stage[stage].pt) {
    //Computo
    case COMP_MATRIX:
      initMatrix(&(config_file.iter_stage[stage].double_array), config_file.matrix_tam);
    case COMP_PI:
      if(group.myId == ROOT && compute) {
        result+= process_stage(config_file_void, stage, group_void, comm);
      }
        break;
    //Comunicación
    case COMP_POINT:
      if(config_file.iter_stage[stage].array != NULL)
        free(config_file.iter_stage[stage].array);
      config_file.iter_stage[stage].array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes);
      break;
    case COMP_BCAST:
      if(config_file.iter_stage[stage].array != NULL)
        free(config_file.iter_stage[stage].array);
      config_file.iter_stage[stage].array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes);
      break;
    case COMP_ALLTOALL:
      if(config_file.iter_stage[stage].array != NULL)
        free(config_file.iter_stage[stage].array);
      config_file.iter_stage[stage].array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes);
      if(config_file.iter_stage[stage].full_array != NULL)
        free(config_file.iter_stage[stage].full_array);
      config_file.iter_stage[stage].full_array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes * group.numP);
      break;
    case COMP_REDUCE:
      if(config_file.iter_stage[stage].array != NULL)
        free(config_file.iter_stage[stage].array);
      config_file.iter_stage[stage].array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes);
      //Full array para el reduce necesita el mismo tamanyo
      if(config_file.iter_stage[stage].full_array != NULL)
        free(config_file.iter_stage[stage].full_array);
      config_file.iter_stage[stage].full_array = malloc(sizeof(char) * config_file.iter_stage[stage].bytes);
      break;
  }
  if(compute) {
    config_file.iter_stage[stage].t_op = (MPI_Wtime() - start_time) / qty; //Tiempo de una operacion
    MPI_Bcast(&(config_file.iter_stage[stage].t_op), 1, MPI_DOUBLE, ROOT, comm);
  }
  config_file.iter_stage[stage].operations = t_stage / config_file.iter_stage[stage].t_op;
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
      point_to_point(group.myId, group.numP, ROOT, comm, stage_data.array, stage_data.bytes);
      break;
    case COMP_BCAST:
      MPI_Bcast(stage_data.array, stage_data.bytes, MPI_CHAR, ROOT, comm);
      break;
    case COMP_ALLTOALL:
      MPI_Alltoall(stage_data.array, stage_data.bytes, MPI_CHAR, stage_data.full_array, stage_data.bytes, MPI_CHAR, comm);
      break;
    case COMP_REDUCE:
      MPI_Reduce(stage_data.array, stage_data.full_array, stage_data.bytes, MPI_CHAR, MPI_MAX, ROOT, comm);
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
    for(i=0; i<loop_count; i++){
    
      MPI_Barrier(comm);
      start_time = MPI_Wtime();
      if(myId % 2 == 0){
        MPI_Ssend(&aux, 1, MPI_CHAR, myId+1, 99, comm);
        MPI_Recv(&aux, 1, MPI_CHAR, myId+1, 99, comm, MPI_STATUS_IGNORE);
      }
      else if(myId % 2 == 1){
        MPI_Recv(&aux, 1, MPI_CHAR, myId-1, 99, comm, MPI_STATUS_IGNORE);
        MPI_Ssend(&aux, 1, MPI_CHAR, myId-1, 99, comm);
      }
      MPI_Barrier(comm);
      stop_time = MPI_Wtime();
      elapsed_time += stop_time - start_time;
    }
  }

  if(myId %2 == 0) {
    elapsed_time/=loop_count;
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

  if(myId+1 != numP || (myId+1 == numP && numP % 2 == 0)) {
    for(i=0; i<loop_count; i++){
    
      MPI_Barrier(comm);
      start_time = MPI_Wtime();
      if(myId %2 == 0){
        MPI_Ssend(aux, n, MPI_CHAR, myId+1, 99, comm);
        MPI_Recv(aux, n, MPI_CHAR, myId+1, 99, comm, MPI_STATUS_IGNORE);
      }
      else if(myId %2 == 1){
        MPI_Recv(aux, n, MPI_CHAR, myId-1, 99, comm, MPI_STATUS_IGNORE);
        MPI_Ssend(aux, n, MPI_CHAR, myId-1, 99, comm);
      }
      MPI_Barrier(comm);
      stop_time = MPI_Wtime();
      elapsed_time += stop_time - start_time;
    }
  }

  if(myId %2 == 0) {
    time = elapsed_time / loop_count - latency;
  }
  MPI_Allreduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, comm);
  bw = ((double)n_bytes * 2) / max_time;
  return bw;
}
