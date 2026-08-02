#include "MAM_Types.h"
#include "MAM_DataStructures.h"
#include "MAM_Configuration.h"

/**
 * @file MAM_Types.c
 * @brief Implementation of the malleability data registries and their MPI packing helpers.
 */

/**
 * @brief Initialise a registry, allocating storage for up to @p i_size entries.
 *
 * Allocates the parallel arrays describing a series of data entries that
 * share the same location/usage characteristics, sized for up to @p i_size
 * entries, and sets their per-entry pointers/handles to a safe empty value
 * (@c NULL / @c MPI_WIN_NULL), since neither @c calloc nor @c memset
 * guarantee a portable @c NULL representation.
 *
 * @param[in,out] io_data_struct Registry to initialise.
 * @param[in]     i_size         Initial capacity to allocate.
 */
void init_malleability_data_struct(malleability_data_t *io_data_struct, size_t i_size);

/**
 * @brief Grow a registry's capacity by @p i_qty_to_add entries.
 *
 * Reallocates the parallel arrays describing a series of data entries that
 * share the same location/usage characteristics, adding @p i_qty_to_add new
 * entries on top of the existing capacity. Aborts the job (@c MPI_Abort) if
 * any of the reallocations fails.
 *
 * @param[in,out] io_data_struct Registry to grow.
 * @param[in]     i_qty_to_add   Number of additional entries to reserve.
 */
void realloc_malleability_data_struct(malleability_data_t *io_data_struct, size_t i_qty_to_add);

/**
 * @brief Build a derived MPI datatype to send the entry counts of two registries.
 *
 * @param[in]  i_data_struct_rep  Replicated-data registry whose entry count is included.
 * @param[in]  i_data_struct_dist Distributed-data registry whose entry count is included.
 * @param[out] o_new_type         Resulting committed MPI datatype (caller must free it).
 */
void def_malleability_entries(malleability_data_t *i_data_struct_rep, malleability_data_t *i_data_struct_dist, MPI_Datatype *o_new_type);

/**
 * @brief Build a derived MPI datatype to send the per-entry quantities and datatypes of two registries.
 *
 * The @c requests array is not included, since it is only needed on the
 * sources (parents).
 *
 * @param[in]  i_data_struct_rep  Replicated-data registry whose quantities/datatypes are included.
 * @param[in]  i_data_struct_dist Distributed-data registry whose quantities/datatypes are included.
 * @param[out] o_new_type         Resulting committed MPI datatype (caller must free it).
 *
 * @note TODO: Refactor?
 */
void def_malleability_qty_type(malleability_data_t *i_data_struct_rep, malleability_data_t *i_data_struct_dist, MPI_Datatype *o_new_type);


//======================================================||
//======================================================||
//===================PUBLIC FUNCTIONS===================||
//======================================================||
//======================================================||

/**
 * @brief Append one data entry to a registry.
 *
 * Registers a new set of data with a global total of @p i_total_qty elements
 * distributed across all sources (parents); the local buffer @p i_data only
 * represents the portion of data held by this source. Grows the registry's
 * capacity first if needed (initial allocation, or reallocation by
 * ::MAM_TYPES_INIT_DATA_QTY entries once it is full).
 *
 * @param[in]     i_data         Local buffer for this source (caller-owned).
 * @param[in]     i_total_qty    Global element count for the distributed array.
 * @param[in]     i_type         Element MPI datatype.
 * @param[in]     i_request_qty  Number of asynchronous request slots to allocate (0 if none).
 * @param[in,out] io_data_struct Registry to extend with the new entry.
 */
