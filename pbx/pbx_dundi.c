/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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

/*! \file
 *
 * \brief Distributed Universal Number Discovery (DUNDi)
 *
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(SOLARIS) || defined(__Darwin__)
#include <sys/types.h>
#include <netinet/in_systm.h>
#endif
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__Darwin__)
#include <net/if_dl.h>
#include <ifaddrs.h>
#endif
#include <zlib.h>

#include <openssl/aes.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/chanvars.h"
#include "callweaver/config.h"
#include "callweaver/options.h"
#include "callweaver/switch.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/frame.h"
#include "callweaver/file.h"
#include "callweaver/cli.h"
#include "callweaver/lock.h"
#include "callweaver/sched.h"
#include "callweaver/io.h"
#include "callweaver/utils.h"
#include "callweaver/crypto.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/acl.h"
#include "callweaver/keywords.h"

#include "include/dundi.h"

#include "dundi-parser.h"

#define MAX_RESULTS	64

#define MAX_PACKET_SIZE 8192


static const char tdesc[] = "Distributed Universal Number Discovery (DUNDi)";

static void *dundi_func;
static const char dundifunc_name[] = "DUNDILOOKUP";
static const char dundifunc_synopsis[] = "Do a DUNDi lookup of a phone number.";
static const char dundifunc_syntax[] = "DUNDILOOKUP(number[,context[,options]])";
static const char dundifunc_desc[] =
	"This will do a DUNDi lookup of the given phone number.\n"
	"If no context is given, the default will be e164. The result of\n"
	"this function will the Technology/Resource found in the DUNDi\n"
	"lookup. If no results were found, the result will be blank.\n"
	"If the 'b' option is specified, the internal DUNDi cache will\n"
	"be bypassed.\n";


#define DUNDI_MODEL_INBOUND		(1 << 0)
#define DUNDI_MODEL_OUTBOUND	(1 << 1)
#define DUNDI_MODEL_SYMMETRIC	(DUNDI_MODEL_INBOUND | DUNDI_MODEL_OUTBOUND)

/* Keep times of last 10 lookups */
#define DUNDI_TIMING_HISTORY	10

#define FLAG_ISREG		(1 << 0)		/* Transaction is register request */
#define FLAG_DEAD		(1 << 1)		/* Transaction is dead */
#define FLAG_FINAL		(1 << 2)		/* Transaction has final message sent */
#define FLAG_ISQUAL		(1 << 3)		/* Transaction is a qualification */
#define FLAG_ENCRYPT	(1 << 4)		/* Transaction is encrypted wiht ECX/DCX */
#define FLAG_SENDFULLKEY	(1 << 5)		/* Send full key on transaction */
#define FLAG_STOREHIST	(1 << 6)		/* Record historic performance */

#define DUNDI_FLAG_INTERNAL_NOPARTIAL (1 << 17)

#if 0
#define DUNDI_SECRET_TIME 15	/* Testing only */
#else
#define DUNDI_SECRET_TIME DUNDI_DEFAULT_CACHE_TIME
#endif

#define KEY_OUT			0
#define KEY_IN			1

static cw_io_context_t io;
static struct sched_context *sched;
static int netsocket = -1;
static struct cw_io_rec netsocket_io_id;
static pthread_t netthreadid = CW_PTHREADT_NULL;
static pthread_t precachethreadid = CW_PTHREADT_NULL;
static int tos = 0;
static int dundidebug = 0;
static int dundi_ttl = DUNDI_DEFAULT_TTL;
static int dundi_key_ttl = DUNDI_DEFAULT_KEY_EXPIRE;
static int dundi_cache_time = DUNDI_DEFAULT_CACHE_TIME;
static int global_autokilltimeout = 0;
static dundi_eid global_eid;
static int default_expiration = 60;
static int global_storehistory = 0;
static char dept[80];
static char org[80];
static char locality[80];
static char stateprov[80];
static char country[80];
static char email[80];
static char phone[80];
static char secretpath[80];
static char cursecret[80];
static char ipaddr[80];
static time_t rotatetime;
static dundi_eid empty_eid = { { 0, 0, 0, 0, 0, 0 } };
struct permission {
	struct permission *next;
	int allow;
	char name[0];
};

struct dundi_packet {
	struct dundi_hdr *h;
	struct dundi_packet *next;
	int datalen;
	struct dundi_transaction *parent;
	struct sched_state retransid;
	int retrans;
	unsigned char data[0];
};

struct dundi_hint_metadata {
	unsigned short flags;
	char exten[CW_MAX_EXTENSION];
};

struct dundi_precache_queue {
	struct dundi_precache_queue *next;
	char *context;
	time_t expiration;
	char number[0];
};

struct dundi_request;

struct dundi_transaction {
	struct sockaddr_in addr;	/* Other end of transaction */
	struct timeval start;		/* When this transaction was created */
	dundi_eid eids[DUNDI_MAX_STACK + 1];
	int eidcount;				/* Number of eids in eids */
	dundi_eid us_eid;			/* Our EID, to them */
	dundi_eid them_eid;			/* Their EID, to us */
	AES_KEY	ecx;		/* AES 128 Encryption context */
	AES_KEY	dcx;		/* AES 128 Decryption context */
	unsigned int flags;				/* Has final packet been sent */
	int ttl;					/* Remaining TTL for queries on this one */
	int thread;					/* We have a calling thread */
	int retranstimer;			/* How long to wait before retransmissions */
	struct sched_state autokillid;				/* ID to kill connection if answer doesn't come back fast enough */
	int autokilltimeout;		/* Recommended timeout for autokill */
	unsigned short strans;		/* Our transaction identifier */
	unsigned short dtrans;		/* Their transaction identifer */
	unsigned char iseqno;		/* Next expected received seqno */
	unsigned char oiseqno;		/* Last received incoming seqno */
	unsigned char oseqno;		/* Next transmitted seqno */
	unsigned char aseqno;		/* Last acknowledge seqno */
	struct dundi_packet *packets;	/* Packets to be retransmitted */
	struct dundi_packet *lasttrans;	/* Last transmitted / ACK'd packet */
	struct dundi_transaction *next;	/* Next with respect to the parent */
	struct dundi_request *parent;	/* Parent request (if there is one) */
	struct dundi_transaction *allnext; /* Next with respect to all DUNDi transactions */
} *alltrans;

struct dundi_request {
	char dcontext[CW_MAX_EXTENSION];
	char number[CW_MAX_EXTENSION];
	dundi_eid query_eid;
	dundi_eid root_eid;
	struct dundi_result *dr;
	struct dundi_entity_info *dei;
	struct dundi_hint_metadata *hmd;
	int maxcount;
	int respcount;
	int expiration;
	int cbypass;
	int pfds[2];
	unsigned long crc32;							/* CRC-32 of all but root EID's in avoid list */
	struct dundi_transaction *trans;	/* Transactions */
	struct dundi_request *next;
} *requests;

static struct dundi_mapping {
	char dcontext[CW_MAX_EXTENSION];
	char lcontext[CW_MAX_EXTENSION];
	int weight;
	int options;
	int tech;
	int dead;
	char dest[CW_MAX_EXTENSION];
	struct dundi_mapping *next;
} *mappings = NULL;

static struct dundi_peer {
	dundi_eid eid;
	struct sockaddr_in addr;	/* Address of DUNDi peer */
	struct permission *permit;
	struct permission *include;
	struct permission *precachesend;
	struct permission *precachereceive;
	dundi_eid us_eid;
	char inkey[80];
	char outkey[80];
	int dead;
	struct sched_state registerid;
	struct sched_state qualifyid;
	int sentfullkey;
	int order;
	unsigned char txenckey[256]; /* Transmitted encrypted key + sig */
	unsigned char rxenckey[256]; /* Cache received encrypted key + sig */
	unsigned long us_keycrc32;	/* CRC-32 of our key */
	AES_KEY	us_ecx;		/* Cached AES 128 Encryption context */
	AES_KEY	us_dcx;		/* Cached AES 128 Decryption context */
	unsigned long them_keycrc32;/* CRC-32 of our key */
	AES_KEY	them_ecx;	/* Cached AES 128 Encryption context */
	AES_KEY	them_dcx;	/* Cached AES 128 Decryption context */
	time_t keyexpire;			/* When to expire/recreate key */
	struct sched_state registerexpire;
	int lookuptimes[DUNDI_TIMING_HISTORY];
	char *lookups[DUNDI_TIMING_HISTORY];
	int avgms;
	struct dundi_transaction *regtrans;	/* Registration transaction */
	struct dundi_transaction *qualtrans;	/* Qualify transaction */
	struct dundi_transaction *keypending;
	int model;					/* Pull model */
	int pcmodel;				/* Push/precache model */
	int dynamic;				/* Are we dynamic? */
	int lastms;					/* Last measured latency */
	int maxms;					/* Max permissible latency */
	struct timeval qualtx;		/* Time of transmit */
	struct dundi_peer *next;
} *peers;

static struct dundi_precache_queue *pcq;

CW_MUTEX_DEFINE_STATIC(peerlock);
CW_MUTEX_DEFINE_STATIC(pclock);

static int dundi_xmit(struct dundi_packet *pack);

static void dundi_debug_output(const char *data)
{
	if (dundidebug)
		cw_verbose("%s", data);
}

static void dundi_error_output(const char *data)
{
	cw_log(CW_LOG_WARNING, "%s", data);
}

static int has_permission(struct permission *ps, const char *cont)
{
	int res = 0;

	while(ps) {
		if (!strcasecmp(ps->name, "all") || !strcasecmp(ps->name, cont))
			res = ps->allow;
		ps = ps->next;
	}
	return res;
}

static const char *tech2str(int tech)
{
	switch(tech) {
	case DUNDI_PROTO_NONE:
		return "None";
	case DUNDI_PROTO_IAX:
		return "IAX2";
	case DUNDI_PROTO_SIP:
		return "SIP";
	case DUNDI_PROTO_H323:
		return "H323";
	default:
		return "Unknown";
	}
}

static int str2tech(const char *str)
{
	if (!strcasecmp(str, "IAX") || !strcasecmp(str, "IAX2"))
		return DUNDI_PROTO_IAX;
	else if (!strcasecmp(str, "SIP"))
		return DUNDI_PROTO_SIP;
	else if (!strcasecmp(str, "H323"))
		return DUNDI_PROTO_H323;
	else
		return -1;
}

static int dundi_lookup_internal(struct dundi_result *result, int maxret, struct cw_channel *chan, const char *dcontext, const char *number, int ttl, int blockempty, struct dundi_hint_metadata *md, int *expiration, int cybpass, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int direct[]);
static int dundi_precache_internal(const char *context, const char *number, int ttl, dundi_eid *avoids[]);
static struct dundi_transaction *create_transaction(struct dundi_peer *p);
static struct dundi_transaction *find_transaction(struct dundi_hdr *hdr, struct sockaddr_in *sain)
{
	/* Look for an exact match first */
	struct dundi_transaction *trans;
	trans = alltrans;
	while(trans) {
		if (!inaddrcmp(&trans->addr, sain) &&
		     ((trans->strans == (ntohs(hdr->dtrans) & 32767)) /* Matches our destination */ ||
			  ((trans->dtrans == (ntohs(hdr->strans) & 32767)) && (!hdr->dtrans))) /* We match their destination */) {
			  if (hdr->strans)
				  trans->dtrans = ntohs(hdr->strans) & 32767;
			  break;
		}
		trans = trans->allnext;
	}
	if (!trans) {
		switch(hdr->cmdresp & 0x7f) {
		case DUNDI_COMMAND_DPDISCOVER:
		case DUNDI_COMMAND_EIDQUERY:
		case DUNDI_COMMAND_PRECACHERQ:
		case DUNDI_COMMAND_REGREQ:
		case DUNDI_COMMAND_NULL:
		case DUNDI_COMMAND_ENCRYPT:
			if (hdr->strans) {	
				/* Create new transaction */
				trans = create_transaction(NULL);
				if (trans) {
					memcpy(&trans->addr, sain, sizeof(trans->addr));
					trans->dtrans = ntohs(hdr->strans) & 32767;
				} else
					cw_log(CW_LOG_WARNING, "Out of memory\n");
			}
			break;
		default:
			break;
		}
	}
	return trans;
}

static int dundi_send(struct dundi_transaction *trans, int cmdresp, int flags, int final, struct dundi_ie_data *ied);

static int dundi_ack(struct dundi_transaction *trans, int final)
{
	return dundi_send(trans, DUNDI_COMMAND_ACK, 0, final, NULL);
}
static void dundi_reject(struct dundi_hdr *h, struct sockaddr_in *sain)
{
	struct {
		struct dundi_packet pack;
		struct dundi_hdr hdr;
	} tmp;
	struct dundi_transaction trans;
	/* Never respond to an INVALID with another INVALID */
	if (h->cmdresp == DUNDI_COMMAND_INVALID)
		return;
	memset(&tmp, 0, sizeof(tmp));
	memset(&trans, 0, sizeof(trans));
	memcpy(&trans.addr, sain, sizeof(trans.addr));
	tmp.hdr.strans = h->dtrans;
	tmp.hdr.dtrans = h->strans;
	tmp.hdr.iseqno = h->oseqno;
	tmp.hdr.oseqno = h->iseqno;
	tmp.hdr.cmdresp = DUNDI_COMMAND_INVALID;
	tmp.hdr.cmdflags = 0;
	tmp.pack.h = (struct dundi_hdr *)tmp.pack.data;
	tmp.pack.datalen = sizeof(struct dundi_hdr);
	tmp.pack.parent = &trans;
	dundi_xmit(&tmp.pack);
}

