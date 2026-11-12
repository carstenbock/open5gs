/*
 * Copyright (C) 2019-2023 by Sukchan Lee <acetcom@gmail.com>
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

#include "gtp-path.h"
#include "pfcp-path.h"

static void _gtpv2_c_recv_cb(short when, ogs_socket_t fd, void *data)
{
    sgwc_event_t *e = NULL;
    int rv;
    ssize_t size;
    ogs_pkbuf_t *pkbuf = NULL;
    ogs_sockaddr_t from;
    ogs_gtp_node_t *gnode = NULL;
    char frombuf[OGS_ADDRSTRLEN];

    ogs_assert(fd != INVALID_SOCKET);

    pkbuf = ogs_pkbuf_alloc(NULL, OGS_MAX_SDU_LEN);
    ogs_assert(pkbuf);
    ogs_pkbuf_put(pkbuf, OGS_MAX_SDU_LEN);

    size = ogs_recvfrom(fd, pkbuf->data, pkbuf->len, 0, &from);
    if (size <= 0) {
        ogs_log_message(OGS_LOG_ERROR, ogs_socket_errno,
                "ogs_recvfrom() failed");
        ogs_pkbuf_free(pkbuf);
        return;
    }

    ogs_pkbuf_trim(pkbuf, size);

    /*
     * 5.5.2 in spec 29.274
     *
     * If a peer's TEID is not available, the TEID field still shall be
     * present in the header and its value shall be set to "0" in the
     * following messages:
     *
     * - Create Session Request message on S2a/S2b/S5/S8
     *
     * - Create Session Request message on S4/S11, if for a given UE,
     *   the SGSN/MME has not yet obtained the Control TEID of the SGW.
     *
     * - If a node receives a message and the TEID-C in the GTPv2 header of
     *   the received message is not known, it shall respond with
     *   "Context not found" Cause in the corresponding response message
     *   to the sender, the TEID used in the GTPv2-C header in the response
     *   message shall be then set to zero.
     *
     * - If a node receives a request message containing protocol error,
     *   e.g. Mandatory IE missing, which requires the receiver to reject
     *   the message as specified in clause 7.7, it shall reject
     *   the request message. For the response message, the node should
     *   look up the remote peer's TEID and accordingly set the GTPv2-C
     *   header TEID and the message cause code. As an implementation
     *   option, the node may not look up the remote peer's TEID and
     *   set the GTPv2-C header TEID to zero in the response message.
     *   However in this case, the cause code shall not be set to
     *   "Context not found".
     */
    gnode = ogs_gtp_node_find_by_addr(&sgwc_self()->pgw_s5c_list, &from);
    if (gnode) {
        e = sgwc_event_new(SGWC_EVT_S5C_MESSAGE);
        ogs_assert(e);
        e->gnode = gnode;
    } else {
        gnode = ogs_gtp_node_find_by_addr(&sgwc_self()->mme_s11_list, &from);
        if (!gnode) {
            gnode = ogs_gtp_node_add_by_addr(&sgwc_self()->mme_s11_list, &from);
            if (!gnode) {
                ogs_error("Failed to create new gnode(%s:%u), mempool full, ignoring msg!",
                          OGS_ADDR(&from, frombuf), OGS_PORT(&from));
                ogs_pkbuf_free(pkbuf);
                return;
            }
            gnode->sock = data;
        }
        e = sgwc_event_new(SGWC_EVT_S11_MESSAGE);
        ogs_assert(e);
        e->gnode = gnode;
    }

    e->pkbuf = pkbuf;

    rv = ogs_queue_push(ogs_app()->queue, e);
    if (rv != OGS_OK) {
        ogs_error("ogs_queue_push() failed:%d", (int)rv);
        ogs_pkbuf_free(e->pkbuf);
        sgwc_event_free(e);
    }
}

