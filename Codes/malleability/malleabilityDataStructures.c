#include "malleabilityDataStructures.h"

int state = MALL_UNRESERVED;

/*
 * Crea un tipo derivado para mandar las dos estructuras principales
 * de MaM.
 */
void MAM_Def_main_datatype() {
  int i, counts = 10;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts];

  for(i=0; i<counts; i++) {
    blocklengths[i] = 1;
    types[i] = MPI_INT;
  }

  // Obtener direccion base
  MPI_Get_address(&(mall_conf->spawn_method), &displs[0]);
  MPI_Get_address(&(mall_conf->spawn_strategies), &displs[1]);
  MPI_Get_address(&(mall_conf->spawn_dist), &displs[2]);
  MPI_Get_address(&(mall_conf->red_method), &displs[3]);
  MPI_Get_address(&(mall_conf->red_strategies), &displs[4]);
  MPI_Get_address(&(mall->root_parents), &displs[5]);
  MPI_Get_address(&(mall->num_parents), &displs[6]); //TODO Add only when Intercomm strat active?
  MPI_Get_address(&(mall->num_cpus), &displs[7]);
  MPI_Get_address(&(mall->num_nodes), &displs[8]);
  MPI_Get_address(&(mall->nodelist_len), &displs[9]);

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
void MAM_Comm_main_structures(int rootBcast) {

  MPI_Bcast(MPI_BOTTOM, 1, mall->struct_type, rootBcast, mall->intercomm);

  if(mall->nodelist == NULL) {
    mall->nodelist = malloc((mall->nodelist_len+1) * sizeof(char));
    mall->nodelist[mall->nodelist_len] = '\0';
  }
  MPI_Bcast(mall->nodelist, mall->nodelist_len, MPI_CHAR, rootBcast, mall->intercomm);
}
