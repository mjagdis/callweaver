/*
 * Vale - a library for media streaming.
 *
 * rfc3489.h - STUN - Simple Traversal of User Datagram Protocol (UDP)
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
 * $Id: rfc3489.h,v 1.1.1.1 2007/04/03 13:13:37 steveu Exp $
 */

#if !defined(_VALE_RFC3489_H_)
#define _VALE_RFC3489_H_

enum
{
    RFC3489_STATE_IDLE = 0,
    RFC3489_STATE_REQUEST_PENDING,
    RFC3489_STATE_RESPONSE_RECEIVED
};
 
#define RFC3489_IGNORE                                          0
#define RFC3489_ACCEPT                                          1

#define RFC3489_MSG_TYPE_BINDING_REQUEST                        0x0001
#define RFC3489_MSG_TYPE_BINDING_RESPONSE                       0x0101
#define RFC3489_MSG_TYPE_BINDING_ERROR                          0x0111
#define RFC3489_MSG_TYPE_SHARED_SECRET_REQUEST                  0x0002
#define RFC3489_MSG_TYPE_SHARED_SECRET_RESPONSE                 0x0102
#define RFC3489_MSG_TYPE_SHARED_SECRET_ERROR                    0x0112
#define RFC3489_MSG_TYPE_ALLOCATE_REQUEST                       0x0003
#define RFC3489_MSG_TYPE_ALLOCATE_RESPONSE                      0x0103
#define RFC3489_MSG_TYPE_ALLOCATE_ERROR_RESPONSE                0x0113
#define RFC3489_MSG_TYPE_SEND_REQUEST                           0x0004
#define RFC3489_MSG_TYPE_SEND_RESPONSE                          0x0104
#define RFC3489_MSG_TYPE_SEND_ERROR_RESPONSE                    0x0114
#define RFC3489_MSG_TYPE_DATA_INDICATION                        0x0005
#define RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_REQUEST         0x0006
#define RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_RESPONSE        0x0106
#define RFC3489_MSG_TYPE_SET_ACTIVE_DESTINATION_ERROR_RESPONSE  0x0116

#define RFC3489_ATTRIB_MAPPED_ADDRESS                           0x0001
#define RFC3489_ATTRIB_RESPONSE_ADDRESS                         0x0002
#define RFC3489_ATTRIB_CHANGE_REQUEST                           0x0003
#define RFC3489_ATTRIB_SOURCE_ADDRESS                           0x0004
#define RFC3489_ATTRIB_CHANGED_ADDRESS                          0x0005
#define RFC3489_ATTRIB_USERNAME                                 0x0006
#define RFC3489_ATTRIB_PASSWORD                                 0x0007
#define RFC3489_ATTRIB_MESSAGE_INTEGRITY                        0x0008
#define RFC3489_ATTRIB_ERROR_CODE                               0x0009
#define RFC3489_ATTRIB_UNKNOWN_ATTRIBUTES                       0x000A
#define RFC3489_ATTRIB_REFLECTED_FROM                           0x000B
#define RFC3489_ATTRIB_LIFETIME                                 0x000D
#define RFC3489_ATTRIB_ALTERNATE_SERVER_A                       0x000E
#define RFC3489_ATTRIB_MAGIC_COOKIE                             0x000F
#define RFC3489_ATTRIB_BANDWIDTH                                0x0010
#define RFC3489_ATTRIB_DESTINATION_ADDRESS                      0x0011
#define RFC3489_ATTRIB_REMOTE_ADDRESS                           0x0012
#define RFC3489_ATTRIB_DATA                                     0x0013
#define RFC3489_ATTRIB_REALM                                    0x0014
#define RFC3489_ATTRIB_NONCE                                    0x0015
#define RFC3489_ATTRIB_REQUESTED_ADDRESS_TYPE                   0x0016
#define RFC3489_ATTRIB_XOR_ONLY                                 0x0021
#define RFC3489_ATTRIB_XOR_MAPPED_ADDRESS                       0x8020
#define RFC3489_ATTRIB_FINGERPRINT                              0x8021
#define RFC3489_ATTRIB_SERVER                                   0x8022
#define RFC3489_ATTRIB_ALTERNATE_SERVER_B                       0x8023
#define RFC3489_ATTRIB_REFRESH_INTERVAL                         0x8024

#define RFC3489_ADDR_FAMILY_IPV4                                1

typedef struct
{
    uint32_t id[4];
} __attribute__((packed)) rfc3489_trans_id_t;

typedef struct
{
    uint16_t msgtype;
    uint16_t msglen;
    rfc3489_trans_id_t id;
    uint8_t ies[0];
} __attribute__((packed)) rfc3489_header_t;

typedef struct
{
    uint16_t attr;
    uint16_t len;
    uint8_t value[0];
} __attribute__((packed)) rfc3489_attr_t;

typedef struct
{
    uint8_t unused;
    uint8_t family;
    uint16_t port;
    uint32_t addr;
} __attribute__((packed)) rfc3489_addr_t;

typedef struct rfc3489_request_s
{
    rfc3489_header_t req_head;
    struct rfc3489_request_s *next;
    time_t whendone;
    int got_response;
    rfc3489_addr_t mapped_addr;
} rfc3489_request_t;

typedef struct
{
    uint16_t msgtype;
    rfc3489_trans_id_t id;
    uint8_t *username;
    uint8_t *password;
    uint8_t *server;
    rfc3489_addr_t *mapped_addr;
    rfc3489_addr_t *response_addr;
    rfc3489_addr_t *source_addr;
} rfc3489_state_t;

extern struct sockaddr_in rfc3489_server_ip;
extern int rfc3489_active;

#if defined(__cplusplus)
extern "C"
{
#endif

extern CW_API_PUBLIC const char *rfc3489_msg_type_to_str(int msg);

extern CW_API_PUBLIC const char *rfc3489_attribute_type_to_str(int attr);

extern CW_API_PUBLIC int rfc3489_addr_to_sockaddr(struct sockaddr_in *sin, rfc3489_addr_t *addr);

extern CW_API_PUBLIC struct rfc3489_request_s *rfc3489_udp_binding_request(int fdus,
                                                      struct sockaddr_in *suggestion, 
                                                      const char *username,
                                                      const char *password);

extern CW_API_PUBLIC rfc3489_request_t *rfc3489_find_request(rfc3489_trans_id_t *id);

extern CW_API_PUBLIC int rfc3489_delete_request(rfc3489_trans_id_t *id);

extern CW_API_PUBLIC int rfc3489_handle_packet(int s, struct sockaddr_in *src, uint8_t data[], size_t len, rfc3489_state_t *st);

extern CW_API_PUBLIC void rfc3489_init(struct sockaddr_in *rfc3489_server);

#if defined(__cplusplus)
}
#endif

#endif
/*- End of file ------------------------------------------------------------*/
