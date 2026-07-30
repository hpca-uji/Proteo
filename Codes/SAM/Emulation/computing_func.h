#ifndef COMPUTING_FUNC_H
#define COMPUTING_FUNC_H

/**
 * @file computing_func.h
 * @brief Synthetic compute kernels used by SAM compute stages.
 */

/**
 * @brief Synthetic CPU load over an @p i_n by @p i_n flat matrix.
 *
 * Accumulates the sum of @c (int)(element * element) for every element.
 * This is not a real matrix multiply; it only burns CPU time.
 *
 * @param[in] i_matrix Flat row-major matrix of length @p i_n * @p i_n.
 * @param[in] i_n      Matrix dimension.
 * @return Accumulated sum-of-squares value (as @c double).
 */
double computeMatrix(double *i_matrix, int i_n);

/**
 * @brief Approximate π with a serial rectangle (midpoint) rule.
 *
 * @param[in] i_n Number of rectangles.
 * @return Approximate value of π.
 */
double computePiSerial(int i_n);

/**
 * @brief Allocate and fill an @p i_n by @p i_n matrix for ::computeMatrix.
 *
 * Frees any previous matrix pointed to by @p io_matrix first.
 *
 * @param[in,out] io_matrix Receives the newly allocated matrix pointer.
 * @param[in]     i_n       Matrix dimension.
 */
void initMatrix(double **io_matrix, size_t i_n);

/**
 * @brief Free a matrix allocated by ::initMatrix.
 * @param[in,out] io_matrix Pointer to the matrix pointer (set to @c NULL).
 */
void freeMatrix(double **io_matrix);

#endif
