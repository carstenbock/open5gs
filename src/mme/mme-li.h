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

#ifndef MME_LI_H
#define MME_LI_H

#include "mme-context.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lawful interception (helm ADR-0064): the MME reports every attached UE,
 * its changes of location and its detach to the sidecar on the socket
 * `li_feed_socket` and knows no target. Without that configuration key
 * these functions do nothing.
 */
int mme_li_open(void);
void mme_li_close(void);

/* The attach is complete. */
void mme_li_ue_attached(mme_ue_t *mme_ue);
/* The UE context has a new cell or tracking area (told only if it differs
 * from what was told last). */
void mme_li_ue_location(mme_ue_t *mme_ue);
/* The UE detaches (`by_ue`), or the network removes its context. */
void mme_li_ue_detached(mme_ue_t *mme_ue, bool by_ue);

#ifdef __cplusplus
}
#endif

#endif /* MME_LI_H */
