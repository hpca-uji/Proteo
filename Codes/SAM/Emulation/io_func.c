#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <mpi.h>
#include "io_func.h"

/**
 * @file io_func.c
 * @brief Implementation of SAM file naming and byte I/O helpers.
 */

//--------------PRIVATE CONSTANTS------------------//
/** @brief Fixed overhead in the generated file-name format string. */
#define SAM_FILE_CONSTANT_SIZE 20 // 18 Chars + 3 Int as char + '\0'
/** @brief printf-style pattern for per-rank stage temporary files. */
#define SAM_FILE_NAME "SAM_%c_J%s_S%03ld_ID%d.tmp"

int generate_name_file(char **o_filename, char i_type, size_t i_stid, int i_rank) {
  int get_env, j_c, r_c, count, err_sn;

  char *tmp_job_id = getenv("SLURM_JOB_ID");
  if (tmp_job_id == NULL) {
    j_c = 2;
    tmp_job_id = malloc(j_c * sizeof *tmp_job_id);
    tmp_job_id[0] = '0';
    tmp_job_id[1] = '\0';
    get_env = 0;
  } else {
    j_c = snprintf(NULL, 0, "%s", tmp_job_id);
    get_env = 1;
  }
  r_c = snprintf(NULL, 0, "%d", i_rank);
  count = SAM_FILE_CONSTANT_SIZE + j_c + r_c;

  *o_filename = malloc(count * sizeof **o_filename);
  err_sn = snprintf(*o_filename, count, SAM_FILE_NAME, i_type, tmp_job_id, i_stid, i_rank);
  if (err_sn < 0) {
    perror("SAM Generate Name snprintf error");
    MPI_Abort(MPI_COMM_WORLD, -1);
    return -1;
  }

  if (!get_env) { free(tmp_job_id); }
  return 0;
}

ssize_t write_n_bytes(int i_fd, char *i_array, size_t i_n) {
    ssize_t written = write(i_fd, i_array, i_n);
    if (written < 0) {
        perror("SAM write");
        close(i_fd);
        return -1;
    }
    return written;
}

ssize_t read_n_bytes(int i_fd, char *o_array, size_t i_n) {
    ssize_t bytes_read = read(i_fd, o_array, i_n);
    if (bytes_read < 0) {
      perror("read");
      close(i_fd);
      return -1;
    } else if (bytes_read == 0) {
      lseek(i_fd, 0, SEEK_SET);
      bytes_read = read_n_bytes(i_fd, o_array, i_n);
    }
    return bytes_read;
}
