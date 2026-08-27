#ifndef MAM_RMS_H
#define MAM_RMS_H

/**
 * @file MAM_RMS.h
 * @brief Host/CPU discovery helpers for MaM physical placement (internal).
 */

/**
 * @brief Discover the node list and per-node CPU counts into ::mall fields.
 *
 * Uses Slurm when @c MAM_USE_SLURM is enabled; otherwise MPI-based discovery.
 */
void MAM_check_hosts(void);

/**
 * @brief Report whether the current job spans more than one node for at least one of their MPI_COMM_WORLD.
 * @return Non-zero if inter-node, 0 otherwise.
 */
int MAM_Is_internode_group(void);

#endif
