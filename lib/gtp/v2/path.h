/*
 * Copyright (C) 2019-2024 by Sukchan Lee <acetcom@gmail.com>
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

#if !defined(OGS_GTP_INSIDE) && !defined(OGS_GTP_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_GTP2_PATH_H
#define OGS_GTP2_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ogs_gtp_xact_s ogs_gtp_xact_t;

ogs_pkbuf_t *ogs_gtp2_handle_echo_req(ogs_pkbuf_t *pkb);
void ogs_gtp2_send_error_message(
        ogs_gtp_xact_t *xact, uint32_t teid, uint8_t type, uint8_t cause_value);

/*
 * KI-170: own GTP-C restart counter (TS 23.007, Recovery IE of
 * TS 29.274). Derived from the process start time like the PGW-C's
 * (KI-123), so it changes on every restart without a state file and also
 * after a container is recreated.
 */
uint8_t ogs_gtp2_restart_counter_from_time(time_t start_time);

/*
 * KI-170: track a peer's restart counter. The first value seen is stored
 * and is not a restart. Returns true when a later value differs (the peer
 * restarted); the new value is then stored.
 */
bool ogs_gtp2_peer_restart_detected(
        uint8_t *stored, bool *known, uint8_t received);

/* TS 29.274 §8.87 EPC Timer (e.g. PGW Back-Off Time) to seconds;
 * "deactivated" (unit 7) gives 0 */
uint32_t ogs_gtp2_epc_timer_to_sec(uint8_t epc_timer);

/*
 * KI-174/KI-182: a session belongs to the peer's previous life when it is
 * established and the peer's restart counter in its own Create Session
 * Response is unknown or differs from the peer's current one. A session
 * still waiting for its Create Session Response is never counted.
 */
bool ogs_gtp2_session_predates_restart(bool established,
        bool session_known, uint8_t session_counter, uint8_t peer_counter);

void ogs_gtp2_send_echo_request(
        ogs_gtp_node_t *gnode, uint8_t recovery, uint8_t features);
void ogs_gtp2_send_echo_response(ogs_gtp_xact_t *xact,
        uint8_t recovery, uint8_t features);

void ogs_gtp1_send_error_indication(
        ogs_sock_t *sock, uint32_t teid, uint8_t qfi, const ogs_sockaddr_t *to);

#ifdef __cplusplus
}
#endif

#endif /* OGS_GTP2_PATH_H */