static void reset_global_eid(void)
{
#if defined(SIOCGIFHWADDR)
	char eid_str[20];
	struct ifreq ifr;
	int x, s;

	s = socket_cloexec(AF_INET, SOCK_STREAM, 0);
	if (s >= 0) {
		x = 0;
		for(x=0;x<10;x++) {
			memset(&ifr, 0, sizeof(ifr));
			snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "eth%d", x);
			if (!ioctl(s, SIOCGIFHWADDR, &ifr)) {
				memcpy(&global_eid, ((unsigned char *)&ifr.ifr_hwaddr) + 2, sizeof(global_eid));
				cw_log(CW_LOG_DEBUG, "Seeding global EID '%s' from '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid), ifr.ifr_name);
				close(s);
				return;
			}
		}
		close(s);
	}
#else
#if defined(ifa_broadaddr) && !defined(SOLARIS)
	char eid_str[20];
	struct ifaddrs *ifap;
	
	if (getifaddrs(&ifap) == 0) {
		struct ifaddrs *p;
		for (p = ifap; p; p = p->ifa_next) {
			if (p->ifa_addr->sa_family == AF_LINK) {
				struct sockaddr_dl* sdp = (struct sockaddr_dl*) p->ifa_addr;
				memcpy(
					&(global_eid.eid),
					sdp->sdl_data + sdp->sdl_nlen, 6);
				cw_log(CW_LOG_DEBUG, "Seeding global EID '%s' from '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid), ifap->ifa_name);
				freeifaddrs(ifap);
				return;
			}
		}
		freeifaddrs(ifap);
	}
#endif
#endif
	cw_log(CW_LOG_NOTICE, "No ethernet interface found for seeding global EID  You will have to set it manually.\n");
}

static int get_trans_id(void)
{
	struct dundi_transaction *t;
	int stid = (cw_random() % 32766) + 1;
	int tid = stid;
	do {
		t = alltrans;
		while(t) {
			if (t->strans == tid) 
				break;
			t = t->allnext;
		}
		if (!t)
			return tid;
		tid = (tid % 32766) + 1;
	} while (tid != stid);
	return 0;
}

static int reset_transaction(struct dundi_transaction *trans)
{
	int tid;
	tid = get_trans_id();
	if (tid < 1)
		return -1;
	trans->strans = tid;
	trans->dtrans = 0;
	trans->iseqno = 0;
	trans->oiseqno = 0;
	trans->oseqno = 0;
	trans->aseqno = 0;
	cw_clear_flag(trans, FLAG_FINAL);	
	return 0;
}

static struct dundi_peer *find_peer(dundi_eid *eid)
{
	struct dundi_peer *cur;
	if (!eid)
		eid = &empty_eid;
	cur = peers;
	while(cur) {
		if (!dundi_eid_cmp(&cur->eid,eid))
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static void build_iv(unsigned char *iv)
{
	/* XXX Would be nice to be more random XXX */
	unsigned int *fluffy;
	int x;
	fluffy = (unsigned int *)(iv);
	for (x=0;x<4;x++)
		fluffy[x] = cw_random();
}

struct dundi_query_state {
	dundi_eid *eids[DUNDI_MAX_STACK + 1]; 
	int directs[DUNDI_MAX_STACK + 1]; 
	dundi_eid reqeid;
	char called_context[CW_MAX_EXTENSION];
	char called_number[CW_MAX_EXTENSION];
	struct dundi_mapping *maps;
	int nummaps;
	int nocache;
	struct dundi_transaction *trans;
	void *chal;
	int challen;
	int ttl;
	char fluffy[0];
};

static int dundi_lookup_local(struct dundi_result *dr, struct dundi_mapping *map, char *called_number, dundi_eid *us_eid, int anscnt, struct dundi_hint_metadata *hmd)
{
	struct cw_flags flags = {0};
	int x;
	if (!cw_strlen_zero(map->lcontext)) {
		if (cw_exists_extension(NULL, map->lcontext, called_number, 1, NULL))
			cw_set_flag(&flags, DUNDI_FLAG_EXISTS);
		if (cw_canmatch_extension(NULL, map->lcontext, called_number, 1, NULL))
			cw_set_flag(&flags, DUNDI_FLAG_CANMATCH);
		if (cw_matchmore_extension(NULL, map->lcontext, called_number, 1, NULL))
			cw_set_flag(&flags, DUNDI_FLAG_MATCHMORE);
		if (cw_ignore_pattern(map->lcontext, called_number))
			cw_set_flag(&flags, DUNDI_FLAG_IGNOREPAT);

		/* Clearly we can't say 'don't ask' anymore if we found anything... */
		if (cw_test_flag(&flags, CW_FLAGS_ALL)) 
			cw_clear_flag_nonstd(hmd, DUNDI_HINT_DONT_ASK);

		if (map->options & DUNDI_FLAG_INTERNAL_NOPARTIAL) {
			/* Skip partial answers */
			cw_clear_flag(&flags, DUNDI_FLAG_MATCHMORE|DUNDI_FLAG_CANMATCH);
		}
		if (cw_test_flag(&flags, CW_FLAGS_ALL)) {
			cw_set_flag(&flags, map->options & 0xffff);
			cw_copy_flags(dr + anscnt, &flags, CW_FLAGS_ALL);
			dr[anscnt].techint = map->tech;
			dr[anscnt].weight = map->weight;
			dr[anscnt].expiration = dundi_cache_time;
			cw_copy_string(dr[anscnt].tech, tech2str(map->tech), sizeof(dr[anscnt].tech));
			dr[anscnt].eid = *us_eid;
			dundi_eid_to_str(dr[anscnt].eid_str, sizeof(dr[anscnt].eid_str), &dr[anscnt].eid);
			cw_dynstr_init(&dr[anscnt].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);
			if (cw_test_flag(&flags, DUNDI_FLAG_EXISTS)) {
				struct cw_registry reg;
				cw_var_registry_init(&reg, 8);
				cw_var_assign(&reg, "NUMBER", called_number);
				cw_var_assign(&reg, "EID", dr[anscnt].eid_str);
				cw_var_assign(&reg, "SECRET", cursecret);
				cw_var_assign(&reg, "IPADDR", ipaddr);
				pbx_substitute_variables(NULL, &reg, map->dest, &dr[anscnt].dest);
				if (!dr[anscnt].dest.error && !cw_split_args(NULL, dr[anscnt].dest.data, "", '\0', NULL))
					anscnt++;
				cw_registry_destroy(&reg);
			} else
				anscnt++;
		} else {
			/* No answers...  Find the fewest number of digits from the
			   number for which we have no answer. */
			char tmp[CW_MAX_EXTENSION];
			for (x=0;x<CW_MAX_EXTENSION;x++) {
				tmp[x] = called_number[x];
				if (!tmp[x])
					break;
				if (!cw_canmatch_extension(NULL, map->lcontext, tmp, 1, NULL)) {
					/* Oops found something we can't match.  If this is longer
					   than the running hint, we have to consider it */
					if (strlen(tmp) > strlen(hmd->exten)) {
						cw_copy_string(hmd->exten, tmp, sizeof(hmd->exten));
					}
					break;
				}
			}
		}
	}
	return anscnt;
}

static void destroy_trans(struct dundi_transaction *trans, int fromtimeout);

static void *dundi_lookup_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_result dr[MAX_RESULTS];
	struct dundi_ie_data ied;
	struct dundi_hint_metadata hmd;
	char eid_str[20];
	int res, x;
	int ouranswers=0;
	int max = 999999;
	int expiration = dundi_cache_time;

	cw_log(CW_LOG_DEBUG, "Whee, looking up '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));
	memset(&dr, 0, sizeof(dr));
	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_init(&dr[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);
	memset(&hmd, 0, sizeof(hmd));
	/* Assume 'don't ask for anything' and 'unaffected', no TTL expired */
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;
	for (x=0;x<st->nummaps;x++)
		ouranswers = dundi_lookup_local(dr, st->maps + x, st->called_number, &st->trans->us_eid, ouranswers, &hmd);
	if (ouranswers < 0)
		ouranswers = 0;
	for (x=0;x<ouranswers;x++) {
		if (dr[x].weight < max)
			max = dr[x].weight;
	}
		
	if (max) {
		/* If we do not have a canonical result, keep looking */
		res = dundi_lookup_internal(dr + ouranswers, MAX_RESULTS - ouranswers, NULL, st->called_context, st->called_number, st->ttl, 1, &hmd, &expiration, st->nocache, 0, NULL, st->eids, st->directs);
		if (res > 0) {
			/* Append answer in result */
			ouranswers += res;
		} else {
			if ((res < -1) && (!ouranswers))
				dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_DUPLICATE, "Duplicate Request Pending");
		}
	}
	cw_mutex_lock(&peerlock);
	/* Truncate if "don't ask" isn't present */
	if (!cw_test_flag_nonstd(&hmd, DUNDI_HINT_DONT_ASK))
		hmd.exten[0] = '\0';
	if (cw_test_flag(st->trans, FLAG_DEAD)) {
		cw_log(CW_LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		for (x=0;x<ouranswers;x++) {
			/* Add answers */
			if (dr[x].expiration && (expiration > dr[x].expiration))
				expiration = dr[x].expiration;
			dundi_ie_append_answer(&ied, DUNDI_IE_ANSWER, &dr[x].eid, dr[x].techint, dr[x].flags, dr[x].weight, dr[x].dest.data);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, expiration);
		dundi_send(st->trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
		st->trans->thread = 0;
	}
	cw_mutex_unlock(&peerlock);
	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_free(&dr[x].dest);
	free(st);
	return NULL;	
}

static void *dundi_precache_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_ie_data ied;
	char eid_str[20];

	cw_log(CW_LOG_DEBUG, "Whee, precaching '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));

	/* Now produce precache */
	dundi_precache_internal(st->called_context, st->called_number, st->ttl, st->eids);

	cw_mutex_lock(&peerlock);
	if (cw_test_flag(st->trans, FLAG_DEAD)) {
		cw_log(CW_LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		dundi_send(st->trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
		st->trans->thread = 0;
	}
	cw_mutex_unlock(&peerlock);
	free(st);
	return NULL;	
}

static int dundi_query_eid_internal(struct dundi_entity_info *dei, const char *dcontext, dundi_eid *eid, struct dundi_hint_metadata *hmd, int ttl, int blockempty, dundi_eid *avoid[]);

static void *dundi_query_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_entity_info dei;
	struct dundi_ie_data ied;
	struct dundi_hint_metadata hmd;
	char eid_str[20];
	int res;
	cw_log(CW_LOG_DEBUG, "Whee, looking up '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));
	memset(&dei, 0, sizeof(dei));
	memset(&hmd, 0, sizeof(hmd));
	if (!dundi_eid_cmp(&st->trans->us_eid, &st->reqeid)) {
		/* Ooh, it's us! */
		cw_log(CW_LOG_DEBUG, "Neat, someone look for us!\n");
		cw_copy_string(dei.orgunit, dept, sizeof(dei.orgunit));
		cw_copy_string(dei.org, org, sizeof(dei.org));
		cw_copy_string(dei.locality, locality, sizeof(dei.locality));
		cw_copy_string(dei.stateprov, stateprov, sizeof(dei.stateprov));
		cw_copy_string(dei.country, country, sizeof(dei.country));
		cw_copy_string(dei.email, email, sizeof(dei.email));
		cw_copy_string(dei.phone, phone, sizeof(dei.phone));
		res = 1;
	} else {
		/* If we do not have a canonical result, keep looking */
		res = dundi_query_eid_internal(&dei, st->called_context, &st->reqeid, &hmd, st->ttl, 1, st->eids);
	}
	cw_mutex_lock(&peerlock);
	if (cw_test_flag(st->trans, FLAG_DEAD)) {
		cw_log(CW_LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		if (res) {
			dundi_ie_append_str(&ied, DUNDI_IE_DEPARTMENT, dei.orgunit);
			dundi_ie_append_str(&ied, DUNDI_IE_ORGANIZATION, dei.org);
			dundi_ie_append_str(&ied, DUNDI_IE_LOCALITY, dei.locality);
			dundi_ie_append_str(&ied, DUNDI_IE_STATE_PROV, dei.stateprov);
			dundi_ie_append_str(&ied, DUNDI_IE_COUNTRY, dei.country);
			dundi_ie_append_str(&ied, DUNDI_IE_EMAIL, dei.email);
			dundi_ie_append_str(&ied, DUNDI_IE_PHONE, dei.phone);
			if (!cw_strlen_zero(dei.ipaddr))
				dundi_ie_append_str(&ied, DUNDI_IE_IPADDR, dei.ipaddr);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_send(st->trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
		st->trans->thread = 0;
	}
	cw_mutex_unlock(&peerlock);
	free(st);
	return NULL;	
}

static int dundi_answer_entity(struct dundi_transaction *trans, struct dundi_ies *ies)
{
	struct dundi_query_state *st;
	int totallen;
	int x;
	int skipfirst=0;
	struct dundi_ie_data ied;
	char eid_str[20];
	char *s;
	pthread_t lookupthread;

	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}
	totallen = sizeof(struct dundi_query_state);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		cw_copy_string(st->called_context, ies->called_context, sizeof(st->called_context));
		memcpy(&st->reqeid, ies->reqeid, sizeof(st->reqeid));
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			s += sizeof(dundi_eid);
		}
		cw_log(CW_LOG_DEBUG, "Answering EID query for '%s@%s'!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), ies->reqeid), ies->called_context);
		trans->thread = 1;
		if (cw_pthread_create(&lookupthread, &global_attr_detached, dundi_query_thread, st)) {
			trans->thread = 0;
			cw_log(CW_LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
			return -1;
		}
	} else {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int cache_save_hint(dundi_eid *eidpeer, struct dundi_request *req, struct dundi_hint *hint, int expiration)
{
	int unaffected;
	char key1[256];
	char key2[256];
	char eidpeer_str[20];
	char eidroot_str[20];
	char data[80];
	time_t timeout;

	if (expiration < 0)
		expiration = dundi_cache_time;

	/* Only cache hint if "don't ask" is there... */
	if (!cw_test_flag_nonstd(hint, htons(DUNDI_HINT_DONT_ASK)))	
		return 0;

	unaffected = cw_test_flag_nonstd(hint, htons(DUNDI_HINT_UNAFFECTED));

	dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), eidpeer);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	snprintf(key1, sizeof(key1), "hint/%s/%s/%s/e%08lx", eidpeer_str, (char *)hint->data, req->dcontext, unaffected ? 0 : req->crc32);
	snprintf(key2, sizeof(key2), "hint/%s/%s/%s/r%s", eidpeer_str, (char *)hint->data, req->dcontext, eidroot_str);

	time(&timeout);
	timeout += expiration;
	snprintf(data, sizeof(data), "%ld,", (long)(timeout));
	
	cw_db_put("dundi/cache", key1, data);
	cw_log(CW_LOG_DEBUG, "Caching hint at '%s'\n", key1);
	cw_db_put("dundi/cache", key2, data);
	cw_log(CW_LOG_DEBUG, "Caching hint at '%s'\n", key2);
	return 0;
}

static int cache_save(dundi_eid *eidpeer, struct dundi_request *req, int start, int unaffected, int expiration, int push)
{
	int x;
	char key1[256];
	char key2[256];
	char data[1024];
	char eidpeer_str[20];
	char eidroot_str[20];
	time_t timeout;

	if (expiration < 1)	
		expiration = dundi_cache_time;

	/* Keep pushes a little longer, cut pulls a little short */
	if (push)
		expiration += 10;
	else
		expiration -= 10;
	if (expiration < 1)
		expiration = 1;
	dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), eidpeer);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	snprintf(key1, sizeof(key1), "%s/%s/%s/e%08lx", eidpeer_str, req->number, req->dcontext, unaffected ? 0 : req->crc32);
	snprintf(key2, sizeof(key2), "%s/%s/%s/r%s", eidpeer_str, req->number, req->dcontext, eidroot_str);
	/* Build request string */
	time(&timeout);
	timeout += expiration;
	snprintf(data, sizeof(data), "%ld,", (long)(timeout));
	for (x=start;x<req->respcount;x++) {
		/* Skip anything with an illegal comma in it */
		if (strchr(req->dr[x].dest.data, ','))
			continue;
		snprintf(data + strlen(data), sizeof(data) - strlen(data), "%u/%d/%d/%s/%s,",
			req->dr[x].flags, req->dr[x].weight, req->dr[x].techint, req->dr[x].dest.data,
			dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), &req->dr[x].eid));
	}
	cw_db_put("dundi/cache", key1, data);
	cw_db_put("dundi/cache", key2, data);
	return 0;
}

static int dundi_prop_precache(struct dundi_transaction *trans, struct dundi_ies *ies, const char *ccontext)
{
	struct dundi_query_state *st;
	int totallen;
	int x,z;
	struct dundi_ie_data ied;
	char *s;
	struct dundi_result dr2[MAX_RESULTS];
	struct dundi_request dr;
	struct dundi_hint_metadata hmd;
	struct dundi_mapping *cur;
	int mapcount;
	int skipfirst = 0;
	
	pthread_t lookupthread;

	memset(&dr2, 0, sizeof(dr2));
	for (x = 0; x < arraysize(dr2); x++)
		cw_dynstr_init(&dr2[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);
	memset(&dr, 0, sizeof(dr));
	memset(&hmd, 0, sizeof(hmd));

	/* Forge request structure to hold answers for cache */
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;
	dr.dr = dr2;
	dr.maxcount = MAX_RESULTS;
	dr.expiration = dundi_cache_time;
	dr.hmd = &hmd;
	dr.pfds[0] = dr.pfds[1] = -1;
	trans->parent = &dr;
	cw_copy_string(dr.dcontext, ies->called_context ? ies->called_context : "e164", sizeof(dr.dcontext));
	cw_copy_string(dr.number, ies->called_number, sizeof(dr.number));
	
	for (x=0;x<ies->anscount;x++) {
		if (trans->parent->respcount < trans->parent->maxcount) {
			/* Make sure it's not already there */
			for (z=0;z<trans->parent->respcount;z++) {
				if ((trans->parent->dr[z].techint == ies->answers[x]->protocol) &&
				    !strcmp(trans->parent->dr[z].dest.data, (char *)ies->answers[x]->data))
						break;
			}
			if (z == trans->parent->respcount) {
				/* Copy into parent responses */
				trans->parent->dr[trans->parent->respcount].flags = ntohs(ies->answers[x]->flags);
				trans->parent->dr[trans->parent->respcount].techint = ies->answers[x]->protocol;
				trans->parent->dr[trans->parent->respcount].weight = ntohs(ies->answers[x]->weight);
				trans->parent->dr[trans->parent->respcount].eid = ies->answers[x]->eid;
				if (ies->expiration > 0)
					trans->parent->dr[trans->parent->respcount].expiration = ies->expiration;
				else
					trans->parent->dr[trans->parent->respcount].expiration = dundi_cache_time;
				dundi_eid_to_str(trans->parent->dr[trans->parent->respcount].eid_str, 
					sizeof(trans->parent->dr[trans->parent->respcount].eid_str),
					&ies->answers[x]->eid);
				cw_dynstr_printf(&trans->parent->dr[trans->parent->respcount].dest, "%s", (char *)ies->answers[x]->data);
				cw_copy_string(trans->parent->dr[trans->parent->respcount].tech, tech2str(ies->answers[x]->protocol),
					sizeof(trans->parent->dr[trans->parent->respcount].tech));
				trans->parent->respcount++;
				cw_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);	
			} else if (trans->parent->dr[z].weight > ies->answers[x]->weight) {
				/* Update weight if appropriate */
				trans->parent->dr[z].weight = ies->answers[x]->weight;
			}
		} else
			cw_log(CW_LOG_NOTICE, "Dropping excessive answers in precache for %s@%s\n",
				trans->parent->number, trans->parent->dcontext);

	}

	for (x = 0; x < arraysize(dr2); x++)
		cw_dynstr_free(&dr2[x].dest);

	/* Save all the results (if any) we had.  Even if no results, still cache lookup. */
	cache_save(&trans->them_eid, trans->parent, 0, 0, ies->expiration, 1);
	if (ies->hint)
		cache_save_hint(&trans->them_eid, trans->parent, ies->hint, ies->expiration);

	totallen = sizeof(struct dundi_query_state);
	/* Count matching map entries */
	mapcount = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, ccontext))
			mapcount++;
		cur = cur->next;
	}
	
	/* If no maps, return -1 immediately */
	if (!mapcount)
		return -1;

	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}

	/* Prepare to run a query and then propagate that as necessary */
	totallen += mapcount * sizeof(struct dundi_mapping);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		cw_copy_string(st->called_context, ies->called_context, sizeof(st->called_context));
		cw_copy_string(st->called_number, ies->called_number, sizeof(st->called_number));
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		st->nocache = ies->cbypass;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			st->directs[x-skipfirst] = ies->eid_direct[x];
			s += sizeof(dundi_eid);
		}
		/* Append mappings */
		x = 0;
		st->maps = (struct dundi_mapping *)s;
		cur = mappings;
		while(cur) {
			if (!strcasecmp(cur->dcontext, ccontext)) {
				if (x < mapcount) {
					st->maps[x] = *cur;
					st->maps[x].next = NULL;
					x++;
				}
			}
			cur = cur->next;
		}
		st->nummaps = mapcount;
		cw_log(CW_LOG_DEBUG, "Forwarding precache for '%s@%s'!\n", ies->called_number, ies->called_context);
		trans->thread = 1;
		if (cw_pthread_create(&lookupthread, &global_attr_detached, dundi_precache_thread, st)) {
			trans->thread = 0;
			cw_log(CW_LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
			return -1;
		}
	} else {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int dundi_answer_query(struct dundi_transaction *trans, struct dundi_ies *ies, const char *ccontext)
{
	struct dundi_query_state *st;
	int totallen;
	int x;
	struct dundi_ie_data ied;
	char *s;
	struct dundi_mapping *cur;
	int mapcount;
	int skipfirst = 0;
	pthread_t lookupthread;

	totallen = sizeof(struct dundi_query_state);
	/* Count matching map entries */
	mapcount = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, ccontext))
			mapcount++;
		cur = cur->next;
	}
	/* If no maps, return -1 immediately */
	if (!mapcount)
		return -1;

	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}

	totallen += mapcount * sizeof(struct dundi_mapping);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		cw_copy_string(st->called_context, ies->called_context, sizeof(st->called_context));
		cw_copy_string(st->called_number, ies->called_number, sizeof(st->called_number));
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		st->nocache = ies->cbypass;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			st->directs[x-skipfirst] = ies->eid_direct[x];
			s += sizeof(dundi_eid);
		}
		/* Append mappings */
		x = 0;
		st->maps = (struct dundi_mapping *)s;
		cur = mappings;
		while(cur) {
			if (!strcasecmp(cur->dcontext, ccontext)) {
				if (x < mapcount) {
					st->maps[x] = *cur;
					st->maps[x].next = NULL;
					x++;
				}
			}
			cur = cur->next;
		}
		st->nummaps = mapcount;
		cw_log(CW_LOG_DEBUG, "Answering query for '%s@%s'!\n", ies->called_number, ies->called_context);
		trans->thread = 1;
		if (cw_pthread_create(&lookupthread, &global_attr_detached, dundi_lookup_thread, st)) {
			trans->thread = 0;
			cw_log(CW_LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
			return -1;
		}
	} else {
		cw_log(CW_LOG_WARNING, "Out of memory\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int cache_lookup_internal(time_t now, struct dundi_request *req, char *key, char *eid_str_full, int *lowexpiration)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	char *ptr, *term, *src;
	int tech;
	struct cw_flags flags;
	int weight;
	int length;
	int z;
	int expiration;
	char fs[256];
	time_t timeout;

	/* Build request string */
	if (!cw_db_get("dundi/cache", key, &ds)) {
		ptr = ds.data;
		if (sscanf(ptr, "%ld,%n", &timeout, &length) == 1) {
			expiration = timeout - now;
			if (expiration > 0) {
				cw_log(CW_LOG_DEBUG, "Found cache expiring in %d seconds!\n", (int)(timeout - now));
				ptr += length;
				while((sscanf(ptr, "%u/%d/%d/%n", &(flags.flags), &weight, &tech, &length) == 3)) {
					ptr += length;
					term = strchr(ptr, ',');
					if (term) {
						*term = '\0';
						src = strrchr(ptr, '/');
						if (src) {
							*src = '\0';
							src++;
						} else
							src = (char *)"";
						cw_log(CW_LOG_DEBUG, "Found cached answer '%s/%s' originally from '%s' with flags '%s' on behalf of '%s'\n", 
							tech2str(tech), ptr, src, dundi_flags2str(fs, sizeof(fs), flags.flags), eid_str_full);
						/* Make sure it's not already there */
						for (z=0;z<req->respcount;z++) {
							if ((req->dr[z].techint == tech) &&
							    !strcmp(req->dr[z].dest.data, ptr))
									break;
						}
						if (z == req->respcount) {
							/* Copy into parent responses */
							cw_copy_flags(&(req->dr[req->respcount]), &flags, CW_FLAGS_ALL);	
							req->dr[req->respcount].weight = weight;
							req->dr[req->respcount].techint = tech;
							req->dr[req->respcount].expiration = expiration;
							dundi_str_short_to_eid(&req->dr[req->respcount].eid, src);
							dundi_eid_to_str(req->dr[req->respcount].eid_str, 
								sizeof(req->dr[req->respcount].eid_str), &req->dr[req->respcount].eid);
							cw_dynstr_printf(&req->dr[req->respcount].dest, "%s", ptr);
							cw_copy_string(req->dr[req->respcount].tech, tech2str(tech),
								sizeof(req->dr[req->respcount].tech));
							req->respcount++;
							cw_clear_flag_nonstd(req->hmd, DUNDI_HINT_DONT_ASK);	
						} else if (req->dr[z].weight > weight)
							req->dr[z].weight = weight;
						ptr = term + 1;
					}
				}
				/* We found *something* cached */
				if (expiration < *lowexpiration)
					*lowexpiration = expiration;
				return 1;
			} else 
				cw_db_del("dundi/cache", key);
		} else 
			cw_db_del("dundi/cache", key);

		cw_dynstr_free(&ds);
	}
		
	return 0;
}

static int cache_lookup(struct dundi_request *req, dundi_eid *peer_eid, unsigned long csum_crc32, int *lowexpiration)
{
	char key[256];
	char eid_str[20];
	char eidroot_str[20];
	time_t now;
	int res=0;
	int res2=0;
	char eid_str_full[20];
	char tmp[256]="";
	int x;

	time(&now);
	dundi_eid_to_str_short(eid_str, sizeof(eid_str), peer_eid);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	dundi_eid_to_str(eid_str_full, sizeof(eid_str_full), peer_eid);
	snprintf(key, sizeof(key), "%s/%s/%s/e%08lx", eid_str, req->number, req->dcontext, csum_crc32);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	snprintf(key, sizeof(key), "%s/%s/%s/e%08lx", eid_str, req->number, req->dcontext, 0L);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	snprintf(key, sizeof(key), "%s/%s/%s/r%s", eid_str, req->number, req->dcontext, eidroot_str);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	x = 0;
	if (!req->respcount) {
		while(!res2) {
			/* Look and see if we have a hint that would preclude us from looking at this
			   peer for this number. */
			if (!(tmp[x] = req->number[x])) 
				break;
			x++;
			/* Check for hints */
			snprintf(key, sizeof(key), "hint/%s/%s/%s/e%08lx", eid_str, tmp, req->dcontext, csum_crc32);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			snprintf(key, sizeof(key), "hint/%s/%s/%s/e%08lx", eid_str, tmp, req->dcontext, 0L);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			snprintf(key, sizeof(key), "hint/%s/%s/%s/r%s", eid_str, tmp, req->dcontext, eidroot_str);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			if (res2) {
				if (strlen(tmp) > strlen(req->hmd->exten)) {
					/* Update meta data if appropriate */
					cw_copy_string(req->hmd->exten, tmp, sizeof(req->hmd->exten));
				}
			}
		}
		res |= res2;
	}

	return res;
}

static void qualify_peer(struct dundi_peer *peer, int schedonly);

static void apply_peer(struct dundi_transaction *trans, struct dundi_peer *p)
{
	if (!trans->addr.sin_addr.s_addr)
		memcpy(&trans->addr, &p->addr, sizeof(trans->addr));
	trans->us_eid = p->us_eid;
	trans->them_eid = p->eid;
	/* Enable encryption if appropriate */
	if (!cw_strlen_zero(p->inkey))
		cw_set_flag(trans, FLAG_ENCRYPT);	
	if (p->maxms) {
		trans->autokilltimeout = p->maxms;
		trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
		if (p->lastms > 1) {
			trans->retranstimer = p->lastms * 2;
			/* Keep it from being silly */
			if (trans->retranstimer < 150)
				trans->retranstimer = 150;
		}
		if (trans->retranstimer > DUNDI_DEFAULT_RETRANS_TIMER)
			trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
	} else
		trans->autokilltimeout = global_autokilltimeout;
}

static int do_register_expire(void *data)
{
	struct dundi_peer *peer = data;
	char eid_str[20];

	cw_mutex_lock(&peerlock);

	cw_log(CW_LOG_DEBUG, "Register expired for '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
	peer->lastms = 0;
	memset(&peer->addr, 0, sizeof(peer->addr));

	cw_mutex_lock(&peerlock);

	return 0;
}

static int update_key(struct dundi_peer *peer)
{
	unsigned char key[16];
	struct cw_key *ekey, *skey;
	char eid_str[20];
	int res;
	if (!peer->keyexpire || (peer->keyexpire < time(NULL))) {
		build_iv(key);
		AES_set_encrypt_key(key, 128, &peer->us_ecx);
		AES_set_decrypt_key(key, 128, &peer->us_dcx);
		ekey = cw_key_get(peer->inkey, CW_KEY_PUBLIC);
		if (!ekey) {
			cw_log(CW_LOG_NOTICE, "No such key '%s' for creating RSA encrypted shared key for '%s'!\n",
				peer->inkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			return -1;
		}
		skey = cw_key_get(peer->outkey, CW_KEY_PRIVATE);
		if (!skey) {
			cw_log(CW_LOG_NOTICE, "No such key '%s' for signing RSA encrypted shared key for '%s'!\n",
				peer->outkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			return -1;
		}
		if ((res = cw_encrypt_bin(peer->txenckey, key, sizeof(key), ekey)) != 128) {
			cw_log(CW_LOG_NOTICE, "Whoa, got a weird encrypt size (%d != %d)!\n", res, 128);
			return -1;
		}
		if ((res = cw_sign_bin(skey, (char *)peer->txenckey, 128, peer->txenckey + 128))) {
			cw_log(CW_LOG_NOTICE, "Failed to sign key (%d)!\n", res);
			return -1;
		}
		peer->us_keycrc32 = crc32(0L, peer->txenckey, 128);
		peer->sentfullkey = 0;
		/* Looks good */
		time(&peer->keyexpire);
		peer->keyexpire += dundi_key_ttl;
	}
	return 0;
}

static int encrypt_memcpy(unsigned char *dst, unsigned char *src, int len, unsigned char *iv, AES_KEY *ecx) 
{
	unsigned char curblock[16];
	int x;
	memcpy(curblock, iv, sizeof(curblock));
	while(len > 0) {
		for (x=0;x<16;x++)
			curblock[x] ^= src[x];
		AES_encrypt(curblock, dst, ecx);
		memcpy(curblock, dst, sizeof(curblock)); 
		dst += 16;
		src += 16;
		len -= 16;
	}
	return 0;
}
static int decrypt_memcpy(unsigned char *dst, unsigned char *src, int len, unsigned char *iv, AES_KEY *dcx) 
{
	unsigned char lastblock[16];
	int x;
	memcpy(lastblock, iv, sizeof(lastblock));
	while(len > 0) {
		AES_decrypt(src, dst, dcx);
		for (x=0;x<16;x++)
			dst[x] ^= lastblock[x];
		memcpy(lastblock, src, sizeof(lastblock));
		dst += 16;
		src += 16;
		len -= 16;
	}
	return 0;
}

static struct dundi_hdr *dundi_decrypt(struct dundi_transaction *trans, unsigned char *dst, int *dstlen, struct dundi_hdr *ohdr, struct dundi_encblock *src, int srclen)
{
	int space = *dstlen;
	unsigned long bytes;
	struct dundi_hdr *h;
	unsigned char *decrypt_space;
	decrypt_space = alloca(srclen);
	decrypt_memcpy(decrypt_space, src->encdata, srclen, src->iv, &trans->dcx);
	/* Setup header */
	h = (struct dundi_hdr *)dst;
	*h = *ohdr;
	bytes = space - 6;
	if (uncompress(dst + 6, &bytes, decrypt_space, srclen) != Z_OK) {
		cw_log(CW_LOG_DEBUG, "Ouch, uncompress failed :(\n");
		return NULL;
	}
	/* Update length */
	*dstlen = bytes + 6;
	/* Return new header */
	return h;
}

static int dundi_encrypt(struct dundi_transaction *trans, struct dundi_packet *pack)
{
	unsigned char *compress_space;
	int len;
	int res;
	unsigned long bytes;
	struct dundi_ie_data ied;
	struct dundi_peer *peer;
	unsigned char iv[16];

	len = pack->datalen + pack->datalen / 100 + 42;
	compress_space = alloca(len);

	/* We care about everthing save the first 6 bytes of header */
	bytes = len;
	res = compress(compress_space, &bytes, pack->data + 6, pack->datalen - 6);
	if (res != Z_OK) {
		cw_log(CW_LOG_DEBUG, "Ouch, compression failed!\n");
		return -1;
	}

	memset(&ied, 0, sizeof(ied));

	/* Say who we are */
	if (!pack->h->iseqno && !pack->h->oseqno) {
		/* Need the key in the first copy */
		if (!(peer = find_peer(&trans->them_eid))) 
			return -1;
		if (update_key(peer))
			return -1;
		if (!peer->sentfullkey)
			cw_set_flag(trans, FLAG_SENDFULLKEY);	
		/* Append key data */
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
		if (cw_test_flag(trans, FLAG_SENDFULLKEY)) {
			dundi_ie_append_raw(&ied, DUNDI_IE_SHAREDKEY, peer->txenckey, 128);
			dundi_ie_append_raw(&ied, DUNDI_IE_SIGNATURE, peer->txenckey + 128, 128);
		} else {
			dundi_ie_append_int(&ied, DUNDI_IE_KEYCRC32, peer->us_keycrc32);
		}
		/* Setup contexts */
		trans->ecx = peer->us_ecx;
		trans->dcx = peer->us_dcx;

		/* We've sent the full key */
		peer->sentfullkey = 1;
	}
	/* Build initialization vector */
	build_iv(iv);
	/* Add the field, rounded up to 16 bytes */
	dundi_ie_append_encdata(&ied, DUNDI_IE_ENCDATA, iv, NULL, ((bytes + 15) / 16) * 16);
	/* Copy the data */
	if ((ied.pos + bytes) >= sizeof(ied.buf)) {
		cw_log(CW_LOG_NOTICE, "Final packet too large!\n");
		return -1;
	}
	encrypt_memcpy(ied.buf + ied.pos, compress_space, bytes, iv, &trans->ecx);
	ied.pos += ((bytes + 15) / 16) * 16;
	/* Reconstruct header */
	pack->datalen = sizeof(struct dundi_hdr);
	pack->h->cmdresp = DUNDI_COMMAND_ENCRYPT;
	pack->h->cmdflags = 0;
	memcpy(pack->h->ies, ied.buf, ied.pos);
	pack->datalen += ied.pos;
	return 0;
}

static int check_key(struct dundi_peer *peer, unsigned char *newkey, unsigned char *newsig, unsigned long keycrc32)
{
	unsigned char dst[128];
	int res;
	struct cw_key *key, *skey;
	char eid_str[20];
	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Expected '%08lx' got '%08lx'\n", peer->them_keycrc32, keycrc32);
	if (peer->them_keycrc32 && (peer->them_keycrc32 == keycrc32)) {
		/* A match */
		return 1;
	} else if (!newkey || !newsig)
		return 0;
	if (!memcmp(peer->rxenckey, newkey, 128) &&
	    !memcmp(peer->rxenckey + 128, newsig, 128)) {
		/* By definition, a match */
		return 1;
	}
	/* Decrypt key */
	key = cw_key_get(peer->outkey, CW_KEY_PRIVATE);
	if (!key) {
		cw_log(CW_LOG_NOTICE, "Unable to find key '%s' to decode shared key from '%s'\n",
			peer->outkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		return -1;
	}

	skey = cw_key_get(peer->inkey, CW_KEY_PUBLIC);
	if (!skey) {
		cw_log(CW_LOG_NOTICE, "Unable to find key '%s' to verify shared key from '%s'\n",
			peer->inkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		return -1;
	}

	/* First check signature */
	res = cw_check_signature_bin(skey, (char *)newkey, 128, newsig);
	if (res) 
		return 0;

	res = cw_decrypt_bin(dst, newkey, sizeof(dst), key);
	if (res != 16) {
		if (res >= 0)
			cw_log(CW_LOG_NOTICE, "Weird, key decoded to the wrong size (%d)\n", res);
		return 0;
	}
	/* Decrypted, passes signature */
	cw_log(CW_LOG_DEBUG, "Wow, new key combo passed signature and decrypt!\n");
	memcpy(peer->rxenckey, newkey, 128);
	memcpy(peer->rxenckey + 128, newsig, 128);
	peer->them_keycrc32 = crc32(0L, peer->rxenckey, 128);
	AES_set_decrypt_key(dst, 128, &peer->them_dcx);
	AES_set_encrypt_key(dst, 128, &peer->them_ecx);
	return 1;
}

static int handle_command_response(struct dundi_transaction *trans, struct dundi_hdr *hdr, int datalen, int encrypted)
{
	/* Handle canonical command / response */
	int final = hdr->cmdresp & 0x80;
	int cmd = hdr->cmdresp & 0x7f;
	int x,y,z;
	int resp;
	int res;
	int authpass=0;
	unsigned char *bufcpy;
	struct dundi_ie_data ied;
	struct dundi_ies ies;
	struct dundi_peer *peer;
	char eid_str[20];
	char eid_str2[20];
	memset(&ied, 0, sizeof(ied));
	memset(&ies, 0, sizeof(ies));
	if (datalen) {
		bufcpy = alloca(datalen);
		/* Make a copy for parsing */
		memcpy(bufcpy, hdr->ies, datalen);
		cw_log(CW_LOG_DEBUG, "Got canonical message %d (%d), %d bytes data%s\n", cmd, hdr->oseqno, datalen, final ? " (Final)" : "");
		if (dundi_parse_ies(&ies, bufcpy, datalen) < 0) {
			cw_log(CW_LOG_WARNING, "Failed to parse DUNDI information elements!\n");
			return -1;
		}
	}
	switch(cmd) {
	case DUNDI_COMMAND_DPDISCOVER:
	case DUNDI_COMMAND_EIDQUERY:
	case DUNDI_COMMAND_PRECACHERQ:
		if (cmd == DUNDI_COMMAND_EIDQUERY)
			resp = DUNDI_COMMAND_EIDRESPONSE;
		else if (cmd == DUNDI_COMMAND_PRECACHERQ)
			resp = DUNDI_COMMAND_PRECACHERP;
		else
			resp = DUNDI_COMMAND_DPRESPONSE;
		/* A dialplan or entity discover -- qualify by highest level entity */
		peer = find_peer(ies.eids[0]);
		if (!peer) {
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, NULL);
			dundi_send(trans, resp, 0, 1, &ied);
		} else {
			int hasauth = 0;
			trans->us_eid = peer->us_eid;
			if (strlen(peer->inkey)) {
				hasauth = encrypted;
			} else 
				hasauth = 1;
			if (hasauth) {
				/* Okay we're authentiated and all, now we check if they're authorized */
				if (!ies.called_context)
					ies.called_context = "e164";
				if (cmd == DUNDI_COMMAND_EIDQUERY) {
					res = dundi_answer_entity(trans, &ies);
				} else {
					if (!ies.called_number || cw_strlen_zero(ies.called_number)) {
						/* They're not permitted to access that context */
						dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Invalid or missing number/entity");
						dundi_send(trans, resp, 0, 1, &ied);
					} else if ((cmd == DUNDI_COMMAND_DPDISCOVER) &&
					           (peer->model & DUNDI_MODEL_INBOUND) &&
							   has_permission(peer->permit, ies.called_context)) {
						res = dundi_answer_query(trans, &ies, ies.called_context);
						if (res < 0) {
							/* There is no such dundi context */
							dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unsupported DUNDI Context");
							dundi_send(trans, resp, 0, 1, &ied);
						}
					} else if ((cmd == DUNDI_COMMAND_PRECACHERQ) &&
					           (peer->pcmodel & DUNDI_MODEL_INBOUND) &&
							   has_permission(peer->include, ies.called_context)) {
						res = dundi_prop_precache(trans, &ies, ies.called_context);
						if (res < 0) {
							/* There is no such dundi context */
							dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unsupported DUNDI Context");
							dundi_send(trans, resp, 0, 1, &ied);
						}
					} else {
						/* They're not permitted to access that context */
						dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Permission to context denied");
						dundi_send(trans, resp, 0, 1, &ied);
					}
				}
			} else {
				/* They're not permitted to access that context */
				dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unencrypted responses not permitted");
				dundi_send(trans, resp, 0, 1, &ied);
			}
		}
		break;
	case DUNDI_COMMAND_REGREQ:
		/* A register request -- should only have one entity */
		peer = find_peer(ies.eids[0]);
		if (!peer || !peer->dynamic) {
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, NULL);
			dundi_send(trans, DUNDI_COMMAND_REGRESPONSE, 0, 1, &ied);
		} else {
			int hasauth = 0;
			trans->us_eid = peer->us_eid;
			if (!cw_strlen_zero(peer->inkey)) {
				hasauth = encrypted;
			} else
				hasauth = 1;
			if (hasauth) {
				int expire = default_expiration;
				char iabuf[INET_ADDRSTRLEN];
				char data[256];
				int needqual = 0;
				cw_sched_modify(sched, &peer->registerexpire, (expire + 10) * 1000, do_register_expire, peer);
				cw_inet_ntoa(iabuf, sizeof(iabuf), trans->addr.sin_addr);
				snprintf(data, sizeof(data), "%s:%d:%d", iabuf, ntohs(trans->addr.sin_port), expire);
				cw_db_put("dundi/dpeers", dundi_eid_to_str_short(eid_str, sizeof(eid_str), &peer->eid), data);
				if (inaddrcmp(&peer->addr, &trans->addr)) {
					if (option_verbose > 2)
						cw_verbose(VERBOSE_PREFIX_3 "Registered DUNDi peer '%s' at '%s:%d'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), iabuf, ntohs(trans->addr.sin_port));
					needqual = 1;
				}
					
				memcpy(&peer->addr, &trans->addr, sizeof(peer->addr));
				dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, default_expiration);
				dundi_send(trans, DUNDI_COMMAND_REGRESPONSE, 0, 1, &ied);
				if (needqual)
					qualify_peer(peer, 1);
			}
		}
		break;
	case DUNDI_COMMAND_DPRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			/* Success of some sort */
			cw_log(CW_LOG_DEBUG, "Looks like success of some sort (%d), %d answers\n", ies.cause, ies.anscount);
			if (cw_test_flag(trans, FLAG_ENCRYPT)) {
				authpass = encrypted;
			} else 
				authpass = 1;
			if (authpass) {
				/* Pass back up answers */
				if (trans->parent && trans->parent->dr) {
					y = trans->parent->respcount;
					for (x=0;x<ies.anscount;x++) {
						if (trans->parent->respcount < trans->parent->maxcount) {
							/* Make sure it's not already there */
							for (z=0;z<trans->parent->respcount;z++) {
								if ((trans->parent->dr[z].techint == ies.answers[x]->protocol) &&
								    !strcmp(trans->parent->dr[z].dest.data, (char *)ies.answers[x]->data))
										break;
							}
							if (z == trans->parent->respcount) {
								/* Copy into parent responses */
								trans->parent->dr[trans->parent->respcount].flags = ntohs(ies.answers[x]->flags);
								trans->parent->dr[trans->parent->respcount].techint = ies.answers[x]->protocol;
								trans->parent->dr[trans->parent->respcount].weight = ntohs(ies.answers[x]->weight);
								trans->parent->dr[trans->parent->respcount].eid = ies.answers[x]->eid;
								if (ies.expiration > 0)
									trans->parent->dr[trans->parent->respcount].expiration = ies.expiration;
								else
									trans->parent->dr[trans->parent->respcount].expiration = dundi_cache_time;
								dundi_eid_to_str(trans->parent->dr[trans->parent->respcount].eid_str, 
									sizeof(trans->parent->dr[trans->parent->respcount].eid_str),
									&ies.answers[x]->eid);
								cw_dynstr_printf(&trans->parent->dr[trans->parent->respcount].dest, "%s", (char *)ies.answers[x]->data);
								cw_copy_string(trans->parent->dr[trans->parent->respcount].tech, tech2str(ies.answers[x]->protocol),
									sizeof(trans->parent->dr[trans->parent->respcount].tech));
								trans->parent->respcount++;
								cw_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);
							} else if (trans->parent->dr[z].weight > ies.answers[x]->weight) {
								/* Update weight if appropriate */
								trans->parent->dr[z].weight = ies.answers[x]->weight;
							}
						} else
							cw_log(CW_LOG_NOTICE, "Dropping excessive answers to request for %s@%s\n",
								trans->parent->number, trans->parent->dcontext);
					}
					/* Save all the results (if any) we had.  Even if no results, still cache lookup.  Let
					   the cache know if this request was unaffected by our entity list. */
					cache_save(&trans->them_eid, trans->parent, y, 
							ies.hint ? cw_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_UNAFFECTED)) : 0, ies.expiration, 0);
					if (ies.hint) {
						cache_save_hint(&trans->them_eid, trans->parent, ies.hint, ies.expiration);
						if (cw_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_TTL_EXPIRED)))
							cw_set_flag_nonstd(trans->parent->hmd, DUNDI_HINT_TTL_EXPIRED);
						if (cw_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_DONT_ASK))) { 
							if (strlen((char *)ies.hint->data) > strlen(trans->parent->hmd->exten)) {
								cw_copy_string(trans->parent->hmd->exten, (char *)ies.hint->data, 
									sizeof(trans->parent->hmd->exten));
							}
						} else {
							cw_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);
						}
					}
					if (ies.expiration > 0) {
						if (trans->parent->expiration > ies.expiration) {
							trans->parent->expiration = ies.expiration;
						}
					}
				}
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
			
		} else {
			/* Auth failure, check for data */
			if (!final) {
				/* Cancel if they didn't already */
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_EIDRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			/* Success of some sort */
			cw_log(CW_LOG_DEBUG, "Looks like success of some sort (%d)\n", ies.cause);
			if (cw_test_flag(trans, FLAG_ENCRYPT)) {
				authpass = encrypted;
			} else 
				authpass = 1;
			if (authpass) {
				/* Pass back up answers */
				if (trans->parent && trans->parent->dei && ies.q_org) {
					if (!trans->parent->respcount) {
						trans->parent->respcount++;
						if (ies.q_dept)
							cw_copy_string(trans->parent->dei->orgunit, ies.q_dept, sizeof(trans->parent->dei->orgunit));
						if (ies.q_org)
							cw_copy_string(trans->parent->dei->org, ies.q_org, sizeof(trans->parent->dei->org));
						if (ies.q_locality)
							cw_copy_string(trans->parent->dei->locality, ies.q_locality, sizeof(trans->parent->dei->locality));
						if (ies.q_stateprov)
							cw_copy_string(trans->parent->dei->stateprov, ies.q_stateprov, sizeof(trans->parent->dei->stateprov));
						if (ies.q_country)
							cw_copy_string(trans->parent->dei->country, ies.q_country, sizeof(trans->parent->dei->country));
						if (ies.q_email)
							cw_copy_string(trans->parent->dei->email, ies.q_email, sizeof(trans->parent->dei->email));
						if (ies.q_phone)
							cw_copy_string(trans->parent->dei->phone, ies.q_phone, sizeof(trans->parent->dei->phone));
						if (ies.q_ipaddr)
							cw_copy_string(trans->parent->dei->ipaddr, ies.q_ipaddr, sizeof(trans->parent->dei->ipaddr));
						if (!dundi_eid_cmp(&trans->them_eid, &trans->parent->query_eid)) {
							/* If it's them, update our address */
							cw_inet_ntoa(trans->parent->dei->ipaddr, sizeof(trans->parent->dei->ipaddr),
								trans->addr.sin_addr);
						}
					}
					if (ies.hint) {
						if (cw_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_TTL_EXPIRED)))
							cw_set_flag_nonstd(trans->parent->hmd, DUNDI_HINT_TTL_EXPIRED);
					}
				}
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
			
		} else {
			/* Auth failure, check for data */
			if (!final) {
				/* Cancel if they didn't already */
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_REGRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			int hasauth;
			/* Success of some sort */
			if (cw_test_flag(trans, FLAG_ENCRYPT)) {
				hasauth = encrypted;
			} else 
				hasauth = 1;
			
			if (!hasauth) {
				cw_log(CW_LOG_NOTICE, "Reponse to register not authorized!\n");
				if (!final) {
					dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Improper signature in answer");
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, &ied);
				}
			} else {
				cw_log(CW_LOG_DEBUG, "Yay, we've registered as '%s' to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->us_eid),
							dundi_eid_to_str(eid_str2, sizeof(eid_str2), &trans->them_eid));
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		} else {
			/* Auth failure, cancel if they didn't for some reason */
			if (!final) {
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_INVALID:
	case DUNDI_COMMAND_NULL:
	case DUNDI_COMMAND_PRECACHERP:
		/* Do nothing special */
		if (!final) 
			dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		break;
	case DUNDI_COMMAND_ENCREJ:
		if ((cw_test_flag(trans, FLAG_SENDFULLKEY)) || !trans->lasttrans || !(peer = find_peer(&trans->them_eid))) {
			/* No really, it's over at this point */
			if (!final) 
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		} else {
			/* Send with full key */
			cw_set_flag(trans, FLAG_SENDFULLKEY);
			if (final) {
				/* Ooops, we got a final message, start by sending ACK... */
				dundi_ack(trans, hdr->cmdresp & 0x80);
				trans->aseqno = trans->iseqno;
				/* Now, we gotta create a new transaction */
				if (!reset_transaction(trans)) {
					/* Make sure handle_frame doesn't destroy us */
					hdr->cmdresp &= 0x7f;
					/* Parse the message we transmitted */
					memset(&ies, 0, sizeof(ies));
					dundi_parse_ies(&ies, trans->lasttrans->h->ies, trans->lasttrans->datalen - sizeof(struct dundi_hdr));
					/* Reconstruct outgoing encrypted packet */
					memset(&ied, 0, sizeof(ied));
					dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
					dundi_ie_append_raw(&ied, DUNDI_IE_SHAREDKEY, peer->txenckey, 128);
					dundi_ie_append_raw(&ied, DUNDI_IE_SIGNATURE, peer->txenckey + 128, 128);
					if (ies.encblock) 
						dundi_ie_append_encdata(&ied, DUNDI_IE_ENCDATA, ies.encblock->iv, ies.encblock->encdata, ies.enclen);
					dundi_send(trans, DUNDI_COMMAND_ENCRYPT, 0, trans->lasttrans->h->cmdresp & 0x80, &ied);
					peer->sentfullkey = 1;
				}
			}
		}
		break;
	case DUNDI_COMMAND_ENCRYPT:
		if (!encrypted) {
			/* No nested encryption! */
			if ((trans->iseqno == 1) && !trans->oseqno) {
				if (!ies.eids[0] || !(peer = find_peer(ies.eids[0])) || 
					((!ies.encsharedkey || !ies.encsig) && !ies.keycrc32) || 
					(check_key(peer, ies.encsharedkey, ies.encsig, ies.keycrc32) < 1)) {
					if (!final) {
						dundi_send(trans, DUNDI_COMMAND_ENCREJ, 0, 1, NULL);
					}
					break;
				}
				apply_peer(trans, peer);
				/* Key passed, use new contexts for this session */
				trans->ecx = peer->them_ecx;
				trans->dcx = peer->them_dcx;
			}
			if (cw_test_flag(trans, FLAG_ENCRYPT) && ies.encblock && ies.enclen) {
				struct dundi_hdr *dhdr;
				unsigned char decoded[MAX_PACKET_SIZE];
				int ddatalen;
				ddatalen = sizeof(decoded);
				dhdr = dundi_decrypt(trans, decoded, &ddatalen, hdr, ies.encblock, ies.enclen);
				if (dhdr) {
					/* Handle decrypted response */
					if (dundidebug)
						dundi_showframe(dhdr, 3, &trans->addr, ddatalen - sizeof(struct dundi_hdr));
					handle_command_response(trans, dhdr, ddatalen - sizeof(struct dundi_hdr), 1);
					/* Carry back final flag */
					hdr->cmdresp |= dhdr->cmdresp & 0x80;
					break;
				} else
					cw_log(CW_LOG_DEBUG, "Ouch, decrypt failed :(\n");
			}
		}
		if (!final) {
			/* Turn off encryption */
			cw_clear_flag(trans, FLAG_ENCRYPT);
			dundi_send(trans, DUNDI_COMMAND_ENCREJ, 0, 1, NULL);
		}
		break;
	default:
		/* Send unknown command if we don't know it, with final flag IFF it's the
		   first command in the dialog and only if we haven't recieved final notification */
		if (!final) {
			dundi_ie_append_byte(&ied, DUNDI_IE_UNKNOWN, cmd);
			dundi_send(trans, DUNDI_COMMAND_UNKNOWN, 0, !hdr->oseqno, &ied);
		}
	}
	return 0;
}

static void destroy_packet(struct dundi_packet *pack, int needfree);
static void destroy_packets(struct dundi_packet *p)
{
	struct dundi_packet *prev;
	while(p) {
		prev = p;
		p = p->next;
		cw_sched_del(sched, &prev->retransid);
		free(prev);
	}
}


static int ack_trans(struct dundi_transaction *trans, int iseqno)
{
	/* Ack transmitted packet corresponding to iseqno */
	struct dundi_packet *pack;
	pack = trans->packets;
	while(pack) {
		if ((pack->h->oseqno + 1) % 255 == iseqno) {
			destroy_packet(pack, 0);
			if (trans->lasttrans) {
				cw_log(CW_LOG_WARNING, "Whoa, there was still a last trans?\n");
				destroy_packets(trans->lasttrans);
			}
			trans->lasttrans = pack;
			cw_sched_del(sched, &trans->autokillid);
			return 1;
		}
		pack = pack->next;
	}
	return 0;
}

static int handle_frame(struct dundi_hdr *h, struct sockaddr_in *sain, int datalen)
{
	struct dundi_transaction *trans;
	trans = find_transaction(h, sain);
	if (!trans) {
		dundi_reject(h, sain);
		return 0;
	}
	/* Got a transaction, see where this header fits in */
	if (h->oseqno == trans->iseqno) {
		/* Just what we were looking for...  Anything but ack increments iseqno */
		if (ack_trans(trans, h->iseqno) && cw_test_flag(trans, FLAG_FINAL)) {
			/* If final, we're done */
			destroy_trans(trans, 0);
			return 0;
		}
		if (h->cmdresp != DUNDI_COMMAND_ACK) {
			trans->oiseqno = trans->iseqno;
			trans->iseqno++;
			handle_command_response(trans, h, datalen, 0);
		}
		if (trans->aseqno != trans->iseqno) {
			dundi_ack(trans, h->cmdresp & 0x80);
			trans->aseqno = trans->iseqno;
		}
		/* Delete any saved last transmissions */
		destroy_packets(trans->lasttrans);
		trans->lasttrans = NULL;
		if (h->cmdresp & 0x80) {
			/* Final -- destroy now */
			destroy_trans(trans, 0);
		}
	} else if (h->oseqno == trans->oiseqno) {
		/* Last incoming sequence number -- send ACK without processing */
		dundi_ack(trans, 0);
	} else {
		/* Out of window -- simply drop */
		cw_log(CW_LOG_DEBUG, "Dropping packet out of window!\n");
	}
	return 0;
}

static int socket_read(struct cw_io_rec *ior, int fd, short events, void *cbdata)
{
	char buf[MAX_PACKET_SIZE];
	struct sockaddr_in sain;
	struct dundi_hdr *h;
	int res;

	CW_UNUSED(ior);
	CW_UNUSED(fd);
	CW_UNUSED(events);
	CW_UNUSED(cbdata);

	socklen_t len;
	len = sizeof(sain);
	res = recvfrom(netsocket, buf, sizeof(buf) - 1, 0,(struct sockaddr *) &sain, &len);
	if (res < 0) {
		if (errno != ECONNREFUSED)
			cw_log(CW_LOG_WARNING, "Error: %s\n", strerror(errno));
		return 1;
	}
	if (res < sizeof(struct dundi_hdr)) {
		cw_log(CW_LOG_WARNING, "midget packet received (%d of %d min)\n", res, (int)sizeof(struct dundi_hdr));
		return 1;
	}
	buf[res] = '\0';
	h = (struct dundi_hdr *)buf;
	if (dundidebug)
		dundi_showframe(h, 1, &sain, res - sizeof(struct dundi_hdr));
	cw_mutex_lock(&peerlock);
	handle_frame(h, &sain, res - sizeof(struct dundi_hdr));
	cw_mutex_unlock(&peerlock);
	return 1;
}

static void build_secret(char *secret, int seclen)
{
	unsigned char tmp[16];
	char *s;
	build_iv(tmp);
	secret[0] = '\0';
	cw_base64encode(secret, tmp, sizeof(tmp), seclen);
	/* Eliminate potential bad characters */
	while((s = strchr(secret, ';'))) *s = '+';
	while((s = strchr(secret, '/'))) *s = '+';
	while((s = strchr(secret, ':'))) *s = '+';
	while((s = strchr(secret, '@'))) *s = '+';
}


static void save_secret(const char *newkey, const char *oldkey)
{
	char tmp[256];
	if (oldkey)
		snprintf(tmp, sizeof(tmp), "%s;%s", oldkey, newkey);
	else
		snprintf(tmp, sizeof(tmp), "%s", newkey);
	rotatetime = time(NULL) + DUNDI_SECRET_TIME;
	cw_db_put(secretpath, "secret", tmp);
	snprintf(tmp, sizeof(tmp), "%ld", rotatetime);
	cw_db_put(secretpath, "secretexpiry", tmp);
}

static void load_password(void)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	char *current = NULL;
	char *last = NULL;
	time_t expired;

	if (!cw_db_get(secretpath, "secretexpiry", &ds)) {
		if (sscanf(ds.data, "%ld", &expired) == 1) {
			cw_dynstr_reset(&ds);

			if (!cw_db_get(secretpath, "secret", &ds)) {
				current = strchr(ds.data, ';');
				if (!current)
					current = ds.data;
				else {
					*current = '\0';
					current++;
				};
				if ((time(NULL) - expired) < 0) {
					if ((expired - time(NULL)) > DUNDI_SECRET_TIME)
						expired = time(NULL) + DUNDI_SECRET_TIME;
				} else if ((time(NULL) - (expired + DUNDI_SECRET_TIME)) < 0) {
					last = current;
					current = NULL;
				} else {
					last = NULL;
					current = NULL;
				}
			}
		}
	}

	if (current) {
		/* Current key is still valid, just setup rotatation properly */
		cw_copy_string(cursecret, current, sizeof(cursecret));
		rotatetime = expired;
	} else {
		/* Current key is out of date, rotate or eliminate all together */
		build_secret(cursecret, sizeof(cursecret));
		save_secret(cursecret, last);
	}

	cw_dynstr_free(&ds);
}

