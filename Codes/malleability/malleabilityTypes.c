#include "malleabilityTypes.h"


void init_malleability_data_struct(malleability_data_t *data_struct, int size);
void realloc_malleability_data_struct(malleability_data_t *data_struct, int qty_to_add);

void def_malleability_entries(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, MPI_Datatype *new_type);
void def_malleability_qty_type(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, MPI_Datatype *new_type);


//======================================================||
//======================================================||
//===================PUBLIC FUNCTIONS===================||
//======================================================||
//======================================================||

/*
 * Anyade en la estructura de datos a comunicar con los hijos
 * un nuevo set de datos de un total "total_qty" distribuido entre
 * todos los padres. La nueva serie "data" solo representa los datos
 * que tiene este padre.
 */
void add_data(void *data, int total_qty, int type, int request_qty, malleability_data_t *data_struct) {
  int i;
  
  if(data_struct->entries == 0) {
    init_malleability_data_struct(data_struct, MALLEABILITY_INIT_DATA_QTY);
  } else if(data_struct->entries == data_struct->max_entries) {
    realloc_malleability_data_struct(data_struct, MALLEABILITY_INIT_DATA_QTY);
  }
  
  data_struct->qty[data_struct->entries] = total_qty;
  data_struct->types[data_struct->entries] = type;
  data_struct->arrays[data_struct->entries] = data;

  data_struct->requests[data_struct->entries] = (MPI_Request *) malloc(request_qty * sizeof(MPI_Request));
  for(i=0; i < request_qty; i++) {
    data_struct->requests[data_struct->entries][i] = MPI_REQUEST_NULL;
  }
  data_struct->entries+=1;
}


/*
 * Comunicar desde los padres a los hijos las estructuras de datos sincronas o asincronas
 * No es necesario que las estructuras esten inicializadas para un buen funcionamiento.
 *
 * En el argumento "root" todos tienen que indicar quien es el proceso raiz de los padres
 * unicamente.
 */
void comm_data_info(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, int is_children_group, int myId, int root, MPI_Comm intercomm) {
  int rootBcast = MPI_PROC_NULL;
  MPI_Datatype entries_type, struct_type;

  if(is_children_group) {
    rootBcast = root;
  } else {
    if(myId == root) rootBcast = MPI_ROOT;
  }

  // Mandar primero numero de entradas
  def_malleability_entries(data_struct_dist, data_struct_rep, &entries_type);
  MPI_Bcast(&(data_struct_rep->entries), 1, entries_type, rootBcast, intercomm);

  if(is_children_group) {
    if(data_struct_rep->entries == 0) init_malleability_data_struct(data_struct_rep, data_struct_rep->entries);
    if(data_struct_dist->entries == 0) init_malleability_data_struct(data_struct_dist, data_struct_dist->entries);
  }

  def_malleability_qty_type(data_struct_dist, data_struct_rep, &struct_type);
  MPI_Bcast(&data_struct_rep, 1, struct_type, rootBcast, intercomm);

  MPI_Type_free(&entries_type);
  MPI_Type_free(&struct_type);
}

//======================================================||
//======================================================||
//=========INIT/REALLOC/FREE RESULTS FUNCTIONS==========||
//======================================================||
//======================================================||

/*
 * Inicializa la estructura que describe una serie de datos con las mismas
 * caracteristicas de localización y uso. Se inicializa para utilizar hasta
 * "size" elementos.
 */
void init_malleability_data_struct(malleability_data_t *data_struct, int size) {
  data_struct->max_entries = size;
  data_struct->qty = (int *) malloc(size * sizeof(int));
  data_struct->types = (int *) malloc(size * sizeof(int));
  data_struct->requests = (MPI_Request **) malloc(size * sizeof(MPI_Request *));
  data_struct->arrays = (void **) malloc(size * sizeof(void *));

  data_struct->request_ibarrier = MPI_REQUEST_NULL;
}

/*
 * Realoja la estructura que describe una serie de datos con las mismas
 * caracteristicas de localización y uso. Se anyaden "size" entradas nuevas
 * a las ya existentes.
 */
void realloc_malleability_data_struct(malleability_data_t *data_struct, int qty_to_add) {
  int *qty_aux, *types_aux, needed;
  MPI_Request **requests_aux;
  void **arrays_aux;

  needed = data_struct->max_entries + qty_to_add;
  qty_aux = (int *) realloc(data_struct->qty, needed * sizeof(int));
  types_aux = (int *) realloc(data_struct->types, needed * sizeof(int));
  requests_aux = (MPI_Request **) realloc(data_struct->requests, needed * sizeof(MPI_Request *));
  arrays_aux = (void **) realloc(data_struct->arrays, needed * sizeof(void *));

  if(qty_aux == NULL || arrays_aux == NULL || requests_aux == NULL || types_aux == NULL) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria constante de datos a redistribuir/comunicar\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  data_struct->qty = qty_aux;
  data_struct->types = types_aux;
  data_struct->requests = requests_aux;
  data_struct->arrays = arrays_aux;
  data_struct->max_entries = needed;
}

void free_malleability_data_struct(malleability_data_t *data_struct) {
  int i, max;

  max = data_struct->entries;
  if(max != 0) {
    for(i=0; i<max; i++) {
      free(data_struct->requests[i]);
    }

    free(data_struct->qty);
    free(data_struct->types);
    free(data_struct->requests);
    free(data_struct->arrays);
  }
}

//======================================================||
//======================================================||
//================MPI DERIVED DATATYPES=================||
//======================================================||
//======================================================||

/*
 * Crea un tipo derivado para mandar el numero de entradas
 * en dos estructuras de descripcion de datos.
 */
void def_malleability_entries(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, MPI_Datatype *new_type) {
  int counts = 2;
  int blocklength = 1;
  MPI_Aint displs, dir;

  // Obtener direccion base
  MPI_Get_address(&(data_struct_rep->entries), &dir);
  MPI_Get_address(&(data_struct_dist->entries), &displs);
  displs -= dir;

  MPI_Type_create_hvector(counts, blocklength, displs, MPI_INT, new_type);
  MPI_Type_commit(new_type);
}

/*
 * Crea un tipo derivado para mandar las cantidades y tipo
 * de datos de dos estructuras de descripcion de datos.
 * El vector de "requests" no es enviado ya que solo es necesario
 * en los padres.
 */
void def_malleability_qty_type(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, MPI_Datatype *new_type) {
  int i, counts = 4;
  int blocklengths[counts];
  MPI_Aint displs[counts], dir;
  MPI_Datatype types[counts];

  types[0] = types[1] = types[2] = types[3] = MPI_INT;
  blocklengths[0] = blocklengths[1] = data_struct_rep->entries;
  blocklengths[2] = blocklengths[3] = data_struct_dist->entries;

  // Obtener direccion base
  MPI_Get_address(data_struct_rep, &dir);

  MPI_Get_address(&(data_struct_rep->qty), &displs[0]);
  MPI_Get_address(&(data_struct_rep->types), &displs[1]);
  MPI_Get_address(&(data_struct_dist->qty), &displs[2]);
  MPI_Get_address(&(data_struct_dist->types), &displs[3]);

  for(i=0;i<counts;i++) displs[i] -= dir;

  MPI_Type_create_struct(counts, blocklengths, displs, types, new_type);
  MPI_Type_commit(new_type);
}
