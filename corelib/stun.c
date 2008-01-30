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

#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <vale/rfc3489.h>
#include <vale/udp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/rtp.h"
#include "callweaver/lock.h"
#include "callweaver/stun.h"
#include "callweaver/logger.h"
#include "callweaver/cli.h"
#include "callweaver/utils.h"
#include "callweaver/options.h"
#include "callweaver/udpfromto.h"

char stunserver_host[MAXHOSTNAMELEN] = "";
struct sockaddr_in stunserver_ip;
int stunserver_portno;
int stundebug = 0;

rfc3489_request_t *stun_req_queue;

static int stun_process_attr(rfc3489_state_t *state, rfc3489_attr_t *attr)
{
    char iabuf[INET_ADDRSTRLEN];
    struct sockaddr_in sin;

    if (stundebug  &&  option_debug)
        cw_verbose("Found STUN Attribute %s (%04x), length %d\n",
            rfc3489_attribute_type_to_str(ntohs(attr->attr)), ntohs(attr->attr), ntohs(attr->len));
    switch (ntohs(attr->attr))
    {
    case RFC3489_ATTRIB_USERNAME:
        state->username = (unsigned char *)(attr->value);
        break;
    case RFC3489_ATTRIB_PASSWORD:
        state->password = (unsigned char *)(attr->value);
        break;
    case RFC3489_ATTRIB_MAPPED_ADDRESS:
        state->mapped_addr = (rfc3489_addr_t *)(attr->value);
        if (stundebug)
        {
            rfc3489_addr_to_sockaddr(&sin, state->mapped_addr);
            cw_verbose("STUN: Mapped address is %s\n", cw_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr));
            cw_verbose("STUN: Mapped port is %d\n", ntohs(state->mapped_addr->port));
        }
        break;
    case RFC3489_ATTRIB_RESPONSE_ADDRESS:
        state->response_addr = (rfc3489_addr_t *)(attr->value);
        break;
    case RFC3489_ATTRIB_SOURCE_ADDRESS:
        state->source_addr = (rfc3489_addr_t *)(attr->value);
        break;
    default:
        if (stundebug && option_debug)
        {
            cw_verbose("Ignoring STUN attribute %s (%04x), length %d\n", 
                         rfc3489_attribute_type_to_str(ntohs(attr->attr)),
                         ntohs(attr->attr),
                         ntohs(attr->len));
        }
        break;
    }
    return 0;
}

static void append_attr_string(rfc3489_attr_t **attr, int attrval, const char *s, int *len, int *left)
{
    int size = sizeof(**attr) + strlen(s);

    if (*left > size)
    {
        (*attr)->attr = htons(attrval);
        (*attr)->len = htons(strlen(s));
        memcpy((*attr)->value, s, strlen(s));
        (*attr) = (rfc3489_attr_t *)((*attr)->value + strlen(s));
        *len += size;
        *left -= size;
    }
}

static void append_attr_address(rfc3489_attr_t **attr, int attrval, struct sockaddr_in *sin, int *len, int *left)
{
    int size = sizeof(**attr) + 8;
    rfc3489_addr_t *addr;
    
    if (*left > size)
    {
        (*attr)->attr = htons(attrval);
        (*attr)->len = htons(8);
        addr = (rfc3489_addr_t *)((*attr)->value);
        addr->unused = 0;
        addr->family = 0x01;
        addr->port = sin->sin_port;
        addr->addr = sin->sin_addr.s_addr;
        (*attr) = (rfc3489_attr_t *)((*attr)->value + 8);
        *len += size;
        *left -= size;
    }
}

static int stun_send(int s, struct sockaddr_in *dst, rfc3489_header_t *resp)
{
    return sendto(s,
                  resp,
                  ntohs(resp->msglen) + sizeof(*resp),
                  0,
                  (struct sockaddr *) dst,
                  sizeof(*dst));
/*
    // Alternative way to send STUN PACKETS using CallWeaver library functions.
    return cw_sendfromto(
	s,
	resp,ntohs(resp->msglen) + sizeof(*resp),0,
	NULL,0,
        (struct sockaddr *) dst,
        sizeof( struct sockaddr_in ) 
	);
*/
}

