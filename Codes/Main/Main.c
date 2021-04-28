#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../IOcodes/read_ini.h"
#include "../IOcodes/results.h"
#include "../malleability/ProcessDist.h"
#include "../malleability/CommDist.h"

#define ROOT 0

int work();
void Sons_init();

int checkpoint(int iter, int state, MPI_Request **comm_req);
void TC(int numS);
int start_redistribution(int numS, MPI_Request **comm_req);
int check_redistribution(int iter, MPI_Request **comm_req);

void iterate(double *matrix, int n, int async_comm);
void computeMatrix(double *matrix, int n);
void initMatrix(double **matrix, int n);

void print_general_info(int myId, int grp, int numP);

typedef struct {
  int myId;
  int numP;
  int grp;
  int iter_start;

  MPI_Comm children, parents;
  char **argv;
  char *sync_array, *async_array;
} group_data;

configuration *config_file;
group_data *group;
results_data *results;

int main(int argc, char *argv[]) {
    int numP, myId;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);

    group = malloc(1 * sizeof(group_data));
    group->myId        = myId;
    group->numP        = numP;
    group->grp         = 0;
    group->iter_start  = 0;
    group->argv        = argv;

    MPI_Comm_get_parent(&(group->parents));
    if(group->parents != MPI_COMM_NULL ) { // Si son procesos hijos deben comunicarse con las padres
      Sons_init();

    } else { // Si son el primer grupo de procesos, recogen la configuracion inicial
      config_file = read_ini_file(argv[1]);
      init_results_data(&results, config_file->resizes - 1, config_file->iters[group->grp]);
      if(config_file->sdr > 0) {
        malloc_comm_array(&(group->sync_array), config_file->sdr , group->myId, group->numP);
      }
      if(config_file->adr > 0) {
        malloc_comm_array(&(group->async_array), config_file->adr , group->myId, group->numP);
      }
    }


    //if(myId== ROOT) print_config(config_file, group->grp);
    work();

    if(group->myId == ROOT) { // Print results
      print_config_group(config_file, group->grp);
      print_iter_results(results, config_file->iters[group->grp] -1);
    }


    if(config_file->sdr > 0) {
      free(group->sync_array);
    }
    if(config_file->adr > 0) {
      free(group->async_array);
    }
    free(group);
    free_config(config_file);
    free_results_data(&results); //FIXME Provoca un error - Entro mal a algun vector??
    
    MPI_Finalize();
    return 0;
}

/*
 * Función de trabajo principal.
 *
 * Incializa los datos para realizar el computo y a continuacion
 * pasa a realizar "maxiter" iteraciones de computo.
 *
 * Terminadas las iteraciones realiza el redimensionado de procesos.
 * Si el redimensionado se realiza de forma asincrona se 
 * siguen realizando iteraciones de computo hasta que termine la 
 * comunicacion asincrona y realizar entonces la sincrona.
 */
int work() {
  int iter, maxiter, state;
  double *matrix;
  MPI_Request *async_comm;

  maxiter = config_file->iters[group->grp];
  initMatrix(&matrix, config_file->matrix_tam);
  state = MAL_COMM_UNINITIALIZED;

  for(iter=group->iter_start; iter < maxiter; iter++) {
    iterate(matrix, config_file->matrix_tam, state);
  }
  state = checkpoint(iter, state, &async_comm);
  
  iter = 0;
  while(state == MAL_ASYNC_PENDING) {
    iterate(matrix, config_file->matrix_tam, state);
    iter++;
    state = checkpoint(iter, state, &async_comm);
  }
  
  return 0;
}

/*
 * Se realiza el redimensionado de procesos por parte de los padres.
 *
 * Se crean los nuevos procesos con la distribucion fisica elegida y
 * a continuacion se transmite la informacion a los mismos.
 *
 * Si hay datos asincronos a transmitir, primero se comienza a
 * transmitir estos y se termina la funcion. Se tiene que comprobar con
 * llamando a la función de nuevo que se han terminado de enviar
 *
 * Si hay ademas datos sincronos a enviar, no se envian aun.
 *
 * Si solo hay datos sincronos se envian tras la creacion de los procesos
 * y finalmente se desconectan los dos grupos de procesos.
 */
