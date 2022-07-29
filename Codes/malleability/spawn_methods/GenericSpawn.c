#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <mpi.h>
#include <string.h>
#include "../malleabilityStates.h"
#include "ProcessDist.h"
#include "GenericSpawn.h"
#include "Baseline.h"
#include "Merge.h"

// This code is a Singleton object -- Only one instance can be used at a given time and
// no multiple calls to perform diferent resizes can be performed at the same time.

int commState = MALL_NOT_STARTED;
Spawn_data *spawn_data;
pthread_t spawn_thread;
//pthread_mutex_t spawn_mutex; FIXME BORRAR
//pthread_cond_t cond_adapt_rdy; FIXME BORRAR
MPI_Comm *returned_comm;

double end_time; //FIXME REFACTOR

//--------------PRIVATE CONFIGURATION DECLARATIONS---------------//
void set_spawn_configuration(char *cmd, int num_cpus, int num_nodes, char *nodelist, int myId, int root, int initial_qty, int target_qty, int type_dist, int spawn_method, int spawn_strategies, MPI_Comm comm);
void set_basic_spawn_dtype();
void deallocate_spawn_data();

//--------------PRIVATE DECLARATIONS---------------//
void generic_spawn(MPI_Comm *child, int data_stage);

int check_single_state(MPI_Comm comm, int global_state);
int check_generic_state(MPI_Comm comm, MPI_Comm *child, int local_state, double *real_time);
int check_merge_shrink_state();

//--------------PRIVATE THREADS DECLARATIONS---------------//
int allocate_thread_spawn();
void* thread_work(void* arg);


//--------------PUBLIC FUNCTIONS---------------//

/*
 * Se solicita la creacion de un nuevo grupo de "numP" procesos con una distribucion
 * fisica "type_dist".
 *
 * Se puede solicitar en primer plano, encargandose por tanto el proceso que llama a esta funcion,
 * o en segundo plano, donde un hilo se encarga de configurar esta creacion.
 *
 * Si se pide en primer plano, al terminarla es posible llamar a "check_spawn_state()" para crear
 * los procesos.
 *
 * Si se pide en segundo plano, llamar a "check_spawn_state()" comprobara si la configuracion para
 * crearlos esta lista, y si es asi, los crea.
 *
 * Devuelve el estado de el procedimiento. Si no devuelve "MALL_SPAWN_COMPLETED", es necesario llamar a
 * "check_spawn_state()".
 */
int init_spawn(char *argv, int num_cpus, int num_nodes, char *nodelist, int myId, int initial_qty, int target_qty, int root, int type_dist, int spawn_method, int spawn_strategies, MPI_Comm comm, MPI_Comm *child) {
  int local_state;
  set_spawn_configuration(argv, num_cpus, num_nodes, nodelist, myId, root, initial_qty, target_qty, type_dist, spawn_method, spawn_strategies, comm);

  if(!spawn_data->spawn_is_async) {
    generic_spawn(child, NOT_STARTED);
    local_state = get_spawn_state(spawn_data->spawn_is_async);
    if (local_state == MALL_SPAWN_COMPLETED)
      deallocate_spawn_data();


  } else {
    local_state = spawn_data->spawn_is_single ? MALL_SPAWN_SINGLE_PENDING : MALL_SPAWN_PENDING;
    set_spawn_state(local_state, 0);
    if((spawn_data->spawn_is_single && myId == root) || !spawn_data->spawn_is_single) {
      allocate_thread_spawn();
    }
  }
    
  return commState;
}

/*
 * Comprueba si una configuracion para crear un nuevo grupo de procesos esta lista,
 * y en caso de que lo este, se devuelve el communicador a estos nuevos procesos.
 */
int check_spawn_state(MPI_Comm *child, MPI_Comm comm, int data_dist_completed, double *real_time) { 
  int local_state;
  int global_state=MALL_NOT_STARTED;

  if(spawn_data->spawn_is_async) {
    local_state = get_spawn_state(spawn_data->spawn_is_async);

    if(local_state == MALL_SPAWN_SINGLE_PENDING || local_state == MALL_SPAWN_SINGLE_COMPLETED) {	    
      global_state = check_single_state(comm, local_state);

    } else if(local_state == MALL_SPAWN_ADAPT_POSTPONE && data_dist_completed) {
      global_state = check_merge_shrink_state();

    } else if(local_state == MALL_SPAWN_PENDING) {
      global_state = check_generic_state(comm, child, local_state, real_time);

    } else {
      printf("Error Check spawn: Configuracion invalida\n");
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -10;
    }
  } else if(spawn_data->spawn_method == MALL_SPAWN_MERGE){
    generic_spawn(child, MALL_DIST_COMPLETED);
    global_state = get_spawn_state(spawn_data->spawn_is_async);
    if(global_state == MALL_SPAWN_COMPLETED)
      deallocate_spawn_data();
  }
  return global_state;
}

