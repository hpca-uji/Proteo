#ifndef IO_FUNC_H
#define IO_FUNC_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define SAM_FILE_RNAME "SAM_R_FILE.tmp"
#define SAM_FILE_WRITE 'W'
#define SAM_IO_MAX_BYTES 1048576.0 // 1MB

int generate_name_file(char **filename, char type, size_t stid);
ssize_t write_n_bytes(int fd, char *array, size_t n);
ssize_t read_n_bytes(int fd, char *array, size_t n);

#endif