static void stun_req_id(rfc3489_header_t *req)
{
    int x;
    
    for (x = 0;  x < 4;  x++)
        req->id.id[x] = cw_random();
}

/* ************************************************************************* */

rfc3489_addr_t *cw_stun_find_request(rfc3489_trans_id_t *st)
{
    rfc3489_request_t *req_queue;
    rfc3489_addr_t *a = NULL;

    req_queue=stun_req_queue;

    if (stundebug)
        cw_verbose("** Trying to lookup stun response for this sip packet %d\n", st->id[0]);
    while (req_queue != NULL)
    {
        //cw_verbose("** STUN FIND REQUEST compare trans_id %d\n",req_queue->req_head.id.id[0]);

        if (req_queue->got_response
            &&
            !memcmp((void *) &req_queue->req_head.id, st, sizeof(rfc3489_trans_id_t))) 
        {
            if (stundebug)
                cw_verbose("** Found request in request queue for reqresp lookup\n");
            struct sockaddr_in sin;
            //char iabuf[INET_ADDRSTRLEN];

            rfc3489_addr_to_sockaddr(&sin, &req_queue->mapped_addr);
            //cw_verbose("STUN: passing Mapped address is %s\n", cw_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr));
            //cw_verbose("STUN: passing Mapped port is %d\n", ntohs(req_queue->mapped_addr.port));
            a = &req_queue->mapped_addr;        
            if (!stundebug)
                return a;
        }
        req_queue = req_queue->next;
    }
    return a;
}

/* ************************************************************************* */
int stun_remove_request(rfc3489_trans_id_t *st)
{
    rfc3489_request_t *req_queue;
    rfc3489_request_t *req_queue_prev;
    rfc3489_request_t *delqueue;
    time_t now;
    int found = 0;

    time(&now);
    req_queue = stun_req_queue;
    req_queue_prev = NULL;

    if (stundebug)
        cw_verbose("** Trying to lookup for removal stun queue %d\n", st->id[0]);
    while (req_queue != NULL)
    {
        if (req_queue->got_response
            &&
            !memcmp(&req_queue->req_head.id, st, sizeof(req_queue->req_head.id))) 
        {
            found = 1;
            delqueue = req_queue;
            if (stundebug)
                cw_verbose("** Found: request in removal stun queue %d\n", st->id[0]);
            if (req_queue_prev != NULL)
            {
                req_queue_prev->next = req_queue->next;
                req_queue = req_queue_prev;
            }
            else
            {
                stun_req_queue = req_queue->next;
                req_queue = stun_req_queue;
            }
            free(delqueue);
        }
        req_queue_prev = req_queue;
        if (req_queue != NULL)
            req_queue = req_queue->next;
    }
    if (!found)
        cw_verbose("** Not Found: request in removal stun queue %d\n", st->id[0]);

    /* Removing old requests, caused by "sip reload" whose requests are not linked to any transmission */
    req_queue = stun_req_queue;
    req_queue_prev = NULL;
    while (req_queue != NULL)
    {
        if (req_queue->whendone + 300 < now)
        {
            if (stundebug)
                cw_verbose("** DROP: request in removal stun queue %d (too old)\n",req_queue->req_head.id.id[0]);
            delqueue = req_queue;
            if (req_queue_prev != NULL)
            {
                req_queue_prev->next = req_queue->next;
                req_queue = req_queue_prev;
            }
            else
            {
                stun_req_queue = req_queue->next;
                req_queue = stun_req_queue;
            }
            free(delqueue);
        }
        req_queue_prev = req_queue;
        if (req_queue)
            req_queue = req_queue->next;
    }
    return 0;
}

/* ************************************************************************* */