int sgwc_gtp_open(void)
{
    ogs_socknode_t *node = NULL;
    ogs_sock_t *sock = NULL;

    ogs_list_for_each(&ogs_gtp_self()->gtpc_list, node) {
        sock = ogs_gtp_server(node);
        if (!sock) return OGS_ERROR;

        node->poll = ogs_pollset_add(ogs_app()->pollset,
                OGS_POLLIN, sock->fd, _gtpv2_c_recv_cb, sock);
        ogs_assert(node->poll);
    }
    ogs_list_for_each(&ogs_gtp_self()->gtpc_list6, node) {
        sock = ogs_gtp_server(node);
        if (!sock) return OGS_ERROR;

        node->poll = ogs_pollset_add(ogs_app()->pollset,
                OGS_POLLIN, sock->fd, _gtpv2_c_recv_cb, sock);
        ogs_assert(node->poll);
    }

    OGS_SETUP_GTPC_SERVER;

    /* KI-174 */
    sgwc_pgw_path_init();
    ogs_assert(ogs_gtp_self()->gtpc_sock || ogs_gtp_self()->gtpc_sock6);
    ogs_assert(ogs_gtp_self()->gtpc_addr || ogs_gtp_self()->gtpc_addr6);

    return OGS_OK;
}

void sgwc_gtp_close(void)
{
    /* KI-174 */
    if (sgwc_self()->t_pgw_echo) {
        ogs_timer_delete(sgwc_self()->t_pgw_echo);
        sgwc_self()->t_pgw_echo = NULL;
    }
    if (sgwc_self()->t_pgw_release) {
        ogs_timer_delete(sgwc_self()->t_pgw_release);
        sgwc_self()->t_pgw_release = NULL;
    }

    ogs_socknode_remove_all(&ogs_gtp_self()->gtpc_list);
    ogs_socknode_remove_all(&ogs_gtp_self()->gtpc_list6);
}

static void bearer_timeout(ogs_gtp_xact_t *xact, void *data)
{
    sgwc_bearer_t *bearer = data;
    ogs_pool_id_t bearer_id = OGS_INVALID_POOL_ID;
    sgwc_sess_t *sess = NULL;
    sgwc_ue_t *sgwc_ue = NULL;
    uint8_t type = 0;

    ogs_assert(xact);
    type = xact->seq[0].type;

    ogs_assert(data);
    bearer_id = OGS_POINTER_TO_UINT(data);
    ogs_assert(bearer_id >= OGS_MIN_POOL_ID && bearer_id <= OGS_MAX_POOL_ID);

    bearer = sgwc_bearer_find_by_id(bearer_id);
    if (!bearer) {
        ogs_error("Bearer[%d] has already been removed [%d]", bearer_id, type);
        return;
    }

    sess = sgwc_sess_find_by_id(bearer->sess_id);
    ogs_assert(sess);
    sgwc_ue = sgwc_ue_find_by_id(sess->sgwc_ue_id);
    ogs_assert(sgwc_ue);

    switch (type) {
    case OGS_GTP2_DOWNLINK_DATA_NOTIFICATION_TYPE:
        ogs_warn("[%s] No Downlink Data Notification ACK", sgwc_ue->imsi_bcd);
        break;
    default:
        ogs_error("GTP Timeout : IMSI[%s] Message-Type[%d]",
                sgwc_ue->imsi_bcd, type);
    }
}

