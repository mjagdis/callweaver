/*
 * Vale - a library for media streaming.
 *
 * rfc3489.c - STUN - Simple Traversal of User Datagram Protocol (UDP)
 *             Through Network Address Translators (NATs)
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2006 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: rfc3489.c,v 1.2 2007/04/13 12:24:49 steveu Exp $
 */

/*! \file */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/udp.h"
#include "callweaver/rfc3489.h"

struct sockaddr_in rfc3489_server_ip;
int rfc3489_active = FALSE;

rfc3489_request_t *rfc3489_req_queue;

const char *rfc3489_msg_type_to_str(int msg)
{
    switch (msg)
    {
    case RFC3489_MSG_TYPE_BINDING_REQUEST:
        return "Binding request";
    case RFC3489_MSG_TYPE_BINDING_RESPONSE:
        return "Binding response";
    case RFC3489_MSG_TYPE_BINDING_ERROR:
        return "Binding error response";
    case RFC3489_MSG_TYPE_SHARED_SECRET_REQUEST:
        return "Shared secret request";
    case RFC3489_MSG_TYPE_SHARED_SECRET_RESPONSE:
        return "Shared secret response";
    case RFC3489_MSG_TYPE_SHARED_SECRET_ERROR:
        return "Shared secret error response";
    case RFC3489_MSG_TYPE_ALLOCATE_REQUEST:
        return "Allocate request";
    case RFC3489_MSG_TYPE_ALLOCATE_RESPONSE:
        return "Allocate response";
    case RFC3489_MSG_TYPE_ALLOCATE_ERROR_RESPONSE:
        return "Allocate error response";
    case RFC3489_MSG_TYPE_SEND_REQUEST:
        return "Send request";
    case RFC3489_MSG_TYPE_SEND_RESPONSE:
        return "Send response";
    case RFC3489_MSG_TYPE_SEND_ERROR_RESPONSE:
        return "Send error response";
    case RFC3489_MSG_TYPE_DATA_INDICATION:
        return "Data indication";
    case RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_REQUEST:
        return "Set active destination request";
    case RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_RESPONSE:
        return "Set active destination response";
    case RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_ERROR_RESPONSE:
        return "Set active destination error response";
    }
    return "Non-RFC3489 message";
}
/*- End of function --------------------------------------------------------*/

const char *rfc3489_attribute_type_to_str(int attr)
{
    switch (attr)
    {
    case RFC3489_ATTRIB_MAPPED_ADDRESS:
        return "Mapped address";
    case RFC3489_ATTRIB_RESPONSE_ADDRESS:
        return "Response address";
    case RFC3489_ATTRIB_CHANGED_ADDRESS:
        return "Changed address";
    case RFC3489_ATTRIB_CHANGE_REQUEST:
        return "Change request";
    case RFC3489_ATTRIB_SOURCE_ADDRESS:
        return "Source address";
    case RFC3489_ATTRIB_USERNAME:
        return "Username";
    case RFC3489_ATTRIB_PASSWORD:
        return "Password";
    case RFC3489_ATTRIB_MESSAGE_INTEGRITY:
        return "Message integrity";
    case RFC3489_ATTRIB_ERROR_CODE:
        return "Error code";
    case RFC3489_ATTRIB_UNKNOWN_ATTRIBUTES:
        return "Unknown attributes";
    case RFC3489_ATTRIB_REFLECTED_FROM:
        return "Reflected from";
    case RFC3489_ATTRIB_LIFETIME:
        return "Lifetime";
    case RFC3489_ATTRIB_MAGIC_COOKIE:
        return "Magic cookie";
    case RFC3489_ATTRIB_BANDWIDTH:
        return "Bandwidth";
    case RFC3489_ATTRIB_DESTINATION_ADDRESS:
        return "Destination address";
    case RFC3489_ATTRIB_REMOTE_ADDRESS:
        return "Remote address";
    case RFC3489_ATTRIB_DATA:
        return "Data";
    case RFC3489_ATTRIB_REALM:
        return "Realm";
    case RFC3489_ATTRIB_NONCE:
        return "Nonce";
    case RFC3489_ATTRIB_REQUESTED_ADDRESS_TYPE:
        return "Requested address type";
    case RFC3489_ATTRIB_XOR_ONLY:
        return "XOR only";
    case RFC3489_ATTRIB_XOR_MAPPED_ADDRESS:
        return "XOR mapped address";
    case RFC3489_ATTRIB_FINGERPRINT:
        return "Fingerprint";
    case RFC3489_ATTRIB_SERVER:
        return "Server";
    case RFC3489_ATTRIB_ALTERNATE_SERVER_A:
    case RFC3489_ATTRIB_ALTERNATE_SERVER_B:
        return "Alternate server";
    case RFC3489_ATTRIB_REFRESH_INTERVAL:
        return "Refresh interval";
    default:
        break;
    }
    return "Non-RFC3489 attribute";
}
/*- End of function --------------------------------------------------------*/

