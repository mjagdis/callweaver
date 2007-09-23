/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * See http://www.callweaver.org for more information about
 * the CallWeaver project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#if !defined(USE_VALE)
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
#endif

extern char stunserver_host[MAXHOSTNAMELEN];
extern struct sockaddr_in stunserver_ip;
extern int stunserver_portno;
extern int stun_active;             /*!< Is STUN globally enabled ?*/
extern int stundebug;               /*!< Are we debugging stun? */

typedef struct
{
    unsigned int id[4];
} __attribute__((packed)) stun_trans_id;

struct stun_header
{
    unsigned short msgtype;
    unsigned short msglen;
    stun_trans_id  id;
    uint8_t ies[0];
} __attribute__((packed));


struct stun_attr
{
    unsigned short attr;
    unsigned short len;
    uint8_t value[0];
} __attribute__((packed));

struct stun_addr
{
    uint8_t unused;
    uint8_t family;
    unsigned short port;
    unsigned int addr;
} __attribute__((packed));

struct stun_request
{
    struct stun_header req_head;
    struct stun_request *next;
    time_t whendone;
    int got_response;
    struct stun_addr mapped_addr;
};

struct stun_state
{
    uint16_t msgtype;
    stun_trans_id  id;
    uint8_t *username;
    uint8_t *password;
    struct stun_addr *mapped_addr;
    struct stun_addr *response_addr;
    struct stun_addr *source_addr;
};

int stun_addr2sockaddr(struct sockaddr_in *sin, struct stun_addr *addr);

struct stun_addr *opbx_stun_find_request(stun_trans_id *st);

struct stun_request *opbx_udp_stun_bindrequest(int fdus,
                                               struct sockaddr_in *suggestion, 
                                               const char *username,
                                               const char *password);

int stun_remove_request(stun_trans_id *st);

int stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len, struct stun_state *st);

int stun_do_debug(int fd, int argc, char *argv[]);

int stun_no_debug(int fd, int argc, char *argv[]);

void opbx_stun_init(void);

//static void append_attr_string(struct stun_attr **attr, int attrval, const char *s, int *len, int *left)