int sgwc_gtp_send_create_session_response(
    sgwc_sess_t *sess, ogs_gtp_xact_t *xact)
{
    int rv;

    sgwc_ue_t *sgwc_ue = NULL;

    ogs_gtp2_header_t h;
    ogs_pkbuf_t *pkbuf = NULL;

    ogs_assert(sess);
    sgwc_ue = sgwc_ue_find_by_id(sess->sgwc_ue_id);
    ogs_assert(sgwc_ue);
    ogs_assert(xact);

    memset(&h, 0, sizeof(ogs_gtp2_header_t));
    h.type = OGS_GTP2_CREATE_SESSION_RESPONSE_TYPE;
    h.teid = sgwc_ue->mme_s11_teid;

    pkbuf = sgwc_s11_build_create_session_response(h.type, sess);
    if (!pkbuf) {
        ogs_error("sgwc_s11_build_create_session_response() failed");
        return OGS_ERROR;
    }

    rv = ogs_gtp_xact_update_tx(xact, &h, pkbuf);
    if (rv != OGS_OK) {
        ogs_error("ogs_gtp_xact_update_tx() failed");
        return OGS_ERROR;
    }

    rv = ogs_gtp_xact_commit(xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

int sgwc_gtp_send_downlink_data_notification(
    uint8_t cause_value, sgwc_bearer_t *bearer)
{
    int rv;

    sgwc_ue_t *sgwc_ue = NULL;
    sgwc_sess_t *sess = NULL;

    ogs_gtp_xact_t *gtp_xact = NULL;

    ogs_gtp2_header_t h;
    ogs_pkbuf_t *pkbuf = NULL;

    ogs_assert(bearer);

    sess = sgwc_sess_find_by_id(bearer->sess_id);
    ogs_assert(sess);
    sgwc_ue = sgwc_ue_find_by_id(bearer->sgwc_ue_id);
    ogs_assert(sgwc_ue);
    ogs_assert(sgwc_ue->gnode);

    ogs_info("Downlink Data Notification [%d]", bearer->id);
    ogs_info("    MME_S11_TEID[%d] SGW_S11_TEID[%d]",
        sgwc_ue->mme_s11_teid, sgwc_ue->sgw_s11_teid);

    memset(&h, 0, sizeof(ogs_gtp2_header_t));
    h.type = OGS_GTP2_DOWNLINK_DATA_NOTIFICATION_TYPE;
    h.teid = sgwc_ue->mme_s11_teid;

    pkbuf = sgwc_s11_build_downlink_data_notification(cause_value, bearer);
    if (!pkbuf) {
        ogs_error("sgwc_s11_build_downlink_data_notification() failed");
        return OGS_ERROR;
    }

    gtp_xact = ogs_gtp_xact_local_create(
            sgwc_ue->gnode, &h, pkbuf, bearer_timeout,
            OGS_UINT_TO_POINTER(bearer->id));
    if (!gtp_xact) {
        ogs_error("ogs_gtp_xact_local_create() failed");
        return OGS_ERROR;
    }
    gtp_xact->local_teid = sgwc_ue->sgw_s11_teid;

    rv = ogs_gtp_xact_commit(gtp_xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

/*
 * Visited-core patch (KI-119): a PDN connection lost at the PGW.
 *
 * Upstream deletes the session locally when the PGW-U answers S5/S8-U
 * traffic of a default bearer with a GTP-U Error Indication, so the MME
 * and the UE keep a dead PDN connection. Tell the MME with a Delete Bearer
 * Request for the default bearer (Linked EBI) and cause "Reactivation
 * requested" (TS 29.274 Delete Bearer Request, cause values Table 8.4-1),
 * so that the UE re-requests the PDN connection instead of keeping a dead
 * one (TS 23.007 restoration principle: the bearers of a lost PDN
 * connection are released towards the UE). The session is deleted locally
 * when the MME answers or the request times out.
 */
static void delete_bearer_timeout(ogs_gtp_xact_t *xact, void *data)
{
    sgwc_bearer_t *bearer = NULL;
    sgwc_sess_t *sess = NULL;
    sgwc_ue_t *sgwc_ue = NULL;
    ogs_pool_id_t bearer_id = OGS_INVALID_POOL_ID;

    ogs_assert(xact);
    ogs_assert(data);
    bearer_id = OGS_POINTER_TO_UINT(data);

    bearer = sgwc_bearer_find_by_id(bearer_id);
    if (!bearer) {
        ogs_warn("Bearer[%d] has already been removed", bearer_id);
        return;
    }
    sess = sgwc_sess_find_by_id(bearer->sess_id);
    if (!sess) {
        ogs_warn("Session[%d] has already been removed", bearer->sess_id);
        return;
    }
    sgwc_ue = sgwc_ue_find_by_id(sess->sgwc_ue_id);

    ogs_error("[%s] No Delete Bearer Response; deleting the session locally",
            sgwc_ue ? sgwc_ue->imsi_bcd : "Unknown");
    ogs_expect(OGS_OK ==
        sgwc_pfcp_send_session_deletion_request(
            sess, OGS_INVALID_POOL_ID, NULL));
}

int sgwc_gtp_send_delete_bearer_request_to_mme(
        sgwc_sess_t *sess, uint8_t cause_value)
{
    int rv;

    sgwc_ue_t *sgwc_ue = NULL;
    sgwc_bearer_t *bearer = NULL;

    ogs_gtp_xact_t *gtp_xact = NULL;

    ogs_gtp2_message_t message;
    ogs_gtp2_delete_bearer_request_t *req = NULL;
    ogs_gtp2_cause_t cause;
    ogs_pkbuf_t *pkbuf = NULL;

    ogs_assert(sess);
    sgwc_ue = sgwc_ue_find_by_id(sess->sgwc_ue_id);
    ogs_assert(sgwc_ue);
    bearer = sgwc_default_bearer_in_sess(sess);
    ogs_assert(bearer);

    if (!sgwc_ue->gnode || !sgwc_ue->mme_s11_teid) {
        ogs_error("[%s] No S11 path to the MME", sgwc_ue->imsi_bcd);
        return OGS_ERROR;
    }

    if (sess->release_to_mme_pending) {
        ogs_warn("[%s] Delete Bearer Request to MME already pending",
                sgwc_ue->imsi_bcd);
        return OGS_OK;
    }

    ogs_info("[%s] Delete Bearer Request to MME [LBI:%d CAUSE:%d]",
            sgwc_ue->imsi_bcd, bearer->ebi, cause_value);

    memset(&message, 0, sizeof(ogs_gtp2_message_t));
    message.h.type = OGS_GTP2_DELETE_BEARER_REQUEST_TYPE;
    message.h.teid = sgwc_ue->mme_s11_teid;

    req = &message.delete_bearer_request;

    req->linked_eps_bearer_id.presence = 1;
    req->linked_eps_bearer_id.u8 = bearer->ebi;

    memset(&cause, 0, sizeof(cause));
    cause.value = cause_value;
    req->cause.presence = 1;
    req->cause.len = sizeof(cause);
    req->cause.data = &cause;

    pkbuf = ogs_gtp2_build_msg(&message);
    if (!pkbuf) {
        ogs_error("ogs_gtp2_build_msg() failed");
        return OGS_ERROR;
    }

    gtp_xact = ogs_gtp_xact_local_create(
            sgwc_ue->gnode, &message.h, pkbuf, delete_bearer_timeout,
            OGS_UINT_TO_POINTER(bearer->id));
    if (!gtp_xact) {
        ogs_error("ogs_gtp_xact_local_create() failed");
        return OGS_ERROR;
    }
    gtp_xact->local_teid = sgwc_ue->sgw_s11_teid;
    sess->release_to_mme_pending = true;

    rv = ogs_gtp_xact_commit(gtp_xact);
    ogs_expect(rv == OGS_OK);

    return rv;
}

/*
 * KI-174: S5/S8 path management towards the PGWs (TS 29.274 §7.1,
 * TS 23.007 §16.1.1). The SGW sends Echo Requests to every PGW it has
 * sessions with and learns the PGW's restart counter from Echo Responses
 * and Create Session Responses. When the counter changes, the PDN
 * connections set up before the PGW restart are released: a Delete Bearer
 * Request with cause "Reactivation requested" goes to the MME (KI-119
 * path: ESM #39, or detach with re-attach required for the last PDN
 * connection), and the session is deleted when the MME answers.
 */
#define SGWC_PGW_RELEASE_BATCH      50
#define SGWC_PGW_RELEASE_PACING     ogs_time_from_msec(100)

static void pgw_echo_timeout(void *data)
{
    ogs_gtp_node_t *pgw = NULL;

    ogs_list_for_each(&sgwc_self()->pgw_s5c_list, pgw)
        ogs_gtp2_send_echo_request(
                pgw, sgwc_self()->gtpc_restart_counter, 0);

    if (sgwc_self()->pgw_echo_interval)
        ogs_timer_start(sgwc_self()->t_pgw_echo,
                sgwc_self()->pgw_echo_interval);
}

static void pgw_release_timeout(void *data)
{
    sgwc_pgw_release_lost_sessions();
}

void sgwc_pgw_path_init(void)
{
    sgwc_self()->t_pgw_echo = ogs_timer_add(
            ogs_app()->timer_mgr, pgw_echo_timeout, NULL);
    ogs_assert(sgwc_self()->t_pgw_echo);
    sgwc_self()->t_pgw_release = ogs_timer_add(
            ogs_app()->timer_mgr, pgw_release_timeout, NULL);
    ogs_assert(sgwc_self()->t_pgw_release);

    if (sgwc_self()->pgw_echo_interval)
        ogs_timer_start(sgwc_self()->t_pgw_echo,
                sgwc_self()->pgw_echo_interval);
}

static bool is_pgw_node(ogs_gtp_node_t *gnode)
{
    ogs_gtp_node_t *pgw = NULL;

    ogs_list_for_each(&sgwc_self()->pgw_s5c_list, pgw)
        if (pgw == gnode)
            return true;
    return false;
}

void sgwc_pgw_recovery_received(
        ogs_gtp_node_t *gnode, uint8_t recovery, const char *source)
{
    char buf[OGS_ADDRSTRLEN];
    sgwc_pgw_state_t *state = NULL;
    bool first;

    if (!gnode || !is_pgw_node(gnode))
        return;

    if (!gnode->data_ptr) {
        gnode->data_ptr = ogs_calloc(1, sizeof(sgwc_pgw_state_t));
        ogs_assert(gnode->data_ptr);
    }
    state = gnode->data_ptr;

    first = !state->restart_counter_known;

    /*
     * KI-182: an answer of the old PGW instance that arrives after the
     * restart was detected carries the previous counter. It is not another
     * restart; its session belongs to the old instance and is released
     * with the others (its own counter differs from the current one).
     */
    if (state->restart_counter_known && state->previous_counter_known &&
        recovery != state->restart_counter &&
        recovery == state->previous_counter) {
        ogs_warn("PGW [%s] late answer of the old instance "
                "(restart counter %d, %s)",
                OGS_ADDR(&gnode->addr, buf), recovery, source);
        state->release_pending = true;
        ogs_timer_start(sgwc_self()->t_pgw_release, ogs_time_from_msec(1));
        return;
    }

    {
        uint8_t old_counter = state->restart_counter;
        bool old_known = state->restart_counter_known;

        if (ogs_gtp2_peer_restart_detected(&state->restart_counter,
                    &state->restart_counter_known, recovery)) {
            state->previous_counter = old_counter;
            state->previous_counter_known = old_known;
            ogs_warn("PGW [%s] restarted (restart counter now %d, %s): "
                    "releasing the PDN connections set up before the "
                    "restart", OGS_ADDR(&gnode->addr, buf), recovery, source);
            state->release_pending = true;
            ogs_timer_start(sgwc_self()->t_pgw_release,
                    ogs_time_from_msec(1));
            return;
        }
    }

    if (first) {
        ogs_info("PGW [%s] restart counter %d (%s)",
                OGS_ADDR(&gnode->addr, buf), recovery, source);
    }
}

void sgwc_pgw_release_lost_sessions(void)
{
    sgwc_ue_t *sgwc_ue = NULL;
    sgwc_sess_t *sess = NULL;
    ogs_gtp_node_t *pgw = NULL;
    int count = 0;
    bool more = false;

    ogs_list_for_each(&sgwc_self()->sgw_ue_list, sgwc_ue) {
        ogs_list_for_each(&sgwc_ue->sess_list, sess) {
            sgwc_pgw_state_t *state = NULL;

            if (!sess->gnode || !sess->gnode->data_ptr)
                continue;
            state = sess->gnode->data_ptr;
            if (!state->release_pending)
                continue;
            /*
             * KI-182: a session whose Create Session Response has not
             * arrived yet is still being set up; whether it belongs to the
             * old or the new PGW instance is decided by the Recovery value
             * in its own response (a late answer of the old instance
             * triggers another release pass, see above).
             */
            if (!ogs_gtp2_session_predates_restart(
                        sess->pgw_s5c_teid != 0,
                        sess->pgw_restart_counter_known,
                        sess->pgw_restart_counter, state->restart_counter))
                continue;
            if (sess->release_to_mme_pending)
                continue;

            if (count >= SGWC_PGW_RELEASE_BATCH) {
                more = true;
                goto done;
            }

            if (sgwc_gtp_send_delete_bearer_request_to_mme(sess,
                    OGS_GTP2_CAUSE_REACTIVATION_REQUESTED) != OGS_OK)
                ogs_expect(OGS_OK ==
                    sgwc_pfcp_send_session_deletion_request(
                        sess, OGS_INVALID_POOL_ID, NULL));
            count++;
        }
    }

done:
    if (count)
        ogs_info("PGW restart: released %d PDN connection(s)%s",
                count, more ? ", more to follow" : "");

    if (more) {
        ogs_timer_start(sgwc_self()->t_pgw_release, SGWC_PGW_RELEASE_PACING);
    } else {
        ogs_list_for_each(&sgwc_self()->pgw_s5c_list, pgw) {
            if (pgw->data_ptr)
                ((sgwc_pgw_state_t *)pgw->data_ptr)->release_pending = false;
        }
    }
}
