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

/*
 * Packets are bulk data on a stream the sidecar reads at its own pace. Above
 * this backlog a packet is dropped and counted; session events are never
 * dropped for it.
 */
#define UPF_LI_MAX_PACKET_BACKLOG   (4*1024*1024)

static uint32_t dropped;

static void send_session(uint8_t event, upf_sess_t *sess)
{
    if (!ogs_li_feed_connected())
        return;

    ogs_li_feed_begin(event);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_SESSION, sess->smf_n4_f_seid.seid);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_TIME, ogs_time_now());
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMSI, sess->imsi_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMEISV, sess->imeisv_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_MSISDN, sess->msisdn_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_APN, sess->apn_dnn);
    if (sess->ipv4)
        ogs_li_feed_put(OGS_LI_FEED_T_UE_IPV4, &sess->ipv4->addr[0], 4);
    if (sess->ipv6) {
        uint8_t prefix[1 + OGS_IPV6_LEN];
        prefix[0] = OGS_IPV6_DEFAULT_PREFIX_LEN;
        memcpy(prefix + 1, sess->ipv6->addr, OGS_IPV6_LEN);
        ogs_li_feed_put(OGS_LI_FEED_T_UE_IPV6, prefix, sizeof(prefix));
    }
    ogs_li_feed_end();
}

/* After a connect: every established session; nothing is copied yet. */
static void send_all(void)
{
    upf_sess_t *sess = NULL;

    dropped = 0;
    ogs_list_for_each(&upf_self()->sess_list, sess) {
        sess->li_copy = false;
        if (sess->li_reported)
            send_session(OGS_LI_FEED_EVENT_ACTIVE, sess);
    }
}

/* Without a sidecar nobody takes the copies. */
static void disconnected(void)
{
    upf_sess_t *sess = NULL;

    ogs_list_for_each(&upf_self()->sess_list, sess)
        sess->li_copy = false;
}

static void command(uint8_t cmd, const uint8_t *elements, size_t len)
{
    upf_sess_t *sess = NULL;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    uint64_t seid;

    if (cmd != OGS_LI_FEED_CMD_COPY_ON && cmd != OGS_LI_FEED_CMD_COPY_OFF)
        return;
    value = ogs_li_feed_find(
            elements, len, OGS_LI_FEED_T_SESSION, &value_len);
    if (!value || value_len != sizeof(seid))
        return;
    memcpy(&seid, value, sizeof(seid));
    seid = be64toh(seid);

    /* Rare, and the SMF's SEID alone is no key of a hash here. */
    ogs_list_for_each(&upf_self()->sess_list, sess)
        if (sess->li_reported && sess->smf_n4_f_seid.seid == seid)
            sess->li_copy = (cmd == OGS_LI_FEED_CMD_COPY_ON);
}

int upf_li_open(void)
{
    if (!upf_self()->li_feed_socket)
        return OGS_OK;

    return ogs_li_feed_open(upf_self()->li_feed_socket,
            OGS_LI_FEED_SOURCE_UPF, send_all, command, disconnected);
}

void upf_li_close(void)
{
    ogs_li_feed_close();
}

void upf_li_session_up(upf_sess_t *sess)
{
    ogs_assert(sess);

    if (sess->li_reported)
        return;
    /* Also without a sidecar: the session is in the next sync. */
    sess->li_reported = true;
    send_session(OGS_LI_FEED_EVENT_UP, sess);
}

void upf_li_session_down(upf_sess_t *sess)
{
    ogs_assert(sess);

    if (!sess->li_reported)
        return;
    send_session(OGS_LI_FEED_EVENT_DOWN, sess);
    sess->li_reported = false;
    sess->li_copy = false;
}

void upf_li_packet_copy(
        upf_sess_t *sess, uint8_t direction, const void *data, size_t len)
{
    if (!ogs_li_feed_connected() || len == 0 || len > 0xffff)
        return;
    if (ogs_li_feed_backlog() > UPF_LI_MAX_PACKET_BACKLOG) {
        dropped++;
        return;
    }

    ogs_li_feed_begin(OGS_LI_FEED_EVENT_PACKET);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_SESSION, sess->smf_n4_f_seid.seid);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_TIME, ogs_time_now());
    ogs_li_feed_put_u8(OGS_LI_FEED_T_DIRECTION, direction);
    if (dropped) {
        ogs_li_feed_put_u32(OGS_LI_FEED_T_DROPPED, dropped);
        dropped = 0;
    }
    ogs_li_feed_put(OGS_LI_FEED_T_PACKET, data, len);
    ogs_li_feed_end();
}
