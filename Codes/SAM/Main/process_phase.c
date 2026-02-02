#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "computing_func.h"
#include "comunication_func.h"
#include "Main_datatypes.h"
#include "process_phase.h"
#include "process_stage.h"
#include "../MaM/MAM.h"

double iterate(phase_t *phase, double *time, double *time_stages, int rigid_times, group_data group, MPI_Comm comm);
double iterate_with_reconf(phase_t *phase, int state, results_data *results, int rigid_times, int actual_phase, group_data group, MPI_Comm comm);

double iterate_relaxed(phase_t *phase, double *time, double *times_stages, group_data group, MPI_Comm comm);
double iterate_rigid(phase_t *phase, double *time, double *times_stages, group_data group, MPI_Comm comm);

/*
 * Obtiene cuanto tiempo es necesario para realizar una operacion de PI
 *
 * Si compute esta a 1 se considera que se esta inicializando el entorno
 * y realizará trabajo extra.
 *
 * Si compute esta a 0 se considera un entorno inicializado y solo hay que
 * realizar algunos cambios de reserva de memoria. Si es necesario recalcular
 * algo se obtiene el total de tiempo utilizado en dichas tareas y se resta
 * al tiempo total de ejecucion.
 */
void init_phases(group_data *group, configuration *config_file, results_data *results, int compute, MPI_Comm comm) {
  size_t i, ii;
  double time = 0;
  phase_t *phase;

  for(i=0; i<config_file->n_phases; i++) {
    phase = config_file->phases + i;
    for(ii=0; ii<phase->qty_stages; ii++) {
      time+=init_stage(phase->stages+ii, phase, *group, comm, compute);
    }
  }
  if(!compute) {results->wasted_time += time;}
}


/*
 * Función de trabajo principal.
 *
 * Ejecuta iteraciones de la aplicación emulada con dos posibles salidas,
 * terminar todas las salidas del programa, o hacer todas las iteraciones
 * del grupo de procesos.
 *
 * Si sale por terminar la aplicación, se termina la ejecución.
 * Si sale por terminar el grupo, se tiene que llamar a work_reconf.
 */
int phase_normal(group_data *group, configuration *config_file, results_data *results, MPI_Comm comm) { 
  int exec_iters, max_iter, res;
  double *times_aux, **times_stages_aux;
  size_t recorded_iters, recorded_stages, phase_ind, iter;
  phase_t *phase;

  max_iter = (group->grp+1) < config_file->n_groups ? config_file->groups[group->grp].iters : -1;
  exec_iters = 0;
  res = 0;

  // Start arrays for recording times
  recorded_iters = (config_file->phases+group->actual_phase)->qty_iters;
  recorded_stages = (config_file->phases+group->actual_phase)->qty_stages;
  for(phase_ind = group->actual_phase; phase_ind < config_file->n_phases; phase_ind++) {
    if(recorded_iters < (config_file->phases+phase_ind)->qty_iters) { recorded_iters = (config_file->phases+phase_ind)->qty_iters; }
    if(recorded_stages < (config_file->phases+phase_ind)->qty_stages) { recorded_stages = (config_file->phases+phase_ind)->qty_stages; }
  }
  times_aux = malloc(recorded_iters * sizeof *times_aux);
  times_stages_aux = malloc(recorded_iters * sizeof *times_aux);
  for(size_t iter_ind = 0; iter_ind < recorded_iters; iter_ind++) {
    times_stages_aux[iter_ind] = malloc(recorded_stages * sizeof *(times_stages_aux[iter_ind]));
  }
  phase_ind = group->actual_phase;

  // Start work
  for(; group->actual_phase < config_file->n_phases; group->actual_phase++) {
    phase = config_file->phases+group->actual_phase;

    for(; group->actual_iter < phase->qty_iters; group->actual_iter++) {
      if(exec_iters == max_iter) {
        capture_m_iterations(results, group->actual_phase, group->actual_iter, times_aux, times_stages_aux);
        for(iter = 0; iter < recorded_iters; iter++) { free(times_stages_aux[iter]); }
        free(times_aux);
        free(times_stages_aux);
        group->actual_phase++;
        return res;
      }

      iterate(phase, times_aux+group->actual_iter, times_stages_aux[group->actual_iter], config_file->rigid_times, *group, comm);
      exec_iters++;
    }
    
    capture_m_iterations(results, group->actual_phase, phase->qty_iters, times_aux, times_stages_aux);
    group->actual_iter = 0;
  }

  for(iter = 0; iter < recorded_iters; iter++) { free(times_stages_aux[iter]); }
  free(times_aux);
  free(times_stages_aux);
  res=1;
  return res;
}