void add_data(void *i_data, size_t i_total_qty, MPI_Datatype i_type, size_t i_request_qty, malleability_data_t *io_data_struct) {
  size_t i;
  
  if(io_data_struct->entries == 0) {
    init_malleability_data_struct(io_data_struct, MAM_TYPES_INIT_DATA_QTY);
  } else if(io_data_struct->entries == io_data_struct->max_entries) {
    realloc_malleability_data_struct(io_data_struct, MAM_TYPES_INIT_DATA_QTY);
  }
  
  io_data_struct->qty[io_data_struct->entries] = i_total_qty;
  io_data_struct->types[io_data_struct->entries] = i_type;
  io_data_struct->arrays[io_data_struct->entries] = i_data;
  io_data_struct->request_qty[io_data_struct->entries] = i_request_qty;

  if(i_request_qty) {
    io_data_struct->requests[io_data_struct->entries] = (MPI_Request *) malloc(i_request_qty * sizeof(MPI_Request));
    for(i=0; i < i_request_qty; i++) {
      io_data_struct->requests[io_data_struct->entries][i] = MPI_REQUEST_NULL;
    }
  }
  io_data_struct->entries+=1;
}

/**
 * @brief Replace an existing entry at @p i_index.
 *
 * Updates the entry at @p i_index with a new set of data with a global total
 * of @p i_total_qty elements distributed across all sources (parents); the
 * local buffer @p i_data only represents the portion of data held by this
 * source. Any previously allocated request array at that index is discarded
 * (dropped, not freed) before allocating the new one.
 *
 * @param[in]     i_data         New local buffer for this source (caller-owned).
 * @param[in]     i_index        Entry index to replace (no-op if out of range).
 * @param[in]     i_total_qty    Global element count for the distributed array.
 * @param[in]     i_type         Element MPI datatype.
 * @param[in]     i_request_qty  Number of asynchronous request slots to allocate.
 * @param[in,out] io_data_struct Registry to update.
 */
void modify_data(void *i_data, size_t i_index, size_t i_total_qty, MPI_Datatype i_type, size_t i_request_qty, malleability_data_t *io_data_struct) {
  size_t i;
  
  if(io_data_struct->entries <= i_index) { // Index does not exist
    return;
  }
  if(io_data_struct->requests[i_index] != NULL) {
    //free(io_data_struct->requests[i_index]); TODO: Error when trying to free
    io_data_struct->requests[i_index] = NULL;
  }

  io_data_struct->qty[i_index] = i_total_qty;
  io_data_struct->types[i_index] = i_type;
  io_data_struct->arrays[i_index] = i_data;
  io_data_struct->request_qty[i_index] = i_request_qty;

  if(i_request_qty) {
    io_data_struct->requests[i_index] = (MPI_Request *) malloc(i_request_qty * sizeof(MPI_Request));
    for(i=0; i < i_request_qty; i++) {
      io_data_struct->requests[i_index][i] = MPI_REQUEST_NULL;
    }
  }
}

/**
 * @brief Exchange entry metadata between parents and children over @c mall->intercomm.
 *
 * Broadcasts, over @c mall->intercomm using @c mall->root_collectives as
 * root, first the number of entries of each registry and then their
 * per-entry quantities and datatypes. The registries do not need to be
 * pre-initialised on either side for this to work correctly.
 *
 * On the children side (@p i_is_children_group), the registries are
 * (re)initialised to the received number of entries, and buffers/request
 * arrays are then allocated for each entry: replicated-entry buffers are
 * allocated locally, while distributed-entry buffers are left as @c NULL
 * to be filled in later by the caller.
 *
 * @param[in,out] io_data_struct_rep  Replicated-data registry.
 * @param[in,out] io_data_struct_dist Distributed-data registry.
 * @param[in]     i_is_children_group Non-zero on the children / newly spawned side.
 */