static void check_password(void)
{
	char oldsecret[80];
	time_t now;
	
	time(&now);	
#if 0
	printf("%ld/%ld\n", now, rotatetime);
#endif
	if ((now - rotatetime) >= 0) {
		/* Time to rotate keys */
		cw_copy_string(oldsecret, cursecret, sizeof(oldsecret));
		build_secret(cursecret, sizeof(cursecret));
		save_secret(cursecret, oldsecret);
	}
}

static void network_thread_cleanup(void *data)
{
	CW_UNUSED(data);

	if (cw_io_isactive(&netsocket_io_id))
		cw_io_remove(io, &netsocket_io_id);

	if (netsocket >= 0) {
		close(netsocket);
		netsocket = -1;
	}
}

static void *network_thread(void *data)
{
	CW_UNUSED(data);

	/* Our job is simple: Send queued messages, retrying if necessary.  Read frames 
	   from the network, and queue them for delivery to the channels */
	pthread_cleanup_push(network_thread_cleanup, NULL);

	/* Establish I/O callback for socket read */
	cw_io_add(io, &netsocket_io_id, netsocket, CW_IO_IN);
	for (;;) {
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		/* 10s select timeout */
		cw_io_run(io, 10000);
		check_password();
	}

	/* NOT REACHED */
	pthread_cleanup_pop(0);
	return NULL;
}

