#ifndef MAM_GENERIC_SPAWN_H
#define MAM_GENERIC_SPAWN_H

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include "Spawn_DataStructure.h"

/**
 * @file GenericSpawn.h
 * @brief Public entry points to create/join a new group of processes (spawn) in MaM.
 *
 * Dispatches to the Baseline or Merge spawn method (@c mam_spawn_methods) and,
 * when configured, runs the spawn configuration/creation on an auxiliary
 * thread (@c spawn_is_async). Sources drive ::init_spawn and
 * ::check_spawn_state; children call ::malleability_connect_children once
 * they have been spawned.
 *
 * Terminology: sources are the ranks that exist before the reconfiguration
 * (also parents); children are the ranks newly created by the spawn (always
 * targets); targets are the ranks that continue after the reconfiguration
 * (children plus, under Merge, reused sources; a process can be a target
 * without being a child).
 */

/**
 * @brief Request creation of a new group of @c target_qty processes (sources side).
 *
 * Builds the spawn configuration from @p i_comm and, depending on whether the
 * configured strategy is synchronous or asynchronous (@c spawn_is_async):
 * - Synchronous: creates the processes immediately (blocking call).
 * - Asynchronous: starts an auxiliary thread that configures/creates them in
 *   the background; ::check_spawn_state must be called afterwards to
 *   retrieve the result.
 *
 * @param[in]     i_comm   Communicator among the sources requesting the spawn.
 * @param[in,out] io_child Receives the intercommunicator (or, under Merge,
 *                          the merged/split communicator) once ready. Only
 *                          fully populated when the returned state is
 *                          @c MAM_I_SPAWN_COMPLETED; otherwise
 *                          ::check_spawn_state must be called.
 * @return Spawn state (@c mam_inner_states). If different from
 *         @c MAM_I_SPAWN_COMPLETED, ::check_spawn_state must be called to
 *         finish the operation.
 */
int init_spawn(MPI_Comm i_comm, MPI_Comm *io_child);

/**
 * @brief Check/advance an in-progress spawn requested via ::init_spawn (sources side).
 *
 * For an asynchronous spawn, polls (or, if @p i_wait_completed, blocks until)
 * the auxiliary thread's progress and returns the resulting communicator once
 * ready. For a synchronous Merge shrink, performs the pending zombie split now
 * that data redistribution has completed.
 *
 * @param[in,out] io_child         Receives the resulting communicator to the
 *                                  new group of processes once the spawn
 *                                  completes.
 * @param[in]     i_comm           Communicator among the sources, used to
 *                                  synchronise/broadcast the spawn state.
 * @param[in]     i_wait_completed Non-zero (@c MAM_WAIT_COMPLETION) to block
 *                                  until completion; zero
 *                                  (@c MAM_CHECK_COMPLETION) to just poll the
 *                                  current state.
 * @return Spawn state (@c mam_inner_states); @c MAM_I_SPAWN_COMPLETED or
 *         @c MAM_I_SPAWN_ADAPTED once finished.
 */
int check_spawn_state(MPI_Comm *io_child, MPI_Comm i_comm, int i_wait_completed);

/**
 * @brief Blocking entry point for newly spawned children to join the reconfiguration.
 *
 * Builds a minimal spawn configuration and runs the children-side path of the
 * configured spawn method (Baseline or Merge), ensuring all spawn-creation
 * tasks complete correctly before returning. Children also obtain basic
 * information about the sources (process counts and root id) needed for the
 * later data-redistribution step.
 *
 * @param[in,out] io_parents Intercommunicator to the sources
 *                             (@c MPI_Comm_get_parent result) on entry;
 *                             under Merge, replaced by the merged intracomm
 *                             of targets on exit.
 */
void malleability_connect_children(MPI_Comm *io_parents);


/**
 * @brief Clear the @c MAM_I_SPAWN_ADAPT_POSTPONE flag blocking the auxiliary
 *        spawn threads under Merge shrink.
 *
 * The auxiliary threads are blocked on this flag so that the Merge shrink
 * (zombie split) does not proceed until data redistribution has completed.
 * Clearing it lets the threads continue.
 *
 * As a safety measure, the change is only applied if all 3 conditions hold:
 * the current internal spawn state is @c MAM_I_SPAWN_ADAPT_POSTPONE,
 * @p i_outside_state is @c MAM_I_SPAWN_ADAPT_PENDING, and the spawn is
 * asynchronous.
 *
 * @param[in] i_outside_state Spawn state as seen by the MaM state machine
 *                              outside of this module.
 */
void unset_spawn_postpone_flag(int i_outside_state);

#endif