/*
 * Funcion bloqueante de los hijos para asegurar que todas las tareas del paso
 * de creacion de los hijos se terminan correctamente.
 *
 * Ademas los hijos obtienen informacion basica de los padres
 * para el paso de redistribucion de datos (Numeros de procesos y Id del Root).
 *
 */
void malleability_connect_children(int myId, int numP, int root, int *numP_parents, int *root_parents, MPI_Comm *parents) {
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));
  spawn_data->root = root;
  spawn_data->myId = myId;
  spawn_data->spawn_qty = numP;
  spawn_data->target_qty = numP;
  spawn_data->comm = comm;

  set_basic_spawn_dtype();
  MPI_Bcast(spawn_data, 1, spawn_data->dtype, MALLEABILITY_ROOT, *parents);

  
  switch(spawn_data->spawn_method) {
    case MALL_SPAWN_BASELINE:
      local_state = baseline(*spawn_data, parents);
      break;
    case MALL_SPAWN_MERGE:
      spawn_data->target_qty += numP_parents;
      local_state = merge(*spawn_data, parents, NOT_STARTED);
      break;
  }

  MPI_Type_free(&(spawn_data->dtype));
  free(spawn_data);
}

//--------------PRIVATE CONFIGURATION FUNCTIONS---------------//
/*
 * Agrupa en una sola estructura todos los datos de configuración necesarios
 * e inicializa las estructuras necesarias.
 */
void set_spawn_configuration(char *cmd, int num_cpus, int num_nodes, char *nodelist, int myId, int root, int initial_qty, int target_qty, int type_dist, int spawn_method, int spawn_strategies, MPI_Comm comm) {
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));

  spawn_data->myId = myId;
  spawn_data->root = root;
  spawn_data->root_parents = root;
  spawn_data->initial_qty = initial_qty;
  spawn_data->target_qty = target_qty;
  spawn_data->spawn_method = spawn_method;
  spawn_data->spawn_is_single = spawn_strategies % MALL_SPAWN_SINGLE ? 0 : 1;
  spawn_data->spawn_is_async = spawn_strategies % MALL_SPAWN_PTHREAD ? 0 : 1;
  spawn_data->comm = comm;

  set_basic_spawn_dtype();

  switch(spawn_data->spawn_method) {
    case MALL_SPAWN_BASELINE:
      spawn_data->spawn_qty = spawn_data->target_qty;
      spawn_data->already_created = 0;
    case MALL_SPAWN_MERGE:
      spawn_data->spawn_qty = spawn_data->target_qty - spawn_data->initial_qty;
      spawn_data->already_created = spawn_data->initial_qty;
  }

  if(spawn_data->spawn_is_async) {
    pthread_mutex_init(&(spawn_data->spawn_mutex),NULL);
    pthread_cond_init(&(spawn_data->cond_adapt_rdy),NULL);
    spawn_thread = pthread_self();
  }

  if(spawn_data->myId == spawn_data->root) {
    physical_struct_create(target_qty, spawn_data->already_created, num_cpus, num_nodes, nodelist, type_dist, MALL_DIST_STRING, &(spawn_data->dist));

    //COPY PROGRAM NAME
    spawn_data->cmd = malloc(strlen(cmd) * sizeof(char));
    strcpy(spawn_data->cmd, cmd);

  } else {
    spawn_data->cmd = malloc(1 * sizeof(char));
    spawn_data->mapping = MPI_INFO_NULL; //It is only needed for the root process
  }
}

/*
 * Crea un tipo derivado para mandar 4 enteros con informacion
 * basica a los hijos. Son datos necesarios para que terminen
 * la creacion de procesos.
 */
void set_basic_spawn_dtype() {
  int i, counts = 4;
  int blocklengths[] = {1, 1, 1, 1};
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  // Rellenar vector types
  types[0] = types[1] = types[2] = types[3] = MPI_INT;

  // Rellenar vector displs
  MPI_Get_address(spawn_data, &dir);

  MPI_Get_address(&(spawn_data->root_parents), &displs[0]);
  MPI_Get_address(&(spawn_data->initial_qty), &displs[1]);
  MPI_Get_address(&(spawn_data->spawn_is_single), &displs[2]);
  MPI_Get_address(&(spawn_data->spawn_method), &displs[3]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, &(spawn_data->dtype));
  MPI_Type_commit(&(spawn_data->dtype));
}

/*
 * Libera una estructura de datos spawn_data
 * junto a la destrucion de aquellas estructuras que utiliza.
 */
void deallocate_spawn_data() {
  free(spawn_data->cmd);
 
  MPI_Type_free(&(spawn_data->dtype));
  if(spawn_data->mapping != MPI_INFO_NULL) {
    MPI_Info_free(&(spawn_data->mapping));
  }

  if(spawn_data->spawn_is_async) {
    pthread_cond_destroy(&(spawn_data->cond_adapt_rdy));
    pthread_mutex_destroy(&(spawn_data->spawn_mutex));
    spawn_thread = pthread_self();
  }
  free(spawn_data); 
}


