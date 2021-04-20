#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "../IOcodes/read_ini.h"
#include "../malleability/ProcessDist.h"
#include "../malleability/CommDist.h"

#define ROOT 0

int work();
void Sons_init();

int checkpoint(int iter);
void TC(int numS);

void iterate(double *matrix, int n);
void computeMatrix(double *matrix, int n);
void initMatrix(double **matrix, int n);

typedef struct {
  int myId;
  int numP;
  int grp;


  MPI_Comm children, parents;
  char **argv;
  char *sync_array;
} group_data;

configuration *config_file;
group_data *group;

int main(int argc, char *argv[]) {
    int numP, myId;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numP);
    MPI_Comm_rank(MPI_COMM_WORLD, &myId);

    group = malloc(1 * sizeof(group_data));
    group->myId = myId;
    group->numP = numP;
    group->grp  = 0;
    group->argv = argv;


    MPI_Comm_get_parent(&(group->parents));
    if(group->parents != MPI_COMM_NULL ) { // Si son procesos hijos deben recoger la distribucion
      Sons_init();
    } else {
      config_file = read_ini_file(argv[1]);
      if(config_file->sdr > 0) {
        malloc_comm_array(&(group->sync_array), config_file->sdr , group->myId, group->numP);
	printf("Vector reservado por padres\n");
	fflush(stdout);
      }
    }

    if(myId== ROOT) print_config(config_file);
    int res = work();

    if(res) { // Ultimo set de procesos comprueba resultados
	    //RESULTADOS
    }

    free_config(config_file);
    free(group->sync_array);
    free(group);



    MPI_Finalize();
    return 0;
}

/*
 * Bucle de computo principal
 */
int work() {
  int iter, maxiter;
  double *matrix;

  maxiter = config_file->iters[group->grp];
  initMatrix(&matrix, config_file->matrix_tam);

  for(iter=0; iter < maxiter; iter++) {
    iterate(matrix, config_file->matrix_tam);
  }

  checkpoint(iter);

  return 0;
}

int checkpoint(int iter) {

  // Comprobar si se tiene que realizar un redimensionado
  if(config_file->iters[group->grp] < iter || group->grp!= 0) {return 0;}

  int numS = config_file->procs[group->grp +1];

  TC(numS);

  int rootBcast = MPI_PROC_NULL;
  if(group->myId == ROOT) rootBcast = MPI_ROOT;

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, rootBcast, group->children);

  send_config_file(config_file, rootBcast, group->children);

  if(config_file->sdr > 0) {
    send_sync(group->sync_array, config_file->sdr, group->myId, group->numP, ROOT, group->children, numS);
  }

  // Desconectar intercomunicador con los hijos
  MPI_Comm_disconnect(&(group->children));
  //MPI_Comm_free(&(group->children));

  return 1;
}

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


void Sons_init() {

  // Enviar a los hijos que grupo de procesos son
  MPI_Bcast(&(group->grp), 1, MPI_INT, ROOT, group->parents);
  group->grp++;


  config_file = recv_config_file(ROOT, group->parents);
  int numP_parents = config_file->procs[group->grp -1];

  if(config_file->sdr > 0) {
    recv_sync(&(group->sync_array), config_file->sdr, group->myId, group->numP, ROOT, group->parents, numP_parents);
    group->sync_array = malloc(5);
  }

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
 */
void iterate(double *matrix, int n) {
  double start_time, actual_time;
  double time = config_file->general_time * config_file->factors[group->grp];

  start_time = actual_time = MPI_Wtime();
  while (actual_time - start_time < time) {
    computeMatrix(matrix, n);
    actual_time = MPI_Wtime();
  }
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
