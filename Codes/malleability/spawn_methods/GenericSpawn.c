#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <mpi.h>
#include <string.h>
#include "../malleabilityStates.h"
#include "../malleabilityDataStructures.h"
#include "../MAM_Configuration.h"
#include "ProcessDist.h"
#include "GenericSpawn.h"
#include "Baseline.h"
#include "Merge.h"
#include "Spawn_state.h"

// This code is a Singleton object -- Only one instance can be used at a given time and
// no multiple calls to perform diferent resizes can be performed at the same time.

Spawn_data *spawn_data = NULL;
pthread_t spawn_thread;
MPI_Comm *returned_comm;

//--------------PRIVATE CONFIGURATION DECLARATIONS---------------//
void set_spawn_configuration(MPI_Comm comm);
void deallocate_spawn_data();

//--------------PRIVATE DECLARATIONS---------------//
void generic_spawn(MPI_Comm *child, int data_stage);

int check_single_state(MPI_Comm comm, int global_state, int wait_completed);
int check_generic_state(MPI_Comm comm, MPI_Comm *child, int local_state, int wait_completed);

//--------------PRIVATE THREADS DECLARATIONS---------------//
int allocate_thread_spawn();
void* thread_work();


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
int init_spawn(MPI_Comm comm, MPI_Comm *child) {
  int local_state;
  set_spawn_configuration(comm);

  if(!spawn_data->spawn_is_async) {
    generic_spawn(child, MALL_NOT_STARTED);
    local_state = get_spawn_state(spawn_data->spawn_is_async);
    if (local_state == MALL_SPAWN_COMPLETED)
      deallocate_spawn_data();

  } else {
    local_state = spawn_data->spawn_is_single ? 
	    MALL_SPAWN_SINGLE_PENDING : MALL_SPAWN_PENDING;
    local_state = mall_conf->spawn_method == MALL_SPAWN_MERGE && spawn_data->initial_qty > spawn_data->target_qty ?
	    MALL_SPAWN_ADAPT_POSTPONE : local_state;
    set_spawn_state(local_state, 0);
    if((spawn_data->spawn_is_single && mall->myId == mall->root) || !spawn_data->spawn_is_single) {
      allocate_thread_spawn();
    }
  }
    
  return local_state;
}

/*
 * Comprueba si una configuracion para crear un nuevo grupo de procesos esta lista,
 * y en caso de que lo este, se devuelve el communicador a estos nuevos procesos.
 */
int check_spawn_state(MPI_Comm *child, MPI_Comm comm, int wait_completed) { 
  int local_state;
  int global_state=MALL_NOT_STARTED;

  if(spawn_data->spawn_is_async) { // Async
    local_state = get_spawn_state(spawn_data->spawn_is_async);

    if(local_state == MALL_SPAWN_SINGLE_PENDING || local_state == MALL_SPAWN_SINGLE_COMPLETED) { // Single
      global_state = check_single_state(comm, local_state, wait_completed);

    } else if(local_state == MALL_SPAWN_PENDING || local_state == MALL_SPAWN_COMPLETED || local_state == MALL_SPAWN_ADAPTED) { // Generic
      global_state = check_generic_state(comm, child, local_state, wait_completed);

    } else if(local_state == MALL_SPAWN_ADAPT_POSTPONE) {
      global_state = local_state;
      
    } else {
      printf("Error Check spawn: Configuracion invalida State = %d\n", local_state);
      MPI_Abort(MPI_COMM_WORLD, -1);
      return -10;
    }
  } else if(mall_conf->spawn_method == MALL_SPAWN_MERGE){ // Start Merge shrink Sync
    generic_spawn(child, MALL_DIST_COMPLETED);
    global_state = get_spawn_state(spawn_data->spawn_is_async);
  }
  if(global_state == MALL_SPAWN_COMPLETED || global_state == MALL_SPAWN_ADAPTED)
    deallocate_spawn_data();

  return global_state;
}

/*
 * Elimina la bandera bloqueante MALL_SPAWN_ADAPT_POSTPONE para los hilos 
 * auxiliares. Esta bandera los bloquea para que el metodo Merge shrink no 
 * avance hasta que se complete la redistribucion de datos. Por tanto,
 * al modificar la bandera los hilos pueden continuar.
 *
 * Por seguridad se comprueba que no se realice el cambio a la bandera a 
 * no ser que se cumplan las 3 condiciones.
 */
void unset_spawn_postpone_flag(int outside_state) {
  int local_state = get_spawn_state(spawn_data->spawn_is_async);
  if(local_state == MALL_SPAWN_ADAPT_POSTPONE && outside_state == MALL_SPAWN_ADAPT_PENDING && spawn_data->spawn_is_async) { 
    set_spawn_state(MALL_SPAWN_PENDING, spawn_data->spawn_is_async);
    wakeup_redistribution();
  }
}

/*
 * Funcion bloqueante de los hijos para asegurar que todas las tareas del paso
 * de creacion de los hijos se terminan correctamente.
 *
 * Ademas los hijos obtienen informacion basica de los padres
 * para el paso de redistribucion de datos (Numeros de procesos y Id del Root).
 *
 */
