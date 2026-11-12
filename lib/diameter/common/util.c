/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
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

#include "ogs-diameter-common.h"

bool ogs_diam_is_relay_or_app_advertised(uint32_t app_id)
{
    struct fd_list *li = NULL;
    struct fd_app *found = NULL;

    CHECK_POSIX( pthread_rwlock_rdlock(&fd_g_peers_rw) );
    for (li = fd_g_peers.next; li != &fd_g_peers; li = li->next) {
        struct peer_hdr *p = (struct peer_hdr *)li->o;
        int state = fd_peer_get_state(p);

        ogs_assert(p);
        ogs_assert(p->info.pi_diamid);
        ogs_assert(p->info.pi_diamidlen);

        if (state == STATE_OPEN) {
            ogs_debug("'%s' STATE is OPEN", p->info.pi_diamid);

            if (p->info.runtime.pir_relay) {
                ogs_debug("'%s' RELAY is enabled", p->info.pi_diamid);
                CHECK_POSIX( pthread_rwlock_unlock(&fd_g_peers_rw) );
                return true;
            } else {
                /* Check if the remote peer advertised the message's appli */
                fd_app_check(&p->info.runtime.pir_apps, app_id, &found);
                if (found) {
                    CHECK_POSIX( pthread_rwlock_unlock(&fd_g_peers_rw) );
                    return true;
                }
            }
        } else {
            ogs_debug("'%s' STATE[%d] is NOT open ", p->info.pi_diamid, state);
        }
    }
    CHECK_POSIX( pthread_rwlock_unlock(&fd_g_peers_rw) );

    return false;
}

/*
 * Make a REQUIRED base rule of a shared command optional (KI-77).
 *
 * The freeDiameter fork's base dictionaries follow the RFCs strictly (e.g.
 * RFC 4006: Service-Context-Id in every CCR, RFC 7155: Auth-Request-Type in
 * every AAR/AAA). Gx and Rx reuse those commands without these AVPs
 * (TS 29.212 §5.6.2, TS 29.214 §5.6.1), so a node that receives them would
 * answer DIAMETER_MISSING_AVP.
 */
int ogs_diam_rule_set_optional(struct dict_object *cmd, const char *avp_name)
{
    struct dict_object *avp = NULL, *rule = NULL;
    struct dict_rule_request req;
    struct dict_rule_data data;

    CHECK_FCT( fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME,
            avp_name, &avp, ENOENT) );

    req.rule_parent = cmd;
    req.rule_avp = avp;
    CHECK_FCT( fd_dict_search(fd_g_config->cnf_dict, DICT_RULE,
            RULE_BY_AVP_AND_PARENT, &req, &rule, 0) );
    if (!rule)
        return 0;

    CHECK_FCT( fd_dict_getval(rule, &data) );
    if (data.rule_position != RULE_REQUIRED)
        return 0;

    CHECK_FCT( fd_dict_delete(rule) );
    data.rule_position = RULE_OPTIONAL;
    data.rule_order = 0;
    data.rule_min = -1;
    CHECK_FCT( fd_dict_new(fd_g_config->cnf_dict, DICT_RULE, &data, cmd, NULL) );

    return 0;
}