int phase_reconf(group_data *group, configuration *config_file, results_data *results, void (*callback)(void *), MPI_Comm comm) {
  int state, res;
  phase_t *phase;

  state = MAM_NOT_STARTED;
  res = 0;

  MAM_Checkpoint(&state, MAM_CHECK_COMPLETION, callback, NULL);
  if(MAM_COMPLETED == state) return res;

  for(; group->actual_phase < config_file->n_phases; group->actual_phase++) {
    phase = config_file->phases+group->actual_phase;
    for(; group->actual_iter < phase->qty_iters; group->actual_iter++) {
      
      iterate_with_reconf(phase, state, results, config_file->rigid_times, group->actual_phase, *group, comm);
      MAM_Checkpoint(&state, MAM_CHECK_COMPLETION, callback, NULL);
      if(MAM_COMPLETED == state) { return res; }
    }
    group->actual_iter = 0;
  }

  // Si no nada más que hacer, esperar a que termine Checkpoint
  if(MAM_COMPLETED != state) {
    MAM_Checkpoint(&state, MAM_WAIT_COMPLETION, callback, NULL);
  }

  res=1;
  return res;
}

/////////////////////////////////////////
/////////////////////////////////////////
//ITERATE FUNCTIONS
/////////////////////////////////////////
/////////////////////////////////////////

/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 * que dura al menos un tiempo determinado por la suma de todas las
 * etapas definidas en la configuracion.
 */
double iterate(phase_t *phase, double *time, double *time_stages, int rigid_times, group_data group, MPI_Comm comm) {
  double aux = 0;

  if(rigid_times) {
    aux = iterate_rigid(phase, time, time_stages, group, comm);
  } else {
    aux = iterate_relaxed(phase, time, time_stages, group, comm);
  }

  return aux;
}

/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 * que dura al menos un tiempo determinado por la suma de todas las
 * etapas definidas en la configuracion.
 * 
 * A diferencia, de iterate(...), se hace mientras una reconfiguración en 
 * segundo plano se esta realizando, por lo que hay que guardar los datos
 * en cada iteración.
 */
double iterate_with_reconf(phase_t *phase, int state, results_data *results, int rigid_times, int actual_phase, group_data group, MPI_Comm comm) {
  int is_async = 0;
  double time, *times_stages_aux;
  double aux = 0;

  times_stages_aux = malloc(phase->qty_stages * sizeof(double));

  if(rigid_times) {
    aux = iterate_rigid(phase, &time, times_stages_aux, group, comm);
  } else {
    aux = iterate_relaxed(phase, &time, times_stages_aux, group, comm);
  }

  // Se esta realizando una redistribucion de datos asincrona
  if(MAM_PENDING == state || MAM_USER_PENDING == state) { is_async = 1; }
  capture_iteration(results, actual_phase, is_async, time, times_stages_aux);
  free(times_stages_aux);

  return aux;
}

/*
 * Performs an iteration. The gathered times for iterations
 * and stages could be IMPRECISE in order to ensure the 
 * global execution time is precise.
 */
double iterate_relaxed(phase_t *phase, double *time, double *times_stages, group_data group, MPI_Comm comm) {
  size_t i;
  double start_time, start_time_stage, aux=0;
  start_time = MPI_Wtime(); // Imprecise timings

  for(i=0; i < phase->qty_stages; i++) {
    start_time_stage = MPI_Wtime(); 
    aux+= process_stage(phase->stages[i], group, comm);
    times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  *time = MPI_Wtime() - start_time;
  return aux;
}

/*
 * Performs an iteration. The gathered times for iterations
 * and stages are ensured to be precise but the global 
 * execution time could be imprecise.
 */
double iterate_rigid(phase_t *phase, double *time, double *times_stages, group_data group, MPI_Comm comm) {
  size_t i;
  double start_time, start_time_stage, aux=0;

  MPI_Barrier(comm);
  start_time = MPI_Wtime();

  for(i=0; i < phase->qty_stages; i++) {
    start_time_stage = MPI_Wtime();
    aux+= process_stage(phase->stages[i], group, comm);
    MPI_Barrier(comm);
    times_stages[i] = MPI_Wtime() - start_time_stage;
  }

  MPI_Barrier(comm);
  *time = MPI_Wtime() - start_time; // Guardar tiempos
  return aux;
}