void comm_data_info(malleability_data_t *io_data_struct_rep, malleability_data_t *io_data_struct_dist, int i_is_children_group) {
  int type_size;
  size_t i, j;
  MPI_Datatype entries_type, struct_type;

  // Send the number of entries first
  def_malleability_entries(io_data_struct_dist, io_data_struct_rep, &entries_type);
  MPI_Bcast(MPI_BOTTOM, 1, entries_type, mall->root_collectives, mall->intercomm);

  if(i_is_children_group) {
    if(io_data_struct_rep->entries != 0) { init_malleability_data_struct(io_data_struct_rep, io_data_struct_rep->entries); }
    if(io_data_struct_dist->entries != 0) { init_malleability_data_struct(io_data_struct_dist, io_data_struct_dist->entries); } //FIXME: Valgrind not freed
  }

  def_malleability_qty_type(io_data_struct_dist, io_data_struct_rep, &struct_type);
  MPI_Bcast(MPI_BOTTOM, 1, struct_type, mall->root_collectives, mall->intercomm);

  if(i_is_children_group) {
    for(i=0; i < io_data_struct_rep->entries; i++) {
      MPI_Type_size(io_data_struct_rep->types[i], &type_size);
      io_data_struct_rep->arrays[i] = (void *) malloc(io_data_struct_rep->qty[i] * (size_t) type_size); //FIXME: This memory is not freed -- how should this be handled?
      if(io_data_struct_rep->request_qty[i]) {
        io_data_struct_rep->requests[i] = (MPI_Request *) malloc(io_data_struct_rep->request_qty[i] * sizeof(MPI_Request));
        for(j=0; j < io_data_struct_rep->request_qty[i]; j++) {
          io_data_struct_rep->requests[i][j] = MPI_REQUEST_NULL;
        }
      }
    }
    for(i=0; i < io_data_struct_dist->entries; i++) {
      io_data_struct_dist->arrays[i] = (void *) NULL; // TODO: Could this be initialised here?
      if(io_data_struct_dist->request_qty[i]) {
        io_data_struct_dist->requests[i] = (MPI_Request *) malloc(io_data_struct_dist->request_qty[i] * sizeof(MPI_Request));
        for(j=0; j < io_data_struct_dist->request_qty[i]; j++) {
          io_data_struct_dist->requests[i][j] = MPI_REQUEST_NULL;
        }
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

/**
 * @brief Initialise a registry, allocating storage for up to @p i_size entries.
 *
 * Allocates the parallel arrays describing a series of data entries that
 * share the same location/usage characteristics, sized for up to @p i_size
 * entries, and sets their per-entry pointers/handles to a safe empty value
 * (@c NULL / @c MPI_WIN_NULL), since neither @c calloc nor @c memset
 * guarantee a portable @c NULL representation.
 *
 * @param[in,out] io_data_struct Registry to initialise.
 * @param[in]     i_size         Initial capacity to allocate.
 */
void init_malleability_data_struct(malleability_data_t *io_data_struct, size_t i_size) {
  size_t i;

  io_data_struct->max_entries = i_size;
  io_data_struct->qty = (size_t *) malloc(i_size * sizeof(size_t));
  io_data_struct->types = (MPI_Datatype *) malloc(i_size * sizeof(MPI_Datatype));
  io_data_struct->request_qty = (size_t *) malloc(i_size * sizeof(size_t));
  io_data_struct->requests = (MPI_Request **) malloc(i_size * sizeof(MPI_Request *));
  io_data_struct->windows = (MPI_Win *) malloc(i_size * sizeof(MPI_Win));
  io_data_struct->arrays = (void **) malloc(i_size * sizeof(void *));
  io_data_struct->idS = NULL;

  for(i=0; i<i_size; i++) { //calloc and memset does not ensure a NULL value
    io_data_struct->requests[i] = NULL;
    io_data_struct->windows[i] = MPI_WIN_NULL;
    io_data_struct->arrays[i] = NULL;
  }
}

/**
 * @brief Grow a registry's capacity by @p i_qty_to_add entries.
 *
 * Reallocates the parallel arrays describing a series of data entries that
 * share the same location/usage characteristics, adding @p i_qty_to_add new
 * entries on top of the existing capacity. Aborts the job (@c MPI_Abort) if
 * any of the reallocations fails.
 *
 * @param[in,out] io_data_struct Registry to grow.
 * @param[in]     i_qty_to_add   Number of additional entries to reserve.
 */
void realloc_malleability_data_struct(malleability_data_t *io_data_struct, size_t i_qty_to_add) {
  size_t i, needed, *qty_aux, *request_qty_aux;
  MPI_Datatype *types_aux;
  MPI_Win *windows_aux;
  MPI_Request **requests_aux;
  void **arrays_aux;

  needed = io_data_struct->max_entries + i_qty_to_add;
  qty_aux = (size_t *) realloc(io_data_struct->qty, needed * sizeof(int));
  types_aux = (MPI_Datatype *) realloc(io_data_struct->types, needed * sizeof(MPI_Datatype));
  request_qty_aux = (size_t *) realloc(io_data_struct->request_qty, needed * sizeof(int));
  requests_aux = (MPI_Request **) realloc(io_data_struct->requests, needed * sizeof(MPI_Request *));
  windows_aux = (MPI_Win *) realloc(io_data_struct->windows, needed * sizeof(MPI_Win));
  arrays_aux = (void **) realloc(io_data_struct->arrays, needed * sizeof(void *));

  if(qty_aux == NULL || arrays_aux == NULL || requests_aux == NULL || types_aux == NULL || request_qty_aux == NULL || windows_aux == NULL) {
    fprintf(stderr, "Fatal error - No se ha podido realojar la memoria constante de datos a redistribuir/comunicar\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  for(i=io_data_struct->max_entries; i<needed; i++) { //realloc does not ensure a NULL value
    requests_aux[i] = NULL;
    windows_aux[i] = MPI_WIN_NULL;
    arrays_aux[i] = NULL;
  }

  // Check if old array can be freed
  if(io_data_struct->qty != qty_aux && io_data_struct->qty != NULL) free(io_data_struct->qty);
  if(io_data_struct->types != types_aux && io_data_struct->types != NULL) free(io_data_struct->types);
  if(io_data_struct->request_qty != request_qty_aux && io_data_struct->request_qty != NULL) free(io_data_struct->request_qty);
  if(io_data_struct->requests != requests_aux && io_data_struct->requests != NULL) free(io_data_struct->requests);
  if(io_data_struct->windows != windows_aux && io_data_struct->windows != NULL) free(io_data_struct->windows);
  if(io_data_struct->arrays != arrays_aux && io_data_struct->arrays != NULL) free(io_data_struct->arrays);

  io_data_struct->qty = qty_aux;
  io_data_struct->types = types_aux;
  io_data_struct->request_qty = request_qty_aux;
  io_data_struct->requests = requests_aux;
  io_data_struct->windows = windows_aux;
  io_data_struct->arrays = arrays_aux;
  io_data_struct->max_entries = needed;
}

/**
 * @brief Free metadata of a registry (and array buffers only for zombie ranks).
 *
 * Frees the per-entry parallel arrays (quantities, datatypes, RMA windows)
 * and any pending asynchronous requests together with the request-count
 * array. The data buffers themselves (@c arrays[]) are caller-owned and are
 * only freed here on zombie ranks (@c mall->zombie), i.e. ranks that MaM
 * itself is about to terminate.
 *
 * @param[in,out] io_data_struct Registry to free.
 */
void free_malleability_data_struct(malleability_data_t *io_data_struct) {
  size_t i, j, max;

  max = io_data_struct->entries;
  if(max != 0) {
    if(io_data_struct->qty != NULL) {
      free(io_data_struct->qty);
    }
    if(io_data_struct->types != NULL) {
      free(io_data_struct->types);
    }
    if(io_data_struct->requests != NULL && io_data_struct->request_qty != NULL) {
      for(i=0; i<max; i++) {
        if(io_data_struct->requests[i] != NULL) {
          for(j=0; j<io_data_struct->request_qty[i]; j++) {
            if(io_data_struct->requests[i][j] != MPI_REQUEST_NULL) {
                    MPI_Request_free(&(io_data_struct->requests[i][j]));
              io_data_struct->requests[i][j] = MPI_REQUEST_NULL;
            }
          }
          free(io_data_struct->requests[i]);
	}
      }
      free(io_data_struct->request_qty);
      free(io_data_struct->requests);  
    }

    if(io_data_struct->windows != NULL) {
      free(io_data_struct->windows);
    }

    if(io_data_struct->arrays != NULL) {
      for(i=0; i<max && mall->zombie; i++) { // Only for zombies that MaM will kill
        if(io_data_struct->arrays[i] != NULL) {
          free(io_data_struct->arrays[i]);
          io_data_struct->arrays[i] = NULL;
        }
      }
      free(io_data_struct->arrays);
    }
  }
}

//======================================================||
//======================================================||
//================MPI DERIVED DATATYPES=================||
//======================================================||
//======================================================||

/**
 * @brief Build a derived MPI datatype to send the entry counts of two registries.
 *
 * @param[in]  i_data_struct_rep  Replicated-data registry whose entry count is included.
 * @param[in]  i_data_struct_dist Distributed-data registry whose entry count is included.
 * @param[out] o_new_type         Resulting committed MPI datatype (caller must free it).
 */
void def_malleability_entries(malleability_data_t *i_data_struct_rep, malleability_data_t *i_data_struct_dist, MPI_Datatype *o_new_type) {
  int counts = 2;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  blocklengths[0] = blocklengths[1] = 1;
  types[0] = types[1] = type_size_t;

  // Get the base address
  MPI_Get_address(&(i_data_struct_rep->entries), &displs[0]);
  MPI_Get_address(&(i_data_struct_dist->entries), &displs[1]);

  MPI_Type_create_struct(counts, blocklengths, displs, types, o_new_type);
  MPI_Type_commit(o_new_type);
}

/**
 * @brief Build a derived MPI datatype to send the per-entry quantities and datatypes of two registries.
 *
 * The @c requests array is not included, since it is only needed on the
 * sources (parents).
 *
 * @param[in]  i_data_struct_rep  Replicated-data registry whose quantities/datatypes are included.
 * @param[in]  i_data_struct_dist Distributed-data registry whose quantities/datatypes are included.
 * @param[out] o_new_type         Resulting committed MPI datatype (caller must free it).
 *
 * @note TODO: Refactor?
 */
void def_malleability_qty_type(malleability_data_t *i_data_struct_rep, malleability_data_t *i_data_struct_dist, MPI_Datatype *o_new_type) {
  int counts = 6;
  int blocklengths[counts];
  MPI_Aint displs[counts];
  MPI_Datatype types[counts], type_size_t;
  MPI_Type_match_size(MPI_TYPECLASS_INTEGER, sizeof(size_t), &type_size_t);

  types[0] = types[1] = types[3] = types[4] = type_size_t;
  types[2] = types[5] = MPI_INT;
  blocklengths[0] = blocklengths[1] = blocklengths[2] = i_data_struct_rep->entries;
  blocklengths[3] = blocklengths[4] = blocklengths[5] = i_data_struct_dist->entries;

  MPI_Get_address((i_data_struct_rep->qty), &displs[0]);
  MPI_Get_address((i_data_struct_rep->request_qty), &displs[1]);
  MPI_Get_address((i_data_struct_rep->types), &displs[2]); // MPI_Datatype uses typedef int to be declared
  MPI_Get_address((i_data_struct_dist->qty), &displs[3]);
  MPI_Get_address((i_data_struct_dist->request_qty), &displs[4]);
  MPI_Get_address((i_data_struct_dist->types), &displs[5]); // MPI_Datatype uses typedef int to be declared

  MPI_Type_create_struct(counts, blocklengths, displs, types, o_new_type);
  MPI_Type_commit(o_new_type);
}