void malleability_connect_children(MPI_Comm comm, MPI_Comm *parents) {
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));
  spawn_data->spawn_qty = mall->numP;
  spawn_data->target_qty = mall->numP;
  spawn_data->comm = comm;

  MAM_Comm_main_structures(MALLEABILITY_ROOT); //FIXME What if root is another id different to 0? Send from spawn to root id?
  MPI_Comm_remote_size(*parents, &spawn_data->initial_qty);
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_SINGLE, &(spawn_data->spawn_is_single));
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, &(spawn_data->spawn_is_async));

  switch(mall_conf->spawn_method) {
    case MALL_SPAWN_BASELINE:
      baseline(*spawn_data, parents);
      break;
    case MALL_SPAWN_MERGE:
      spawn_data->target_qty += spawn_data->initial_qty;
      merge(*spawn_data, parents, MALL_NOT_STARTED);
      break;
  }

  mall->num_parents = spawn_data->initial_qty;

  free(spawn_data);
}

//--------------PRIVATE CONFIGURATION FUNCTIONS---------------//
/*
 * Agrupa en una sola estructura todos los datos de configuración necesarios
 * e inicializa las estructuras necesarias.
 */
void set_spawn_configuration(MPI_Comm comm) {
  spawn_data = (Spawn_data *) malloc(sizeof(Spawn_data));

  spawn_data->initial_qty = mall->numP;
  spawn_data->target_qty = mall->numC;
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_SINGLE, &(spawn_data->spawn_is_single)); 
  MAM_Contains_strat(MAM_SPAWN_STRATEGIES, MAM_STRAT_SPAWN_PTHREAD, &(spawn_data->spawn_is_async));
  spawn_data->comm = comm;
  spawn_data->mapping_fill_method = MALL_DIST_STRING;


  switch(mall_conf->spawn_method) {
    case MALL_SPAWN_BASELINE:
      spawn_data->spawn_qty = spawn_data->target_qty;
      spawn_data->already_created = 0;
      break;
    case MALL_SPAWN_MERGE:
      spawn_data->spawn_qty = spawn_data->target_qty - spawn_data->initial_qty;
      spawn_data->already_created = spawn_data->initial_qty;
      break;
  }

  if(spawn_data->spawn_is_async) {
    init_spawn_state();
  }
  spawn_data->mapping = MPI_INFO_NULL;
}

/*
 * Libera una estructura de datos spawn_data
 * junto a la destrucion de aquellas estructuras que utiliza.
 */
void deallocate_spawn_data() {
  if(spawn_data == NULL) return;

  if(spawn_data->mapping != MPI_INFO_NULL) {
    MPI_Info_free(&(spawn_data->mapping));
  }
  if(spawn_data->spawn_is_async) {
    free_spawn_state();
  }
  free(spawn_data); 
  spawn_data = NULL;
}


//--------------PRIVATE SPAWN CREATION FUNCTIONS---------------//

/*
 * Funcion generica para la creacion de procesos. Obtiene la configuracion
 * y segun esta, elige como deberian crearse los procesos.
 *
 * Cuando termina, modifica la variable global para indicar este cambio
 */
void generic_spawn(MPI_Comm *child, int data_stage) {
  int local_state, aux_state;

  // WORK
  if(mall->myId == mall->root && spawn_data->spawn_qty > 0) { //SET MAPPING FOR NEW PROCESSES
    processes_dist(*spawn_data, &(spawn_data->mapping));
  }
  switch(mall_conf->spawn_method) {
    case MALL_SPAWN_BASELINE:
      local_state = baseline(*spawn_data, child);
      break;
    case MALL_SPAWN_MERGE:
      local_state = merge(*spawn_data, child, data_stage);
      break;
  }
  // END WORK
  aux_state = get_spawn_state(spawn_data->spawn_is_async);
  if(!(aux_state == MALL_SPAWN_PENDING && local_state == MALL_SPAWN_ADAPT_POSTPONE)) {
    set_spawn_state(local_state, spawn_data->spawn_is_async);
  }
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
void* thread_work() {
  int local_state;
  returned_comm = (MPI_Comm *) malloc(sizeof(MPI_Comm));
 
  generic_spawn(returned_comm, MALL_NOT_STARTED);

  local_state = get_spawn_state(spawn_data->spawn_is_async);
  if(local_state == MALL_SPAWN_ADAPT_POSTPONE || local_state == MALL_SPAWN_PENDING) {
    // El grupo de procesos se terminara de juntar tras la redistribucion de datos

    local_state = wait_redistribution();
    generic_spawn(returned_comm, MALL_DIST_COMPLETED);
  }
  wakeup_completion();

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
int check_single_state(MPI_Comm comm, int global_state, int wait_completed) {
  while(wait_completed && mall->myId == mall->root && global_state == MALL_SPAWN_SINGLE_PENDING) {
    global_state = wait_completion();
  }
  MPI_Bcast(&global_state, 1, MPI_INT, mall->root, comm);

  // Non-root processes join root to finalize the spawn
  // They also must join if the application has ended its work
  if(global_state == MALL_SPAWN_SINGLE_COMPLETED) { 
    global_state = MALL_SPAWN_PENDING;
    set_spawn_state(global_state, spawn_data->spawn_is_async);

    if(mall->myId != mall->root) {
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
int check_generic_state(MPI_Comm comm, MPI_Comm *child, int local_state, int wait_completed) {
  int global_state;

  while(wait_completed && local_state == MALL_SPAWN_PENDING) local_state = wait_completion();

  MPI_Allreduce(&local_state, &global_state, 1, MPI_INT, MPI_MIN, comm);
  if(global_state == MALL_SPAWN_COMPLETED || global_state == MALL_SPAWN_ADAPTED) {
    set_spawn_state(global_state, spawn_data->spawn_is_async);
    *child = *returned_comm;
    deallocate_spawn_data();
  }
  return global_state;
}
