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

#include "li.h"

/* No function of this file may log a session or count one (helm ADR-0056). */

static void send_session(uint8_t event, smf_sess_t *sess)
{
    smf_ue_t *smf_ue = NULL;
    smf_bearer_t *bearer = NULL;

    if (!ogs_li_feed_connected())
        return;

    smf_ue = smf_ue_find_by_id(sess->smf_ue_id);
    if (!smf_ue)
        return;

    ogs_li_feed_begin(event);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_SESSION, sess->smf_n4_seid);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_TIME, ogs_time_now());
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMSI, smf_ue->imsi_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMEISV, smf_ue->imeisv_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_MSISDN, smf_ue->msisdn_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_APN, sess->session.name);
    if (sess->ipv4)
        ogs_li_feed_put(OGS_LI_FEED_T_UE_IPV4, &sess->ipv4->addr[0], 4);
    if (sess->ipv6) {
        uint8_t prefix[1 + OGS_IPV6_LEN];
        prefix[0] = OGS_IPV6_DEFAULT_PREFIX_LEN;
        memcpy(prefix + 1, sess->ipv6->addr, OGS_IPV6_LEN);
        ogs_li_feed_put(OGS_LI_FEED_T_UE_IPV6, prefix, sizeof(prefix));
    }
    ogs_li_feed_put_u8(OGS_LI_FEED_T_PDN_TYPE, sess->paa.session_type);
    if (sess->epc) {
        ogs_li_feed_put_u8(OGS_LI_FEED_T_RAT_TYPE, sess->gtp_rat_type);
        ogs_li_feed_put(OGS_LI_FEED_T_SERVING_PLMN,
                &sess->serving_plmn_id, OGS_PLMN_ID_LEN);
        bearer = smf_default_bearer_in_sess(sess);
        if (bearer) {
            ogs_li_feed_put_u8(OGS_LI_FEED_T_EBI, bearer->ebi);
            ogs_li_feed_put_u32(
                    OGS_LI_FEED_T_UP_LOCAL_TEID, bearer->pgw_s5u_teid);
            if (bearer->pgw_s5u_addr)
                ogs_li_feed_put(OGS_LI_FEED_T_UP_LOCAL_ADDR,
                        &bearer->pgw_s5u_addr->sin.sin_addr, OGS_IPV4_LEN);
            else if (bearer->pgw_s5u_addr6)
                ogs_li_feed_put(OGS_LI_FEED_T_UP_LOCAL_ADDR,
                        &bearer->pgw_s5u_addr6->sin6.sin6_addr, OGS_IPV6_LEN);
            ogs_li_feed_put_u32(
                    OGS_LI_FEED_T_UP_PEER_TEID, bearer->sgw_s5u_teid);
            if (bearer->sgw_s5u_ip.ipv4)
                ogs_li_feed_put(OGS_LI_FEED_T_UP_PEER_ADDR,
                        &bearer->sgw_s5u_ip.addr, OGS_IPV4_LEN);
            else if (bearer->sgw_s5u_ip.ipv6)
                ogs_li_feed_put(OGS_LI_FEED_T_UP_PEER_ADDR,
                        bearer->sgw_s5u_ip.addr6, OGS_IPV6_LEN);
        }
        if (sess->sgw_s5c_ip.ipv4)
            ogs_li_feed_put(OGS_LI_FEED_T_PEER_ADDR,
                    &sess->sgw_s5c_ip.addr, OGS_IPV4_LEN);
        else if (sess->sgw_s5c_ip.ipv6)
            ogs_li_feed_put(OGS_LI_FEED_T_PEER_ADDR,
                    sess->sgw_s5c_ip.addr6, OGS_IPV6_LEN);
        ogs_li_feed_put_u32(OGS_LI_FEED_T_PEER_TEID, sess->sgw_s5c_teid);
        ogs_li_feed_put_u32(OGS_LI_FEED_T_LOCAL_TEID, sess->smf_n4_teid);
        if (sess->gtp.user_location_information.presence &&
            sess->gtp.user_location_information.len <= 0xffff)
            ogs_li_feed_put(OGS_LI_FEED_T_ULI,
                    sess->gtp.user_location_information.data,
                    sess->gtp.user_location_information.len);
        ogs_li_feed_put_u32(OGS_LI_FEED_T_CHARGING_ID, sess->charging.id);
    } else {
        ogs_li_feed_put_u8(OGS_LI_FEED_T_PSI, sess->psi);
    }
    ogs_li_feed_end();
}

/* After a connect: every established session, so the sidecar starts afresh. */
static void send_all(void)
{
    smf_ue_t *smf_ue = NULL;
    smf_sess_t *sess = NULL;

    ogs_list_for_each(&smf_self()->smf_ue_list, smf_ue)
        ogs_list_for_each(&smf_ue->sess_list, sess)
            if (sess->li_reported)
                send_session(OGS_LI_FEED_EVENT_ACTIVE, sess);
}

int smf_li_open(void)
{
    if (!smf_self()->li_feed_socket)
        return OGS_OK;

    return ogs_li_feed_open(smf_self()->li_feed_socket,
            OGS_LI_FEED_SOURCE_SMF, send_all, NULL, NULL);
}

void smf_li_close(void)
{
    ogs_li_feed_close();
}

void smf_li_session_up(smf_sess_t *sess)
{
    ogs_assert(sess);

    if (sess->li_reported)
        return;
    /* Also without a sidecar: the session is in the next sync. */
    sess->li_reported = true;
    send_session(OGS_LI_FEED_EVENT_UP, sess);
}

void smf_li_session_modified(smf_sess_t *sess)
{
    ogs_assert(sess);

    if (sess->li_reported)
        send_session(OGS_LI_FEED_EVENT_MODIFIED, sess);
}

void smf_li_session_down(smf_sess_t *sess)
{
    ogs_assert(sess);

    if (!sess->li_reported)
        return;
    send_session(OGS_LI_FEED_EVENT_DOWN, sess);
    sess->li_reported = false;
}
