#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "Main_datatypes.h"
#include "process_stage.h"
#include "../malleability/distribution_methods/block_distribution.h"

double init_emulation_comm_time(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);
double init_emulation_icomm_time(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm);

double init_matrix_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_pi_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_ptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_iptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_bcast_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_allgatherv_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_reduce_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute);
double init_comm_wait_pt(configuration *config_file, iter_stage_t *stage);

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
      break;
    case COMP_PI:
      result = init_pi_pt(group, config_file, stage, comm, compute);
      break;

    //Comunicación
    case COMP_POINT:
      result = init_comm_ptop_pt(group, config_file, stage, comm, compute);
      break;
    case COMP_IPOINT:
      result = init_comm_iptop_pt(group, config_file, stage, comm, compute);
      break;
    case COMP_BCAST:
      result = init_comm_bcast_pt(group, config_file, stage, comm, compute);
      break;
    case COMP_ALLGATHER:
      result = init_comm_allgatherv_pt(group, config_file, stage, comm, compute);
      break;
    case COMP_REDUCE:
    case COMP_ALLREDUCE:
      result = init_comm_reduce_pt(group, config_file, stage, comm, compute);
      break;
    case COMP_WAIT:
      result = init_comm_wait_pt(config_file, stage);
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
  int i=0;
  double result, t_start, t_total;
  t_start = MPI_Wtime();
  t_total = 0;

  switch(stage.pt) {
    //Computo
    case COMP_PI:
      for(i=0; i < stage.operations; i++) {
        result += computePiSerial(config_file.granularity);
      }
      break;
    case COMP_MATRIX:
      for(i=0; i < stage.operations; i++) {
        result += computeMatrix(stage.double_array, config_file.granularity);
      } 
      break;
    //Comunicaciones
    case COMP_POINT:
      if(stage.t_capped) {
        while(t_total < stage.t_stage) {
          point_to_point_inter(group.myId, group.numP, comm, stage.array, stage.full_array, stage.real_bytes);
	  t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
      } else {
        for(i=0; i < stage.operations; i++) {
          point_to_point_inter(group.myId, group.numP, comm, stage.array, stage.full_array, stage.real_bytes);
	}
      }
      break;
    case COMP_IPOINT:
      for(i=0; i < stage.operations; i++) {
        point_to_point_asynch_inter(group.myId, group.numP, comm, stage.array, stage.full_array, stage.real_bytes, &(stage.reqs[i*2])); //FIXME Magical number
      }
      break;
      
    case COMP_BCAST:
      if(stage.t_capped) {
        while(t_total < stage.t_stage) {
          MPI_Bcast(stage.array, stage.real_bytes, MPI_CHAR, ROOT, comm);
	  t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
      } else {
        for(i=0; i < stage.operations; i++) {
          MPI_Bcast(stage.array, stage.real_bytes, MPI_CHAR, ROOT, comm);
	}
      }
      break;
    case COMP_ALLGATHER:
      if(stage.t_capped) {
	while(t_total < stage.t_stage) {
          MPI_Allgatherv(stage.array, stage.my_bytes, MPI_CHAR, stage.full_array, stage.counts.counts, stage.counts.displs, MPI_CHAR, comm);
	  t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
      } else {
        for(i=0; i < stage.operations; i++) {
          MPI_Allgatherv(stage.array, stage.my_bytes, MPI_CHAR, stage.full_array, stage.counts.counts, stage.counts.displs, MPI_CHAR, comm);
	}
      }
      break;
    case COMP_REDUCE:
      if(stage.t_capped) {
        while(t_total < stage.t_stage) {
          MPI_Reduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, ROOT, comm);
	  t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
      } else {
        for(i=0; i < stage.operations; i++) {
          MPI_Reduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, ROOT, comm);
	}
      }
      break;
    case COMP_ALLREDUCE:
      if(stage.t_capped) {
        while(t_total < stage.t_stage) {
          MPI_Allreduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, comm);
	  t_total = MPI_Wtime() - t_start;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
      } else {
        for(i=0; i < stage.operations; i++) {
          MPI_Allreduce(stage.array, stage.full_array, stage.real_bytes, MPI_CHAR, MPI_MAX, comm);
	}
      }
      break;
    case COMP_WAIT:
      if(stage.t_capped) { //FIXME Right now, COMP_WAIT with t_capped only works for P2P comms
	int remaining;
	i = 0;
        while(t_total < stage.t_stage) {
          MPI_Waitall(2, &(stage.reqs[i*2]), MPI_STATUSES_IGNORE); //FIXME Magical number
	  t_total = MPI_Wtime() - t_start;
	  i++;
          MPI_Bcast(&t_total, 1, MPI_DOUBLE, ROOT, comm);
	}
	remaining = stage.operations - i;
	if (remaining) {
  	  for(; i < stage.operations; i++) { 
            MPI_Cancel(&(stage.reqs[i*2])); //FIXME Magical number
            MPI_Cancel(&(stage.reqs[i*2+1])); //FIXME Magical number
  	  }
          MPI_Waitall(remaining*2, &(stage.reqs[(stage.operations-remaining)*2]), MPI_STATUSES_IGNORE); //FIXME Magical number
	}
      } else {
        MPI_Waitall(stage.req_count, stage.reqs, MPI_STATUSES_IGNORE);
      }
      break;
  }
  return result;
}

/*
 * ========================================================================================
 * ========================================================================================
 * =================================INIT STAGE FUNCTIONS===================================
 * ========================================================================================
 * ========================================================================================
*/
double init_emulation_comm_time(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  double start_time, end_time, time = 0;
  double t_stage;

  MPI_Barrier(comm);
  start_time = MPI_Wtime();
  process_stage(*config_file, *stage, group, comm);
  MPI_Barrier(comm);
  end_time = MPI_Wtime();
  stage->t_op = (end_time - start_time) / stage->operations; //Tiempo de una operacion
  t_stage = stage->t_stage * config_file->groups[group.grp].factor;
  stage->operations = ceil(t_stage / stage->t_op);
  MPI_Bcast(&(stage->operations), 1, MPI_INT, ROOT, comm);

  return time;
}

double init_emulation_icomm_time(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm) {
  double start_time, end_time, time = 0;
  double t_stage;
  iter_stage_t wait_stage;
  wait_stage.pt = COMP_WAIT;
  wait_stage.id = stage->id;
  wait_stage.operations = stage->operations;
  wait_stage.req_count = stage->req_count;
  wait_stage.reqs = stage->reqs;

  MPI_Barrier(comm);
  start_time = MPI_Wtime();
  process_stage(*config_file, *stage, group, comm);
  process_stage(*config_file, wait_stage, group, comm);
  MPI_Barrier(comm);
  end_time = MPI_Wtime();
  stage->t_op = (end_time - start_time) / stage->operations; //Tiempo de una operacion
  t_stage = stage->t_stage * config_file->groups[group.grp].factor;
  stage->operations = ceil(t_stage / stage->t_op);
  MPI_Bcast(&(stage->operations), 1, MPI_INT, ROOT, comm);

  return time;
}

double init_matrix_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double result, t_stage, start_time;

  result = 0;
  t_stage = stage->t_stage * config_file->groups[group.grp].factor;
  initMatrix(&(stage->double_array), config_file->granularity);

  if(compute) {
    if(group.myId == ROOT) {
      start_time = MPI_Wtime();
      result+= process_stage(*config_file, *stage, group, comm);
      stage->t_op = (MPI_Wtime() - start_time) / stage->operations; //Tiempo de una operacion
      stage->operations = ceil(t_stage / stage->t_op);
    }
    MPI_Bcast(&(stage->operations), 1, MPI_INT, ROOT, comm);
  } else {
    stage->operations = ceil(t_stage / stage->t_op);
  }

  return result;
}