//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

/*
 * Funcion generica para la creacion de procesos. Obtiene la configuracion
 * y segun esta, elige como deberian crearse los procesos.
 *
 * Cuando termina, modifica la variable global para indicar este cambio
 */
void generic_spawn(MPI_Comm *child, int data_stage) {
  int local_state;

  // WORK
  if(spawn_data->myId == spawn_data->root) { //SET MAPPING
    processes_dist(spawn_data->dist, &(spawn_data->mapping));
  }
  switch(spawn_data->spawn_method) {
    case MALL_SPAWN_BASELINE:
      local_state = baseline(*spawn_data, child);
      break;
    case MALL_SPAWN_MERGE:
      local_state = merge(*spawn_data, child, data_stage);
      break;
  }
  // END WORK

  local_state = get_spawn_state(spawn_data->spawn_is_async);
  end_time = MPI_Wtime();
}


//--------------PRIVATE THREAD FUNCTIONS---------------//

/*
 * Aloja la memoria para un hilo auxiliar dedicado a la creacion de procesos.
 * No se puede realizar un "join" sobre el hilo y el mismo libera su memoria
 * asociado al terminar.
 */
int allocate_thread_spawn() {
  if(pthread_create(&spawn_thread, NULL, thread_work, NULL)) {
    printf("Error al crear el hilo de SPAWN\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  if(pthread_detach(spawn_thread)) {
    printf("Error when detaching spawning thread\n");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }
  return 0;
}

/*
 * Funcion llamada por un hilo para que este se encarge
 * de configurar la creacion de un nuevo grupo de procesos.
 *
 * Una vez esta lista la configuracion y es posible crear los procesos
 * se avisa al hilo maestro.
 */
void* thread_work(void* arg) {
  returned_comm = (MPI_Comm *) malloc(sizeof(MPI_Comm));
 
  generic_spawn(returned_comm, NOT_STARTED);
  while(commState == MALL_SPAWN_ADAPT_POSTPONE) {
    // El grupo de procesos se terminara de juntar tras la redistribucion de datos
    pthread_cond_wait(&(spawn_data->cond_adapt_rdy), &(spawn_data->spawn_mutex));
    generic_spawn(returned_comm, MALL_DIST_COMPLETED);
  }

  deallocate_spawn_data();
  pthread_exit(NULL);
}

/*
 * Comprueba si una creacion de procesos asincrona en el
 * paso "single" ha terminado. 
 * Si no ha terminado se mantiene el estado 
 * "MALL_SPAWN_SINGLE_PENDING".
 *
 * Si ha terminado se crean los hilos auxiliares para 
 * los procesos no root y se devuelve el estado
 * "MALL_SPAWN_PENDING".
 */
int check_single_state(MPI_Comm comm, int global_state) {
  MPI_Bcast(&global_state, 1, MPI_INT, spawn_data->root, comm);

  // Non-root processes join root to finalize the spawn
  // They also must join if the application has ended its work
  if(global_state == MALL_SPAWN_SINGLE_COMPLETED) { 
    global_state = MALL_SPAWN_PENDING;
    pthread_mutex_lock(&(spawn_data->spawn_mutex));
    commState = global_state;
    pthread_mutex_unlock(&(spawn_data->spawn_mutex));

    // TODO Refactor - No debería ser necesario
    int threads_not_spawned = pthread_equal(pthread_self(), spawn_thread);

    if(spawn_data->myId != spawn_data->root && threads_not_spawned) {
      allocate_thread_spawn(spawn_data);
    }
  }
  return global_state;
}

/*
 * Comprueba si una creación de procesos asincrona en el
 * paso "generic" ha terminado.
 * Si no ha terminado devuelve el estado 
 * "MALL_SPAWN_PENDING".
 *
 * Si ha terminado libera la memoria asociada a spawn_data
 * y devuelve el estado "MALL_SPAWN_COMPLETED".
 */
int check_generic_state(MPI_Comm comm, MPI_Comm *child, int local_state, double *real_time) {
  int global_state;

  MPI_Allreduce(&local_state, &global_state, 1, MPI_INT, MPI_MIN, comm);
  if(global_state == MALL_SPAWN_COMPLETED || global_state == MALL_SPAWN_ADAPTED) {
    *child = *returned_comm;
    deallocate_spawn_data(spawn_data);
    *real_time=end_time;
  }
  return global_state;
}

/*
 * Permite a una reduccion merge asincrona
 * de procesos que estaba a la espera de que la
 * distribucion de los datos se completase continue.
 */
int check_merge_shrink_state() {
  // FIXME Pasar como caso especial para evitar iteracion no necesaria
  int global_state = MALL_SPAWN_ADAPT_PENDING;
  pthread_mutex_lock(&(spawn_data->spawn_mutex));
  commState = global_state;
  pthread_mutex_unlock(&(spawn_data->spawn_mutex));
  pthread_cond_signal(&(spawn_data->cond_adapt_rdy));
  return global_state
}
