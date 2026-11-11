/*
 * Copyright (C) 2019,2020 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "local-path.h"

void smf_trigger_session_release(smf_sess_t *sess,
    ogs_sbi_stream_t *stream, int pfcp_trigger)
{
    int rv;
    smf_event_t *e;
    ogs_pool_id_t sid;

    /* Ensure session is valid */
    ogs_assert(sess);

    /* Create a session release event */
    e = smf_event_new(SMF_EVT_SESSION_RELEASE);
    ogs_assert(e);

    /* Assign session ID */
    e->sess_id = sess->id;

    /* Store the PFCP trigger in the SBI state field */
    e->h.sbi.state = pfcp_trigger;

    /* Attach SBI stream ID if provided */
    if (stream) {
        sid = ogs_sbi_id_from_stream(stream);
        ogs_assert(sid >= OGS_MIN_POOL_ID &&
                   sid <= OGS_MAX_POOL_ID);
        e->h.sbi.data = OGS_UINT_TO_POINTER(sid);
    }

    /* Push event to the application queue */
    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_error("ogs_queue_push failed:%d", rv);
        ogs_event_free(e);
    }
}

/*
 * Shutdown drain (KI-119)
 *
 * The SMF keeps its sessions in memory only. Without the drain, a restart
 * drops every PDN connection while the SGW, the MME and the UE keep them,
 * and the UE stays without a working PDN connection (e.g. off IMS) until it
 * re-attaches. On SIGTERM, the SMF releases each EPC session over S5/S8/S2b with
 * a PGW-initiated bearer deactivation of the default bearer (TS 23.401
 * 5.4.4.1), i.e. a Delete Bearer Request with the Linked EPS Bearer ID and
 * Cause "Reactivation requested" (TS 29.274 7.2.9.1, Table 8.4-1). The
 * MME is expected to deactivate the PDN connection towards the UE with ESM
 * cause #39 "reactivation requested" (TS 24.301 6.4.4), so that the UE
 * re-establishes it. Note: a stock Open5GS MME always sends ESM cause #36
 * "regular deactivation" and does not detach the UE when this is its last
 * PDN connection; the UE then decides itself whether to reconnect.
 *
 * smf_terminate() calls smf_drain_request() and waits for
 * smf_drain_finished(). The event loop releases the sessions through the
 * normal state machine (Delete Bearer Response -> PFCP Session Deletion ->
 * Gx/Gy/S6b termination) and ends the drain when no EPC session is left or
 * `shutdown_drain_timeout` has expired.
 */
#define SMF_DRAIN_CHECK_INTERVAL ogs_time_from_msec(100)

static ogs_thread_mutex_t drain_mutex;
static bool drain_done;
static ogs_time_t drain_started;

static bool drain_candidate(smf_sess_t *sess)
{
    ogs_assert(sess);

    /* GTPv2 sessions: S5/S8 and S2b (ePDG; on a Delete Bearer Request for
     * the default bearer it deletes the IKE SA, so the UE sets up a new
     * one). Not 5GC (unchanged) and not Gn/GTPv1. */
    return sess->epc == true && sess->gtp.version == 2;
}

static int drain_count(void)
{
    smf_ue_t *smf_ue = NULL;
    smf_sess_t *sess = NULL;
    int count = 0;

    ogs_list_for_each(&smf_self()->smf_ue_list, smf_ue) {
        ogs_list_for_each(&smf_ue->sess_list, sess) {
            if (drain_candidate(sess))
                count++;
        }
    }

    return count;
}

static void drain_finish(void)
{
    ogs_thread_mutex_lock(&drain_mutex);
    drain_done = true;
    ogs_thread_mutex_unlock(&drain_mutex);
}

static void drain_check(void *data)
{
    int count;
    ogs_time_t elapsed;

    count = drain_count();
    elapsed = ogs_get_monotonic_time() - drain_started;

    if (count == 0) {
        ogs_info("Shutdown drain: all EPC sessions released in %lld ms",
                (long long)ogs_time_to_msec(elapsed));
        drain_finish();
        return;
    }

    if (elapsed >= smf_self()->shutdown_drain_timeout) {
        ogs_warn("Shutdown drain: timeout after %lld ms, "
                "%d EPC session(s) not released",
                (long long)ogs_time_to_msec(elapsed), count);
        drain_finish();
        return;
    }

    if (!smf_self()->t_shutdown_drain) {
        smf_self()->t_shutdown_drain =
            ogs_timer_add(ogs_app()->timer_mgr, drain_check, NULL);
        ogs_assert(smf_self()->t_shutdown_drain);
    }
    ogs_timer_start(smf_self()->t_shutdown_drain, SMF_DRAIN_CHECK_INTERVAL);
}

void smf_drain_request(void)
{
    int rv;
    smf_event_t *e = NULL;

    /* Initialized before the event is queued, so the event loop sees it */
    ogs_thread_mutex_init(&drain_mutex);
    drain_done = false;

    e = smf_event_new(SMF_EVT_SHUTDOWN_DRAIN);
    ogs_assert(e);

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_error("ogs_queue_push failed:%d", rv);
        ogs_event_free(e);
        drain_finish();
        return;
    }
    ogs_pollset_notify(ogs_app()->pollset);
}

bool smf_drain_finished(void)
{
    bool done;

    ogs_thread_mutex_lock(&drain_mutex);
    done = drain_done;
    ogs_thread_mutex_unlock(&drain_mutex);

    return done;
}

void smf_drain_start(void)
{
    smf_ue_t *smf_ue = NULL;
    smf_sess_t *sess = NULL;
    int count = 0;

    smf_self()->draining = true;
    drain_started = ogs_get_monotonic_time();

    ogs_list_for_each(&smf_self()->smf_ue_list, smf_ue) {
        ogs_list_for_each(&smf_ue->sess_list, sess) {
            if (!drain_candidate(sess))
                continue;
            smf_trigger_session_release(
                    sess, NULL, OGS_PFCP_DELETE_TRIGGER_SMF_INITIATED);
            count++;
        }
    }

    ogs_info("Shutdown drain: releasing %d EPC session(s) "
            "[timeout:%lld ms]", count,
            (long long)ogs_time_to_msec(smf_self()->shutdown_drain_timeout));

    drain_check(NULL);
}