static void dundi_mutex_unlock(void *mutex)
{
	cw_mutex_unlock(mutex);
}

static __attribute__((noreturn)) void *process_precache(void *data)
{
	char context[256];
	char number[256];
	struct dundi_precache_queue *qe;
	time_t now;
	int run;

	CW_UNUSED(data);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	for (;;) {
		time(&now);
		run = 0;

		pthread_cleanup_push(dundi_mutex_unlock, &pclock);
		cw_mutex_lock(&pclock);

		if (pcq) {
			if (!pcq->expiration) {
				/* Gone...  Remove... */
				qe = pcq;
				pcq = pcq->next;
				free(qe);
			} else if (pcq->expiration < now) {
				/* Process this entry */
				pcq->expiration = 0;
				cw_copy_string(context, pcq->context, sizeof(context));
				cw_copy_string(number, pcq->number, sizeof(number));
				run = 1;
			}
		}

		pthread_cleanup_pop(1);

		if (run) {
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			dundi_precache(context, number);
			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		} else
			sleep(1);
	}
}

static int dundi_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	dundidebug = 1;
	cw_dynstr_printf(ds_p, "DUNDi Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int dundi_do_store_history(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	global_storehistory = 1;
	cw_dynstr_printf(ds_p, "DUNDi History Storage Enabled\n");
	return RESULT_SUCCESS;
}

static int dundi_flush(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int stats=0;
	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;
	if (argc > 2) {
		if (!strcasecmp(argv[2], "stats"))
			stats = 1;
		else
			return RESULT_SHOWUSAGE;
	}
	if (stats) {
		/* Flush statistics */
		struct dundi_peer *p;
		int x;
		cw_mutex_lock(&peerlock);
		p = peers;
		while(p) {
			for (x=0;x<DUNDI_TIMING_HISTORY;x++) {
				free(p->lookups[x]);
				p->lookups[x] = NULL;
				p->lookuptimes[x] = 0;
			}
			p->avgms = 0;
			p = p->next;
		}
		cw_mutex_unlock(&peerlock);
	} else {
		cw_db_deltree("dundi/cache", NULL);
		cw_dynstr_printf(ds_p, "DUNDi Cache Flushed\n");
	}
	return RESULT_SUCCESS;
}

static int dundi_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	dundidebug = 0;
	cw_dynstr_printf(ds_p, "DUNDi Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int dundi_no_store_history(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(argv);

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	global_storehistory = 0;
	cw_dynstr_printf(ds_p, "DUNDi History Storage Disabled\n");
	return RESULT_SUCCESS;
}

static const char *model2str(int model)
{
	switch(model) {
	case DUNDI_MODEL_INBOUND:
		return "Inbound";
	case DUNDI_MODEL_OUTBOUND:
		return "Outbound";
	case DUNDI_MODEL_SYMMETRIC:
		return "Symmetric";
	default:
		return "Unknown";
	}
}

static void complete_peer_4(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	char eid_str[20];
	struct dundi_peer *p;

	if (lastarg == 3) {
		cw_mutex_lock(&peerlock);

		for (p = peers; p; p = p->next) {
			if (!strncasecmp(argv[3], dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), lastarg_len))
				cw_dynstr_printf(ds_p, "%s\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid));
		}

		cw_mutex_unlock(&peerlock);
	}
}

static int rescomp(const void *a, const void *b)
{
	const struct dundi_result *resa, *resb;
	resa = a;
	resb = b;
	if (resa->weight < resb->weight)
		return -1;
	if (resa->weight > resb->weight)
		return 1;
	return 0;
}

static void sort_results(struct dundi_result *results, int count)
{
	qsort(results, count, sizeof(results[0]), rescomp);
}