rfc3489_request_t *cw_udp_stun_bindrequest(int fdus,
                                             struct sockaddr_in *suggestion, 
                                             const char *username,
                                             const char *password)
{
    //rfc3489_request_t *req;
    rfc3489_header_t *reqh;
    unsigned char reqdata[1024];
    int reqlen, reqleft;
    rfc3489_attr_t *attr;
    rfc3489_request_t *myreq=NULL;

    reqh = (rfc3489_header_t *) reqdata;
    stun_req_id(reqh);
    reqlen = 0;
    reqleft = sizeof(reqdata) - sizeof(rfc3489_header_t);
    reqh->msgtype = 0;
    reqh->msglen = 0;
    attr = (rfc3489_attr_t *) reqh->ies;

    if (username)
        append_attr_string(&attr, RFC3489_ATTRIB_USERNAME, username, &reqlen, &reqleft);
    if (password)
        append_attr_string(&attr, RFC3489_ATTRIB_PASSWORD, password, &reqlen, &reqleft);

    reqh->msglen = htons(reqlen);
    reqh->msgtype = htons(RFC3489_MSG_TYPE_BINDING_REQUEST);

    if ((myreq = malloc(sizeof(rfc3489_request_t))) != NULL)
    {
        memset(myreq, 0, sizeof(rfc3489_request_t));
        memcpy(&myreq->req_head, reqh, sizeof(rfc3489_header_t));
        if (stun_send(fdus, suggestion, reqh) != -1)
        {
            if (stundebug) 
                cw_verbose("** STUN Packet SENT %d %d\n", reqh->id.id[0], myreq->req_head.id.id[0]);
            time(&myreq->whendone);
            myreq->next = stun_req_queue;
            stun_req_queue = myreq;
            return myreq;
        }
        else
        {
            free(myreq);
        }
    }

    return NULL;
}

int stun_handle_packet(int s, 
                       struct sockaddr_in *src, 
                       unsigned char *data,
                       size_t len, 
                       rfc3489_state_t *st)    
{
    rfc3489_header_t *resp;
    rfc3489_header_t *hdr = (rfc3489_header_t *)data;
    rfc3489_attr_t *attr;
    //rfc3489_state_t st;
    int ret = RFC3489_IGNORE;
    unsigned char respdata[1024];
    int resplen;
    int respleft;
    rfc3489_request_t *req_queue;
    rfc3489_request_t *req_queue_prev;

    memset(st, 0, sizeof(st));
    memcpy(&st->id, &hdr->id, sizeof(st->id));

    if (len < sizeof(rfc3489_header_t))
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Runt STUN packet (only %zd, wanting at least %zd)\n", len, sizeof(rfc3489_header_t));
        return -1;
    }
    if (stundebug)
        cw_verbose("STUN Packet, msg %s (%04x), length: %d\n", rfc3489_msg_type_to_str(ntohs(hdr->msgtype)), ntohs(hdr->msgtype), ntohs(hdr->msglen));

    if (ntohs(hdr->msglen) > len - sizeof(rfc3489_header_t))
    {
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "Scrambled STUN packet length (got %d, expecting %zd)\n", ntohs(hdr->msglen), len - sizeof(rfc3489_header_t));
    }
    else
        len = ntohs(hdr->msglen);
    data += sizeof(rfc3489_header_t);

    while (len)
    {
        if (len < sizeof(rfc3489_attr_t))
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Runt Attribute (got %zd, expecting %zd)\n", len, sizeof(rfc3489_attr_t));
            break;
        }
        attr = (rfc3489_attr_t *) data;
        if (ntohs(attr->len) > len)
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Inconsistent Attribute (length %d exceeds remaining msg len %zd)\n", ntohs(attr->len), len);
            break;
        }

        if (stun_process_attr(st, attr))
        {
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Failed to handle attribute %s (%04x)\n", rfc3489_attribute_type_to_str(ntohs(attr->attr)), ntohs(attr->attr));
            break;
        }
        /* Clear attribute in case previous entry was a string */
        attr->attr = 0;
        data += ntohs(attr->len) + sizeof(rfc3489_attr_t);
        len -= ntohs(attr->len) + sizeof(rfc3489_attr_t);
    }

    /* Null terminate any string */
    *data = '\0';
    resp = (rfc3489_header_t *)respdata;
    resplen = 0;
    respleft = sizeof(respdata) - sizeof(rfc3489_header_t);
    resp->id = hdr->id;
    resp->msgtype = 0;
    resp->msglen = 0;
    attr = (rfc3489_attr_t *)resp->ies;
    if (!len)
    {
        st->msgtype=ntohs(hdr->msgtype);
        switch (ntohs(hdr->msgtype))
        {
        case RFC3489_MSG_TYPE_BINDING_REQUEST:
            if (stundebug)
            {
                cw_verbose("STUN Bind Request, username: %s\n", 
                             st->username  ?  (const char *) st->username  :  "<none>");
            }
            if (st->username)
                append_attr_string(&attr, RFC3489_ATTRIB_USERNAME, (const char *)st->username, &resplen, &respleft);
            append_attr_address(&attr, RFC3489_ATTRIB_MAPPED_ADDRESS, src, &resplen, &respleft);
            resp->msglen = htons(resplen);
            resp->msgtype = htons(RFC3489_MSG_TYPE_BINDING_RESPONSE);
            stun_send(s, src, resp);
            ret = RFC3489_ACCEPT;
            break;
        case RFC3489_MSG_TYPE_BINDING_RESPONSE:
            if (stundebug)
                cw_verbose("** STUN Bind Response\n");
            req_queue = stun_req_queue;
            req_queue_prev = NULL;
            while (req_queue != NULL)
            {
                if (!req_queue->got_response
                    && 
                    memcmp(&req_queue->req_head.id, (void *) &st->id, sizeof(req_queue->req_head.id)) == 0)
                {
                    if (stundebug)
                        cw_verbose("** Found response in request queue. ID: %d done at: %ld gotresponse: %d\n",req_queue->req_head.id.id[0],(long int)req_queue->whendone,req_queue->got_response);
                    req_queue->got_response = 1;
                    memcpy(&req_queue->mapped_addr, st->mapped_addr, sizeof(rfc3489_addr_t));
                }
                else
                {
                    if (stundebug)
                        cw_verbose("** STUN request not matching. ID: %d done at: %ld gotresponse %d:\n",req_queue->req_head.id.id[0],(long int)req_queue->whendone,req_queue->got_response);
                }

                req_queue_prev = req_queue;
                req_queue = req_queue->next;
            }
            ret = RFC3489_ACCEPT;
            break;
        default:
            if (stundebug)
                cw_verbose("Dunno what to do with STUN message %04x (%s)\n", ntohs(hdr->msgtype), rfc3489_msg_type_to_str(ntohs(hdr->msgtype)));
            break;
        }
    }
    return ret;
}

