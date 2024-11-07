#include "MAM_DataStructures.h"

malleability_config_t *mall_conf = NULL;
malleability_t *mall = NULL;
int state = MAM_I_UNRESERVED;

/*
 * Crea un tipo derivado para mandar las dos estructuras principales
 * de MaM.
 */
void MAM_Def_main_datatype() {
  int i, counts = 13;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts];

  for(i=0; i<5; i++) {
    blocklengths[i] = 1;
    types[i] = MPI_UNSIGNED;
  }
  for(i=5; i<counts; i++) {
    blocklengths[i] = 1;
    types[i] = MPI_INT;
  }

  // Obtain base direction
  MPI_Get_address(&(mall_conf->spawn_method), &displs[0]);
  MPI_Get_address(&(mall_conf->spawn_strategies), &displs[1]);
  MPI_Get_address(&(mall_conf->spawn_dist), &displs[2]);
  MPI_Get_address(&(mall_conf->red_method), &displs[3]);
  MPI_Get_address(&(mall_conf->red_strategies), &displs[4]);
  MPI_Get_address(&(mall_conf->slurm_jid), &displs[5]); //DMR ADDITION - Which is the original Slurm JobId

  MPI_Get_address(&(mall->root_parents), &displs[6]);
  MPI_Get_address(&(mall->num_parents), &displs[7]); //TODO Add only when Single strat active?
  MPI_Get_address(&(mall->numC), &displs[8]); //TODO Add only when MultipleSpawn strat active?
  MPI_Get_address(&(mall->gid), &displs[9]); //TODO Add only when ParallelSpawn strat active?
  MPI_Get_address(&(mall->num_cpus), &displs[10]);
  MPI_Get_address(&(mall->num_nodes), &displs[11]);
  MPI_Get_address(&(mall->nodelist_len), &displs[12]);

  MPI_Type_create_struct(counts, blocklengths, displs, types, &mall->struct_type);
  MPI_Type_commit(&mall->struct_type);
}

void MAM_Free_main_datatype() {
  if(mall->struct_type != MPI_DATATYPE_NULL) {
    MPI_Type_free(&mall->struct_type);
  }
}

/*
 * Comunica datos necesarios de las estructuras
 * principales de MAM de sources a targets.
 */
void MAM_Comm_main_structures(MPI_Comm comm, int rootBcast) {

  MPI_Bcast(MPI_BOTTOM, 1, mall->struct_type, rootBcast, comm);

  if(mall->nodelist == NULL) {
    mall->nodelist = malloc((mall->nodelist_len) * sizeof(char));
    mall->nodelist[mall->nodelist_len-1] = '\0';
  }
  MPI_Bcast(mall->nodelist, mall->nodelist_len, MPI_CHAR, rootBcast, comm);
}

/*
 * Muestra por pantalla el estado actual de todos los comunicadores
 */
void MAM_print_comms_state() {
  int tester;
  char *comm_name = malloc(MPI_MAX_OBJECT_NAME * sizeof(char));

  MPI_Comm_get_name(mall->comm, comm_name, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, mall->comm, comm_name);
  MPI_Comm_get_name(*(mall->user_comm), comm_name, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, *(mall->user_comm), comm_name);
  if(mall->intercomm != MPI_COMM_NULL) {
    MPI_Comm_get_name(mall->intercomm, comm_name, &tester);
    printf("P%d Comm=%d Name=%s\n", mall->myId, mall->intercomm, comm_name);
  }
  free(comm_name);
}

/*
 * Función para modificar los comunicadores principales de MaM
 */
void MAM_comms_update(MPI_Comm comm) {
  if(mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_disconnect(&(mall->thread_comm));
  if(mall->comm != MPI_COMM_WORLD) MPI_Comm_disconnect(&(mall->comm));

  MPI_Comm_dup(comm, &(mall->thread_comm));
  MPI_Comm_dup(comm, &(mall->comm));

  MPI_Comm_set_name(mall->thread_comm, "MAM_THREAD");
  MPI_Comm_set_name(mall->comm, "MAM_MAIN");
}