static int dundi_do_lookup(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct dundi_result dr[MAX_RESULTS];
	char tmp[256];
	char fs[80] = "";
	struct timeval start;
	char *context;
	int res;
	int x;
	int bypass = 0;

	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;

	if (argc > 3) {
		if (!strcasecmp(argv[3], "bypass"))
			bypass=1;
		else
			return RESULT_SHOWUSAGE;
	}

	cw_copy_string(tmp, argv[2], sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}

	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_init(&dr[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);

	start = cw_tvnow();
	res = dundi_lookup(dr, MAX_RESULTS, NULL, context, tmp, bypass);
	
	if (res < 0) 
		cw_dynstr_printf(ds_p, "DUNDi lookup returned error.\n");
	else if (!res) 
		cw_dynstr_printf(ds_p, "DUNDi lookup returned no results.\n");
	else
		sort_results(dr, res);

	for (x = 0; x < res; x++) {
		cw_dynstr_printf(ds_p, "%3d. %5d %s/%s (%s)\n", x + 1, dr[x].weight, dr[x].tech, dr[x].dest.data, dundi_flags2str(fs, sizeof(fs), dr[x].flags));
		cw_dynstr_printf(ds_p, "     from %s, expires in %d s\n", dr[x].eid_str, dr[x].expiration);
	}

	cw_dynstr_printf(ds_p, "DUNDi lookup completed in %d ms\n", cw_tvdiff_ms(cw_tvnow(), start));

	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_free(&dr[x].dest);

	return RESULT_SUCCESS;
}

static int dundi_do_precache(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res;
	char tmp[256];
	char *context;
	struct timeval start;
	if ((argc < 3) || (argc > 3))
		return RESULT_SHOWUSAGE;
	cw_copy_string(tmp, argv[2], sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	start = cw_tvnow();
	res = dundi_precache(context, tmp);
	
	if (res < 0) 
		cw_dynstr_printf(ds_p, "DUNDi precache returned error.\n");
	else if (!res) 
		cw_dynstr_printf(ds_p, "DUNDi precache returned no error.\n");
	cw_dynstr_printf(ds_p, "DUNDi lookup completed in %d ms\n", cw_tvdiff_ms(cw_tvnow(), start));
	return RESULT_SUCCESS;
}

static int dundi_do_query(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res;
	char tmp[256];
	char *context;
	dundi_eid eid;
	struct dundi_entity_info dei;
	if ((argc < 3) || (argc > 3))
		return RESULT_SHOWUSAGE;
	if (dundi_str_to_eid(&eid, argv[2])) {
		cw_dynstr_printf(ds_p, "'%s' is not a valid EID!\n", argv[2]);
		return RESULT_SHOWUSAGE;
	}
	cw_copy_string(tmp, argv[2], sizeof(tmp));
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	res = dundi_query_eid(&dei, context, eid);
	if (res < 0) 
		cw_dynstr_printf(ds_p, "DUNDi Query EID returned error.\n");
	else if (!res) 
		cw_dynstr_printf(ds_p, "DUNDi Query EID returned no results.\n");
	else {
		cw_dynstr_tprintf(ds_p, 9,
			cw_fmtval("DUNDi Query EID succeeded:\n"),
			cw_fmtval("Department:      %s\n", dei.orgunit),
			cw_fmtval("Organization:    %s\n", dei.org),
			cw_fmtval("City/Locality:   %s\n", dei.locality),
			cw_fmtval("State/Province:  %s\n", dei.stateprov),
			cw_fmtval("Country:         %s\n", dei.country),
			cw_fmtval("E-mail:          %s\n", dei.email),
			cw_fmtval("Phone:           %s\n", dei.phone),
			cw_fmtval("IP Address:      %s\n", dei.ipaddr)
		);
	}
	return RESULT_SUCCESS;
}

static int dundi_show_peer(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char iabuf[INET_ADDRSTRLEN];
	char eid_str[20];
	struct dundi_peer *peer;
	struct permission *p;
	int x, cnt;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		if (!strcasecmp(dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), argv[3]))
			break;
		peer = peer->next;
	}
	if (peer) {
		cw_dynstr_tprintf(ds_p, 8,
			cw_fmtval("Peer:    %s\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid)),
			cw_fmtval("Model:   %s\n", model2str(peer->model)),
			cw_fmtval("Host:    %s\n", (peer->addr.sin_addr.s_addr ? cw_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "<Unspecified>")),
			cw_fmtval("Dynamic: %s\n", (peer->dynamic ? "yes" : "no")),
			cw_fmtval("KeyPend: %s\n", (peer->keypending ? "yes" : "no")),
			cw_fmtval("Reg:     %s\n", (cw_sched_state_scheduled(&peer->registerid) ? "Yes" : "No")),
			cw_fmtval("In Key:  %s\n", (cw_strlen_zero(peer->inkey) ? "<None>" : peer->inkey)),
			cw_fmtval("Out Key: %s\n", (cw_strlen_zero(peer->outkey) ? "<None>" : peer->outkey))
		);
		if (peer->include) {
			cw_dynstr_printf(ds_p, "Include logic%s:\n", peer->model & DUNDI_MODEL_OUTBOUND ? "" : " (IGNORED)");
		}
		p = peer->include;
		while(p) {
			cw_dynstr_printf(ds_p, "-- %s %s\n", p->allow ? "include" : "do not include", p->name);
			p = p->next;
		}
		if (peer->permit) {
			cw_dynstr_printf(ds_p, "Query logic%s:\n", peer->model & DUNDI_MODEL_INBOUND ? "" : " (IGNORED)");
		}
		p = peer->permit;
		while(p) {
			cw_dynstr_printf(ds_p, "-- %s %s\n", p->allow ? "permit" : "deny", p->name);
			p = p->next;
		}
		cnt = 0;
		for (x=0;x<DUNDI_TIMING_HISTORY;x++) {
			if (peer->lookups[x]) {
				if (!cnt)
					cw_dynstr_printf(ds_p, "Last few query times:\n");
				cw_dynstr_printf(ds_p, "-- %d. %s (%d ms)\n", x + 1, peer->lookups[x], peer->lookuptimes[x]);
				cnt++;
			}
		}
		if (cnt)
			cw_dynstr_printf(ds_p, "Average query time: %d ms\n", peer->avgms);
	} else
		cw_dynstr_printf(ds_p, "No such peer '%s'\n", argv[3]);
	cw_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
}

static int dundi_show_peers(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-20.20s %-15.15s     %-10.10s %-8.8s %-15.15s\n"
#define FORMAT "%-20.20s %-15.15s %s %-10.10s %-8.8s %-15.15s\n"
	struct dundi_peer *peer;
	char iabuf[INET_ADDRSTRLEN];
	int registeredonly=0;
	char avgms[20];
	char eid_str[20];
	int online_peers = 0;
	int offline_peers = 0;
	int unmonitored_peers = 0;
	int total_peers = 0;

	if ((argc != 3) && (argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if ((argc == 4)) {
 		if (!strcasecmp(argv[3], "registered")) {
			registeredonly = 1;
		} else
			return RESULT_SHOWUSAGE;
 	}
	cw_mutex_lock(&peerlock);
	cw_dynstr_printf(ds_p, FORMAT2, "EID", "Host", "Model", "AvgTime", "Status");
	for (peer = peers;peer;peer = peer->next) {
		char status[20];
		int print_line = -1;
		char srch[2000];
		total_peers++;
		if (registeredonly && !peer->addr.sin_addr.s_addr)
			continue;
		if (peer->maxms) {
			if (peer->lastms < 0) {
				strcpy(status, "UNREACHABLE");
				offline_peers++;
			}
			else if (peer->lastms > peer->maxms) {
				snprintf(status, sizeof(status), "LAGGED (%d ms)", peer->lastms);
				offline_peers++;
			}
			else if (peer->lastms) {
				snprintf(status, sizeof(status), "OK (%d ms)", peer->lastms);
				online_peers++;
			}
			else {
				strcpy(status, "UNKNOWN");
				offline_peers++;
			}
		} else {
			strcpy(status, "Unmonitored");
			unmonitored_peers++;
		}
		if (peer->avgms) 
			snprintf(avgms, sizeof(avgms), "%d ms", peer->avgms);
		else
			strcpy(avgms, "Unavail");
		snprintf(srch, sizeof(srch), FORMAT, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), 
					peer->addr.sin_addr.s_addr ? cw_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "(S)", model2str(peer->model), avgms, status);

                if (argc == 5) {
                  if (!strcasecmp(argv[3],"include") && strstr(srch,argv[4])) {
                        print_line = -1;
                   } else if (!strcasecmp(argv[3],"exclude") && !strstr(srch,argv[4])) {
                        print_line = 1;
                   } else if (!strcasecmp(argv[3],"begin") && !strncasecmp(srch,argv[4],strlen(argv[4]))) {
                        print_line = -1;
                   } else {
                        print_line = 0;
                  }
                }
		
        if (print_line) {
			cw_dynstr_printf(ds_p, FORMAT, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid),
					peer->addr.sin_addr.s_addr ? cw_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "(S)", model2str(peer->model), avgms, status);
		}
	}
	cw_dynstr_printf(ds_p, "%d dundi peers [%d online, %d offline, %d unmonitored]\n", total_peers, online_peers, offline_peers, unmonitored_peers);
	cw_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_trans(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-22.22s %-5.5s %-5.5s %-3.3s %-3.3s %-3.3s\n"
#define FORMAT "%-16.16s:%5d %-5.5d %-5.5d %-3.3d %-3.3d %-3.3d\n"
	char iabuf[INET_ADDRSTRLEN];
	struct dundi_transaction *trans;

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(&peerlock);
	cw_dynstr_printf(ds_p, FORMAT2, "Remote", "Src", "Dst", "Tx", "Rx", "Ack");
	for (trans = alltrans;trans;trans = trans->allnext) {
			cw_dynstr_printf(ds_p, FORMAT, cw_inet_ntoa(iabuf, sizeof(iabuf), trans->addr.sin_addr),
					ntohs(trans->addr.sin_port), trans->strans, trans->dtrans, trans->oseqno, trans->iseqno, trans->aseqno);
	}
	cw_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_entityid(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char eid_str[20];

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(&peerlock);
	dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid);
	cw_mutex_unlock(&peerlock);
	cw_dynstr_printf(ds_p, "Global EID for this system is '%s'\n", eid_str);
	return RESULT_SUCCESS;
}

static int dundi_show_requests(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-15s %-15s %-15s %-3.3s %-3.3s\n"
#define FORMAT "%-15s %-15s %-15s %-3.3d %-3.3d\n"
	char eidstr[20];
	struct dundi_request *req;

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(&peerlock);
	cw_dynstr_printf(ds_p, FORMAT2, "Number", "Context", "Root", "Max", "Rsp");
	for (req = requests;req;req = req->next) {
			cw_dynstr_printf(ds_p, FORMAT, req->number, req->dcontext,
						dundi_eid_zero(&req->root_eid) ? "<unspecified>" : dundi_eid_to_str(eidstr, sizeof(eidstr), &req->root_eid), req->maxcount, req->respcount);
	}
	cw_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

/* Grok-a-dial DUNDi */

static int dundi_show_mappings(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-12.12s %-7.7s %-12.12s %-10.10s %-5.5s %-25.25s\n"
#define FORMAT "%-12.12s %-7d %-12.12s %-10.10s %-5.5s %-25.25s\n"
	char fs[256];
	struct dundi_mapping *map;

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_mutex_lock(&peerlock);
	cw_dynstr_printf(ds_p, FORMAT2, "DUNDi Cntxt", "Weight", "Local Cntxt", "Options", "Tech", "Destination");
	for (map = mappings;map;map = map->next) {
			cw_dynstr_printf(ds_p, FORMAT, map->dcontext, map->weight,
			                    cw_strlen_zero(map->lcontext) ? "<none>" : map->lcontext, 
								dundi_flags2str(fs, sizeof(fs), map->options), tech2str(map->tech), map->dest);
	}
	cw_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_precache(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-12.12s %-12.12s %-10.10s\n"
#define FORMAT "%-12.12s %-12.12s %02d:%02d:%02d\n"
	struct dundi_precache_queue *qe;
	int h,m,s;
	time_t now;

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	time(&now);
	cw_mutex_lock(&pclock);
	cw_dynstr_printf(ds_p, FORMAT2, "Number", "Context", "Expiration");
	for (qe = pcq;qe;qe = qe->next) {
		s = qe->expiration - now;
		h = s / 3600;
		s = s % 3600;
		m = s / 60;
		s = s % 60;
		cw_dynstr_printf(ds_p, FORMAT, qe->number, qe->context, h,m,s);
	}
	cw_mutex_unlock(&pclock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static const char debug_usage[] =
"Usage: dundi debug\n"
"       Enables dumping of DUNDi packets for debugging purposes\n";

static const char no_debug_usage[] =
"Usage: dundi no debug\n"
"       Disables dumping of DUNDi packets for debugging purposes\n";

static const char store_history_usage[] =
"Usage: dundi store history\n"
"       Enables storing of DUNDi requests and times for debugging\n"
"purposes\n";

static const char no_store_history_usage[] =
"Usage: dundi no store history\n"
"       Disables storing of DUNDi requests and times for debugging\n"
"purposes\n";

static const char show_peers_usage[] =
"Usage: dundi show peers\n"
"       Lists all known DUNDi peers.\n";

static const char show_trans_usage[] =
"Usage: dundi show trans\n"
"       Lists all known DUNDi transactions.\n";

static const char show_mappings_usage[] =
"Usage: dundi show mappings\n"
"       Lists all known DUNDi mappings.\n";

static const char show_precache_usage[] =
"Usage: dundi show precache\n"
"       Lists all known DUNDi scheduled precache updates.\n";

static const char show_entityid_usage[] =
"Usage: dundi show entityid\n"
"       Displays the global entityid for this host.\n";

static const char show_peer_usage[] =
"Usage: dundi show peer [peer]\n"
"       Provide a detailed description of a specifid DUNDi peer.\n";

static const char show_requests_usage[] =
"Usage: dundi show requests\n"
"       Lists all known pending DUNDi requests.\n";

static const char lookup_usage[] =
"Usage: dundi lookup <number>[@context] [bypass]\n"
"       Lookup the given number within the given DUNDi context\n"
"(or e164 if none is specified).  Bypasses cache if 'bypass'\n"
"keyword is specified.\n";

static const char precache_usage[] =
"Usage: dundi precache <number>[@context]\n"
"       Lookup the given number within the given DUNDi context\n"
"(or e164 if none is specified) and precaches the results to any\n"
"upstream DUNDi push servers.\n";

static const char query_usage[] =
"Usage: dundi query <entity>[@context]\n"
"       Attempts to retrieve contact information for a specific\n"
"DUNDi entity identifier (EID) within a given DUNDi context (or\n"
"e164 if none is specified).\n";

static const char flush_usage[] =
"Usage: dundi flush [stats]\n"
"       Flushes DUNDi answer cache, used primarily for debug.  If\n"
"'stats' is present, clears timer statistics instead of normal\n"
"operation.\n";

static struct cw_clicmd  cli_debug = {
	.cmda = { "dundi", "debug", NULL },
	.handler = dundi_do_debug,
	.summary = "Enable DUNDi debugging",
	.usage = debug_usage,
};

static struct cw_clicmd  cli_store_history = {
	.cmda = { "dundi", "store", "history", NULL },
	.handler = dundi_do_store_history,
	.summary = "Enable DUNDi historic records",
	.usage = store_history_usage,
};

static struct cw_clicmd  cli_no_store_history = {
	.cmda = { "dundi", "no", "store", "history", NULL },
	.handler = dundi_no_store_history,
	.summary = "Disable DUNDi historic records",
	.usage = no_store_history_usage,
};

static struct cw_clicmd  cli_flush = {
	.cmda = { "dundi", "flush", NULL },
	.handler = dundi_flush,
	.summary = "Flush DUNDi cache",
	.usage = flush_usage,
};

static struct cw_clicmd  cli_no_debug = {
	.cmda = { "dundi", "no", "debug", NULL },
	.handler = dundi_no_debug,
	.summary = "Disable DUNDi debugging",
	.usage = no_debug_usage,
};

static struct cw_clicmd  cli_show_peers = {
	.cmda = { "dundi", "show", "peers", NULL },
	.handler = dundi_show_peers,
	.summary = "Show defined DUNDi peers",
	.usage = show_peers_usage,
};

static struct cw_clicmd  cli_show_trans = {
	.cmda = { "dundi", "show", "trans", NULL },
	.handler = dundi_show_trans,
	.summary = "Show active DUNDi transactions",
	.usage = show_trans_usage,
};

static struct cw_clicmd  cli_show_entityid = {
	.cmda = { "dundi", "show", "entityid", NULL },
	.handler = dundi_show_entityid,
	.summary = "Display Global Entity ID",
	.usage = show_entityid_usage,
};

static struct cw_clicmd  cli_show_mappings = {
	.cmda = { "dundi", "show", "mappings", NULL },
	.handler = dundi_show_mappings,
	.summary = "Show DUNDi mappings",
	.usage = show_mappings_usage,
};

static struct cw_clicmd  cli_show_precache = {
	.cmda = { "dundi", "show", "precache", NULL },
	.handler = dundi_show_precache,
	.summary = "Show DUNDi precache",
	.usage = show_precache_usage,
};

static struct cw_clicmd  cli_show_requests = {
	.cmda = { "dundi", "show", "requests", NULL },
	.handler = dundi_show_requests,
	.summary = "Show DUNDi requests",
	.usage = show_requests_usage,
};

static struct cw_clicmd  cli_show_peer = {
	.cmda = { "dundi", "show", "peer", NULL },
	.handler = dundi_show_peer,
	.generator = complete_peer_4,
	.summary = "Show info on a specific DUNDi peer",
	.usage = show_peer_usage,
};

static struct cw_clicmd  cli_lookup = {
	.cmda = { "dundi", "lookup", NULL },
	.handler = dundi_do_lookup,
	.summary = "Lookup a number in DUNDi",
	.usage = lookup_usage,
};

static struct cw_clicmd  cli_precache = {
	.cmda = { "dundi", "precache", NULL },
	.handler = dundi_do_precache,
	.summary = "Precache a number in DUNDi",
	.usage = precache_usage,
};

static struct cw_clicmd  cli_queryeid = {
	.cmda = { "dundi", "query", NULL },
	.handler = dundi_do_query,
	.summary = "Query a DUNDi EID",
	.usage = query_usage,
};


static struct dundi_transaction *create_transaction(struct dundi_peer *p)
{
	struct dundi_transaction *trans;
	int tid;
	
	/* Don't allow creation of transactions to non-registered peers */
	if (p && !p->addr.sin_addr.s_addr)
		return NULL;
	tid = get_trans_id();
	if (tid < 1)
		return NULL;
	trans = malloc(sizeof(struct dundi_transaction));
	if (trans) {
		memset(trans, 0, sizeof(struct dundi_transaction));
		if (global_storehistory) {
			trans->start = cw_tvnow();
			cw_set_flag(trans, FLAG_STOREHIST);
		}
		trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
		cw_sched_state_init(&trans->autokillid);
		if (p) {
			apply_peer(trans, p);
			if (!p->sentfullkey)
				cw_set_flag(trans, FLAG_SENDFULLKEY);
		}
		trans->strans = tid;
		trans->allnext = alltrans;
		alltrans = trans;
	}
	return trans;
}

static int dundi_xmit(struct dundi_packet *pack)
{
	int res;
	char iabuf[INET_ADDRSTRLEN];
	if (dundidebug)
		dundi_showframe(pack->h, 0, &pack->parent->addr, pack->datalen - sizeof(struct dundi_hdr));
	res = sendto(netsocket, pack->data, pack->datalen, 0, (struct sockaddr *)&pack->parent->addr, sizeof(pack->parent->addr));
	if (res < 0) {
		cw_log(CW_LOG_WARNING, "Failed to transmit to '%s:%d': %s\n", 
			cw_inet_ntoa(iabuf, sizeof(iabuf), pack->parent->addr.sin_addr),
			ntohs(pack->parent->addr.sin_port), strerror(errno));
	}
	if (res > 0)
		res = 0;
	return res;
}

static void destroy_packet(struct dundi_packet *pack, int needfree)
{
	struct dundi_packet *prev, *cur;
	if (pack->parent) {
		prev = NULL;
		cur = pack->parent->packets;
		while(cur) {
			if (cur == pack) {
				if (prev)
					prev->next = cur->next;
				else
					pack->parent->packets = cur->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
	}
	cw_sched_del(sched, &pack->retransid);
	if (needfree)
		free(pack);
	else {
		pack->next = NULL;
	}
}

static void destroy_trans(struct dundi_transaction *trans, int fromtimeout)
{
	struct dundi_transaction *cur, *prev;
	struct dundi_peer *peer;
	int ms;
	int x;
	int cnt;
	char eid_str[20];
	if (cw_test_flag(trans, FLAG_ISREG | FLAG_ISQUAL | FLAG_STOREHIST)) {
		peer = peers;
		while (peer) {
			if (peer->regtrans == trans)
				peer->regtrans = NULL;
			if (peer->keypending == trans)
				peer->keypending = NULL;
			if (peer->qualtrans == trans) {
				if (fromtimeout) {
					if (peer->lastms > -1)
						cw_log(CW_LOG_NOTICE, "Peer '%s' has become UNREACHABLE!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
					peer->lastms = -1;
				} else {
					ms = cw_tvdiff_ms(cw_tvnow(), peer->qualtx);
					if (ms < 1)
						ms = 1;
					if (ms < peer->maxms) {
						if ((peer->lastms >= peer->maxms) || (peer->lastms < 0))
							cw_log(CW_LOG_NOTICE, "Peer '%s' has become REACHABLE!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
					} else if (peer->lastms < peer->maxms) {
						cw_log(CW_LOG_NOTICE, "Peer '%s' has become TOO LAGGED (%d ms)\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), ms);
					}
					peer->lastms = ms;
				}
				peer->qualtrans = NULL;
			}
			if (cw_test_flag(trans, FLAG_STOREHIST)) {
				if (trans->parent && !cw_strlen_zero(trans->parent->number)) {
					if (!dundi_eid_cmp(&trans->them_eid, &peer->eid)) {
						peer->avgms = 0;
						cnt = 0;
						free(peer->lookups[DUNDI_TIMING_HISTORY-1]);
						for (x=DUNDI_TIMING_HISTORY-1;x>0;x--) {
							peer->lookuptimes[x] = peer->lookuptimes[x-1];
							peer->lookups[x] = peer->lookups[x-1];
							if (peer->lookups[x]) {
								peer->avgms += peer->lookuptimes[x];
								cnt++;
							}
						}
						peer->lookuptimes[0] = cw_tvdiff_ms(cw_tvnow(), trans->start);
						peer->lookups[0] = malloc(strlen(trans->parent->number) + strlen(trans->parent->dcontext) + 2);
						if (peer->lookups[0]) {
							sprintf(peer->lookups[0], "%s@%s", trans->parent->number, trans->parent->dcontext);
							peer->avgms += peer->lookuptimes[0];
							cnt++;
						}
						if (cnt)
							peer->avgms /= cnt;
					}
				}
			}
			peer = peer->next;
		}
	}
	if (trans->parent) {
		/* Unlink from parent if appropriate */
		prev = NULL;
		cur = trans->parent->trans;
		while(cur) {
			if (cur == trans) {
				if (prev)
					prev->next = trans->next;
				else
					trans->parent->trans = trans->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		if (!trans->parent->trans) {
			/* Wake up sleeper */
			if (trans->parent->pfds[1] > -1) {
				write(trans->parent->pfds[1], "killa!", 6);
			}
		}
	}
	/* Unlink from all trans */
	prev = NULL;
	cur = alltrans;
	while(cur) {
		if (cur == trans) {
			if (prev)
				prev->allnext = trans->allnext;
			else
				alltrans = trans->allnext;
			break;
		}
		prev = cur;
		cur = cur->allnext;
	}
	destroy_packets(trans->packets);
	destroy_packets(trans->lasttrans);
	trans->packets = NULL;
	trans->lasttrans = NULL;
	cw_sched_del(sched, &trans->autokillid);
	if (trans->thread) {
		/* If used by a thread, mark as dead and be done */
		cw_set_flag(trans, FLAG_DEAD);
	} else
		free(trans);
}

static int dundi_rexmit(void *data)
{
	struct dundi_packet *pack;
	char iabuf[INET_ADDRSTRLEN];
	int res;
	cw_mutex_lock(&peerlock);
	pack = data;
	if (pack->retrans < 1) {
		if (!cw_test_flag(pack->parent, FLAG_ISQUAL))
			cw_log(CW_LOG_NOTICE, "Max retries exceeded to host '%s:%d' msg %d on call %d\n", 
				cw_inet_ntoa(iabuf, sizeof(iabuf), pack->parent->addr.sin_addr), 
				ntohs(pack->parent->addr.sin_port), pack->h->oseqno, ntohs(pack->h->strans));
		destroy_trans(pack->parent, 1);
		res = 0;
	} else {
		/* Decrement retransmission, try again */
		pack->retrans--;
		dundi_xmit(pack);
		res = 1;
	}
	cw_mutex_unlock(&peerlock);
	return res;
}

static int dundi_send(struct dundi_transaction *trans, int cmdresp, int flags, int final, struct dundi_ie_data *ied)
{
	struct dundi_packet *pack;
	int res;
	int len;
	char eid_str[20];
	len = sizeof(struct dundi_packet) + sizeof(struct dundi_hdr) + (ied ? ied->pos : 0);
	/* Reserve enough space for encryption */
	if (cw_test_flag(trans, FLAG_ENCRYPT))
		len += 384;
	pack = malloc(len);
	if (pack) {
		memset(pack, 0, len);
		pack->h = (struct dundi_hdr *)(pack->data);
		if (cmdresp != DUNDI_COMMAND_ACK) {
			cw_sched_add(sched, &pack->retransid, trans->retranstimer, dundi_rexmit, pack);
			pack->retrans = DUNDI_DEFAULT_RETRANS - 1;
			pack->next = trans->packets;
			trans->packets = pack;
		}
		pack->parent = trans;
		pack->h->strans = htons(trans->strans);
		pack->h->dtrans = htons(trans->dtrans);
		pack->h->iseqno = trans->iseqno;
		pack->h->oseqno = trans->oseqno;
		pack->h->cmdresp = cmdresp;
		pack->datalen = sizeof(struct dundi_hdr);
		if (ied) {
			memcpy(pack->h->ies, ied->buf, ied->pos);
			pack->datalen += ied->pos;
		} 
		if (final) {
			pack->h->cmdresp |= DUNDI_COMMAND_FINAL;
			cw_set_flag(trans, FLAG_FINAL);
		}
		pack->h->cmdflags = flags;
		if (cmdresp != DUNDI_COMMAND_ACK) {
			trans->oseqno++;
			trans->oseqno = trans->oseqno % 256;
		}
		trans->aseqno = trans->iseqno;
		/* If we have their public key, encrypt */
		if (cw_test_flag(trans, FLAG_ENCRYPT)) {
			switch(cmdresp) {
			case DUNDI_COMMAND_REGREQ:
			case DUNDI_COMMAND_REGRESPONSE:
			case DUNDI_COMMAND_DPDISCOVER:
			case DUNDI_COMMAND_DPRESPONSE:
			case DUNDI_COMMAND_EIDQUERY:
			case DUNDI_COMMAND_EIDRESPONSE:
			case DUNDI_COMMAND_PRECACHERQ:
			case DUNDI_COMMAND_PRECACHERP:
				if (dundidebug)
					dundi_showframe(pack->h, 2, &trans->addr, pack->datalen - sizeof(struct dundi_hdr));
				res = dundi_encrypt(trans, pack);
				break;
			default:
				res = 0;
			}
		} else 
			res = 0;
		if (!res) 
			res = dundi_xmit(pack);
		if (res)
			cw_log(CW_LOG_NOTICE, "Failed to send packet to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->them_eid));
				
		if (cmdresp == DUNDI_COMMAND_ACK)
			free(pack);
		return res;
	}
	return -1;
}

static int do_autokill(void *data)
{
	struct dundi_transaction *trans = data;
	char eid_str[20];
	cw_log(CW_LOG_NOTICE, "Transaction to '%s' took too long to ACK, destroying\n", 
		dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->them_eid));
	cw_mutex_lock(&peerlock);
	destroy_trans(trans, 0); /* We could actually set it to 1 instead of 0, but we won't ;-) */
	cw_mutex_unlock(&peerlock);
	return 0;
}

static void dundi_ie_append_eid_appropriately(struct dundi_ie_data *ied, char *context, dundi_eid *eid, dundi_eid *us)
{
	struct dundi_peer *p;
	if (!dundi_eid_cmp(eid, us)) {
		dundi_ie_append_eid(ied, DUNDI_IE_EID_DIRECT, eid);
		return;
	}
	cw_mutex_lock(&peerlock);
	p = peers;
	while(p) {
		if (!dundi_eid_cmp(&p->eid, eid)) {
			if (has_permission(p->include, context))
				dundi_ie_append_eid(ied, DUNDI_IE_EID_DIRECT, eid);
			else
				dundi_ie_append_eid(ied, DUNDI_IE_EID, eid);
			break;
		}
		p = p->next;
	}
	if (!p)
		dundi_ie_append_eid(ied, DUNDI_IE_EID, eid);
	cw_mutex_unlock(&peerlock);
}

static int dundi_discover(struct dundi_transaction *trans)
{
	struct dundi_ie_data ied;
	int x;
	if (!trans->parent) {
		cw_log(CW_LOG_WARNING, "Tried to discover a transaction with no parent?!?\n");
		return -1;
	}
	memset(&ied, 0, sizeof(ied));
	dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
	if (!dundi_eid_zero(&trans->us_eid))
		dundi_ie_append_eid(&ied, DUNDI_IE_EID_DIRECT, &trans->us_eid);
	for (x=0;x<trans->eidcount;x++)
		dundi_ie_append_eid_appropriately(&ied, trans->parent->dcontext, &trans->eids[x], &trans->us_eid);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_NUMBER, trans->parent->number);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
	dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
	if (trans->parent->cbypass)
		dundi_ie_append(&ied, DUNDI_IE_CACHEBYPASS);
	if (trans->autokilltimeout)
		cw_sched_add(sched, &trans->autokillid, trans->autokilltimeout, do_autokill, trans);
	return dundi_send(trans, DUNDI_COMMAND_DPDISCOVER, 0, 0, &ied);
}

static int precache_trans(struct dundi_transaction *trans, struct dundi_mapping *maps, int mapcount, int *minexp, int *foundanswers)
{
	struct dundi_result dr[MAX_RESULTS];
	struct dundi_hint_metadata hmd;
	struct dundi_ie_data ied;
	dundi_eid *avoid[1] = { NULL, };
	int x, res;
	int max = 999999;
	int expiration = dundi_cache_time;
	int ouranswers = 0;
	int direct[1] = { 0, };

	if (!trans->parent) {
		cw_log(CW_LOG_WARNING, "Tried to discover a transaction with no parent?!?\n");
		return -1;
	}

	memset(&hmd, 0, sizeof(hmd));
	memset(&dr, 0, sizeof(dr));
	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_init(&dr[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);

	/* Look up the answers we're going to include */
	for (x=0;x<mapcount;x++)
		ouranswers = dundi_lookup_local(dr, maps + x, trans->parent->number, &trans->us_eid, ouranswers, &hmd);
	if (ouranswers < 0)
		ouranswers = 0;
	for (x=0;x<ouranswers;x++) {
		if (dr[x].weight < max)
			max = dr[x].weight;
	}
	if (max) {
		/* If we do not have a canonical result, keep looking */
		res = dundi_lookup_internal(dr + ouranswers, MAX_RESULTS - ouranswers, NULL, trans->parent->dcontext, trans->parent->number, trans->ttl, 1, &hmd, &expiration, 0, 1, &trans->them_eid, avoid, direct);
		if (res > 0) {
			/* Append answer in result */
			ouranswers += res;
		}
	}
	
	if (ouranswers > 0) {
		*foundanswers += ouranswers;
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
		if (!dundi_eid_zero(&trans->us_eid))
			dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
		for (x=0;x<trans->eidcount;x++)
			dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->eids[x]);
		dundi_ie_append_str(&ied, DUNDI_IE_CALLED_NUMBER, trans->parent->number);
		dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
		dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
		for (x=0;x<ouranswers;x++) {
			/* Add answers */
			if (dr[x].expiration && (expiration > dr[x].expiration))
				expiration = dr[x].expiration;
			dundi_ie_append_answer(&ied, DUNDI_IE_ANSWER, &dr[x].eid, dr[x].techint, dr[x].flags, dr[x].weight, dr[x].dest.data);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, expiration);
		if (trans->autokilltimeout)
			cw_sched_add(sched, &trans->autokillid, trans->autokilltimeout, do_autokill, trans);
		if (expiration < *minexp)
			*minexp = expiration;
		res = dundi_send(trans, DUNDI_COMMAND_PRECACHERQ, 0, 0, &ied);
	} else {
		/* Oops, nothing to send... */
		destroy_trans(trans, 0);
		res = 0;
	}

	for (x = 0; x < arraysize(dr); x++)
		cw_dynstr_free(&dr[x].dest);

	return res;
}

static int dundi_query(struct dundi_transaction *trans)
{
	struct dundi_ie_data ied;
	int x;
	if (!trans->parent) {
		cw_log(CW_LOG_WARNING, "Tried to query a transaction with no parent?!?\n");
		return -1;
	}
	memset(&ied, 0, sizeof(ied));
	dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
	if (!dundi_eid_zero(&trans->us_eid))
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
	for (x=0;x<trans->eidcount;x++)
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->eids[x]);
	dundi_ie_append_eid(&ied, DUNDI_IE_REQEID, &trans->parent->query_eid);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
	dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
	if (trans->autokilltimeout)
		cw_sched_add(sched, &trans->autokillid, trans->autokilltimeout, do_autokill, trans);
	return dundi_send(trans, DUNDI_COMMAND_EIDQUERY, 0, 0, &ied);
}

static int discover_transactions(struct dundi_request *dr)
{
	struct dundi_transaction *trans;
	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		dundi_discover(trans);
		trans = trans->next;
	}
	cw_mutex_unlock(&peerlock);
	return 0;
}

static int precache_transactions(struct dundi_request *dr, struct dundi_mapping *maps, int mapcount, int *expiration, int *foundanswers)
{
	struct dundi_transaction *trans, *transn;
	/* Mark all as "in thread" so they don't disappear */
	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		if (trans->thread)
			cw_log(CW_LOG_WARNING, "This shouldn't happen, really...\n");
		trans->thread = 1;
		trans = trans->next;
	}
	cw_mutex_unlock(&peerlock);

	trans = dr->trans;
	while(trans) {
		if (!cw_test_flag(trans, FLAG_DEAD))
			precache_trans(trans, maps, mapcount, expiration, foundanswers);
		trans = trans->next;
	}

	/* Cleanup any that got destroyed in the mean time */
	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		transn = trans->next;
		trans->thread = 0;
		if (cw_test_flag(trans, FLAG_DEAD)) {
			cw_log(CW_LOG_DEBUG, "Our transaction went away!\n");
			destroy_trans(trans, 0);
		}
		trans = transn;
	}
	cw_mutex_unlock(&peerlock);
	return 0;
}

static int query_transactions(struct dundi_request *dr)
{
	struct dundi_transaction *trans;
	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		dundi_query(trans);
		trans = trans->next;
	}
	cw_mutex_unlock(&peerlock);
	return 0;
}

static int optimize_transactions(struct dundi_request *dr, int order)
{
	/* Minimize the message propagation through DUNDi by
	   alerting the network to hops which should be not be considered */
	struct dundi_transaction *trans;
	struct dundi_peer *peer;
	dundi_eid tmp;
	int x;
	int needpush;
	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		/* Pop off the true root */
		if (trans->eidcount) {
			tmp = trans->eids[--trans->eidcount];
			needpush = 1;
		} else {
			tmp = trans->us_eid;
			needpush = 0;
		}

		peer = peers;
		while(peer) {
			if (has_permission(peer->include, dr->dcontext) && 
			    dundi_eid_cmp(&peer->eid, &trans->them_eid) &&
				(peer->order <= order)) {
				/* For each other transaction, make sure we don't
				   ask this EID about the others if they're not
				   already in the list */
				if (!dundi_eid_cmp(&tmp, &peer->eid)) 
					x = -1;
				else {
					for (x=0;x<trans->eidcount;x++) {
						if (!dundi_eid_cmp(&trans->eids[x], &peer->eid))
							break;
					}
				}
				if (x == trans->eidcount) {
					/* Nope not in the list, if needed, add us at the end since we're the source */
					if (trans->eidcount < DUNDI_MAX_STACK - needpush) {
						trans->eids[trans->eidcount++] = peer->eid;
						/* Need to insert the real root (or us) at the bottom now as
						   a requirement now.  */
						needpush = 1;
					}
				}
			}
			peer = peer->next;
		}
		/* If necessary, push the true root back on the end */
		if (needpush)
			trans->eids[trans->eidcount++] = tmp;
		trans = trans->next;
	}
	cw_mutex_unlock(&peerlock);
	return 0;
}

static int append_transaction(struct dundi_request *dr, struct dundi_peer *p, int ttl, dundi_eid *avoid[])
{
	struct dundi_transaction *trans;
	int x;
	char eid_str[20];
	char eid_str2[20];
	/* Ignore if not registered */
	if (!p->addr.sin_addr.s_addr)
		return 0;
	if (p->maxms && ((p->lastms < 0) || (p->lastms >= p->maxms)))
		return 0;
	if (cw_strlen_zero(dr->number))
		cw_log(CW_LOG_DEBUG, "Will query peer '%s' for '%s' (context '%s')\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), dundi_eid_to_str(eid_str2, sizeof(eid_str2), &dr->query_eid), dr->dcontext);
	else
		cw_log(CW_LOG_DEBUG, "Will query peer '%s' for '%s@%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), dr->number, dr->dcontext);
	trans = create_transaction(p);
	if (!trans)
		return -1;
	trans->next = dr->trans;
	trans->parent = dr;
	trans->ttl = ttl;
	for (x=0;avoid[x] && (x <DUNDI_MAX_STACK);x++)
		trans->eids[x] = *avoid[x];
	trans->eidcount = x;
	dr->trans = trans;
	return 0;
}

static void cancel_request(struct dundi_request *dr)
{
	struct dundi_transaction *trans, *next;

	cw_mutex_lock(&peerlock);
	trans = dr->trans;
	
	while(trans) {
		next = trans->next;
		/* Orphan transaction from request */
		trans->parent = NULL;
		trans->next = NULL;
		/* Send final cancel */
		dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		trans = next;
	}
	cw_mutex_unlock(&peerlock);
}

static void abort_request(struct dundi_request *dr)
{
	cw_mutex_lock(&peerlock);
	while(dr->trans) 
		destroy_trans(dr->trans, 0);
	cw_mutex_unlock(&peerlock);
}

static void build_transactions(struct dundi_request *dr, int ttl, int order, int *foundcache, int *skipped, int blockempty, int nocache, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int directs[])
{
	struct dundi_peer *p;
	int x;
	int res;
	int pass;
	int allowconnect;
	char eid_str[20];
	cw_mutex_lock(&peerlock);
	p = peers;
	while(p) {
		if (modeselect == 1) {
			/* Send the precache to push upstreams only! */
			pass = has_permission(p->permit, dr->dcontext) && (p->pcmodel & DUNDI_MODEL_OUTBOUND);
			allowconnect = 1;
		} else {
			/* Normal lookup / EID query */
			pass = has_permission(p->include, dr->dcontext);
			allowconnect = p->model & DUNDI_MODEL_OUTBOUND;
		}
		if (skip) {
			if (!dundi_eid_cmp(skip, &p->eid))
				pass = 0;
		}
		if (pass) {
			if (p->order <= order) {
				/* Check order first, then check cache, regardless of
				   omissions, this gets us more likely to not have an
				   affected answer. */
				if((nocache || !(res = cache_lookup(dr, &p->eid, dr->crc32, &dr->expiration)))) {
					res = 0;
					/* Make sure we haven't already seen it and that it won't
					   affect our answer */
					for (x=0;avoid[x];x++) {
						if (!dundi_eid_cmp(avoid[x], &p->eid) || !dundi_eid_cmp(avoid[x], &p->us_eid)) {
							/* If not a direct connection, it affects our answer */
							if (directs && !directs[x]) 
								cw_clear_flag_nonstd(dr->hmd, DUNDI_HINT_UNAFFECTED);
							break;
						}
					}
					/* Make sure we can ask */
					if (allowconnect) {
						if (!avoid[x] && (!blockempty || !dundi_eid_zero(&p->us_eid))) {
							/* Check for a matching or 0 cache entry */
							append_transaction(dr, p, ttl, avoid);
						} else
							cw_log(CW_LOG_DEBUG, "Avoiding '%s' in transaction\n", dundi_eid_to_str(eid_str, sizeof(eid_str), avoid[x]));
					}
				}
				*foundcache |= res;
			} else if (!*skipped || (p->order < *skipped))
				*skipped = p->order;
		}
		p = p->next;
	}
	cw_mutex_unlock(&peerlock);
}

static int register_request(struct dundi_request *dr, struct dundi_request **pending)
{
	struct dundi_request *cur;
	int res=0;
	char eid_str[20];
	cw_mutex_lock(&peerlock);
	cur = requests;
	while(cur) {
		if (option_debug)
			cw_log(CW_LOG_DEBUG, "Checking '%s@%s' vs '%s@%s'\n", cur->dcontext, cur->number,
				dr->dcontext, dr->number);
		if (!strcasecmp(cur->dcontext, dr->dcontext) &&
		    !strcasecmp(cur->number, dr->number) &&
			(!dundi_eid_cmp(&cur->root_eid, &dr->root_eid) || (cur->crc32 == dr->crc32))) {
				cw_log(CW_LOG_DEBUG, "Found existing query for '%s@%s' for '%s' crc '%08lx'\n", 
					cur->dcontext, cur->number, dundi_eid_to_str(eid_str, sizeof(eid_str), &cur->root_eid), cur->crc32);
				*pending = cur;
			res = 1;
			break;
		}
		cur = cur->next;
	}
	if (!res) {
		cw_log(CW_LOG_DEBUG, "Registering request for '%s@%s' on behalf of '%s' crc '%08lx'\n", 
				dr->number, dr->dcontext, dundi_eid_to_str(eid_str, sizeof(eid_str), &dr->root_eid), dr->crc32);
		/* Go ahead and link us in since nobody else is searching for this */
		dr->next = requests;
		requests = dr;
		*pending = NULL;
	}
	cw_mutex_unlock(&peerlock);
	return res;
}

static void unregister_request(struct dundi_request *dr)
{
	struct dundi_request *cur, *prev;
	cw_mutex_lock(&peerlock);
	prev = NULL;
	cur = requests;
	while(cur) {
		if (cur == dr) {
			if (prev)
				prev->next = cur->next;
			else
				requests = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	cw_mutex_unlock(&peerlock);
}

static int check_request(struct dundi_request *dr)
{
	struct dundi_request *cur;
	int res = 0;
	cw_mutex_lock(&peerlock);
	cur = requests;
	while(cur) {
		if (cur == dr) {
			res = 1;
			break;
		}
		cur = cur->next;
	}
	cw_mutex_unlock(&peerlock);
	return res;
}

static unsigned long avoid_crc32(dundi_eid *avoid[])
{
	/* Idea is that we're calculating a checksum which is independent of
	   the order that the EID's are listed in */
	unsigned long acrc32 = 0;
	int x;
	for (x=0;avoid[x];x++) {
		/* Order doesn't matter */
		if (avoid[x+1]) {
			acrc32 ^= crc32(0L, (unsigned char *)avoid[x], sizeof(dundi_eid));
		}
	}
	return acrc32;
}

static int dundi_lookup_internal(struct dundi_result *result, int maxret, struct cw_channel *chan, const char *dcontext, const char *number, int ttl, int blockempty, struct dundi_hint_metadata *hmd, int *expiration, int cbypass, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int direct[])
{
	int res;
	struct dundi_request dr, *pending;
	dundi_eid *rooteid=NULL;
	int x;
	int ttlms;
	int ms;
	int foundcache;
	int skipped=0;
	int order=0;
	char eid_str[20];
	struct timeval start;
	
	/* Don't do anthing for a hungup channel */
	if (chan && chan->_softhangup)
		return 0;

	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;

	for (x=0;avoid[x];x++)
		rooteid = avoid[x];
	/* Now perform real check */
	memset(&dr, 0, sizeof(dr));
	if (pipe(dr.pfds)) {
		cw_log(CW_LOG_WARNING, "pipe failed: %s\n" , strerror(errno));
		return -1;
	}
	dr.dr = result;
	dr.hmd = hmd;
	dr.maxcount = maxret;
	dr.expiration = *expiration;
	dr.cbypass = cbypass;
	dr.crc32 = avoid_crc32(avoid);
	cw_copy_string(dr.dcontext, dcontext ? dcontext : "e164", sizeof(dr.dcontext));
	cw_copy_string(dr.number, number, sizeof(dr.number));
	if (rooteid)
		dr.root_eid = *rooteid;
	res = register_request(&dr, &pending);
	if (res) {
		/* Already a request */
		if (rooteid && !dundi_eid_cmp(&dr.root_eid, &pending->root_eid)) {
			/* This is on behalf of someone else.  Go ahead and close this out since
			   they'll get their answer anyway. */
			cw_log(CW_LOG_DEBUG, "Oooh, duplicate request for '%s@%s' for '%s'\n",
				dr.number,dr.dcontext,dundi_eid_to_str(eid_str, sizeof(eid_str), &dr.root_eid));
			close(dr.pfds[0]);
			close(dr.pfds[1]);
			return -2;
		} else {
			/* Wait for the cache to populate */
			cw_log(CW_LOG_DEBUG, "Waiting for similar request for '%s@%s' for '%s'\n",
				dr.number,dr.dcontext,dundi_eid_to_str(eid_str, sizeof(eid_str), &pending->root_eid));
			start = cw_tvnow();
			while(check_request(pending) && (cw_tvdiff_ms(cw_tvnow(), start) < ttlms) && (!chan || !chan->_softhangup)) {
				/* XXX Would be nice to have a way to poll/select here XXX */
				/* XXX this is a busy wait loop!!! */
				usleep(1);
			}
			/* Continue on as normal, our cache should kick in */
		}
	}
	/* Create transactions */
	do {
		order = skipped;
		skipped = 0;
		foundcache = 0;
		build_transactions(&dr, ttl, order, &foundcache, &skipped, blockempty, cbypass, modeselect, skip, avoid, direct);
	} while (skipped && !foundcache && !dr.trans);
	/* If no TTL, abort and return 0 now after setting TTL expired hint.  Couldn't
	   do this earlier because we didn't know if we were going to have transactions
	   or not. */
	if (!ttl) {
		cw_set_flag_nonstd(hmd, DUNDI_HINT_TTL_EXPIRED);
		abort_request(&dr);
		unregister_request(&dr);
		close(dr.pfds[0]);
		close(dr.pfds[1]);
		return 0;
	}
		
	/* Optimize transactions */
	optimize_transactions(&dr, order);
	/* Actually perform transactions */
	discover_transactions(&dr);
	/* Wait for transaction to come back */
	start = cw_tvnow();
	while(dr.trans && (cw_tvdiff_ms(cw_tvnow(), start) < ttlms) && (!chan || !chan->_softhangup)) {
		ms = 100;
		cw_waitfor_n_fd(dr.pfds, 1, &ms, NULL);
	}
	if (chan && chan->_softhangup)
		cw_log(CW_LOG_DEBUG, "Hrm, '%s' hungup before their query for %s@%s finished\n", chan->name, dr.number, dr.dcontext);
	cancel_request(&dr);
	unregister_request(&dr);
	res = dr.respcount;
	*expiration = dr.expiration;
	close(dr.pfds[0]);
	close(dr.pfds[1]);
	return res;
}

int dundi_lookup(struct dundi_result *result, int maxret, struct cw_channel *chan, const char *dcontext, const char *number, int cbypass)
{
	struct dundi_hint_metadata hmd;
	dundi_eid *avoid[1] = { NULL, };
	int direct[1] = { 0, };
	int expiration = dundi_cache_time;

	memset(&hmd, 0, sizeof(hmd));
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;

	return dundi_lookup_internal(result, maxret, chan, dcontext, number, dundi_ttl, 0, &hmd, &expiration, cbypass, 0, NULL, avoid, direct);
}

static void reschedule_precache(const char *number, const char *context, int expiration)
{
	int len;
	struct dundi_precache_queue *qe, *prev=NULL;
	cw_mutex_lock(&pclock);
	qe = pcq;
	while(qe) {
		if (!strcmp(number, qe->number) && !strcasecmp(context, qe->context)) {
			if (prev)
				prev->next = qe->next;
			else
				pcq = qe->next;
			qe->next = NULL;
			break;
		}
		prev = qe;
		qe = qe->next;
	};
	if (!qe) {
		len = sizeof(struct dundi_precache_queue);
		len += strlen(number) + 1;
		len += strlen(context) + 1;
		qe = malloc(len);
		if (qe) {
			memset(qe, 0, len);
			strcpy(qe->number, number);
			qe->context = qe->number + strlen(number) + 1;
			strcpy(qe->context, context);
		}
	}
	time(&qe->expiration);
	qe->expiration += expiration;
	prev = pcq;
	if (prev) {
		while(prev->next && (prev->next->expiration <= qe->expiration))
			prev = prev->next;
		qe->next = prev->next;
		prev->next = qe;
	} else
		pcq = qe;
	cw_mutex_unlock(&pclock);
	
}

static void dundi_precache_full(void)
{
	struct dundi_mapping *cur;
	struct cw_context *con;
	struct cw_exten *e;
	cur = mappings;
	while(cur) {
		cw_log(CW_LOG_NOTICE, "Should precache context '%s'\n", cur->dcontext);
		cw_lock_contexts();
		con = cw_walk_contexts(NULL);
		while(con) {
			if (!strcasecmp(cur->lcontext, cw_get_context_name(con))) {
				/* Found the match, now queue them all up */
				cw_lock_context(con);
				e = cw_walk_context_extensions(con, NULL);
				while(e) {
					reschedule_precache(cw_get_extension_name(e), cur->dcontext, 0);
					e = cw_walk_context_extensions(con, e);
				}
				cw_unlock_context(con);
			}
			con = cw_walk_contexts(con);
		}
		cw_unlock_contexts();
		cur = cur->next;
	}
}

static int dundi_precache_internal(const char *context, const char *number, int ttl, dundi_eid *avoids[])
{
	struct dundi_result dr2[MAX_RESULTS];
	struct dundi_request dr;
	struct dundi_hint_metadata hmd;
	struct timeval start;
	struct dundi_mapping *maps=NULL, *cur;
	int nummaps;
	int foundanswers;
	int foundcache, skipped, ttlms, ms, x;

	if (!context)
		context = "e164";

	cw_log(CW_LOG_DEBUG, "Precache internal (%s@%s)!\n", number, context);

	memset(&hmd, 0, sizeof(hmd));
	memset(&dr2, 0, sizeof(dr2));
	for (x = 0; x < arraysize(dr2); x++)
		cw_dynstr_init(&dr2[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);
	memset(&dr, 0, sizeof(dr));

	cw_mutex_lock(&peerlock);
	nummaps = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, context))
			nummaps++;
		cur = cur->next;
	}
	if (nummaps) {
		maps = alloca(nummaps * sizeof(struct dundi_mapping));
		nummaps = 0;
		cur = mappings;
		while(cur) {
			if (!strcasecmp(cur->dcontext, context))
				maps[nummaps++] = *cur;
			cur = cur->next;
		}
	}
	cw_mutex_unlock(&peerlock);
	if (!nummaps || !maps)
		return -1;
	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;
	dr.dr = dr2;
	cw_copy_string(dr.number, number, sizeof(dr.number));
	cw_copy_string(dr.dcontext, context ? context : "e164", sizeof(dr.dcontext));
	dr.maxcount = MAX_RESULTS;
	dr.expiration = dundi_cache_time;
	dr.hmd = &hmd;
	dr.pfds[0] = dr.pfds[1] = -1;
	pipe(dr.pfds);
	build_transactions(&dr, ttl, 0, &foundcache, &skipped, 0, 1, 1, NULL, avoids, NULL);
	optimize_transactions(&dr, 0);
	foundanswers = 0;
	precache_transactions(&dr, maps, nummaps, &dr.expiration, &foundanswers);
	if (foundanswers) {
		if (dr.expiration > 0) 
			reschedule_precache(dr.number, dr.dcontext, dr.expiration);
		else
			cw_log(CW_LOG_NOTICE, "Weird, expiration = %d, but need to precache for %s@%s?!\n", dr.expiration, dr.number, dr.dcontext);
	}
	start = cw_tvnow();
	while(dr.trans && (cw_tvdiff_ms(cw_tvnow(), start) < ttlms)) {
		if (dr.pfds[0] > -1) {
			ms = 100;
			cw_waitfor_n_fd(dr.pfds, 1, &ms, NULL);
		} else
			usleep(1);
	}
	cancel_request(&dr);
	if (dr.pfds[0] > -1) {
		close(dr.pfds[0]);
		close(dr.pfds[1]);
	}

	for (x = 0; x < arraysize(dr2); x++)
		cw_dynstr_free(&dr2[x].dest);

	return 0;
}

int dundi_precache(const char *context, const char *number)
{
	dundi_eid *avoid[1] = { NULL, };
	return dundi_precache_internal(context, number, dundi_ttl, avoid);
}

static int dundi_query_eid_internal(struct dundi_entity_info *dei, const char *dcontext, dundi_eid *eid, struct dundi_hint_metadata *hmd, int ttl, int blockempty, dundi_eid *avoid[])
{
	int res;
	struct dundi_request dr;
	dundi_eid *rooteid=NULL;
	int x;
	int ttlms;
	int skipped=0;
	int foundcache=0;
	struct timeval start;
	
	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;

	for (x=0;avoid[x];x++)
		rooteid = avoid[x];
	/* Now perform real check */
	memset(&dr, 0, sizeof(dr));
	dr.hmd = hmd;
	dr.dei = dei;
	dr.pfds[0] = dr.pfds[1] = -1;
	cw_copy_string(dr.dcontext, dcontext ? dcontext : "e164", sizeof(dr.dcontext));
	memcpy(&dr.query_eid, eid, sizeof(dr.query_eid));
	if (rooteid)
		dr.root_eid = *rooteid;
	/* Create transactions */
	build_transactions(&dr, ttl, 9999, &foundcache, &skipped, blockempty, 0, 0, NULL, avoid, NULL);

	/* If no TTL, abort and return 0 now after setting TTL expired hint.  Couldn't
	   do this earlier because we didn't know if we were going to have transactions
	   or not. */
	if (!ttl) {
		cw_set_flag_nonstd(hmd, DUNDI_HINT_TTL_EXPIRED);
		return 0;
	}
		
	/* Optimize transactions */
	optimize_transactions(&dr, 9999);
	/* Actually perform transactions */
	query_transactions(&dr);
	/* Wait for transaction to come back */
	start = cw_tvnow();
	while(dr.trans && (cw_tvdiff_ms(cw_tvnow(), start) < ttlms))
		usleep(1);
	res = dr.respcount;
	return res;
}

int dundi_query_eid(struct dundi_entity_info *dei, const char *dcontext, dundi_eid eid)
{
	dundi_eid *avoid[1] = { NULL, };
	struct dundi_hint_metadata hmd;
	memset(&hmd, 0, sizeof(hmd));
	return dundi_query_eid_internal(dei, dcontext, &eid, &hmd, dundi_ttl, 0, avoid);
}


static int dundifunc_read(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	static int deprecated_app = 0;
	static int deprecated_jump = 0;
	struct dundi_result dr[MAX_RESULTS];
	const char *context;
	struct localuser *u;
	int results;
	int x;
	int bypass = 0;

	if (argc < 1 || argc > 3 || !argv[0][0])
		return cw_function_syntax(dundifunc_syntax);

	if (!result)
		return 0;

	LOCAL_USER_ADD(u);

	context = (argc > 1 && argv[1][0] ? argv[1] : "e164");

	if (argc > 2) {
		for (; argv[2]; argv[2]++) {
			switch (argv[2][0]) {
				case 'b': bypass = 1; break;
			}
		}
	}

	results = dundi_lookup(dr, MAX_RESULTS, NULL, context, argv[0], bypass);
	if (results > 0) {
		sort_results(dr, results);
		for (x = 0; x < results; x++) {
			if (cw_test_flag(dr + x, DUNDI_FLAG_EXISTS)) {
				if (result) {
					cw_dynstr_printf(result, "%s/%s", dr[x].tech, dr[x].dest.data);
				} else {
					/* DEPRECATED
					 * When used as an app rather than a func we return
					 * the result in variables
					 */
					if (!deprecated_app) {
						deprecated_app = 1;
						cw_log(CW_LOG_WARNING, "%s with no return is deprecated. Use Set(varname=${%s(args)}) instead\n", dundifunc_name, dundifunc_name);
					}
					pbx_builtin_setvar_helper(chan, "DUNDTECH", dr[x].tech);
					pbx_builtin_setvar_helper(chan, "DUNDDEST", dr[x].dest.data);
				}
				break;
			}
		}
	} else if (!result && option_priority_jumping) {
		/* DEPRECATED
		 * When used as an app rather than a func we return
		 * the result in variables
		 */
		if (!deprecated_app) {
			deprecated_app = 1;
			cw_log(CW_LOG_WARNING, "%s with no return is deprecated. Use Set(varname=${%s(args)}) instead\n", dundifunc_name, dundifunc_name);
		}
		/* DEPRECATED
		 * When used as an app rather than a func we use
		 * priority jumping (if enabled) on error
		 */
		if (!deprecated_jump) {
			deprecated_jump = 1;
			cw_log(CW_LOG_WARNING, "Priority jumping is deprecated. Use Set(varname=${%s(args)}) and test ${varname} instead\n", dundifunc_name);
		}
		cw_goto_if_exists_n(chan, chan->context, chan->exten, chan->priority + 101);
	}

	LOCAL_USER_REMOVE(u);
	return 0;
}

static void mark_peers(void)
{
	struct dundi_peer *peer;
	cw_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		peer->dead = 1;
		peer = peer->next;
	}
	cw_mutex_unlock(&peerlock);
}

static void mark_mappings(void)
{
	struct dundi_mapping *map;
	cw_mutex_lock(&peerlock);
	map = mappings;
	while(map) {
		map->dead = 1;
		map = map->next;
	}
	cw_mutex_unlock(&peerlock);
}

static void destroy_permissions(struct permission *p)
{
	struct permission *prev;
	while(p) {
		prev = p;
		p = p->next;
		free(prev);
	}
}

static void destroy_peer(struct dundi_peer *peer)
{
	cw_sched_del(sched, &peer->registerid);
	if (peer->regtrans)
		destroy_trans(peer->regtrans, 0);
	if (peer->keypending)
		destroy_trans(peer->keypending, 0);
	cw_sched_del(sched, &peer->qualifyid);
	destroy_permissions(peer->permit);
	destroy_permissions(peer->include);
	free(peer);
}

static void destroy_map(struct dundi_mapping *map)
{
	free(map);
}

static void prune_peers(void)
{
	struct dundi_peer *peer, *prev, *next;
	cw_mutex_lock(&peerlock);
	peer = peers;
	prev = NULL;
	while(peer) {
		next = peer->next;
		if (peer->dead) {
			if (prev)
				prev->next = peer->next;
			else
				peers = peer->next;
			destroy_peer(peer);
		} else
			prev = peer;
		peer = next;
	}
	cw_mutex_unlock(&peerlock);
}

static void prune_mappings(void)
{
	struct dundi_mapping *map, *prev, *next;
	cw_mutex_lock(&peerlock);
	map = mappings;
	prev = NULL;
	while(map) {
		next = map->next;
		if (map->dead) {
			if (prev)
				prev->next = map->next;
			else
				mappings = map->next;
			destroy_map(map);
		} else
			prev = map;
		map = next;
	}
	cw_mutex_unlock(&peerlock);
}

static struct permission *append_permission(struct permission *p, char *s, int allow)
{
	struct permission *start;
	start = p;
	if (p) {
		while(p->next)
			p = p->next;
	}
	if (p) {
		p->next = malloc(sizeof(struct permission) + strlen(s) + 1);
		p = p->next;
	} else {
		p = malloc(sizeof(struct permission) + strlen(s) + 1);
	}
	if (p) {
		memset(p, 0, sizeof(struct permission));
		memcpy(p->name, s, strlen(s) + 1);
		p->allow = allow;
	}
	return start ? start : p;
}

#define MAX_OPTS 128

static void build_mapping(char *name, char *value)
{
	char *t, *fields[MAX_OPTS];
	struct dundi_mapping *map;
	int x;
	int y;
	t = cw_strdupa(value);
	map = mappings;
	while(map) {
		/* Find a double match */
		if (!strcasecmp(map->dcontext, name) && 
			(!strncasecmp(map->lcontext, value, strlen(map->lcontext)) && 
			  (!value[strlen(map->lcontext)] || 
			   (value[strlen(map->lcontext)] == ','))))
			break;
		map = map->next;
	}
	if (!map) {
		map = malloc(sizeof(struct dundi_mapping));
		if (map) {
			memset(map, 0, sizeof(struct dundi_mapping));
			map->next = mappings;
			mappings = map;
			map->dead = 1;
		}
	}
	if (map) {
		map->options = 0;
		memset(fields, 0, sizeof(fields));
		x = 0;
		while(t && x < MAX_OPTS) {
			fields[x++] = t;
			t = strchr(t, ',');
			if (t) {
				*t = '\0';
				t++;
			}
		} /* Russell was here, arrrr! */
		if ((x == 1) && cw_strlen_zero(fields[0])) {
			/* Placeholder mapping */
			cw_copy_string(map->dcontext, name, sizeof(map->dcontext));
			map->dead = 0;
		} else if (x >= 4) {
			cw_copy_string(map->dcontext, name, sizeof(map->dcontext));
			cw_copy_string(map->lcontext, fields[0], sizeof(map->lcontext));
			if ((sscanf(fields[1], "%d", &map->weight) == 1) && (map->weight >= 0) && (map->weight < 60000)) {
				cw_copy_string(map->dest, fields[3], sizeof(map->dest));
				if ((map->tech = str2tech(fields[2]))) {
					map->dead = 0;
				}
			} else {
				cw_log(CW_LOG_WARNING, "Invalid weight '%s' specified, deleting entry '%s/%s'\n", fields[1], map->dcontext, map->lcontext);
			}
			for (y=4;y<x;y++) {
				if (!strcasecmp(fields[y], "nounsolicited"))
					map->options |= DUNDI_FLAG_NOUNSOLICITED;
				else if (!strcasecmp(fields[y], "nocomunsolicit"))
					map->options |= DUNDI_FLAG_NOCOMUNSOLICIT;
				else if (!strcasecmp(fields[y], "residential"))
					map->options |= DUNDI_FLAG_RESIDENTIAL;
				else if (!strcasecmp(fields[y], "commercial"))
					map->options |= DUNDI_FLAG_COMMERCIAL;
				else if (!strcasecmp(fields[y], "mobile"))
					map->options |= DUNDI_FLAG_MOBILE;
				else if (!strcasecmp(fields[y], "nopartial"))
					map->options |= DUNDI_FLAG_INTERNAL_NOPARTIAL;
				else
					cw_log(CW_LOG_WARNING, "Don't know anything about option '%s'\n", fields[y]);
			}
		} else 
			cw_log(CW_LOG_WARNING, "Expected at least %d arguments in map, but got only %d\n", 4, x);
	}
}

static int do_register(void *data)
{
	struct dundi_ie_data ied;
	struct dundi_peer *peer = data;
	char eid_str[20];
	char eid_str2[20];

	cw_mutex_lock(&peerlock);

	cw_log(CW_LOG_DEBUG, "Register us as '%s' to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->us_eid), dundi_eid_to_str(eid_str2, sizeof(eid_str2), &peer->eid));
	cw_sched_add(sched, &peer->registerid, default_expiration * 1000, do_register, data);
	/* Destroy old transaction if there is one */
	if (peer->regtrans)
		destroy_trans(peer->regtrans, 0);
	peer->regtrans = create_transaction(peer);
	if (peer->regtrans) {
		cw_set_flag(peer->regtrans, FLAG_ISREG);
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &peer->regtrans->us_eid);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, default_expiration);
		dundi_send(peer->regtrans, DUNDI_COMMAND_REGREQ, 0, 0, &ied);
		
	} else
		cw_log(CW_LOG_NOTICE, "Unable to create new transaction for registering to '%s'!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));

	cw_mutex_unlock(&peerlock);

	return 0;
}

static int do_qualify(void *data)
{
	struct dundi_peer *peer;
	peer = data;
	cw_mutex_lock(&peerlock);
	qualify_peer(peer, 0);
	cw_mutex_unlock(&peerlock);
	return 0;
}

static void qualify_peer(struct dundi_peer *peer, int schedonly)
{
	int when;

	cw_mutex_lock(&peerlock);

	cw_sched_del(sched, &peer->qualifyid);
	if (peer->qualtrans)
		destroy_trans(peer->qualtrans, 0);
	peer->qualtrans = NULL;
	if (peer->maxms > 0) {
		when = 60000;
		if (peer->lastms < 0)
			when = 10000;
		if (schedonly)
			when = 5000;
		cw_sched_add(sched, &peer->qualifyid, when, do_qualify, peer);
		if (!schedonly)
			peer->qualtrans = create_transaction(peer);
		if (peer->qualtrans) {
			peer->qualtx = cw_tvnow();
			cw_set_flag(peer->qualtrans, FLAG_ISQUAL);
			dundi_send(peer->qualtrans, DUNDI_COMMAND_NULL, 0, 1, NULL);
		}
	}

	cw_mutex_unlock(&peerlock);

}
static void populate_addr(struct dundi_peer *peer, dundi_eid *eid)
{
	char eid_str[20];
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	char *c;
	int port, expire;

	dundi_eid_to_str(eid_str, sizeof(eid_str), eid);
	if (!cw_db_get("dundi/dpeers", eid_str, &ds)) {
		c = strchr(ds.data, ':');
		if (c) {
			*c = '\0';
			c++;
			if (sscanf(c, "%d:%d", &port, &expire) == 2) {
				/* Got it! */
				inet_aton(ds.data, &peer->addr.sin_addr);
				peer->addr.sin_family = AF_INET;
				peer->addr.sin_port = htons(port);
				cw_sched_add(sched, &peer->registerexpire, (expire + 10) * 1000, do_register_expire, peer);
			}
		}

		cw_dynstr_free(&ds);
	}
}


static void build_peer(dundi_eid *eid, struct cw_variable *v, int *globalpcmode)
{
	struct dundi_peer *peer;
	struct cw_hostent he;
	struct hostent *hp;
	dundi_eid testeid;
	int needregister=0;
	char eid_str[20];

	cw_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		if (!dundi_eid_cmp(&peer->eid, eid)) {	
			break;
		}
		peer = peer->next;
	}
	if (!peer) {
		/* Add us into the list */
		peer = malloc(sizeof(struct dundi_peer));
		if (peer) {
			memset(peer, 0, sizeof(struct dundi_peer));
			cw_sched_state_init(&peer->registerid);
			cw_sched_state_init(&peer->registerexpire);
			cw_sched_state_init(&peer->qualifyid);
			peer->addr.sin_family = AF_INET;
			peer->addr.sin_port = htons(DUNDI_PORT);
			populate_addr(peer, eid);
			peer->next = peers;
			peers = peer;
		}
	}
	if (peer) {
		peer->dead = 0;
		peer->eid = *eid;
		peer->us_eid = global_eid;
		destroy_permissions(peer->permit);
		destroy_permissions(peer->include);
		peer->permit = NULL;
		peer->include = NULL;
		cw_sched_del(sched, &peer->registerid);
		while(v) {
			if (!strcasecmp(v->name, "inkey")) {
				cw_copy_string(peer->inkey, v->value, sizeof(peer->inkey));
			} else if (!strcasecmp(v->name, "outkey")) {
				cw_copy_string(peer->outkey, v->value, sizeof(peer->outkey));
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					peer->dynamic = 1;
				} else {
					hp = cw_gethostbyname(v->value, &he);
					if (hp) {
						memcpy(&peer->addr.sin_addr, hp->h_addr, sizeof(peer->addr.sin_addr));
						peer->dynamic = 0;
					} else {
						cw_log(CW_LOG_WARNING, "Unable to find host '%s' at line %d\n", v->value, v->lineno);
						peer->dead = 1;
					}
				}
			} else if (!strcasecmp(v->name, "ustothem")) {
				if (!dundi_str_to_eid(&testeid, v->value))
					peer->us_eid = testeid;
				else
					cw_log(CW_LOG_WARNING, "'%s' is not a valid DUNDi Entity Identifier at line %d\n", v->value, v->lineno);
			} else if (!strcasecmp(v->name, "include")) {
				peer->include = append_permission(peer->include, v->value, 1);
			} else if (!strcasecmp(v->name, "permit")) {
				peer->permit = append_permission(peer->permit, v->value, 1);
			} else if (!strcasecmp(v->name, "noinclude")) {
				peer->include = append_permission(peer->include, v->value, 0);
			} else if (!strcasecmp(v->name, "deny")) {
				peer->permit = append_permission(peer->permit, v->value, 0);
			} else if (!strcasecmp(v->name, "register")) {
				needregister = cw_true(v->value);
			} else if (!strcasecmp(v->name, "order")) {
				if (!strcasecmp(v->value, "primary"))
					peer->order = 0;
				else if (!strcasecmp(v->value, "secondary"))
					peer->order = 1;
				else if (!strcasecmp(v->value, "tertiary"))
					peer->order = 2;
				else if (!strcasecmp(v->value, "quartiary"))
					peer->order = 3;
				else {
					cw_log(CW_LOG_WARNING, "'%s' is not a valid order, should be primary, secondary, tertiary or quartiary at line %d\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%d", &peer->maxms) != 1) {
					cw_log(CW_LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of dundi.conf\n", 
						dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), v->lineno);
					peer->maxms = 0;
				}
			} else if (!strcasecmp(v->name, "model")) {
				if (!strcasecmp(v->value, "inbound"))
					peer->model = DUNDI_MODEL_INBOUND;
				else if (!strcasecmp(v->value, "outbound")) 
					peer->model = DUNDI_MODEL_OUTBOUND;
				else if (!strcasecmp(v->value, "symmetric"))
					peer->model = DUNDI_MODEL_SYMMETRIC;
				else if (!strcasecmp(v->value, "none"))
					peer->model = 0;
				else {
					cw_log(CW_LOG_WARNING, "Unknown model '%s', should be 'none', 'outbound', 'inbound', or 'symmetric' at line %d\n", 
						v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "precache")) {
				if (!strcasecmp(v->value, "inbound"))
					peer->pcmodel = DUNDI_MODEL_INBOUND;
				else if (!strcasecmp(v->value, "outbound")) 
					peer->pcmodel = DUNDI_MODEL_OUTBOUND;
				else if (!strcasecmp(v->value, "symmetric"))
					peer->pcmodel = DUNDI_MODEL_SYMMETRIC;
				else if (!strcasecmp(v->value, "none"))
					peer->pcmodel = 0;
				else {
					cw_log(CW_LOG_WARNING, "Unknown pcmodel '%s', should be 'none', 'outbound', 'inbound', or 'symmetric' at line %d\n", 
						v->value, v->lineno);
				}
			}
			v = v->next;
		}
		(*globalpcmode) |= peer->pcmodel;
		if (!peer->model && !peer->pcmodel) {
			cw_log(CW_LOG_WARNING, "Peer '%s' lacks a model or pcmodel, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if ((peer->model & DUNDI_MODEL_INBOUND) && (peer->pcmodel & DUNDI_MODEL_OUTBOUND)) {
			cw_log(CW_LOG_WARNING, "Peer '%s' may not be both inbound/symmetric model and outbound/symmetric precache model, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if ((peer->model & DUNDI_MODEL_OUTBOUND) && (peer->pcmodel & DUNDI_MODEL_INBOUND)) {
			cw_log(CW_LOG_WARNING, "Peer '%s' may not be both outbound/symmetric model and inbound/symmetric precache model, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if (peer->include && !(peer->model & DUNDI_MODEL_OUTBOUND) && !(peer->pcmodel & DUNDI_MODEL_INBOUND)) {
			cw_log(CW_LOG_WARNING, "Peer '%s' is supposed to be included in outbound searches but isn't an outbound peer or inbound precache!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		} else if (peer->permit && !(peer->model & DUNDI_MODEL_INBOUND) && !(peer->pcmodel & DUNDI_MODEL_OUTBOUND)) {
			cw_log(CW_LOG_WARNING, "Peer '%s' is supposed to have permission for some inbound searches but isn't an inbound peer or outbound precache!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		} else { 
			if (needregister)
				cw_sched_add(sched, &peer->registerid, 2000, do_register, peer);
			qualify_peer(peer, 1);
		}
	}
	cw_mutex_unlock(&peerlock);
}

static int dundi_helper(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *data, int flag)
{
	struct dundi_result results[MAX_RESULTS];
	struct cw_var_t *var = NULL;
	int res;
	int x;
	int found = 0;

	if (!strncasecmp(context, "proc-", 5)) {
		if (!chan) {	
			cw_log(CW_LOG_NOTICE, "Can't use Proc mode without a channel!\n");
			return -1;
		}
		/* If done as a proc, use proc extension */
		if (!strcasecmp(exten, "s")) {
			if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_ARG1, "ARG1")))
				exten = var->value;
			else {
				exten = chan->proc_exten;
				if (cw_strlen_zero(exten)) {
					exten = chan->exten;
					if (cw_strlen_zero(exten)) {
						cw_log(CW_LOG_WARNING, "Called in Proc mode with no ARG1 or PROC_EXTEN?\n");
						return -1;
					}
				}
			}
		}
		if (!data || cw_strlen_zero(data))
			data = "e164";
	} else {
		if (!data || cw_strlen_zero(data))
			data = context;
	}

	res = dundi_lookup(results, MAX_RESULTS, chan, data, exten, 0);

	if (var)
		cw_object_put(var);

	for (x = 0; x < res; x++) {
		if (cw_test_flag(results + x, flag))
			found++;
	}

	if (found >= priority)
		return 1;

	return 0;
}

static int dundi_exists(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(callerid);

	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_EXISTS);
}

static int dundi_canmatch(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(callerid);

	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_CANMATCH);
}

static int dundi_exec(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	char req[1024];
	struct dundi_result results[MAX_RESULTS];
	struct cw_var_t *var = NULL;
	int res;
	int x=0;

	CW_UNUSED(callerid);

	if (!strncasecmp(context, "proc-", 5)) {
		if (!chan) {	
			cw_log(CW_LOG_NOTICE, "Can't use Proc mode without a channel!\n");
			return -1;
		}
		/* If done as a proc, use proc extension */
		if (!strcasecmp(exten, "s")) {
			if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_ARG1, "ARG1")))
				exten = var->value;
			else {
				exten = chan->proc_exten;
				if (cw_strlen_zero(exten)) {
					exten = chan->exten;
					if (cw_strlen_zero(exten)) {
						cw_log(CW_LOG_WARNING, "Called in Proc mode with no ARG1 or PROC_EXTEN?\n");
						return -1;
					}
				}
			}
		}
		if (!data || cw_strlen_zero(data))
			data = "e164";
	} else {
		if (!data || cw_strlen_zero(data))
			data = context;
	}

	memset(results, 0, sizeof(results));
	for (x = 0; x < arraysize(results); x++)
		cw_dynstr_init(&results[x].dest, 0, CW_DYNSTR_DEFAULT_CHUNK);

	res = dundi_lookup(results, MAX_RESULTS, chan, data, exten, 0);

	if (var)
		cw_object_put(var);

	if (res > 0) {
		sort_results(results, res);

		for (x = 0; x < res; x++) {
			if (cw_test_flag(results + x, DUNDI_FLAG_EXISTS)) {
				if (!--priority)
					break;
			}
		}

		if (x < res) {
			/* Got a hit! */
			snprintf(req, sizeof(req), "%s/%s", results[x].tech, results[x].dest.data);
			res = cw_function_exec_str(chan, CW_KEYWORD_Dial, "Dial", req, NULL);
		} else
			res = -1;

		for (x = 0; x < res; x++)
			cw_dynstr_free(&results[x].dest);
	}

	return res;
}

static int dundi_matchmore(struct cw_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	CW_UNUSED(callerid);

	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_MATCHMORE);
}

static struct cw_switch dundi_switch =
{
        name:                   "DUNDi",
        description:    	"DUNDi Discovered Dialplan Switch",
        exists:                 dundi_exists,
        canmatch:               dundi_canmatch,
        exec:                   dundi_exec,
        matchmore:              dundi_matchmore,
};

static int set_config(const char *config_file, struct sockaddr_in* sain)
{
	char hn[MAXHOSTNAMELEN] = "";
	struct cw_hostent he;
	struct sockaddr_in sain2;
	dundi_eid testeid;
	struct cw_config *cfg;
	struct cw_variable *v;
	char *cat;
	struct hostent *hp;
	static int last_port = 0;
	int format;
	int x;
	int globalpcmodel = 0;

	dundi_ttl = DUNDI_DEFAULT_TTL;
	dundi_cache_time = DUNDI_DEFAULT_CACHE_TIME;
	cfg = cw_config_load(config_file);
	
	
	if (!cfg) {
		cw_log(CW_LOG_ERROR, "Unable to load config %s\n", config_file);
		return -1;
	}
	ipaddr[0] = '\0';
	if (!gethostname(hn, sizeof(hn)-1)) {
		hp = cw_gethostbyname(hn, &he);
		if (hp) {
			memcpy(&sain2.sin_addr, hp->h_addr, sizeof(sain2.sin_addr));
			cw_inet_ntoa(ipaddr, sizeof(ipaddr), sain2.sin_addr);
		} else
			cw_log(CW_LOG_WARNING, "Unable to look up host '%s'\n", hn);
	} else
		cw_log(CW_LOG_WARNING, "Unable to get host name!\n");
	cw_mutex_lock(&peerlock);
	reset_global_eid();
	global_storehistory = 0;
	cw_copy_string(secretpath, "dundi", sizeof(secretpath));
	v = cw_variable_browse(cfg, "general");
	while(v) {
		if (!strcasecmp(v->name, "port")){ 
			sain->sin_port = ntohs(atoi(v->value));
			if(last_port==0){
				last_port=sain->sin_port;
			} else if(sain->sin_port != last_port)
				cw_log(CW_LOG_WARNING, "change to port ignored until next callweaver re-start\n");
		} else if (!strcasecmp(v->name, "bindaddr")) {
			hp = cw_gethostbyname(v->value, &he);
			if (hp) {
				memcpy(&sain->sin_addr, hp->h_addr, sizeof(sain->sin_addr));
			} else
				cw_log(CW_LOG_WARNING, "Invalid host/IP '%s'\n", v->value);
		} else if (!strcasecmp(v->name, "authdebug")) {
			cw_log(CW_LOG_WARNING, "\"authdebug\" is deprecated and should be removed. It never did anything anyway.\n");
		} else if (!strcasecmp(v->name, "ttl")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x > 0) && (x < DUNDI_DEFAULT_TTL)) {
				dundi_ttl = x;
			} else {
				cw_log(CW_LOG_WARNING, "'%s' is not a valid TTL at line %d, must be number from 1 to %d\n",
					v->value, v->lineno, DUNDI_DEFAULT_TTL);
			}
		} else if (!strcasecmp(v->name, "autokill")) {
			if (sscanf(v->value, "%d", &x) == 1) {
				if (x >= 0)
					global_autokilltimeout = x;
				else
					cw_log(CW_LOG_NOTICE, "Nice try, but autokill has to be >0 or 'yes' or 'no' at line %d\n", v->lineno);
			} else if (cw_true(v->value)) {
				global_autokilltimeout = DEFAULT_MAXMS;
			} else {
				global_autokilltimeout = 0;
			}
		} else if (!strcasecmp(v->name, "entityid")) {
			if (!dundi_str_to_eid(&testeid, v->value))
				global_eid = testeid;
			else
				cw_log(CW_LOG_WARNING, "Invalid global endpoint identifier '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%d", &format) == 1)
				tos = format & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				tos = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				tos = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				tos = IPTOS_RELIABILITY;
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(SOLARIS)
			else if (!strcasecmp(v->value, "mincost"))
				tos = IPTOS_MINCOST;
#endif
			else if (!strcasecmp(v->value, "none"))
				tos = 0;
			else
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(SOLARIS)
				cw_log(CW_LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
#else
				cw_log(CW_LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', or 'none'\n", v->lineno);
#endif
		} else if (!strcasecmp(v->name, "department")) {
			cw_copy_string(dept, v->value, sizeof(dept));
		} else if (!strcasecmp(v->name, "organization")) {
			cw_copy_string(org, v->value, sizeof(org));
		} else if (!strcasecmp(v->name, "locality")) {
			cw_copy_string(locality, v->value, sizeof(locality));
		} else if (!strcasecmp(v->name, "stateprov")) {
			cw_copy_string(stateprov, v->value, sizeof(stateprov));
		} else if (!strcasecmp(v->name, "country")) {
			cw_copy_string(country, v->value, sizeof(country));
		} else if (!strcasecmp(v->name, "email")) {
			cw_copy_string(email, v->value, sizeof(email));
		} else if (!strcasecmp(v->name, "phone")) {
			cw_copy_string(phone, v->value, sizeof(phone));
		} else if (!strcasecmp(v->name, "storehistory")) {
			global_storehistory = cw_true(v->value);
		} else if (!strcasecmp(v->name, "cachetime")) {
			if ((sscanf(v->value, "%d", &x) == 1)) {
				dundi_cache_time = x;
			} else {
				cw_log(CW_LOG_WARNING, "'%s' is not a valid cache time at line %d. Using default value '%d'.\n",
					v->value, v->lineno, DUNDI_DEFAULT_CACHE_TIME);
			}
		}
		v = v->next;
	}
	cw_mutex_unlock(&peerlock);
	mark_mappings();
	v = cw_variable_browse(cfg, "mappings");
	while(v) {
		build_mapping(v->name, v->value);
		v = v->next;
	}
	prune_mappings();
	mark_peers();
	cat = cw_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general") && strcasecmp(cat, "mappings")) {
			/* Entries */
			if (!dundi_str_to_eid(&testeid, cat))
				build_peer(&testeid, cw_variable_browse(cfg, cat), &globalpcmodel);
			else
				cw_log(CW_LOG_NOTICE, "Ignoring invalid EID entry '%s'\n", cat);
		}
		cat = cw_category_browse(cfg, cat);
	}
	prune_peers();
	cw_config_destroy(cfg);
	load_password();
	if (globalpcmodel & DUNDI_MODEL_OUTBOUND)
		dundi_precache_full();
	return 0;
}

