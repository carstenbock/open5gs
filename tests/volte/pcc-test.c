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

#include "test-common.h"
#include "test-fd-path.h"

#include <poll.h>

/*
 * Tests for the PCC rule handling of the PGW-C, PCRF and PGW-U:
 * bearer binding to the default bearer (KI-120), Flow-Status gating (KI-70),
 * real-time text and Flow-Status REMOVED in the PCRF (KI-71).
 */

/* true if the socket has data within `ms` milliseconds */
static int readable(ogs_socknode_t *node, int ms)
{
    struct pollfd p;

    ogs_assert(node);
    ogs_assert(node->sock);
    memset(&p, 0, sizeof(p));
    p.fd = node->sock->fd;
    p.events = POLLIN;

    return poll(&p, 1, ms) > 0 ? 1 : 0;
}

/*
 * As test_db_new_ims(), plus a PCC rule in the internet session with the
 * QCI and ARP of that session's default bearer.
 */
static bson_t *db_new_pcc(test_ue_t *test_ue)
{
    bson_t *doc = NULL;

    ogs_assert(test_ue);

    doc = BCON_NEW(
            "imsi", BCON_UTF8(test_ue->imsi),
            "msisdn", "[",
                BCON_UTF8(TEST_MSISDN),
                BCON_UTF8(TEST_ADDITIONAL_MSISDN),
            "]",
            "ambr", "{",
                "downlink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
                "uplink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
            "}",
            "slice", "[", "{",
                "sst", BCON_INT32(1),
                "default_indicator", BCON_BOOL(true),
                "session", "[",
                  "{",
                    "name", BCON_UTF8("internet"),
                    "type", BCON_INT32(3),
                    "ambr", "{",
                        "downlink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
                        "uplink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
                    "}",
                    "qos", "{",
                        "index", BCON_INT32(9),
                        "arp", "{",
                            "priority_level", BCON_INT32(8),
                            "pre_emption_vulnerability", BCON_INT32(1),
                            "pre_emption_capability", BCON_INT32(1),
                        "}",
                    "}",
                    "pcc_rule", "[", "{",
                        "qos", "{",
                            "index", BCON_INT32(9),
                            "arp", "{",
                                "priority_level", BCON_INT32(8),
                                "pre_emption_vulnerability", BCON_INT32(1),
                                "pre_emption_capability", BCON_INT32(1),
                            "}",
                        "}",
                        "flow", "[",
                            "{", "direction", BCON_INT32(2),
                                 "description", BCON_UTF8("permit out tcp from 10.200.136.98/32 443 to assigned"), "}",
                            "{", "direction", BCON_INT32(1),
                                 "description", BCON_UTF8("permit out tcp from 10.200.136.98/32 to assigned 443"), "}",
                        "]",
                    "}", "]",
                    "lbo_roaming_allowed", BCON_BOOL(false),
                  "}",
                  "{",
                    "name", BCON_UTF8("ims"),
                    "type", BCON_INT32(3),
                    "ambr", "{",
                        "downlink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
                        "uplink", "{", "value", BCON_INT32(1), "unit", BCON_INT32(3), "}",
                    "}",
                    "qos", "{",
                        "index", BCON_INT32(5),
                        "arp", "{",
                            "priority_level", BCON_INT32(1),
                            "pre_emption_vulnerability", BCON_INT32(1),
                            "pre_emption_capability", BCON_INT32(1),
                        "}",
                    "}",
                    "pcc_rule", "[",
                      "{",
                        "qos", "{",
                            "index", BCON_INT32(1),
                            "arp", "{",
                                "priority_level", BCON_INT32(3),
                                "pre_emption_vulnerability", BCON_INT32(2),
                                "pre_emption_capability", BCON_INT32(2),
                            "}",
                            "mbr", "{",
                                "downlink", "{", "value", BCON_INT32(82), "unit", BCON_INT32(1), "}",
                                "uplink", "{", "value", BCON_INT32(82), "unit", BCON_INT32(1), "}",
                            "}",
                            "gbr", "{",
                                "downlink", "{", "value", BCON_INT32(82), "unit", BCON_INT32(1), "}",
                                "uplink", "{", "value", BCON_INT32(82), "unit", BCON_INT32(1), "}",
                            "}",
                        "}",
                      "}",
                    "]",
                    "lbo_roaming_allowed", BCON_BOOL(false),
                  "}",
                "]",
            "}", "]",
            "security", "{",
                "k", BCON_UTF8(test_ue->k_string),
                "opc", BCON_UTF8(test_ue->opc_string),
                "amf", BCON_UTF8("8000"),
                "sqn", BCON_INT64(64),
            "}",
            "subscribed_rau_tau_timer", BCON_INT32(12),
            "network_access_mode", BCON_INT32(0),
            "subscriber_status", BCON_INT32(0),
            "operator_determined_barring", BCON_INT32(0),
            "access_restriction_data", BCON_INT32(32)
          );
    ogs_assert(doc);

    return doc;
}

