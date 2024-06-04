#include "MAM_Types.h"
#include "MAM_DataStructures.h"
#include "MAM_Configuration.h"


void init_malleability_data_struct(malleability_data_t *data_struct, size_t size);
void realloc_malleability_data_struct(malleability_data_t *data_struct, size_t qty_to_add);

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
void add_data(void *data, size_t total_qty, MPI_Datatype type, size_t request_qty, malleability_data_t *data_struct) {
  size_t i;
  
  if(data_struct->entries == 0) {
    init_malleability_data_struct(data_struct, MALLEABILITY_INIT_DATA_QTY);
  } else if(data_struct->entries == data_struct->max_entries) {
    realloc_malleability_data_struct(data_struct, MALLEABILITY_INIT_DATA_QTY);
  }
  
  data_struct->qty[data_struct->entries] = total_qty;
  data_struct->types[data_struct->entries] = type;
  data_struct->arrays[data_struct->entries] = data;
  data_struct->request_qty[data_struct->entries] = request_qty;

  if(request_qty) {
    data_struct->requests[data_struct->entries] = (MPI_Request *) malloc(request_qty * sizeof(MPI_Request));
    for(i=0; i < request_qty; i++) {
      data_struct->requests[data_struct->entries][i] = MPI_REQUEST_NULL;
    }
  }
  data_struct->entries+=1;
}

/*
 * Modifica en la estructura de datos a comunicar con los hijos
 * un set de datos de un total "total_qty" distribuido entre
 * todos los padres. La nueva serie "data" solo representa los datos
 * que tiene este padre.
 */
void modify_data(void *data, size_t index, size_t total_qty, MPI_Datatype type, size_t request_qty, malleability_data_t *data_struct) {
  size_t i;
  
  if(data_struct->entries < index) { // Index does not exist
    return;
  }
  if(data_struct->requests[index] != NULL) {
    //free(data_struct->requests[index]); TODO Error when trying to free
    data_struct->requests[index] = NULL;
  }

  data_struct->qty[index] = total_qty;
  data_struct->types[index] = type;
  data_struct->arrays[index] = data;
  data_struct->request_qty[index] = request_qty;

  if(request_qty) {
    data_struct->requests[index] = (MPI_Request *) malloc(request_qty * sizeof(MPI_Request));
    for(i=0; i < request_qty; i++) {
      data_struct->requests[index][i] = MPI_REQUEST_NULL;
    }
  }
}

/*
 * Comunicar desde los padres a los hijos las estructuras de datos sincronas o asincronas
 * No es necesario que las estructuras esten inicializadas para un buen funcionamiento.
 *
 * En el argumento "root" todos tienen que indicar quien es el proceso raiz de los padres
 * unicamente.
 */
