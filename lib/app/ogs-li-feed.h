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

#if !defined(OGS_APP_INSIDE) && !defined(OGS_APP_COMPILATION)
#error "This header cannot be included directly."
#endif

#ifndef OGS_LI_FEED_H
#define OGS_LI_FEED_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Session feed for lawful interception (helm ADR-0064).
 *
 * The network function holds no target and no task. It reports every
 * session on a Unix stream socket to a sidecar, which decides what is
 * intercepted. Nothing of this module may be logged per session, and no
 * metric may be derived from it.
 *
 * Frame: 4 octets length (network order) of what follows, then
 *   1 octet  format version (OGS_LI_FEED_VERSION)
 *   1 octet  source (OGS_LI_FEED_SOURCE_*)
 *   1 octet  event (OGS_LI_FEED_EVENT_*)
 *   elements: 1 octet type (OGS_LI_FEED_T_*), 2 octets length, value
 * Integers are in network order. A receiver skips elements it does not know.
 *
 * After every (re)connect the function sends SYNC_BEGIN, one ACTIVE per
 * session it has, and SYNC_END: the sidecar forgets what it knew before.
 * While the sidecar is away, events are dropped.
 *
 * The sidecar answers on the same socket with commands, framed alike:
 *   1 octet format version, 1 octet command (OGS_LI_FEED_CMD_*), elements.
 * A user plane function copies the packets of the sessions the sidecar
 * names (COPY_ON) as PACKET messages until COPY_OFF, the end of the session
 * or the end of the connection.
 */
#define OGS_LI_FEED_VERSION             1

#define OGS_LI_FEED_SOURCE_SMF          1
#define OGS_LI_FEED_SOURCE_UPF          2
#define OGS_LI_FEED_SOURCE_MME          3

#define OGS_LI_FEED_EVENT_UP            1
#define OGS_LI_FEED_EVENT_MODIFIED      2
#define OGS_LI_FEED_EVENT_DOWN          3
#define OGS_LI_FEED_EVENT_ACTIVE        4
#define OGS_LI_FEED_EVENT_SYNC_BEGIN    5
#define OGS_LI_FEED_EVENT_SYNC_END      6
#define OGS_LI_FEED_EVENT_PACKET        7

#define OGS_LI_FEED_CMD_COPY_ON         1   /* element SESSION */
#define OGS_LI_FEED_CMD_COPY_OFF        2   /* element SESSION */

#define OGS_LI_FEED_T_SESSION           1   /* 8 octets, the SMF's SEID */
#define OGS_LI_FEED_T_TIME              2   /* 8 octets, microseconds UTC */
#define OGS_LI_FEED_T_IMSI              3   /* digits */
#define OGS_LI_FEED_T_IMEISV            4   /* digits */
#define OGS_LI_FEED_T_MSISDN            5   /* digits */
#define OGS_LI_FEED_T_APN               6   /* text */
#define OGS_LI_FEED_T_UE_IPV4           7   /* 4 octets */
#define OGS_LI_FEED_T_UE_IPV6           8   /* 1 octet prefix length, 16 octets */
#define OGS_LI_FEED_T_RAT_TYPE          9   /* 1 octet, TS 29.274 8.17 */
#define OGS_LI_FEED_T_SERVING_PLMN      10  /* 3 octets, ogs_plmn_id_t */
#define OGS_LI_FEED_T_EBI               11  /* 1 octet, default bearer */
#define OGS_LI_FEED_T_PEER_ADDR         12  /* 4 or 16 octets, SGW or ePDG */
#define OGS_LI_FEED_T_PEER_TEID         13  /* 4 octets */
#define OGS_LI_FEED_T_LOCAL_TEID        14  /* 4 octets */
#define OGS_LI_FEED_T_PDN_TYPE          15  /* 1 octet, TS 29.274 8.34 */
#define OGS_LI_FEED_T_ULI               16  /* value of the ULI IE, TS 29.274 8.21 */
#define OGS_LI_FEED_T_CHARGING_ID       17  /* 4 octets */
#define OGS_LI_FEED_T_PSI               18  /* 1 octet, PDU session ID (5GC) */
/* User plane of the default bearer: this function's side and the peer's */
#define OGS_LI_FEED_T_UP_LOCAL_TEID     19  /* 4 octets */
#define OGS_LI_FEED_T_UP_LOCAL_ADDR     20  /* 4 or 16 octets */
#define OGS_LI_FEED_T_UP_PEER_TEID      21  /* 4 octets */
#define OGS_LI_FEED_T_UP_PEER_ADDR      22  /* 4 or 16 octets */

/* PACKET: 1 octet, OGS_LI_FEED_DIR_* */
#define OGS_LI_FEED_T_DIRECTION         23
/* PACKET: the IP packet of the UE, without tunnel headers */
#define OGS_LI_FEED_T_PACKET            24
/* PACKET: 4 octets, packets of this connection not copied before this one
 * because the sidecar did not keep up */
#define OGS_LI_FEED_T_DROPPED           25

#define OGS_LI_FEED_DIR_FROM_UE         1
#define OGS_LI_FEED_DIR_TO_UE           2

typedef void (*ogs_li_feed_connected_f)(void);
/* One command of the sidecar; `elements` are its elements, undecoded. */
typedef void (*ogs_li_feed_command_f)(
        uint8_t command, const uint8_t *elements, size_t len);

/*
 * Starts the feed towards the socket `path`. `connected` runs in the event
 * loop after every connect, between SYNC_BEGIN and SYNC_END: it sends one
 * ACTIVE per session. `command` (may be NULL) gets the sidecar's commands;
 * `disconnected` (may be NULL) runs when the connection is lost. Call
 * before the event loop starts.
 */
int ogs_li_feed_open(
        const char *path, uint8_t source, ogs_li_feed_connected_f connected,
        ogs_li_feed_command_f command, void (*disconnected)(void));
void ogs_li_feed_close(void);

/* True while a sidecar is connected: build no message otherwise. */
bool ogs_li_feed_connected(void);
/* Octets not yet taken by the sidecar: a sender of bulk data (packets)
 * drops instead of adding to a large backlog. */
size_t ogs_li_feed_backlog(void);
/* The value of the first element of `type`, or NULL. */
const uint8_t *ogs_li_feed_find(
        const uint8_t *elements, size_t len, uint8_t type, size_t *value_len);

/* One message: begin, any number of put, end. Event loop only. */
void ogs_li_feed_begin(uint8_t event);
void ogs_li_feed_put(uint8_t type, const void *value, size_t len);
void ogs_li_feed_put_str(uint8_t type, const char *value);
void ogs_li_feed_put_u8(uint8_t type, uint8_t value);
void ogs_li_feed_put_u32(uint8_t type, uint32_t value);
void ogs_li_feed_put_u64(uint8_t type, uint64_t value);
void ogs_li_feed_end(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* OGS_LI_FEED_H */
