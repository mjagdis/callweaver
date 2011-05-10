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
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/sched.h"
#include "callweaver/sockaddr.h"
#include "callweaver/udp.h"
#include "callweaver/rtp.h"
#include "callweaver/lock.h"
#include "callweaver/stun.h"
#include "callweaver/logger.h"
#include "callweaver/cli.h"
#include "callweaver/utils.h"
#include "callweaver/options.h"
#include "callweaver/udpfromto.h"


#define STUN_MSG_TYPE_BINDING_RESPONSE	0x0101
#define STUN_MSG_TYPE_BINDING_REQUEST	0x0001

#define STUN_ATTR_MAPPED_ADDRESS	0x0001
#define STUN_ATTR_USERNAME		0x0006
#define STUN_ATTR_PASSWORD		0x0007


struct stun_msg_addr {
	uint8_t unused;
	uint8_t family;
	uint16_t port;
	uint32_t addr;
} __attribute__((packed));

struct stun_msg_attr {
	uint16_t type;
	uint16_t len;
	uint8_t value[0];
} __attribute__((packed));

struct stun_msg_header {
	uint16_t type;
	uint16_t len;
	uint32_t transid[4];
	struct stun_msg_attr attrs[0];
} __attribute__((packed));


struct cw_stun_bindrequest {
	struct cw_stun_bindrequest *next;

	int s;
	struct sockaddr_in *stunaddr;
	struct cw_sockaddr_net from;
	struct cw_sockaddr_net to;

	/* This MUST be last. Its size varies */
	struct stun_msg_header msg;
};


static struct cw_stun_bindrequest *reqs;

static pthread_mutex_t reqs_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct sched_context *sched;

static int stundebug = 0;


static void append_attr_string(struct stun_msg_attr **attr, int *left, uint16_t type, const char *s, int slen)
{
	int size = sizeof(**attr) + slen;

	if (*left > size) {
		(*attr)->type = htons(type);
		(*attr)->len = htons(slen);
		memcpy(&(*attr)->value, s, slen);
		*attr = (struct stun_msg_attr *)&(*attr)->value[slen];
		*left -= size;
	}
}

static void append_attr_address(struct stun_msg_attr **attr, int *left, uint16_t type, const struct sockaddr_in *sin)
{
	struct stun_msg_addr *addr = (struct stun_msg_addr *)&(*attr)->value;
	int size = sizeof(**attr) + sizeof(*addr);

	if (*left > size) {
		(*attr)->type = htons(type);
		(*attr)->len = htons(sizeof(*addr));
		addr->family = htons(1); /* AF_INET */
		addr->port = sin->sin_port;
		addr->addr = sin->sin_addr.s_addr;
		*attr = (struct stun_msg_attr *)&(*attr)->value[sizeof(*addr)];
		*left -= size;
	}
}


static void stun_addr_to_sockaddr(struct sockaddr_in *sin, const struct stun_msg_addr *addr)
{
	sin->sin_family = htons(AF_INET);
	sin->sin_port = addr->port;
	sin->sin_addr.s_addr = addr->addr;
}


static struct cw_stun_bindrequest *stun_remove_request(const struct stun_msg_header *msg)
{
	struct cw_stun_bindrequest **req_p, *req;

	req = NULL;

	pthread_mutex_lock(&reqs_mutex);

	for (req_p = &reqs; *req_p; req_p = &(*req_p)->next) {
		if (!memcmp((*req_p)->msg.transid, msg->transid, sizeof(msg->transid))) {
			req = (*req_p);
			*req_p = (*req_p)->next;
			break;
		}
	}

	pthread_mutex_unlock(&reqs_mutex);

	return req;
}


int cw_stun_bindrequest(int s, const struct sockaddr *from, socklen_t fromlen, const struct sockaddr *to, socklen_t tolen, struct sockaddr_in *stunaddr)
{
	struct cw_stun_bindrequest *req;
	int i, ret = -1;

	if ((req = malloc(sizeof(*req) + 0 * sizeof(req->msg.attrs[0])))) {
		struct stun_msg_header *msg = &req->msg;

		req->stunaddr = stunaddr;

		msg->type = htons(STUN_MSG_TYPE_BINDING_REQUEST);
		msg->len = htons(0);

		for (i = 0; i < 4; i++)
			msg->transid[i] = cw_random();

		pthread_mutex_lock(&reqs_mutex);
		req->next = reqs;
		reqs = req;
		pthread_mutex_unlock(&reqs_mutex);

		if (cw_sendfromto(s, msg, sizeof(*msg), 0, from, sizeof(*from), to, sizeof(*to)) == sizeof(*msg)) {
			req->s = s;
			memcpy(&req->from, from, fromlen);
			memcpy(&req->to, to, tolen);

			ret = 0;
		} else {
			stun_remove_request(msg);
			free(req);
		}
	}

	return ret;
}


