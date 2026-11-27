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

#ifndef SMF_LI_H
#define SMF_LI_H

#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lawful interception (helm ADR-0064): the SMF reports its sessions to the
 * sidecar on the socket `li_feed_socket` and knows no target. Without that
 * configuration key these functions do nothing.
 */
int smf_li_open(void);
void smf_li_close(void);

/* The session is established: the response to the access side is sent. */
void smf_li_session_up(smf_sess_t *sess);
/* The access side changed something of an established session. */
void smf_li_session_modified(smf_sess_t *sess);
/* The session is being removed. */
void smf_li_session_down(smf_sess_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* SMF_LI_H */