static int stop_threads(void)
{
	int res = 0;

	if (!pthread_equal(netthreadid, CW_PTHREADT_NULL)) {
		res |= pthread_cancel(netthreadid);
    		res |= pthread_kill(netthreadid, SIGURG);
		netthreadid = CW_PTHREADT_NULL;
	}
	if (!pthread_equal(precachethreadid, CW_PTHREADT_NULL)) {
		res |= pthread_cancel(precachethreadid);
    		res |= pthread_kill(precachethreadid, SIGURG);
		precachethreadid = CW_PTHREADT_NULL;
	}
	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= stop_threads();
	cw_switch_unregister(&dundi_switch);
	cw_cli_unregister(&cli_debug);
	cw_cli_unregister(&cli_store_history);
	cw_cli_unregister(&cli_flush);
	cw_cli_unregister(&cli_no_debug);
	cw_cli_unregister(&cli_no_store_history);
	cw_cli_unregister(&cli_show_peers);
	cw_cli_unregister(&cli_show_entityid);
	cw_cli_unregister(&cli_show_trans);
	cw_cli_unregister(&cli_show_requests);
	cw_cli_unregister(&cli_show_mappings);
	cw_cli_unregister(&cli_show_precache);
	cw_cli_unregister(&cli_show_peer);
	cw_cli_unregister(&cli_lookup);
	cw_cli_unregister(&cli_precache);
	cw_cli_unregister(&cli_queryeid);
	res |= cw_unregister_function(dundi_func);
	return res;
}