int checkpoint(int iter, int state, MPI_Request **comm_req) {
  
  if(state == MAL_COMM_UNINITIALIZED) {
    // Comprobar si se tiene que realizar un redimensionado
    if(config_file->iters[group->grp] > iter || config_file->resizes == group->grp + 1) {return MAL_COMM_UNINITIALIZED;}

    int numS = config_file->procs[group->grp +1];

      results->spawn_start = MPI_Wtime();
    TC(numS);
      results->spawn_time[group->grp + 1] = MPI_Wtime() - results->spawn_start;

    state = start_redistribution(numS, comm_req);

  } else if(MAL_ASYNC_PENDING) {
    state = check_redistribution(iter, comm_req);
  }

  return state;
}

/*
 * Se encarga de realizar la creacion de los procesos hijos.
 */
void TC(int numS){
  // Inicialización de la comunicación con SLURM
  int dist = config_file->phy_dist[group->grp +1];
  init_slurm_comm(group->argv, group->myId, numS, ROOT, dist, COMM_SPAWN_SERIAL);

  // Esperar a que la comunicación y creación de procesos
  // haya finalizado
  int test = -1;
  while(test != MPI_SUCCESS) {
    test = check_slurm_comm(group->myId, ROOT, MPI_COMM_WORLD, &(group->children));
  }
}

int start_redistribution(int numS, MPI_Request **comm_req) {
  int rootBcast = MPI_PROC_NULL;
  if(group->myId == ROOT) rootBcast = MPI_ROOT;

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, rootBcast, group->children);
  send_config_file(config_file, rootBcast, group->children);

  if(config_file->adr > 0) {
    results->async_start = MPI_Wtime();
    send_async(group->async_array, config_file->adr, group->myId, group->numP, ROOT, group->children, numS, comm_req, config_file->aib);
    return MAL_ASYNC_PENDING;
  } 
  if(config_file->sdr > 0) {
      results->sync_start = MPI_Wtime();
    send_sync(group->sync_array, config_file->sdr, group->myId, group->numP, ROOT, group->children, numS);
  }

  
  send_results(results, rootBcast, group->children);
  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->children));

  return MAL_COMM_COMPLETED;
}

int check_redistribution(int iter, MPI_Request **comm_req) {
  int completed, all_completed, test_err, iter_send;
  int numS = config_file->procs[group->grp +1];
  int rootBcast = MPI_PROC_NULL;
  MPI_Request *req_completed;
  if(group->myId == ROOT) rootBcast = MPI_ROOT;

  if(config_file->aib == MAL_USE_NORMAL) {
    req_completed = &(*comm_req)[0];
  } else { // MAL_USE_IBARRIER
    req_completed = &(*comm_req)[1];
  }
 
  
  test_err = MPI_Test(req_completed, &completed, MPI_STATUS_IGNORE);
  if (test_err != MPI_SUCCESS && test_err != MPI_ERR_PENDING) {
    printf("P%d aborting -- Test Async\n", group->myId);
    MPI_Abort(MPI_COMM_WORLD, test_err);
  }

  MPI_Allreduce(&completed, &all_completed, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
  if(!all_completed) return MAL_ASYNC_PENDING; // Continue only if asynchronous send has ended 

  //MPI_Wait(req_completed, MPI_STATUS_IGNORE);
  if(config_file->aib == MAL_USE_IBARRIER) {
    MPI_Wait(&(*comm_req)[0], MPI_STATUS_IGNORE); // Indicar como completado el envio asincrono
  }
  
  iter_send = iter;
  MPI_Bcast(&iter_send, 1, MPI_INT, rootBcast, group->children);
  if(config_file->sdr > 0) { // Realizar envio sincrono
      results->sync_start = MPI_Wtime();
    send_sync(group->sync_array, config_file->sdr, group->myId, group->numP, ROOT, group->children, numS);
  }
  send_results(results, rootBcast, group->children);

  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->children));
  free(*comm_req);
  return MAL_COMM_COMPLETED;
}