static void test1_func(abts_case *tc, void *data)
{
    int rv;
    ogs_socknode_t *s1ap;
    ogs_socknode_t *gtpu;
    ogs_pkbuf_t *emmbuf;
    ogs_pkbuf_t *esmbuf;
    ogs_pkbuf_t *sendbuf;
    ogs_pkbuf_t *recvbuf;
    ogs_s1ap_message_t message;

    uint8_t *rx_sid = NULL;
    uint8_t *rx_sid2 = NULL;
    test_bearer_t *audio = NULL;

    ogs_nas_5gs_mobile_identity_suci_t mobile_identity_suci;
    test_ue_t *test_ue = NULL;
    test_sess_t *sess = NULL;
    test_bearer_t *bearer = NULL;

    uint32_t enb_ue_s1ap_id;
    uint64_t mme_ue_s1ap_id;

    bson_t *doc = NULL;

    /* Setup Test UE & Session Context */
    memset(&mobile_identity_suci, 0, sizeof(mobile_identity_suci));

    mobile_identity_suci.h.supi_format = OGS_NAS_5GS_SUPI_FORMAT_IMSI;
    mobile_identity_suci.h.type = OGS_NAS_5GS_MOBILE_IDENTITY_SUCI;
    mobile_identity_suci.routing_indicator1 = 0;
    mobile_identity_suci.routing_indicator2 = 0xf;
    mobile_identity_suci.routing_indicator3 = 0xf;
    mobile_identity_suci.routing_indicator4 = 0xf;
    mobile_identity_suci.protection_scheme_id = OGS_PROTECTION_SCHEME_NULL;
    mobile_identity_suci.home_network_pki_value = 0;

    test_ue = test_ue_add_by_suci(&mobile_identity_suci, "1032548691");
    ogs_assert(test_ue);

    test_ue->e_cgi.cell_id = 0x1079baf;
    test_ue->nas.ksi = OGS_NAS_KSI_NO_KEY_IS_AVAILABLE;
    test_ue->nas.value = OGS_NAS_ATTACH_TYPE_COMBINED_EPS_IMSI_ATTACH;

    test_ue->k_string = "465b5ce8b199b49faa5f0a2ee238a6bc";
    test_ue->opc_string = "e8ed289deba952e4283b54e88e6183ca";

    sess = test_sess_add_by_apn(test_ue, "internet", OGS_GTP2_RAT_TYPE_EUTRAN);
    ogs_assert(sess);

    /* eNB connects to MME */
    s1ap = tests1ap_client(AF_INET);
    ABTS_PTR_NOTNULL(tc, s1ap);

    /* eNB connects to SGW */
    gtpu = test_gtpu_server(1, AF_INET);
    ABTS_PTR_NOTNULL(tc, gtpu);

    /* Send S1-Setup Reqeust */
    sendbuf = test_s1ap_build_s1_setup_request(
            S1AP_ENB_ID_PR_macroENB_ID, 0x54f64);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive S1-Setup Response */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(NULL, recvbuf);

    /********** Insert Subscriber in Database */
    doc = db_new_pcc(test_ue);
    ABTS_PTR_NOTNULL(tc, doc);
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_insert_ue(test_ue, doc));

    /* Send Attach Request */
    memset(&sess->pdn_connectivity_param,
            0, sizeof(sess->pdn_connectivity_param));
    sess->pdn_connectivity_param.eit = 1;
    sess->pdn_connectivity_param.request_type =
        OGS_NAS_EPS_REQUEST_TYPE_INITIAL;
    esmbuf = testesm_build_pdn_connectivity_request(
            sess, false, OGS_NAS_EPS_PDN_TYPE_IPV4V6);
    ABTS_PTR_NOTNULL(tc, esmbuf);

    memset(&test_ue->attach_request_param,
            0, sizeof(test_ue->attach_request_param));
    test_ue->attach_request_param.drx_parameter = 1;
    test_ue->attach_request_param.ms_network_capability = 1;
    test_ue->attach_request_param.tmsi_status = 1;
    test_ue->attach_request_param.mobile_station_classmark_2 = 1;
    test_ue->attach_request_param.ue_usage_setting = 1;
    emmbuf = testemm_build_attach_request(test_ue, esmbuf, true, false);
    ABTS_PTR_NOTNULL(tc, emmbuf);

    memset(&test_ue->initial_ue_param, 0, sizeof(test_ue->initial_ue_param));
    sendbuf = test_s1ap_build_initial_ue_message(
            test_ue, emmbuf, S1AP_RRC_Establishment_Cause_mo_Signalling, false);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Authentication Request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send Authentication response */
    emmbuf = testemm_build_authentication_response(test_ue);
    ABTS_PTR_NOTNULL(tc, emmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, emmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive Security mode Command */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send Security mode complete */
    test_ue->mobile_identity_imeisv_presence = true;
    emmbuf = testemm_build_security_mode_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, emmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, emmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive ESM Information Request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send ESM Information Response */
    sess->esm_information_param.pco = 1;
    esmbuf = testesm_build_esm_information_response(sess);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive InitialContextSetupRequest +
     * Attach Accept +
     * Activate Default Bearer Context Request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send UE Capability Info Indication */
    sendbuf = tests1ap_build_ue_radio_capability_info_indication(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send InitialContextSetupResponse */
    sendbuf = test_s1ap_build_initial_context_setup_response(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Attach Complete + Activate default EPS bearer cotext accept */
    test_ue->nr_cgi.cell_id = 0x1234502;
    bearer = test_bearer_find_by_ue_ebi(test_ue, 5);
    ogs_assert(bearer);
    esmbuf = testesm_build_activate_default_eps_bearer_context_accept(
            bearer, false);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    emmbuf = testemm_build_attach_complete(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, emmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, emmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive EMM information */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /*
     * KI-120: the internet session's PCC rule has the default bearer's QCI
     * and ARP, so it is bound to the default bearer (TS 23.203 6.1.1.4):
     * no E-RABSetupRequest / Activate dedicated EPS bearer context request.
     */
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1500));

    /* Send GTP-U ICMP Packet */
    rv = test_gtpu_send_ping(gtpu, bearer, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive GTP-U ICMP Packet */
    recvbuf = test_gtpu_read(gtpu);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /* Send PDN Connectivity Request */
    sess = test_sess_add_by_apn(test_ue, "ims", OGS_GTP2_RAT_TYPE_EUTRAN);
    ogs_assert(sess);
    sess->pti = 5;

    sess->pdn_connectivity_param.apn = 1;
    sess->pdn_connectivity_param.pco = 1;
    sess->pdn_connectivity_param.request_type =
        OGS_NAS_EPS_REQUEST_TYPE_INITIAL;
    esmbuf = testesm_build_pdn_connectivity_request(
            sess, true, OGS_NAS_EPS_PDN_TYPE_IPV4V6);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive E-RABSetupRequest +
     * Activate default EPS bearer context request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            S1AP_ProcedureCode_id_E_RABSetup,
            test_ue->s1ap_procedure_code);

    /* Send E-RABSetupResponse */
    bearer = test_bearer_find_by_ue_ebi(test_ue, 6);
    ogs_assert(bearer);
    sendbuf = test_s1ap_build_e_rab_setup_response(bearer);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Activate default EPS bearer context accept */
    esmbuf = testesm_build_activate_default_eps_bearer_context_accept(
            bearer, true);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send GTP-U ICMP Packet */
    rv = test_gtpu_send_ping(gtpu, bearer, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive GTP-U ICMP Packet */
    recvbuf = test_gtpu_read(gtpu);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /* Send AA-Request */
    test_rx_send_aar_audio(&rx_sid, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);

    /* Receive E-RABSetupRequest +
     * Activate dedicated EPS bearer context request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            S1AP_ProcedureCode_id_E_RABSetup,
            test_ue->s1ap_procedure_code);

    /* Send E-RABSetupResponse */
    bearer = test_bearer_find_by_ue_ebi(test_ue, 7);
    ogs_assert(bearer);
    audio = bearer;
    sendbuf = test_s1ap_build_e_rab_setup_response(bearer);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Activate dedicated EPS bearer context accept */
    esmbuf = testesm_build_activate_dedicated_eps_bearer_context_accept(bearer);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* DELAY is needed in dedicated EPS bearer */
    ogs_msleep(100);

    /* Send GTP-U ICMP Packet */
    rv = test_gtpu_send_ping(gtpu, bearer, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive GTP-U ICMP Packet */
    recvbuf = test_gtpu_read(gtpu);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /*
     * KI-70: Flow-Status DISABLED for the audio rule (call on hold). Only the
     * gate of the bearer's QER changes: PFCP modification, no GTP-C, so
     * nothing reaches the eNB, and the uplink gate drops the ping.
     */
    test_rx_aar_flow_status = OGS_DIAM_RX_FLOW_STATUS_DISABLED;
    test_rx_send_aar_audio(&rx_sid, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1000));

    rv = test_gtpu_send_ping(gtpu, audio, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    ABTS_INT_EQUAL(tc, 0, readable(gtpu, 1000));

    /*
     * KI-70: one direction at a time. ENABLED-DOWNLINK closes only the
     * uplink gate (the ping does not leave the PGW-U); ENABLED-UPLINK closes
     * only the downlink gate (the reply on the dedicated bearer is dropped).
     */
    test_rx_aar_flow_status = OGS_DIAM_RX_FLOW_STATUS_ENABLED_DOWNLINK;
    test_rx_send_aar_audio(&rx_sid, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1000));

    rv = test_gtpu_send_ping(gtpu, audio, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    ABTS_INT_EQUAL(tc, 0, readable(gtpu, 1000));

    test_rx_aar_flow_status = OGS_DIAM_RX_FLOW_STATUS_ENABLED_UPLINK;
    test_rx_send_aar_audio(&rx_sid, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1000));

    rv = test_gtpu_send_ping(gtpu, audio, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    ABTS_INT_EQUAL(tc, 0, readable(gtpu, 1000));

    /* KI-70: Flow-Status ENABLED (resume): the gate opens again */
    test_rx_aar_flow_status = OGS_DIAM_RX_FLOW_STATUS_ENABLED;
    test_rx_send_aar_audio(&rx_sid, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1000));

    rv = test_gtpu_send_ping(gtpu, audio, TEST_PING_IPV4);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);
    ABTS_INT_EQUAL(tc, 1, readable(gtpu, 3000));
    recvbuf = test_gtpu_read(gtpu);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    ogs_pkbuf_free(recvbuf);

    /*
     * KI-71: a real-time text media component (Media-Type TEXT) in its own
     * Rx session is accepted and gets a dedicated bearer of its own.
     */
    test_rx_aar_media_type = OGS_DIAM_RX_MEDIA_TYPE_TEXT;
    test_rx_send_aar_audio(&rx_sid2, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);

    /* Receive E-RABSetupRequest +
     * Activate dedicated EPS bearer context request */
    ABTS_INT_EQUAL(tc, 1, readable(s1ap, 3000));
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            S1AP_ProcedureCode_id_E_RABSetup,
            test_ue->s1ap_procedure_code);

    bearer = test_bearer_find_by_ue_ebi(test_ue, 8);
    ogs_assert(bearer);
    sendbuf = test_s1ap_build_e_rab_setup_response(bearer);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    esmbuf = testesm_build_activate_dedicated_eps_bearer_context_accept(bearer);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    ogs_msleep(100);

    /*
     * KI-71: Flow-Status REMOVED for the text component removes its PCC
     * rule, so its dedicated bearer is released.
     */
    test_rx_aar_media_type = OGS_DIAM_RX_MEDIA_TYPE_TEXT;
    test_rx_aar_flow_status = OGS_DIAM_RX_FLOW_STATUS_REMOVED;
    test_rx_send_aar_audio(&rx_sid2, sess,
            OGS_DIAM_RX_SUBSCRIPTION_ID_TYPE_END_USER_IMSI, 1, 1);

    /* Receive E-RABReleaseCommand +
     * Deactivate EPS bearer context request */
    ABTS_INT_EQUAL(tc, 1, readable(s1ap, 3000));
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            S1AP_ProcedureCode_id_E_RABRelease,
            test_ue->s1ap_procedure_code);

    sendbuf = test_s1ap_build_e_rab_release_response(bearer);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    esmbuf = testesm_build_deactivate_eps_bearer_context_accept(bearer);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    test_bearer_remove(bearer);

    /* The text Rx session has no rule left */
    test_rx_send_str(rx_sid2);
    ABTS_INT_EQUAL(tc, 0, readable(s1ap, 1000));

    /* Send Session-Termination-Request */
    bearer = audio;
    test_rx_send_str(rx_sid);

    /* Receive E-RABReleaseCommand +
     * Dectivate EPS bearer context request */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);
    ABTS_INT_EQUAL(tc,
            S1AP_ProcedureCode_id_E_RABRelease,
            test_ue->s1ap_procedure_code);

    /* Send E-RABReleaseResponse */
    sendbuf = test_s1ap_build_e_rab_release_response(bearer);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Send Deactivate EPS bearer context accept */
    esmbuf = testesm_build_deactivate_eps_bearer_context_accept(bearer);
    ABTS_PTR_NOTNULL(tc, esmbuf);
    sendbuf = test_s1ap_build_uplink_nas_transport(test_ue, esmbuf);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Test Bearer Remove */
    test_bearer_remove(bearer);

    /* Send Detach Request */
    emmbuf = testemm_build_detach_request(test_ue, 1, true, false);
    ABTS_PTR_NOTNULL(tc, emmbuf);
    sendbuf = test_s1ap_build_initial_ue_message(
            test_ue, emmbuf, S1AP_RRC_Establishment_Cause_mo_Signalling, true);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    /* Receive OLD UEContextReleaseCommand */
    enb_ue_s1ap_id = test_ue->enb_ue_s1ap_id;

    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send OLD UEContextReleaseComplete */
    sendbuf = test_s1ap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    test_ue->enb_ue_s1ap_id = enb_ue_s1ap_id;

    /* Receive UEContextReleaseCommand */
    recvbuf = testenb_s1ap_read(s1ap);
    ABTS_PTR_NOTNULL(tc, recvbuf);
    tests1ap_recv(test_ue, recvbuf);

    /* Send UEContextReleaseComplete */
    sendbuf = test_s1ap_build_ue_context_release_complete(test_ue);
    ABTS_PTR_NOTNULL(tc, sendbuf);
    rv = testenb_s1ap_send(s1ap, sendbuf);
    ABTS_INT_EQUAL(tc, OGS_OK, rv);

    ogs_msleep(300);

    /********** Remove Subscriber in Database */
    ABTS_INT_EQUAL(tc, OGS_OK, test_db_remove_ue(test_ue));

    /* eNB disonncect from MME */
    testenb_s1ap_close(s1ap);

    /* eNB disonncect from SGW */
    test_gtpu_close(gtpu);

    test_ue_remove(test_ue);
}


abts_suite *test_pcc(abts_suite *suite)
{
    suite = ADD_SUITE(suite)

    abts_run_test(suite, test1_func, NULL);

    return suite;
}
