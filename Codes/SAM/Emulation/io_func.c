#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mpi.h>
#include "io_func.h"

//--------------PRIVATE CONSTANTS------------------//
#define SAM_FILE_CONSTANT_SIZE 20 // 18 Chars + 3 Int as char + '\0'
#define SAM_FILE_NAME "SAM_%c_J%s_S%03ld_ID%d.tmp"

/* Create a file with read/write permissions */
int generate_name_file(char **filename, char type, size_t stid, int rank) {
  int get_env, j_c, r_c, count, err_sn;

  char *tmp_job_id = getenv("SLURM_JOB_ID");
  if(tmp_job_id == NULL) { 
    j_c = 2;
    tmp_job_id = malloc(j_c * sizeof *tmp_job_id);
    tmp_job_id[0] = '0';
    tmp_job_id[1] = '\0'; 
    get_env = 0;
  } else {
    j_c = snprintf(NULL, 0, "%s", tmp_job_id);
    get_env = 1;
  }
  r_c = snprintf(NULL, 0, "%d", rank);
  count = SAM_FILE_CONSTANT_SIZE + j_c + r_c;

  *filename = malloc(count * sizeof **filename);
  err_sn = snprintf(*filename, count, SAM_FILE_NAME, type, tmp_job_id, stid, rank);
  if(err_sn < 0) { 
    perror("SAM Generate Name snprintf error"); 
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }

  if(!get_env) { free(tmp_job_id); }
  return 0;
}

/* Write N bytes to a file */
ssize_t write_n_bytes(int fd, char *array, size_t n) {
    ssize_t written = write(fd, array, n);
    if (written < 0) {
        perror("SAM write");
        close(fd);
        return -1;
    }
    return written;
}


// FIXME: En la app de python que se puedan desplegar las phases.
// FIXME: Asegurar que lean segun tamanyo fichero, no tamanyo total
ssize_t read_n_bytes(int fd, char *array, size_t n) {
    ssize_t bytes_read = read(fd, array, n);
    if (bytes_read < 0) {
      perror("read");
      close(fd);
      return -1;
    } else if (bytes_read == 0) {
      lseek(fd, 0, SEEK_SET);
      bytes_read = read_n_bytes(fd, array, n);
    } 
    return bytes_read;
}