/* ************************************************************************* */

int stun_do_debug(int fd, int argc, char *argv[])
{
    if (argc != 2)
        return RESULT_SHOWUSAGE;
    stundebug = 1;
    cw_cli(fd, "STUN Debugging Enabled\n");
    return RESULT_SUCCESS;
}
   
int stun_no_debug(int fd, int argc, char *argv[])
{
    if (argc != 3)
        return RESULT_SHOWUSAGE;
    stundebug = 0;
    cw_cli(fd, "STUN Debugging Disabled\n");
    return RESULT_SUCCESS;
}

/* ************************************************************************* */

static char stun_debug_usage[] =
    "Usage: stun debug\n"
    "       Enable STUN (Simple Traversal of UDP through NATs) debugging\n";

static char stun_no_debug_usage[] =
    "Usage: stun no debug\n"
    "       Disable STUN debugging\n";

static struct cw_clicmd  cli_stun_debug =
{
	.cmda = { "stun", "debug", NULL },
	.handler = stun_do_debug,
	.summary = "Enable STUN debugging",
	.usage = stun_debug_usage,
};

static struct cw_clicmd  cli_stun_no_debug =
{
	.cmda = { "stun", "no", "debug", NULL },
	.handler = stun_no_debug,
	.summary = "Disable STUN debugging",
	.usage = stun_no_debug_usage,
};

int cw_stun_init(void)
{
    stundebug = 0;
    rfc3489_active = 0;
    stun_req_queue = NULL;
    cw_cli_register(&cli_stun_debug);
    cw_cli_register(&cli_stun_no_debug);
    return 0;
}