/*
 * Inicializacion de los datos de los hijos.
 * En la misma se reciben datos de los padres: La configuracion
 * de la ejecucion a realizar; y los datos a recibir de los padres
 * ya sea de forma sincrona, asincrona o ambas.
 */
void Sons_init() {

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, ROOT, group->parents);
  group->grp++;

  config_file = recv_config_file(ROOT, group->parents);
  int numP_parents = config_file->procs[group->grp -1];
  init_results_data(&results, config_file->resizes - 1, config_file->iters[group->grp]);

  if(config_file->adr > 0) { // Recibir datos asincronos
    recv_async(&(group->async_array), config_file->adr, group->myId, group->numP, ROOT, group->parents, numP_parents, config_file->aib);
    MPI_Bcast(&(group->iter_start), 1, MPI_INT, ROOT, group->parents);
    results->async_time[group->grp] = MPI_Wtime();
  }
  if(config_file->sdr > 0) { // Recibir datos sincronos
    recv_sync(&(group->sync_array), config_file->sdr, group->myId, group->numP, ROOT, group->parents, numP_parents);
    results->sync_time[group->grp] = MPI_Wtime();
  }
  recv_results(results, ROOT, group->parents);
  results->sync_time[group->grp]  = MPI_Wtime() - results->sync_start;
  results->async_time[group->grp] = MPI_Wtime() - results->async_start;

  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->parents));
}


/////////////////////////////////////////
/////////////////////////////////////////
//COMPUTE FUNCTIONS
/////////////////////////////////////////
/////////////////////////////////////////


/*
 * Simula la ejecucción de una iteración de computo en la aplicación
 * que dura al menos un tiempo de "time" segundos.
 */
void iterate(double *matrix, int n, int async_comm) {
  double start_time, actual_time;
  double time = config_file->general_time * config_file->factors[group->grp];
  int i, operations = 0;

  start_time = actual_time = MPI_Wtime();
  if(async_comm == MAL_ASYNC_PENDING) { // Se esta realizando una redistribucion de datos asincrona
    operations = results->iters_type[config_file->iters[group->grp] - 1];
    for (i=0; i<operations; i++) {
      computeMatrix(matrix, n);
      actual_time = MPI_Wtime(); // Guardar tiempos
    }
    operations = 0;

  } else { // No hay redistribucion de datos actualmente	  
    while (actual_time - start_time < time) {
      computeMatrix(matrix, n);
      operations++;
      actual_time = MPI_Wtime(); // Guardar tiempos
    }
  }

  results->iters_time[results->iter_index] = actual_time - start_time;
  results->iters_type[results->iter_index] = operations;
  results->iter_index = results->iter_index + 1;
}

/*
 * Realiza una multiplicación de matrices de tamaño n
 */
void computeMatrix(double *matrix, int n) {
  int row, col, i, aux;

  for(row=0; i<n; row++) {
    /* COMPUTE */
    for(col=0; col<n; col++) {
      aux=0;
      for(i=0; i<n; i++) {
        aux += matrix[row*n + i] * matrix[i*n + col];
      }
    }
  }
}

/*
 * Init matrix
 */
void initMatrix(double **matrix, int n) {
  int i, j;

  // Init matrix
  if(matrix != NULL) {
    *matrix = malloc(n * n * sizeof(double));
    if(*matrix == NULL) { MPI_Abort(MPI_COMM_WORLD, -1);}
    for(i=0; i < n; i++) {
      for(j=0; j < n; j++) {
        (*matrix)[i*n + j] = i+j;
      }
    }
  }
}

//======================================================||
//======================================================||
//=============???????¿¿¿¿¿¿¿¿ FUNCTIONS================||
//======================================================||
//======================================================||

void print_general_info(int myId, int grp, int numP) {
  int len;
  char *name = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  char *version = malloc(MPI_MAX_LIBRARY_VERSION_STRING * sizeof(char));
  MPI_Get_processor_name(name, &len);
  MPI_Get_library_version(version, &len);
  printf("P%d Nuevo GRUPO %d de %d procs en nodo %s con %s\n", myId, grp, numP, name, version);

  free(name);
  free(version);
}


