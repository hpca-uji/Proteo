#include "MAM_DataStructures.h"

/**
 * @file MAM_DataStructures.c
 * @brief Implementation of MaM global datatype packing and communicator helpers.
 */

malleability_config_t *mall_conf = NULL;
malleability_t *mall = NULL;
int state = MAM_I_UNRESERVED;

/**
 * @brief Build and commit the MPI datatype packing main config/process fields.
 *
 * Used when broadcasting MaM configuration from sources to targets during a
 * reconfiguration.
 *
 * @note TODO: Pack @c num_parents / @c numC / @c gid only when the matching
 *       Single / Multiple / Parallel strategies are active.
 */
void MAM_Def_main_datatype(void) {
  int i, counts = 11;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts];

  for (i = 0; i < 5; i++) {
    blocklengths[i] = 1;
    types[i] = MPI_UNSIGNED;
  }
  for (i = 5; i < counts; i++) {
    blocklengths[i] = 1;
    types[i] = MPI_INT;
  }

  MPI_Get_address(&(mall_conf->spawn_method), &displs[0]);
  MPI_Get_address(&(mall_conf->spawn_strategies), &displs[1]);
  MPI_Get_address(&(mall_conf->spawn_dist), &displs[2]);
  MPI_Get_address(&(mall_conf->red_method), &displs[3]);
  MPI_Get_address(&(mall_conf->red_strategies), &displs[4]);

  MPI_Get_address(&(mall->root_parents), &displs[5]);
  MPI_Get_address(&(mall->num_parents), &displs[6]); // TODO: Add only when Single strat active?
  MPI_Get_address(&(mall->numC), &displs[7]); // TODO: Add only when MultipleSpawn strat active?
  MPI_Get_address(&(mall->gid), &displs[8]); // TODO: Add only when ParallelSpawn strat active?
  MPI_Get_address(&(mall->num_nodes), &displs[9]);
  MPI_Get_address(&(mall->nodelist_len), &displs[10]);

  MPI_Type_create_struct(counts, blocklengths, displs, types, &mall->struct_type);
  MPI_Type_commit(&mall->struct_type);
}

/**
 * @brief Free ::mall->struct_type if committed.
 */
void MAM_Free_main_datatype(void) {
  if (mall->struct_type != MPI_DATATYPE_NULL) {
    MPI_Type_free(&mall->struct_type);
  }
}

/**
 * @brief Broadcast main MaM structures (and nodelist/CPU arrays) from sources to targets.
 *
 * @param[in] i_comm      Inter- or intracomm used for the broadcast.
 * @param[in] i_rootBcast Root for the Bcast (@c MPI_ROOT / @c MPI_PROC_NULL on intercomm).
 */
void MAM_Comm_main_structures(MPI_Comm i_comm, int i_rootBcast) {

  MPI_Bcast(MPI_BOTTOM, 1, mall->struct_type, i_rootBcast, i_comm);

  if (mall->nodelist == NULL) {
    mall->max_cpus = malloc(mall->num_nodes * sizeof *mall->max_cpus);
    mall->assigned_cpus = malloc(mall->num_nodes * sizeof *mall->assigned_cpus);
    mall->spawned_cpus = malloc(mall->num_nodes * sizeof *mall->spawned_cpus);
    mall->nodelist = malloc((mall->nodelist_len) * sizeof(char));
    mall->nodelist[mall->nodelist_len - 1] = '\0';
  }
  MPI_Bcast(mall->nodelist, mall->nodelist_len, MPI_CHAR, i_rootBcast, i_comm);
  MPI_Bcast(mall->max_cpus, mall->num_nodes, MPI_INT, i_rootBcast, i_comm);
  MPI_Bcast(mall->assigned_cpus, mall->num_nodes, MPI_INT, i_rootBcast, i_comm);
  MPI_Bcast(mall->spawned_cpus, mall->num_nodes, MPI_INT, i_rootBcast, i_comm);
}

/**
 * @brief Print names of the main MaM communicators (debug).
 */
void MAM_print_comms_state(void) {
  int tester;
  char *comm_name = malloc(MPI_MAX_OBJECT_NAME * sizeof(char));

  MPI_Comm_get_name(mall->comm, comm_name, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, mall->comm, comm_name);
  MPI_Comm_get_name(*(mall->user_comm), comm_name, &tester);
  printf("P%d Comm=%d Name=%s\n", mall->myId, *(mall->user_comm), comm_name);
  if (mall->intercomm != MPI_COMM_NULL) {
    MPI_Comm_get_name(mall->intercomm, comm_name, &tester);
    printf("P%d Comm=%d Name=%s\n", mall->myId, mall->intercomm, comm_name);
  }
  free(comm_name);
}

/**
 * @brief Replace @c mall->comm and @c mall->thread_comm with duplicates of @p i_comm.
 * @param[in] i_comm New intracomm among the continuing targets.
 */
void MAM_comms_update(MPI_Comm i_comm) {
  if (mall->thread_comm != MPI_COMM_WORLD) MPI_Comm_disconnect(&(mall->thread_comm));
  if (mall->comm != MPI_COMM_WORLD) MPI_Comm_disconnect(&(mall->comm));

  MPI_Comm_dup(i_comm, &(mall->thread_comm));
  MPI_Comm_dup(i_comm, &(mall->comm));

  MPI_Comm_set_name(mall->thread_comm, "MAM_THREAD");
  MPI_Comm_set_name(mall->comm, "MAM_MAIN");
}
