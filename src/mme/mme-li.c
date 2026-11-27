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

#include "mme-li.h"

/* No function of this file may log a UE or count one (helm ADR-0056). */

static void send_ue(uint8_t event, mme_ue_t *mme_ue, int detach)
{
    uint8_t tai[OGS_PLMN_ID_LEN + 2], ecgi[OGS_PLMN_ID_LEN + 4];
    uint16_t tac;
    uint32_t cell;

    if (!ogs_li_feed_connected())
        return;

    ogs_li_feed_begin(event);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_SESSION, mme_ue->li_key);
    ogs_li_feed_put_u64(OGS_LI_FEED_T_TIME, ogs_time_now());
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMSI, mme_ue->imsi_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_IMEISV, mme_ue->imeisv_bcd);
    ogs_li_feed_put_str(OGS_LI_FEED_T_MSISDN, mme_ue->msisdn_bcd);

    memcpy(tai, &mme_ue->tai.plmn_id, OGS_PLMN_ID_LEN);
    tac = htobe16(mme_ue->tai.tac);
    memcpy(tai + OGS_PLMN_ID_LEN, &tac, sizeof(tac));
    ogs_li_feed_put(OGS_LI_FEED_T_TAI, tai, sizeof(tai));

    memcpy(ecgi, &mme_ue->e_cgi.plmn_id, OGS_PLMN_ID_LEN);
    cell = htobe32(mme_ue->e_cgi.cell_id);
    memcpy(ecgi + OGS_PLMN_ID_LEN, &cell, sizeof(cell));
    ogs_li_feed_put(OGS_LI_FEED_T_ECGI, ecgi, sizeof(ecgi));

    if (MME_CURRENT_GUTI_IS_AVAILABLE(mme_ue)) {
        uint8_t guti[OGS_PLMN_ID_LEN + 2 + 1 + 4];
        uint16_t gid = htobe16(mme_ue->current.guti.mme_gid);
        uint32_t m_tmsi = htobe32(mme_ue->current.guti.m_tmsi);

        memcpy(guti, &mme_ue->current.guti.nas_plmn_id, OGS_PLMN_ID_LEN);
        memcpy(guti + 3, &gid, sizeof(gid));
        guti[5] = mme_ue->current.guti.mme_code;
        memcpy(guti + 6, &m_tmsi, sizeof(m_tmsi));
        ogs_li_feed_put(OGS_LI_FEED_T_GUTI, guti, sizeof(guti));
    }

    if (detach) {
        ogs_li_feed_put_u8(OGS_LI_FEED_T_DETACH, detach);
        if (detach == 1 && mme_ue->nas_eps.detach.switch_off)
            ogs_li_feed_put_u8(OGS_LI_FEED_T_SWITCH_OFF, 1);
    }
    ogs_li_feed_end();

    memcpy(&mme_ue->li_tai, &mme_ue->tai, sizeof(mme_ue->li_tai));
    memcpy(&mme_ue->li_e_cgi, &mme_ue->e_cgi, sizeof(mme_ue->li_e_cgi));
}

/* After a connect: every attached UE, so the sidecar starts afresh. */
static void send_all(void)
{
    mme_ue_t *mme_ue = NULL;

    ogs_list_for_each(&mme_self()->mme_ue_list, mme_ue)
        if (mme_ue->li_reported)
            send_ue(OGS_LI_FEED_EVENT_ACTIVE, mme_ue, 0);
}

int mme_li_open(void)
{
    if (!mme_self()->li_feed_socket)
        return OGS_OK;

    return ogs_li_feed_open(mme_self()->li_feed_socket,
            OGS_LI_FEED_SOURCE_MME, send_all, NULL, NULL);
}

void mme_li_close(void)
{
    ogs_li_feed_close();
}

void mme_li_ue_attached(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);

    if (mme_ue->li_reported) {
        /* a second attach of a UE that never detached: where it is now */
        mme_li_ue_location(mme_ue);
        return;
    }
    /* Also without a sidecar: the UE is in the next sync. */
    mme_ue->li_reported = true;
    /*
     * A key of this attach, not of the context: pool IDs are used again, and
     * the sidecar derives the correlation ID of the records from the key.
     */
    mme_ue->li_key = ((uint64_t)ogs_time_now() << 12) ^ mme_ue->id;
    send_ue(OGS_LI_FEED_EVENT_UP, mme_ue, 0);
}

void mme_li_ue_location(mme_ue_t *mme_ue)
{
    ogs_assert(mme_ue);

    if (!mme_ue->li_reported)
        return;
    if (memcmp(&mme_ue->li_tai, &mme_ue->tai, sizeof(mme_ue->li_tai)) == 0 &&
        memcmp(&mme_ue->li_e_cgi, &mme_ue->e_cgi,
            sizeof(mme_ue->li_e_cgi)) == 0)
        return;
    send_ue(OGS_LI_FEED_EVENT_MODIFIED, mme_ue, 0);
}

void mme_li_ue_detached(mme_ue_t *mme_ue, bool by_ue)
{
    ogs_assert(mme_ue);

    if (!mme_ue->li_reported)
        return;
    send_ue(OGS_LI_FEED_EVENT_DOWN, mme_ue, by_ue ? 1 : 2);
    mme_ue->li_reported = false;
}