void comm_data_info(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, int is_children_group) {
  int type_size;
  size_t i, j;
  MPI_Datatype entries_type, struct_type;

  // Mandar primero numero de entradas
  def_malleability_entries(data_struct_dist, data_struct_rep, &entries_type);
  MPI_Bcast(MPI_BOTTOM, 1, entries_type, mall->root_collectives, mall->intercomm);

  if(is_children_group && ( data_struct_rep->entries != 0 || data_struct_dist->entries != 0 )) {
    init_malleability_data_struct(data_struct_rep, data_struct_rep->entries);
    init_malleability_data_struct(data_struct_dist, data_struct_dist->entries);
  }

  def_malleability_qty_type(data_struct_dist, data_struct_rep, &struct_type);
  MPI_Bcast(MPI_BOTTOM, 1, struct_type, mall->root_collectives, mall->intercomm);

  if(is_children_group) {
    for(i=0; i < data_struct_rep->entries; i++) {
      MPI_Type_size(data_struct_rep->types[i], &type_size);
      data_struct_rep->arrays[i] = (void *) malloc(data_struct_rep->qty[i] * type_size);
      data_struct_rep->requests[i] = (MPI_Request *) malloc(data_struct_rep->request_qty[i] * sizeof(MPI_Request));
      for(j=0; j < data_struct_rep->request_qty[i]; j++) {
        data_struct_rep->requests[i][j] = MPI_REQUEST_NULL;
      }
    }
    for(i=0; i < data_struct_dist->entries; i++) {
      data_struct_dist->arrays[i] = (void *) NULL; // TODO Se podria inicializar aqui?
      data_struct_dist->requests[i] = (MPI_Request *) malloc(data_struct_dist->request_qty[i] * sizeof(MPI_Request));
      for(j=0; j < data_struct_dist->request_qty[i]; j++) {
        data_struct_dist->requests[i][j] = MPI_REQUEST_NULL;
      }
    }
  }

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
void init_malleability_data_struct(malleability_data_t *data_struct, size_t size) {
  size_t i;

  data_struct->max_entries = size;
  data_struct->qty = (size_t *) malloc(size * sizeof(size_t));
  data_struct->types = (MPI_Datatype *) malloc(size * sizeof(MPI_Datatype));
  data_struct->request_qty = (size_t *) malloc(size * sizeof(size_t));
  data_struct->requests = (MPI_Request **) malloc(size * sizeof(MPI_Request *));
  data_struct->windows = (MPI_Win *) malloc(size * sizeof(MPI_Win));
  data_struct->arrays = (void **) malloc(size * sizeof(void *));

  for(i=0; i<size; i++) { //calloc and memset does not ensure a NULL value
    data_struct->requests[i] = NULL;
    data_struct->windows[i] = MPI_WIN_NULL;
    data_struct->arrays[i] = NULL;
  }
}

/*
 * Realoja la estructura que describe una serie de datos con las mismas
 * caracteristicas de localización y uso. Se anyaden "size" entradas nuevas
 * a las ya existentes.
 */
void realloc_malleability_data_struct(malleability_data_t *data_struct, size_t qty_to_add) {
  size_t i, needed, *qty_aux, *request_qty_aux;
  MPI_Datatype *types_aux;
  MPI_Win *windows_aux;
  MPI_Request **requests_aux;
  void **arrays_aux;

  needed = data_struct->max_entries + qty_to_add;
  qty_aux = (size_t *) realloc(data_struct->qty, needed * sizeof(int));
  types_aux = (MPI_Datatype *) realloc(data_struct->types, needed * sizeof(MPI_Datatype));
  request_qty_aux = (size_t *) realloc(data_struct->request_qty, needed * sizeof(int));
  requests_aux = (MPI_Request **) realloc(data_struct->requests, needed * sizeof(MPI_Request *));
  windows_aux = (MPI_Win *) realloc(data_struct->windows, needed * sizeof(MPI_Win));
  arrays_aux = (void **) realloc(data_struct->arrays, needed * sizeof(void *));

  if(qty_aux == NULL || arrays_aux == NULL || requests_aux == NULL || types_aux == NULL || request_qty_aux == NULL || windows_aux == NULL) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria constante de datos a redistribuir/comunicar\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for(i=data_struct->max_entries; i<needed; i++) { //realloc does not ensure a NULL value
    requests_aux[i] = NULL;
    windows_aux[i] = MPI_WIN_NULL;
    arrays_aux[i] = NULL;
  }

  // Check if old array can be freed
  if(data_struct->qty != qty_aux && data_struct->qty != NULL) free(data_struct->qty);
  if(data_struct->types != types_aux && data_struct->types != NULL) free(data_struct->types);
  if(data_struct->request_qty != request_qty_aux && data_struct->request_qty != NULL) free(data_struct->request_qty);
  if(data_struct->requests != requests_aux && data_struct->requests != NULL) free(data_struct->requests);
  if(data_struct->windows != windows_aux && data_struct->windows != NULL) free(data_struct->windows);
  if(data_struct->arrays != arrays_aux && data_struct->arrays != NULL) free(data_struct->arrays);

  data_struct->qty = qty_aux;
  data_struct->types = types_aux;
  data_struct->request_qty = request_qty_aux;
  data_struct->requests = requests_aux;
  data_struct->windows = windows_aux;
  data_struct->arrays = arrays_aux;
  data_struct->max_entries = needed;
}

void free_malleability_data_struct(malleability_data_t *data_struct) {
  size_t i, j, max;

  max = data_struct->entries;
  if(max != 0) {
    if(data_struct->qty != NULL) {
      free(data_struct->qty);
    }
    if(data_struct->types != NULL) {
      free(data_struct->types);
    }
    if(data_struct->requests != NULL && data_struct->request_qty != NULL) {
      for(i=0; i<max; i++) {
        if(data_struct->requests[i] != NULL) {
	  for(j=0; j<data_struct->request_qty[i]; j++) {
	    if(data_struct->requests[i][j] != MPI_REQUEST_NULL) {
              MPI_Request_free(&(data_struct->requests[i][j]));
	      data_struct->requests[i][j] = MPI_REQUEST_NULL;
	    }
	  }
          free(data_struct->requests[i]);
	}
      }
      free(data_struct->request_qty);
      free(data_struct->requests);  
    }

    if(data_struct->windows != NULL) {
      free(data_struct->windows);
    }

    if(data_struct->arrays != NULL) {
      free(data_struct->arrays);
    }
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
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  blocklengths[0] = blocklengths[1] = 1;
  types[0] = types[1] = type_size_t;

  // Obtener direccion base
  MPI_Get_address(&(data_struct_rep->entries), &displs[0]);
  MPI_Get_address(&(data_struct_dist->entries), &displs[1]);

  MPI_Type_create_struct(counts, blocklengths, displs, types, new_type);
  MPI_Type_commit(new_type);
}

/*
 * Crea un tipo derivado para mandar las cantidades y tipo
 * de datos de dos estructuras de descripcion de datos.
 * El vector de "requests" no es enviado ya que solo es necesario
 * en los padres.
 * TODO Refactor?
 */
void def_malleability_qty_type(malleability_data_t *data_struct_rep, malleability_data_t *data_struct_dist, MPI_Datatype *new_type) {
  int counts = 6;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  types[0] = types[1] = types[3] = types[4] = type_size_t;
  types[2] = types[5] = MPI_INT;
  blocklengths[0] = blocklengths[1] = blocklengths[2] = data_struct_rep->entries;
  blocklengths[3] = blocklengths[4] = blocklengths[5] = data_struct_dist->entries;

  MPI_Get_address((data_struct_rep->qty), &displs[0]);
  MPI_Get_address((data_struct_rep->request_qty), &displs[1]);
  MPI_Get_address((data_struct_rep->types), &displs[2]); // MPI_Datatype uses typedef int to be declared
  MPI_Get_address((data_struct_dist->qty), &displs[3]);
  MPI_Get_address((data_struct_dist->request_qty), &displs[4]);
  MPI_Get_address((data_struct_dist->types), &displs[5]); // MPI_Datatype uses typedef int to be declared

  MPI_Type_create_struct(counts, blocklengths, displs, types, new_type);
  MPI_Type_commit(new_type);
}