static int reload_module(void)
{
	char iabuf[INET_ADDRSTRLEN];
	struct sockaddr_in sain;

	stop_threads();

	sain.sin_family = AF_INET;
	sain.sin_port = ntohs(DUNDI_PORT);
	sain.sin_addr.s_addr = INADDR_ANY;

	set_config("dundi.conf", &sain);

	if ((netsocket = socket_cloexec(AF_INET, SOCK_DGRAM, IPPROTO_IP)) >= 0) {
		if (!bind(netsocket, (struct sockaddr *)&sain, sizeof(sain))) {
			if (setsockopt(netsocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))
				cw_log(CW_LOG_WARNING, "Unable to set TOS to %d\n", tos);
			else if (option_verbose > 1)
				cw_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

			if (option_verbose > 1)
				cw_verbose(VERBOSE_PREFIX_2 "DUNDi Ready and Listening on %s port %hu\n", cw_inet_ntoa(iabuf, sizeof(iabuf), sain.sin_addr), ntohs(sain.sin_port));

			if (!cw_pthread_create(&netthreadid, &global_attr_default, network_thread, NULL)
			&& !cw_pthread_create(&precachethreadid, &global_attr_default, process_precache, NULL))
				return 0;

			stop_threads();
			cw_log(CW_LOG_ERROR, "Unable to start network threads\n");
		}
		close(netsocket);
	}

	return 1;
}