int rfc3489_addr_to_sockaddr(struct sockaddr_in *sin, rfc3489_addr_t *addr)
{
    if (addr->family == RFC3489_ADDR_FAMILY_IPV4)
    {
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = addr->addr;
        sin->sin_port = addr->port;
        return 0;
    }
    return -1;
}
/*- End of function --------------------------------------------------------*/

static int rfc3489_process_attr(rfc3489_state_t *state, rfc3489_attr_t *attr)
{
    int attr_len;

printf("Attr %s 0x%X\n", rfc3489_attribute_type_to_str(ntohs(attr->attr)), ntohs(attr->attr));
    attr_len = ntohs(attr->len) + sizeof(rfc3489_attr_t);
{
    int i;
    uint8_t *data;
    data = (uint8_t *) attr;
    for (i = 0;  i < attr_len;  i++)
        printf("%02x ", data[i]);
    printf("\n");
}
    switch (ntohs(attr->attr))
    {
    case RFC3489_ATTRIB_MAPPED_ADDRESS:
        state->mapped_addr = (rfc3489_addr_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_RESPONSE_ADDRESS:
        state->response_addr = (rfc3489_addr_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_CHANGE_REQUEST:
        break;
    case RFC3489_ATTRIB_SOURCE_ADDRESS:
        state->source_addr = (rfc3489_addr_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_CHANGED_ADDRESS:
        break;
    case RFC3489_ATTRIB_USERNAME:
        state->username = (uint8_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_PASSWORD:
        state->password = (uint8_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_MESSAGE_INTEGRITY:
        break;
    case RFC3489_ATTRIB_ERROR_CODE:
        break;
    case RFC3489_ATTRIB_UNKNOWN_ATTRIBUTES:
        break;
    case RFC3489_ATTRIB_REFLECTED_FROM:
        break;
    case RFC3489_ATTRIB_LIFETIME:
        break;
    case RFC3489_ATTRIB_MAGIC_COOKIE:
        break;
    case RFC3489_ATTRIB_BANDWIDTH:
        break;
    case RFC3489_ATTRIB_DESTINATION_ADDRESS:
        break;
    case RFC3489_ATTRIB_REMOTE_ADDRESS:
        break;
    case RFC3489_ATTRIB_DATA:
        break;
    case RFC3489_ATTRIB_REALM:
        break;
    case RFC3489_ATTRIB_NONCE:
        break;
    case RFC3489_ATTRIB_REQUESTED_ADDRESS_TYPE:
        break;
    case RFC3489_ATTRIB_XOR_ONLY:
        break;
    case RFC3489_ATTRIB_XOR_MAPPED_ADDRESS:
        break;
    case RFC3489_ATTRIB_FINGERPRINT:
        break;
    case RFC3489_ATTRIB_SERVER:
        state->server = (uint8_t *) (attr->value);
        break;
    case RFC3489_ATTRIB_ALTERNATE_SERVER_A:
    case RFC3489_ATTRIB_ALTERNATE_SERVER_B:
        break;
    case RFC3489_ATTRIB_REFRESH_INTERVAL:
        break;
    default:
        break;
    }
    return 0;
}
/*- End of function --------------------------------------------------------*/

static int add_tlv_string(rfc3489_attr_t **attr, int attr_val, const char *s, int limit)
{
    int size;

    size = sizeof(**attr) + strlen(s);
    if (limit <= size)
        return 0;
    (*attr)->attr = htons(attr_val);
    (*attr)->len = htons(strlen(s));
    memcpy((*attr)->value, s, strlen(s));
    *attr = (rfc3489_attr_t *) ((*attr)->value + strlen(s));
    return size;
}
/*- End of function --------------------------------------------------------*/

static int add_tlv_address(rfc3489_attr_t **attr, int attr_val, const struct sockaddr_in *sin, int limit)
{
    int size;
    rfc3489_addr_t *addr;
    
    size = sizeof(**attr) + 8;
    if (limit <= size)
        return 0;
    (*attr)->attr = htons(attr_val);
    (*attr)->len = htons(1 + 1 + 4 + 2);
    addr = (rfc3489_addr_t *) (*attr)->value;
    addr->unused = 0;
    addr->family = RFC3489_ADDR_FAMILY_IPV4;
    addr->port = sin->sin_port;
    addr->addr = sin->sin_addr.s_addr;
    *attr = (rfc3489_attr_t *) ((*attr)->value + 1 + 1 + 4 + 2);
    return size;
}
/*- End of function --------------------------------------------------------*/

static void rfc3489_req_id(rfc3489_header_t *req)
{
    int x;
    
    for (x = 0;  x < 4;  x++)
        req->id.id[x] = random();
}
/*- End of function --------------------------------------------------------*/

rfc3489_request_t *rfc3489_find_request(rfc3489_trans_id_t *id)
{
    rfc3489_request_t *req_entry;

    req_entry = rfc3489_req_queue;
    while (req_entry)
    {
        if (memcmp(&req_entry->req_head.id, id, sizeof(*id)) == 0)
            break;
        req_entry = req_entry->next;
    }
    return req_entry;
}
/*- End of function --------------------------------------------------------*/

int rfc3489_delete_request(rfc3489_trans_id_t *id)
{
    rfc3489_request_t *req_queue;
    rfc3489_request_t *req_queue_prev;
    rfc3489_request_t *del_entry;
    time_t now;

    time(&now);
    req_queue = rfc3489_req_queue;
    req_queue_prev = NULL;

    while (req_queue)
    {
        if (req_queue->got_response
            &&
            memcmp(&req_queue->req_head.id, id, sizeof(*id)) == 0) 
        {
            del_entry = req_queue;
            if (req_queue_prev)
            {
                req_queue_prev->next = req_queue->next;
                req_queue = req_queue_prev;
            }
            else
            {
                rfc3489_req_queue = req_queue->next;
                req_queue = rfc3489_req_queue;
            }
            free(del_entry);
        }
        req_queue_prev = req_queue;
        if (req_queue)
            req_queue = req_queue->next;
    }
    /* Removing old requests, after a SIP reload, whose requests are not linked to any transmission */
    req_queue = rfc3489_req_queue;
    req_queue_prev = NULL;
    while (req_queue)
    {
        if (req_queue->whendone + 300 < now)
        {
            del_entry = req_queue;
            if (req_queue_prev)
            {
                req_queue_prev->next = req_queue->next;
                req_queue = req_queue_prev;
            }
            else
            {
                rfc3489_req_queue = req_queue->next;
                req_queue = rfc3489_req_queue;
            }
            free(del_entry);
        }
        req_queue_prev = req_queue;
        if (req_queue)
            req_queue = req_queue->next;
    }
    return 0;
}
/*- End of function --------------------------------------------------------*/

rfc3489_request_t *rfc3489_udp_binding_request(int fd,
                                         struct sockaddr_in *server, 
                                         const char *username,
                                         const char *password)
{
    uint8_t reqdata[1024];
    int reqlen;
    int reqlimit;
    rfc3489_header_t *reqh;
    rfc3489_attr_t *attr;
    rfc3489_request_t *myreq;

    if (server == NULL)
    {
        if ((server = &rfc3489_server_ip) == NULL)
            return NULL;
    }
    reqh = (rfc3489_header_t *) reqdata;
    rfc3489_req_id(reqh);
    reqlen = 0;
    reqlimit = sizeof(reqdata) - sizeof(rfc3489_header_t);
    reqh->msgtype = 0;
    reqh->msglen = 0;
    attr = (rfc3489_attr_t *) reqh->ies;
    if (username)
        reqlen += add_tlv_string(&attr, RFC3489_ATTRIB_USERNAME, username, reqlimit - reqlen);
    if (password)
        reqlen += add_tlv_string(&attr, RFC3489_ATTRIB_PASSWORD, password, reqlimit - reqlen);
    reqh->msglen = htons(reqlen);
    reqh->msgtype = htons(RFC3489_MSG_TYPE_BINDING_REQUEST);
    if ((myreq = malloc(sizeof(rfc3489_request_t))))
    {
        memset(myreq, 0, sizeof(rfc3489_request_t));
        memcpy(&myreq->req_head, reqh, sizeof(rfc3489_header_t));
        if (sendto(fd, reqh, sizeof(*reqh) + reqlen, 0, (struct sockaddr *) server, sizeof(*server)) >= 0)
        {
            time(&myreq->whendone);
            myreq->next = rfc3489_req_queue;
            rfc3489_req_queue = myreq;
            return myreq;
        }
        free(myreq);
    }
    return NULL;
}
/*- End of function --------------------------------------------------------*/

int rfc3489_handle_packet(int s,
                       struct sockaddr_in *src, 
                       uint8_t data[],
                       size_t len, 
                       rfc3489_state_t *st)    
{
    rfc3489_header_t *resp;
    rfc3489_header_t *hdr;
    rfc3489_attr_t *attr;
    rfc3489_request_t *req_queue;
    rfc3489_request_t *req_entry;
    uint8_t respdata[1024];
    int ret;
    int resp_len;
    int resp_limit;
    int attr_len;

    ret = RFC3489_IGNORE;    
    hdr = (rfc3489_header_t *) data;
    memset(st, 0, sizeof(st));
    memcpy(&st->id, &hdr->id, sizeof(rfc3489_trans_id_t));

    /* Deal with the header */
    if (len < sizeof(rfc3489_header_t))
        return -1;
    if (len >= sizeof(rfc3489_header_t) + ntohs(hdr->msglen))
        len = ntohs(hdr->msglen);
    data += sizeof(rfc3489_header_t);
    /* Run through the attributes */
    while (len >= 0)
    {
        if (len < sizeof(rfc3489_attr_t))
            break;
        attr = (rfc3489_attr_t *) data;
        if (ntohs(attr->len) > len)
            break;
        if (rfc3489_process_attr(st, attr))
            break;
        /* Clear attribute, so if the previous entry was a string, this will
           NULL terminate it. */
        attr->attr = 0;
        attr_len = ntohs(attr->len) + sizeof(rfc3489_attr_t);
        data += attr_len;
        len -= attr_len;
    }
    /* Clear the byte beyond the message, so if the previous entry was a string, this will
       NULL terminate it. This is a little nasty, as we are stretching the string which was
       passed to us. */
    *data = '\0';
    st->msgtype = ntohs(hdr->msgtype);
    if (len)
        return -1;
    switch (st->msgtype)
    {
    case RFC3489_MSG_TYPE_BINDING_REQUEST:
        /* Build and send a response */
        resp_len = 0;
        resp = (rfc3489_header_t *) respdata;
        attr = (rfc3489_attr_t *) resp->ies;
        resp->id = hdr->id;
        resp_limit = sizeof(respdata) - sizeof(rfc3489_header_t);
        resp->msgtype = htons(RFC3489_MSG_TYPE_BINDING_RESPONSE);
        if (st->username)
            resp_len += add_tlv_string(&attr, RFC3489_ATTRIB_USERNAME, (const char *) st->username, resp_limit - resp_len);
        resp_len += add_tlv_address(&attr, RFC3489_ATTRIB_MAPPED_ADDRESS, src, resp_limit - resp_len);
        resp->msglen = htons(resp_len);
        sendto(s, resp, sizeof(*resp) + resp_len, 0, (struct sockaddr *) src, sizeof(*src));
        ret = RFC3489_ACCEPT;
        break;
    case RFC3489_MSG_TYPE_BINDING_RESPONSE:
        if ((req_entry = rfc3489_find_request(&st->id)))
        {
            req_entry->got_response = TRUE;
            memcpy(&req_entry->mapped_addr, st->mapped_addr, sizeof(rfc3489_addr_t));
        }
        req_queue = rfc3489_req_queue;
        ret = RFC3489_ACCEPT;
        break;
    case RFC3489_MSG_TYPE_BINDING_ERROR:
        break;
    case RFC3489_MSG_TYPE_SHARED_SECRET_REQUEST:
        break;
    case RFC3489_MSG_TYPE_SHARED_SECRET_RESPONSE:
        break;
    case RFC3489_MSG_TYPE_SHARED_SECRET_ERROR:
        break;
    default:
        break;
    }
    return ret;
}
/*- End of function --------------------------------------------------------*/

void rfc3489_init(struct sockaddr_in *rfc3489_server)
{
    rfc3489_active = TRUE;
    rfc3489_server_ip = *rfc3489_server;
    rfc3489_req_queue = NULL;
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
