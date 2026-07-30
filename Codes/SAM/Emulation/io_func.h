#ifndef IO_FUNC_H
#define IO_FUNC_H

/**
 * @file io_func.h
 * @brief File naming and byte I/O helpers for SAM I/O stages.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/** @brief Shared read-only input file name used by ::COMP_IOREAD stages. */
#define SAM_FILE_RNAME "SAM_R_FILE.tmp"
/** @brief Type character embedded in generated write-file names. */
#define SAM_FILE_WRITE 'W'
/** @brief Maximum bytes transferred per I/O operation (1 MiB). */
#define SAM_IO_MAX_BYTES 1048576.0 // 1MB

/**
 * @brief Build a per-rank temporary file name for an I/O stage.
 *
 * Format uses the Slurm job id when @c SLURM_JOB_ID is set, otherwise @c "0".
 *
 * @param[out] o_filename Receives the allocated name string (caller frees).
 * @param[in]  i_type     File type character (e.g. ::SAM_FILE_WRITE).
 * @param[in]  i_stid     Stage index embedded in the name.
 * @param[in]  i_rank     MPI rank embedded in the name.
 * @return 0 on success; aborts via MPI on @c snprintf failure.
 */
int generate_name_file(char **o_filename, char i_type, size_t i_stid, int i_rank);

/**
 * @brief Write @p i_n bytes from @p i_array to @p i_fd.
 *
 * On error, prints a message, closes @p i_fd, and returns @c -1.
 *
 * @param[in] i_fd    Open file descriptor.
 * @param[in] i_array Buffer to write.
 * @param[in] i_n     Number of bytes to write.
 * @return Bytes written, or @c -1 on error.
 */
ssize_t write_n_bytes(int i_fd, char *i_array, size_t i_n);

/**
 * @brief Read @p i_n bytes from @p i_fd into @p o_array.
 *
 * On EOF, rewinds the file and retries the read once. On error, prints a
 * message, closes @p i_fd, and returns @c -1.
 *
 * @param[in]  i_fd     Open file descriptor.
 * @param[out] o_array  Destination buffer.
 * @param[in]  i_n      Number of bytes to read.
 * @return Bytes read, or @c -1 on error.
 */
ssize_t read_n_bytes(int i_fd, char *o_array, size_t i_n);

#endif