int cw_stun_handle_packet(int s, const struct sockaddr_in *src, const unsigned char *data, size_t len, struct sockaddr_in *sin)
{
	struct cw_stun_bindrequest *req = NULL;
	const struct stun_msg_header *msg = (struct stun_msg_header *)data;
	const struct stun_msg_attr *attr;
	const char *attr_username = NULL;
	size_t attrlen, attrlen_username = sizeof("<none>") - 1;
	int msgtype = 0;
	int respleft;

	if (len < sizeof(*msg))
		goto not_enough_data;

	respleft = ntohs(msg->len);

	if (respleft > len - sizeof(*msg))
		goto not_enough_data;

	msgtype = ntohs(msg->type);

	if (msgtype == STUN_MSG_TYPE_BINDING_RESPONSE)
		req = stun_remove_request(msg);

	len = respleft;
	attr = &msg->attrs[0];

	while (len) {
		if (len < sizeof(*attr)) {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Runt Attribute (got %zu, expecting %zu)\n", len, sizeof(*attr));
			break;
		}

		attrlen = ntohs(attr->len);

		if (attrlen > len) {
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Inconsistent Attribute (length %zu exceeds remaining msg len %zu)\n", attrlen, len);
			break;
		}

		switch (ntohs(attr->type)) {
			case STUN_ATTR_USERNAME:
				attr_username = (const char *)&attr->value[0];
				attrlen_username = attrlen;
				break;
			case STUN_ATTR_MAPPED_ADDRESS:
				if (msgtype == STUN_MSG_TYPE_BINDING_RESPONSE) {
					if (req && req->stunaddr)
						stun_addr_to_sockaddr(req->stunaddr, (struct stun_msg_addr *)(&attr->value[0]));
					if (sin)
						stun_addr_to_sockaddr(sin, (struct stun_msg_addr *)(&attr->value[0]));
				}
				break;
			default:
				break;
		}

		data += sizeof(*attr) + attrlen;
		len -= sizeof(*attr) + attrlen;
	}

	if (req)
		free(req);

	if (msgtype == STUN_MSG_TYPE_BINDING_REQUEST) {
		unsigned char respdata[1024];
		struct stun_msg_header *resp = (struct stun_msg_header *)respdata;
		struct stun_msg_attr *rattr;

		if (stundebug)
			cw_verbose("STUN Bind Request, username: %*.*s\n", (int)attrlen_username, (int)attrlen_username, (attr_username ? (const char *)attr_username : "<none>"));

		resp->type = htons(STUN_MSG_TYPE_BINDING_RESPONSE);
		memcpy(&resp->transid, msg->transid, sizeof(msg->transid));

		respleft = sizeof(respdata) - sizeof(*resp);

		rattr = &resp->attrs[0];

		if (attr_username)
			append_attr_string(&rattr, &respleft, STUN_ATTR_USERNAME, (const char *)attr_username, attrlen_username);
		append_attr_address(&rattr, &respleft, STUN_ATTR_MAPPED_ADDRESS, src);

		respleft = sizeof(respdata) - sizeof(*resp) - respleft;
		resp->len = htons(respleft);

		sendto(s, respdata, sizeof(*resp) + respleft, 0, (const struct sockaddr *)src, sizeof(*src));
	}

not_enough_data:
	return msgtype;
}


static int stun_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	stundebug = 1;
	cw_dynstr_printf(ds_p, "STUN Debugging Enabled\n");
	return RESULT_SUCCESS;
}
   

static int stun_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	stundebug = 0;
	cw_dynstr_printf(ds_p, "STUN Debugging Disabled\n");
	return RESULT_SUCCESS;
}


static const char stun_debug_usage[] =
	"Usage: stun debug\n"
	"       Enable STUN (Simple Traversal of UDP through NATs) debugging\n";

static const char stun_no_debug_usage[] =
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
	sched = sched_context_create(1);

	cw_cli_register(&cli_stun_debug);
	cw_cli_register(&cli_stun_no_debug);
	return 0;
}