static int load_module(void)
{
	int res = 0;

	dundi_set_output(dundi_debug_output);
	dundi_set_error(dundi_error_output);

	/* Make a UDP socket */
	io = cw_io_context_create(32);
	sched = sched_context_create(1);

	if (io == CW_IO_CONTEXT_NONE || !sched) {
		cw_log(CW_LOG_ERROR, "Out of memory\n");
		return -1;
	}

	cw_io_init(&netsocket_io_id, socket_read, NULL);

	res |= reload_module();

	cw_switch_register(&dundi_switch);
	cw_cli_register(&cli_debug);
	cw_cli_register(&cli_store_history);
	cw_cli_register(&cli_flush);
	cw_cli_register(&cli_no_debug);
	cw_cli_register(&cli_no_store_history);
	cw_cli_register(&cli_show_peers);
	cw_cli_register(&cli_show_entityid);
	cw_cli_register(&cli_show_trans);
	cw_cli_register(&cli_show_requests);
	cw_cli_register(&cli_show_mappings);
	cw_cli_register(&cli_show_precache);
	cw_cli_register(&cli_show_peer);
	cw_cli_register(&cli_lookup);
	cw_cli_register(&cli_precache);
	cw_cli_register(&cli_queryeid);
	dundi_func = cw_register_function(dundifunc_name, dundifunc_read, dundifunc_synopsis, dundifunc_syntax, dundifunc_desc); 
	
	return res;
}


MODULE_INFO(load_module, reload_module, unload_module, NULL, tdesc)