double init_pi_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double result, t_stage, start_time;

  result = 0;
  t_stage = stage->t_stage * config_file->groups[group.grp].factor;
  if(compute) {
    if(group.myId == ROOT) {
      start_time = MPI_Wtime();
      result+= process_stage(*config_file, *stage, group, comm);
      stage->t_op = (MPI_Wtime() - start_time) / stage->operations; //Tiempo de una operacion
      stage->operations = ceil(t_stage / stage->t_op);
    }
    MPI_Bcast(&(stage->operations), 1, MPI_INT, ROOT, comm);
  } else {
    stage->operations = ceil(t_stage / stage->t_op);
  }

  return result;
}

double init_comm_ptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double time = 0;
  if(stage->array != NULL)
    free(stage->array);
  if(stage->full_array != NULL)
    free(stage->full_array);

  stage->real_bytes = (stage->bytes && !stage->t_capped) ? stage->bytes : config_file->granularity;
  stage->array = malloc(stage->real_bytes * sizeof(char));
  stage->full_array = malloc(stage->real_bytes * sizeof(char));

  if(compute && !stage->bytes && !stage->t_capped) {
    time = init_emulation_comm_time(group, config_file, stage, comm);
  } else {
    stage->operations = 1;
  }
  return time;
}

double init_comm_iptop_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  int i;
  double time = 0;
  if(stage->array != NULL)
    free(stage->array);
  if(stage->full_array != NULL)
    free(stage->full_array);
  if(stage->reqs != NULL) //FIXME May be erroneous if request are active...
    free(stage->reqs);

  stage->real_bytes = (stage->bytes && !stage->t_capped) ? stage->bytes : config_file->granularity;
  stage->array = malloc(stage->real_bytes * sizeof(char));
  stage->full_array = malloc(stage->real_bytes * sizeof(char));

  if(compute && !stage->bytes) { // t_capped is not considered in this case
    stage->req_count = 2 * stage->operations; //FIXME Magical number
    stage->reqs = (MPI_Request *) malloc(stage->req_count * sizeof(MPI_Request));
    time = init_emulation_icomm_time(group, config_file, stage, comm);
    free(stage->reqs);
  } else {
    stage->operations = 1;
  }
  stage->req_count = 2 * stage->operations; //FIXME Magical number
  stage->reqs = (MPI_Request *) malloc(stage->req_count * sizeof(MPI_Request));
  for(i=0; i < stage->req_count; i++) {
    stage->reqs[i] = MPI_REQUEST_NULL;
  }

  return time;
}

