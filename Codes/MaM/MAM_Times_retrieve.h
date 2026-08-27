#ifndef MAM_TIMES_RETRIEVE_H
#define MAM_TIMES_RETRIEVE_H

/**
 * @file MAM_Times_retrieve.h
 * @brief Public API to read durations from the last reconfiguration.
 */

/**
 * @brief Return durations (seconds) from the last completed reconfiguration.
 *
 * Any output pointer may be @c NULL to skip that field. Spawn time is stored
 * directly; sync/async/user/malleability are end−start differences.
 *
 * @param[out] o_sp_time   Spawn duration (or @c NULL).
 * @param[out] o_sy_time   Synchronous redistribution duration (or @c NULL).
 * @param[out] o_asy_time  Asynchronous redistribution duration (or @c NULL).
 * @param[out] o_user_time User-callback phase duration (or @c NULL).
 * @param[out] o_mall_time Whole malleability operation duration (or @c NULL).
 */
void MAM_Retrieve_times(double *o_sp_time, double *o_sy_time, double *o_asy_time,
                        double *o_user_time, double *o_mall_time);
#endif
