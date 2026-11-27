/*
 * Copyright (C) 2026 by volte.io
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

#ifndef UPF_LI_H
#define UPF_LI_H

#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lawful interception (helm ADR-0064): the UPF reports its sessions, with
 * the subscriber the SMF named in the PFCP User ID, to the sidecar on the
 * socket `li_feed_socket` and copies the packets of the sessions the
 * sidecar names. It knows no target. Without that configuration key these
 * functions do nothing.
 */
int upf_li_open(void);
void upf_li_close(void);

/* The session is established: the response to the SMF is sent. */
void upf_li_session_up(upf_sess_t *sess);
/* The session is being removed. */
void upf_li_session_down(upf_sess_t *sess);

/*
 * One IP packet of the UE of `sess`, without tunnel headers. Called for
 * every packet: the first test must stay this cheap.
 */
void upf_li_packet_copy(
        upf_sess_t *sess, uint8_t direction, const void *data, size_t len);
#define upf_li_packet(__sESS, __dIR, __dATA, __lEN) \
    do { \
        if ((__sESS)->li_copy) \
            upf_li_packet_copy((__sESS), (__dIR), (__dATA), (__lEN)); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* UPF_LI_H */
