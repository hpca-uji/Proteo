#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mpi.h>
#include <pthread.h>
#include <math.h>
#include <string.h>
#include <slurm/slurm.h>
#include "ProcessDist.h"

/*
 * ESTE CODIGO ES PARA COMPROBAR EL FUNCIONAMIENTO DEL FICHERO ProcessDist.h
 * NO TIENE QUE VER CON EL BENCHMARK DE MALEABILIDAD
 */

#define ROOT 0
#define MAXGRP 3

#define TYPE_D 1
// 1 Es nodos
// 2 Es por nucleos

// Función para crear un fichero con el formato GxNPyIDz.o{jobId}.
// El proceso que llama a la función pasa a tener como salida estandar
// dicho fichero.
int create_out_file(int myId, int numP, int grp, char *jobId);
int create_out_file(int myId, int numP, int grp, char *jobId) {
  int ptr, err;
  char *file_name;

  file_name = NULL;
  file_name = malloc(40 * sizeof(char));
  if(file_name == NULL) return -1; // No ha sido posible alojar la memoria
  err = snprintf(file_name, 40, "G%dNP%dID%d.o%s", grp, numP, myId, jobId);
  if(err < 0) return -2; // No ha sido posible obtener el nombre de fichero

  ptr = open(file_name, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if(ptr < 0) return -3; // No ha sido posible crear el fichero
  err = close(1);
  if(err < 0) return -4; // No es posible modificar la salida estandar
  err = dup(ptr);
  if(err < 0) return -4; // No es posible modificar la salida estandar

  return 0;
}

// Se realizan varios tests de ancho de banda
// al mandar N datos a los procesos impares desde el
// par inmediatamente anterior. Tras esto, los impares
// vuelven a enviar los N datos al proceso par.
//
// Tras las pruebas se imprime el ancho de banda, todo
// el tiempo necesario para realizar todas las pruebas y
// finalmente el tiempo medio por prueba.
void bandwidth(int myId, double latency, int n);
void bandwidth(int myId, double latency, int n) {
  int i, loop_count = 100, n_bytes;
  double start_time, stop_time, elapsed_time, bw, time;
  char *aux;

  n_bytes = n * sizeof(char);
  aux = malloc(n_bytes);
  elapsed_time = 0;

  for(i=0; i<loop_count; i++){
    
    MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();
    if(myId %2 == 0){
      MPI_Ssend(aux, n, MPI_CHAR, myId+1, 99, MPI_COMM_WORLD);
      MPI_Recv(aux, n, MPI_CHAR, myId+1, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    else if(myId %2 == 1){
      MPI_Recv(aux, n, MPI_CHAR, myId-1, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Ssend(aux, n, MPI_CHAR, myId-1, 99, MPI_COMM_WORLD);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    stop_time = MPI_Wtime();
    elapsed_time += stop_time - start_time;
  }

  if(myId %2 == 0) {
    time = elapsed_time / loop_count - latency;
    bw = ((double)n_bytes * 2) / time;
    printf("MyId %d Bw=%lf GB/s\nTot time=%lf\nTime=%lf\n", myId, bw/ 1000000000.0, elapsed_time, time);
  }
}

// Se realizan varios tests de latencia al 
// mandar un único dato de tipo CHAR a los procesos impares
// desde el par inmediatamente anterior. Tras esto, los impares
// vuelven a enviar el dato al proceso par.
//
// Tras las pruebas se imprime el tiempo necesario para realizar
// TODAS las pruebas y se devuleve el tiempo medio (latencia) de
// las pruebas
double ping_pong(int myId, int start);
double ping_pong(int myId, int start) {
  int i, loop_count = 100;
  double start_time, stop_time, elapsed_time;
  char aux;

  aux = '0';
  elapsed_time = 0;

  for(i=0; i<loop_count; i++){
    
    MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();
    if(myId % 2 == 0){
      MPI_Ssend(&aux, 1, MPI_CHAR, myId+1, 99, MPI_COMM_WORLD);
      MPI_Recv(&aux, 1, MPI_CHAR, myId+1, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    else if(myId % 2 == 1){
      MPI_Recv(&aux, 1, MPI_CHAR, myId-1, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Ssend(&aux, 1, MPI_CHAR, myId-1, 99, MPI_COMM_WORLD);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    stop_time = MPI_Wtime();
    elapsed_time += stop_time - start_time;
  }

  if(myId %2 == 0 && start != 0) {
    printf("MyId %d Ping=%lf\n", myId, elapsed_time);
    elapsed_time/=loop_count;
  }
  MPI_Bcast(&elapsed_time, 1, MPI_DOUBLE, ROOT, MPI_COMM_WORLD);
  return elapsed_time;
}

// Trabajo común para todos los grupos de procesos
int work(int myId, int numP, char **argv, char *job_id) {
  int grp, n_value, aux=0;
  double latency;
  MPI_Comm comm = MPI_COMM_NULL, comm_par= MPI_COMM_NULL;

  int rootBcast = MPI_PROC_NULL;
  if(myId == ROOT) rootBcast = MPI_ROOT;

  //     1.000.000.00 1GB
  n_value = 400000000;
  grp = 0;

  // Obtener que grupo de procesos soy de los padres
  MPI_Comm_get_parent(&comm_par);
  if(comm_par != MPI_COMM_NULL) {
    MPI_Bcast(&grp, 1, MPI_INT, ROOT, comm_par);
    grp+=1;
    MPI_Barrier(comm_par);
    MPI_Bcast(&aux, 1, MPI_INT, rootBcast, comm_par);
    //MPI_Comm_free(&comm_par);
    MPI_Comm_disconnect(&comm_par);
  }

  // Dividir los resultados por procesos
  //create_out_file(myId, numP, grp, job_id);

  /*----- PRUEBAS PRESTACIONES -----*/
  // Asegurar que se ha inicializado la comunicación de MPI
  ping_pong(myId, 0);
  MPI_Barrier(MPI_COMM_WORLD);

  // Obtener la latencia de la red
  latency = ping_pong(myId, 1);
  // Obtener el ancho de banda
  bandwidth(myId, latency, n_value);

  /*----- CREACIÓN DE PROCESOS -----*/

  // Creación de un nuevo grupo de procesos
  // Para evitar que se creen más grupos hay que asignar
  // el valor 0 en la variable MAXGRP
  if(grp != MAXGRP) {

    // Inicialización de la comunicación con SLURM
    int aux = numP;
    init_slurm_comm(argv, myId, aux, ROOT, TYPE_D, COMM_SPAWN_SERIAL);

    // Esperar a que la comunicación y creación de procesos
    // haya finalizado
    int test = -1;
    while(test != MPI_SUCCESS) {
      test = check_slurm_comm(myId, ROOT, MPI_COMM_WORLD, &comm);
    }

    // Enviar a los hijos que grupo de procesos son
    MPI_Bcast(&grp, 1, MPI_INT, rootBcast, comm);
    MPI_Barrier(comm);
    MPI_Bcast(&aux, 1, MPI_INT, ROOT, comm);

    // Desconectar intercomunicador con los hijos
    MPI_Comm_disconnect(&comm);
    //MPI_Comm_free(&comm);
  } //IF GRP

  if(comm != MPI_COMM_NULL || comm_par != MPI_COMM_NULL) {
    printf("GRP=%d || El comunicador no esta a NULO\n", grp);
    fflush(stdout);
  }

  return grp;
}

int main(int argc, char ** argv) {
  int rank, numP, grp, len, pid;
  char *tmp;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &numP);  
  pid = getpid();

  // Imprimir datos sobre el comunicador de
  // este grupo de procesos
  tmp = getenv("SLURM_JOB_ID");
  if(rank == ROOT) {

    //system("printenv"); // Imprime todas las variables de entorno
    printf("DATA\n");
    //print_Info(MPI_COMM_WORLD);
  }

  
  // Imprimir nombre del nodo en el que se encuentra el proceso
  char *name = malloc(MPI_MAX_PROCESSOR_NAME * sizeof(char));
  MPI_Get_processor_name(name,&len);
  printf("ID=%d Name %s PID=%d\n", rank, name, pid); 
  fflush(stdout);
  MPI_Barrier(MPI_COMM_WORLD);
  
  // Se manda el trabajo a los hijos
  grp = work(rank, numP, argv, tmp);
  fflush(stdout);
  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  
  return 0;
  
}
