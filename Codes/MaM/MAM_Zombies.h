#ifndef MAM_ZOMBIES_H
#define MAM_ZOMBIES_H

/**
 * @file MAM_Zombies.h
 * @brief Merge-shrink zombie service: suspend/awake ranks that leave the active set.
 */

/**
 * @brief Allocate the PID table for zombie tracking (call once at init).
 */
void MAM_Zombies_service_init(void);

/**
 * @brief Awake suspended zombies and free the PID table.
 * @return Non-zero if abort is requested (internode zombies remain), else 0.
 */
int MAM_Zombies_service_free(void);

/**
 * @brief Collect new zombies, split them out of @c original_comm, and suspend them.
 */
void MAM_Zombies_update(void);

#endif