// TODO Compute should be always 1 if the number of processes is different
double init_comm_bcast_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double time = 0;
  if(stage->array != NULL)
    free(stage->array);

  stage->real_bytes = (stage->bytes && !stage->t_capped) ? stage->bytes : config_file->granularity;
  stage->array = malloc(stage->real_bytes * sizeof(char)); //FIXME Valgrind indica unitialised

  if(compute && !stage->bytes && !stage->t_capped) {
    time = init_emulation_comm_time(group, config_file, stage, comm);
  } else {
    stage->operations = 1;
  }
  return time;
}

// TODO Compute should be always 1 if the number of processes is different
double init_comm_allgatherv_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double time=0;
  struct Dist_data dist_data;

  if(stage->array != NULL)
    free(stage->array);
  if(stage->counts.counts != NULL)
    freeCounts(&(stage->counts));
  if(stage->full_array != NULL)
    free(stage->full_array);

  stage->real_bytes = (stage->bytes && !stage->t_capped) ? stage->bytes : config_file->granularity;

  prepare_comm_allgatherv(group.numP, stage->real_bytes, &(stage->counts));
      
  get_block_dist(stage->real_bytes, group.myId, group.numP, &dist_data);
  stage->my_bytes = dist_data.tamBl;

  stage->array = malloc(stage->my_bytes * sizeof(char));
  stage->full_array = malloc(stage->real_bytes * sizeof(char));

  if(compute && !stage->bytes && !stage->t_capped) {
    time = init_emulation_comm_time(group, config_file, stage, comm);
  } else {
    stage->operations = 1;
  }

  return time;
}

// TODO Compute should be always 1 if the number of processes is different
double init_comm_reduce_pt(group_data group, configuration *config_file, iter_stage_t *stage, MPI_Comm comm, int compute) {
  double time = 0;
  if(stage->array != NULL)
    free(stage->array);
  if(stage->full_array != NULL)
    free(stage->full_array);

  stage->real_bytes = (stage->bytes && !stage->t_capped) ? stage->bytes : config_file->granularity;
  stage->array = malloc(stage->real_bytes * sizeof(char));
  //Full array para el reduce necesita el mismo tamanyo
  stage->full_array = malloc(stage->real_bytes * sizeof(char));

  if(compute && !stage->bytes && !stage->t_capped) {
    time = init_emulation_comm_time(group, config_file, stage, comm);
  } else {
    stage->operations = 1;
  }

  return time;
}


double init_comm_wait_pt(configuration *config_file, iter_stage_t *stage) {
  size_t i;
  double time = 0;
  iter_stage_t aux_stage;

  if(stage->id < 0) { 
    printf("Error when initializing wait stage. Id is negative\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  for(i=0; i<config_file->n_stages; i++) {
    aux_stage = config_file->stages[i];
    if(aux_stage.id == stage->id) { break; }
  }
  if(i == config_file->n_stages) { 
    printf("Error when initializing wait stage. Not found a corresponding id\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }

  stage->req_count = aux_stage.req_count;
  stage->reqs = aux_stage.reqs;

  return time;
}
