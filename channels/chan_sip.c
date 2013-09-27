/* CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009 - 2011, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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

/*!
 * \file
 * \brief Implementation of Session Initiation Protocol
 * 
 * Implementation of RFC 3261 - without S/MIME, TCP and TLS support
 * Configuration file \link Config_sip sip.conf \endlink
 *
 * \todo SIP over TCP
 * \todo SIP over TLS
 * \todo Better support of forking
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <regex.h>
#include <vale/udptl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <callweaver/udp.h>
#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/config.h"
#include "callweaver/logger.h"
#include "callweaver/object.h"
#include "callweaver/module.h"
#include "callweaver/pbx.h"
#include "callweaver/options.h"
#include "callweaver/lock.h"
#include "callweaver/sched.h"
#include "callweaver/io.h"
#include "callweaver/rtp.h"
#include "callweaver/udptl.h"
//#include "callweaver/tpkt.h"
#include "callweaver/acl.h"
#include "callweaver/manager.h"
#include "callweaver/phone_no_utils.h"
#include "callweaver/cli.h"
#include "callweaver/app.h"
#include "callweaver/musiconhold.h"
#include "callweaver/dsp.h"
#include "callweaver/features.h"
#include "callweaver/acl.h"
#include "callweaver/srv.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/causes.h"
#include "callweaver/utils.h"
#include "callweaver/file.h"
#include "callweaver/devicestate.h"
#include "callweaver/linkedlists.h"
#include "callweaver/localtime.h"
#include "callweaver/udpfromto.h"
#include "callweaver/stun.h"
#include "callweaver/keywords.h"
#include "callweaver/indications.h"
#include "callweaver/blacklist.h"
#include "callweaver/printf.h"

#ifdef OSP_SUPPORT
#include "callweaver/astosp.h"
#endif


#ifndef DEFAULT_USERAGENT
#define DEFAULT_USERAGENT "CallWeaver"
#endif
 
#define VIDEO_CODEC_MASK    0x1fc0000 /* Video codecs from H.261 thru CW_FORMAT_MAX_VIDEO */
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST        0x02
#endif

/* #define VOCAL_DATA_HACK */

#define SIPDUMPER
#define DEFAULT_DEFAULT_EXPIRY  120
#define DEFAULT_MAX_EXPIRY    3600
#define DEFAULT_REGISTRATION_TIMEOUT    20
#define DEFAULT_MAX_FORWARDS    "70"

#ifdef ENABLE_SRTP
#  define SRTP_MASTER_LEN	30
#  define SRTP_MASTERKEY_LEN	16
#  define SRTP_MASTERSALT_LEN	(SRTP_MASTER_LEN - SRTP_MASTERKEY_LEN)
#  define SRTP_MASTER_LEN64	((SRTP_MASTER_LEN * 8 + 5) / 6 + 1)

struct sip_srtp {
	char *a_crypto;
	unsigned char local_key[SRTP_MASTER_LEN];
	char local_key64[SRTP_MASTER_LEN64];
};
#endif

/* guard limit must be larger than guard secs */
/* guard min must be < 1000, and should be >= 250 */
#define EXPIRY_GUARD_SECS    15    /* How long before expiry do we reregister */
#define EXPIRY_GUARD_LIMIT    30    /* Below here, we use EXPIRY_GUARD_PCT instead of 
                       EXPIRY_GUARD_SECS */
#define EXPIRY_GUARD_MIN    500    /* This is the minimum guard time applied. If 
                       GUARD_PCT turns out to be lower than this, it 
                       will use this time instead.
                       This is in milliseconds. */
#define EXPIRY_GUARD_PCT    0.20    /* Percentage of expires timeout to use when 
                       below EXPIRY_GUARD_LIMIT */

#define SIP_LEN_CONTACT		256

static int max_expiry = DEFAULT_MAX_EXPIRY;
static int default_expiry = DEFAULT_DEFAULT_EXPIRY;

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#define CALLERID_UNKNOWN    "Unknown"


#define DEFAULT_MAXMS        2000        /* Must be faster than 2 seconds by default */
#define DEFAULT_FREQ_OK        60 * 1000    /* How often to check for the host to be up */
#define DEFAULT_FREQ_NOTOK    10 * 1000    /* How often to check, if the host is down... */

#define DEFAULT_RFC_TIMER_T1  500        /* Default RTT estimate in ms (RFC3261 requires 500ms) */
static int rfc_timer_t1 = DEFAULT_RFC_TIMER_T1;
#define DEFAULT_RFC_TIMER_T2 4000        /* Maximum retransmit interval for non-INVITEs in ms (RFC3261 requires 4s) */
static int rfc_timer_t2 = DEFAULT_RFC_TIMER_T2;
#define DEFAULT_RFC_TIMER_B    64        /* INVITE transaction timeout, in units of T1. Default gives 7 attempts (RFC3261 requires termination after 64*T1 ms) */
static int rfc_timer_b = DEFAULT_RFC_TIMER_B;
#define RFC_TIMER_F          64        /* non-INVITE transaction timeout, in units of T1 (RFC3261 requires termination after 64*T1 ms) */

#define SLOW_INVITE_RETRANS	5000	/* Frequency of retransmission of INVITEs while waiting for
					 * a final (as opposed to a provisional) response. This is
					 * required to maintain NAT mappings until the call is either
					 * accepted or rejected.
					 * RFC3581 suggests 20s
					 */

#define SIP_ASSUME_SYMMETRIC 1         /* Assume paths are symmetric, i.e. our RTT estimate can be used to judge when to time out waiting for a packet from the remote */

#define MAX_AUTHTRIES        3        /* Try authentication three times, then fail */


#define DEBUG_READ    0            /* Recieved data    */
#define DEBUG_SEND    1            /* Transmit data    */

#include "callweaver/generic_jb.h"
static struct cw_jb_conf global_jbconf;

static const char desc[] = "Session Initiation Protocol (SIP)";
static const char channeltype[] = "SIP";
static const char config[] = "sip.conf";
static const char notify_config[] = "sip_notify.conf";


static void *sipheader_function;
static const char sipheader_func_name[] = "SIP_HEADER";
static const char sipheader_func_synopsis[] = "Gets or sets the specified SIP header";
static const char sipheader_func_syntax[] = "SIP_HEADER(<name>)";
static const char sipheader_func_desc[] = "";

static void *sippeer_function;
static const char sippeer_func_name[] = "SIPPEER";
static const char sippeer_func_synopsis[] = "Gets SIP peer information";
static const char sippeer_func_syntax[] = "SIPPEER(peername[, item])";
static const char sippeer_func_desc[] =
	"Returns information concerning the given SIP peer.\n"
	"\n"
	"Valid items are:\n"
	"\n"
	"    callerid_name         The configured Caller ID name.\n"
	"    callerid_num          The configured Caller ID number.\n"
	"    codecs                The configured codecs.\n"
	"    codec[x]              Preferred codec index number 'x' (beginning with zero).\n"
	"    context               The configured context.\n"
	"    curcalls              Current amount of calls \n"
	"                          (only available if call-limit is set)\n"
	"    dynamic               Is it dynamic? (yes/no).\n"
	"    expire                The epoch time of the next expire.\n"
	"    ip                    The IP address.\n"
	"    language              Default language for peer\n"
	"    limit                 Call limit (call-limit)\n"
	"    mailbox               The configured mailbox.\n"
	"    reachability          Whether the peer is currently reachable\n"
	"                          (Unregistered, Unmonitored, OK, LAGGED or UNREACHABLE)\n"
	"    regexten              Registration extension\n"
	"    rtt                   Current RTT estimate in milliseconds\n"
	"    status                Status (deprecated - use reachability and/or rtt instead).\n"
	"    useragent             Current user agent id for peer\n"
	"\n"
	"If no item is specified SIPPEER returns the IP address\n"
	"\n";

static void *sippeervar_function;
static const char sippeervar_func_name[] = "SIPPEERVAR";
static const char sippeervar_func_synopsis[] = "Gets SIP peer variable";
static const char sippeervar_func_syntax[] = "SIPPEERVAR(peername, varname)";
static const char sippeervar_func_desc[] =
	"Returns the value of varname as specified for the peer in its configuration.\n"
	"\n";

static void *sipchaninfo_function;
static const char sipchaninfo_func_name[] = "SIPCHANINFO";
static const char sipchaninfo_func_synopsis[] = "Gets the specified SIP parameter from the current channel";
static const char sipchaninfo_func_syntax[] = "SIPCHANINFO(item)";
static const char sipchaninfo_func_desc[] =
	"Valid items are:\n"
	"- peerip                The IP address of the peer.\n"
	"- recvip                The source IP address of the peer.\n"
	"- from                  The URI from the From: header.\n"
	"- uri                   The URI from the Contact: header.\n"
	"- useragent             The useragent.\n"
	"- peername              The name of the peer.\n";

static void *checksipdomain_function;
static const char checksipdomain_func_name[] = "CHECKSIPDOMAIN";
static const char checksipdomain_func_synopsis[] = "Checks if domain is a local domain";
static const char checksipdomain_func_syntax[] = "CHECKSIPDOMAIN(<domain|IP>)";
static const char checksipdomain_func_desc[] =
	"This function checks if the domain in the argument is configured\n"
        "as a local SIP domain that this CallWeaver server is configured to handle.\n"
        "Returns the domain name if it is locally handled, otherwise an empty string.\n"
        "Check the domain= configuration in sip.conf\n";

static void *sipbuilddial_function;
static const char sipbuilddial_func_name[] = "SIP_BUILD_DIAL";
static const char sipbuilddial_func_synopsis[] = "Build SIP Dial String using <regex peer>";
static const char sipbuilddial_func_syntax[] = "SIP_BUILD_DIAL(<regex peer>)";
static const char sipbuilddial_func_desc[] = "";

static void *sipblacklist_function;
static const char sipblacklist_func_name[] = "SIPBLACKLIST";
static const char sipblacklist_func_synopsis[] = "Blacklist the peer for this channel";
static const char sipblacklist_func_syntax[] = "SIPBLACKLIST()";
static const char sipblacklist_func_desc[] = "";


#define RTP     1
#define NO_RTP    0

/* Do _NOT_ make any changes to this enum, or the array following it;
   if you think you are doing the right thing, you are probably
   not doing the right thing. If you think there are changes
   needed, get someone else to review them first _before_
   submitting a patch. If these two lists do not match properly
   bad things will happen.
*/

enum subscriptiontype { 
	NONE = 0,
	TIMEOUT,
	XPIDF_XML,
	DIALOG_INFO_XML,
	CPIM_PIDF_XML,
	PIDF_XML
};

static const struct cfsubscription_types {
	enum subscriptiontype type;
	const char * const event;
	const char * const mediatype;
	const char * const text;
} subscription_types[] = {
	{ NONE,            "-",        "unknown",	                  "unknown" },
 	/* IETF draft: draft-ietf-sipping-dialog-package-05.txt */
	{ DIALOG_INFO_XML, "dialog",   "application/dialog-info+xml", "dialog-info+xml" },
	{ CPIM_PIDF_XML,   "presence", "application/cpim-pidf+xml",   "cpim-pidf+xml" },  /* RFC 3863 */
	{ PIDF_XML,        "presence", "application/pidf+xml",        "pidf+xml" },       /* RFC 3863 */
	{ XPIDF_XML,       "presence", "application/xpidf+xml",       "xpidf+xml" }       /* Pre-RFC 3863 with MS additions */
};

enum sipmethod {
	SIP_UNKNOWN   = 0, /* This MUST be 0 - find_sip_method assumes it is */
	SIP_RESPONSE  = 1,
	SIP_REGISTER  = 2,
	SIP_OPTIONS   = 3,
	SIP_NOTIFY    = 4,
	SIP_INVITE    = 5,
	SIP_ACK       = 6,
	SIP_PRACK     = 7,
	SIP_BYE       = 8,
	SIP_REFER     = 9,
	SIP_SUBSCRIBE = 10,
	SIP_MESSAGE   = 11,
	SIP_UPDATE    = 12,
	SIP_INFO      = 13,
	SIP_CANCEL    = 14,
	SIP_PUBLISH   = 15,
};

enum sip_auth_type {
	PROXY_AUTH,
	WWW_AUTH,
};

static const struct { 
	const char * const text;
	int len;
	int can_create;
} sip_methods[] = {
#define S(str)	str, sizeof(str)-1
	[SIP_UNKNOWN]   = { S("-UNKNOWN-"), 2 },
	[SIP_RESPONSE]  = { S("SIP/2.0"),   0 },
	[SIP_REGISTER]  = { S("REGISTER"),  1 },
	[SIP_OPTIONS]   = { S("OPTIONS"),   1 },
	[SIP_NOTIFY]    = { S("NOTIFY"),    2 },
	[SIP_INVITE]    = { S("INVITE"),    1 },
	[SIP_ACK]       = { S("ACK"),       0 },
	[SIP_PRACK]     = { S("PRACK"),     2 },
	[SIP_BYE]       = { S("BYE"),       0 },
	[SIP_REFER]     = { S("REFER"),     2 },
	[SIP_SUBSCRIBE] = { S("SUBSCRIBE"), 1 },
	[SIP_MESSAGE]   = { S("MESSAGE"),   1 },
	[SIP_UPDATE]    = { S("UPDATE"),    0 },
	[SIP_INFO]      = { S("INFO"),      0 },
	[SIP_CANCEL]    = { S("CANCEL"),    0 },
	[SIP_PUBLISH]   = { S("PUBLISH"),   1 }
#undef S
};


#define SIP_HDR_STRLEN(name, alias)	name, sizeof(name) - 1, alias, sizeof(alias) - 1
#define SIP_HDR_ACCEPT_CONTACT		SIP_HDR_STRLEN("Accept-Contact",      "a")
#define SIP_HDR_ALLOW_EVENTS		SIP_HDR_STRLEN("Allow-Events",        "u")
#define SIP_HDR_CALL_ID			SIP_HDR_STRLEN("Call-ID",             "i")
#define SIP_HDR_CONTACT			SIP_HDR_STRLEN("Contact",             "m")
#define SIP_HDR_CONTENT_ENCODING	SIP_HDR_STRLEN("Content-Encoding",    "e")
#define SIP_HDR_CONTENT_LENGTH		SIP_HDR_STRLEN("Content-Length",      "l")
#define SIP_HDR_CONTENT_TYPE		SIP_HDR_STRLEN("Content-Type",        "c")
#define SIP_HDR_EVENT			SIP_HDR_STRLEN("Event",               "o")
#define SIP_HDR_FROM			SIP_HDR_STRLEN("From",                "f")
#define SIP_HDR_IDENTITY		SIP_HDR_STRLEN("Identity",            "y")
#define SIP_HDR_IDENTITY_INFO		SIP_HDR_STRLEN("Identity-Info",       "n")
#define SIP_HDR_REFER_TO		SIP_HDR_STRLEN("Referred-To",         "r")
#define SIP_HDR_REFERRED_BY		SIP_HDR_STRLEN("Referred-By",         "b")
#define SIP_HDR_REJECT_CONTACT		SIP_HDR_STRLEN("Reject-Contact",      "j")
#define SIP_HDR_REQUEST_DISPOSITION	SIP_HDR_STRLEN("Request-Disposition", "d")
#define SIP_HDR_SESSION_EXPIRES		SIP_HDR_STRLEN("Session-Expires",     "x")
#define SIP_HDR_SUBJECT			SIP_HDR_STRLEN("Subject",             "s")
#define SIP_HDR_SUPPORTED		SIP_HDR_STRLEN("Supported",           "k")
#define SIP_HDR_TO			SIP_HDR_STRLEN("To",                  "t")
#define SIP_HDR_VIA			SIP_HDR_STRLEN("Via",                 "v")
#define SIP_HDR_NOSHORT(name)		name, sizeof(name) - 1, NULL, 0
#define SIP_HDR_VNOSHORT(name)		name, strlen(name), NULL, 0
#define SIP_HDR_GENERIC(name)		name, strlen(name), find_alias(name), strlen(find_alias(name))

/*! \brief Structure for conversion between compressed SIP and "normal" SIP */
enum sip_hdr {
	SIP_NHDR_ACCEPT_CONTACT = 0,
	SIP_NHDR_ALLOW_EVENTS,
	SIP_NHDR_CALL_ID,
	SIP_NHDR_CONTACT,
	SIP_NHDR_CONTENT_ENCODING,
	SIP_NHDR_CONTENT_LENGTH,
	SIP_NHDR_CONTENT_TYPE,
	SIP_NHDR_EVENT,
	SIP_NHDR_FROM,
	SIP_NHDR_IDENTITY,
	SIP_NHDR_IDENTITY_INFO,
	SIP_NHDR_REFER_TO,
	SIP_NHDR_REFERRED_BY,
	SIP_NHDR_REJECT_CONTACT,
	SIP_NHDR_REQUEST_DISPOSITION,
	SIP_NHDR_SESSION_EXPIRES,
	SIP_NHDR_SUBJECT,
	SIP_NHDR_SUPPORTED,
	SIP_NHDR_TO,
	SIP_NHDR_VIA,
};

#define SIP_HDR_FULLNAME(key)	CW_CPP_DO(SIP_HDR_FULLNAME2, key)
#define SIP_HDR_FULLNAME2(name, name_len, alias, alias_len)	name
static const char * const sip_hdr_fullname[] = {
	[SIP_NHDR_ACCEPT_CONTACT]      = SIP_HDR_FULLNAME(SIP_HDR_ACCEPT_CONTACT),
	[SIP_NHDR_ALLOW_EVENTS]        = SIP_HDR_FULLNAME(SIP_HDR_ALLOW_EVENTS),
	[SIP_NHDR_CALL_ID]             = SIP_HDR_FULLNAME(SIP_HDR_CALL_ID),
	[SIP_NHDR_CONTACT]             = SIP_HDR_FULLNAME(SIP_HDR_CONTACT),
	[SIP_NHDR_CONTENT_ENCODING]    = SIP_HDR_FULLNAME(SIP_HDR_CONTENT_ENCODING),
	[SIP_NHDR_CONTENT_LENGTH]      = SIP_HDR_FULLNAME(SIP_HDR_CONTENT_LENGTH),
	[SIP_NHDR_CONTENT_TYPE]        = SIP_HDR_FULLNAME(SIP_HDR_CONTENT_TYPE),
	[SIP_NHDR_EVENT]               = SIP_HDR_FULLNAME(SIP_HDR_EVENT),
	[SIP_NHDR_FROM]                = SIP_HDR_FULLNAME(SIP_HDR_FROM),
	[SIP_NHDR_IDENTITY]            = SIP_HDR_FULLNAME(SIP_HDR_IDENTITY),
	[SIP_NHDR_IDENTITY_INFO]       = SIP_HDR_FULLNAME(SIP_HDR_IDENTITY_INFO),
	[SIP_NHDR_REFER_TO]            = SIP_HDR_FULLNAME(SIP_HDR_REFER_TO),
	[SIP_NHDR_REFERRED_BY]         = SIP_HDR_FULLNAME(SIP_HDR_REFERRED_BY),
	[SIP_NHDR_REJECT_CONTACT]      = SIP_HDR_FULLNAME(SIP_HDR_REJECT_CONTACT),
	[SIP_NHDR_REQUEST_DISPOSITION] = SIP_HDR_FULLNAME(SIP_HDR_REQUEST_DISPOSITION),
	[SIP_NHDR_SESSION_EXPIRES]     = SIP_HDR_FULLNAME(SIP_HDR_SESSION_EXPIRES),
	[SIP_NHDR_SUBJECT]             = SIP_HDR_FULLNAME(SIP_HDR_SUBJECT),
	[SIP_NHDR_SUPPORTED]           = SIP_HDR_FULLNAME(SIP_HDR_SUPPORTED),
	[SIP_NHDR_TO]                  = SIP_HDR_FULLNAME(SIP_HDR_TO),
	[SIP_NHDR_VIA]                 = SIP_HDR_FULLNAME(SIP_HDR_VIA),
};
#define SIP_HDR_SHORTNAME(key)	CW_CPP_DO(SIP_HDR_SHORTNAME2, key)
#define SIP_HDR_SHORTNAME2(name, name_len, alias, alias_len)	alias
static const char * const sip_hdr_shortname[] = {
	[SIP_NHDR_ACCEPT_CONTACT]      = SIP_HDR_SHORTNAME(SIP_HDR_ACCEPT_CONTACT),
	[SIP_NHDR_ALLOW_EVENTS]        = SIP_HDR_SHORTNAME(SIP_HDR_ALLOW_EVENTS),
	[SIP_NHDR_CALL_ID]             = SIP_HDR_SHORTNAME(SIP_HDR_CALL_ID),
	[SIP_NHDR_CONTACT]             = SIP_HDR_SHORTNAME(SIP_HDR_CONTACT),
	[SIP_NHDR_CONTENT_ENCODING]    = SIP_HDR_SHORTNAME(SIP_HDR_CONTENT_ENCODING),
	[SIP_NHDR_CONTENT_LENGTH]      = SIP_HDR_SHORTNAME(SIP_HDR_CONTENT_LENGTH),
	[SIP_NHDR_CONTENT_TYPE]        = SIP_HDR_SHORTNAME(SIP_HDR_CONTENT_TYPE),
	[SIP_NHDR_EVENT]               = SIP_HDR_SHORTNAME(SIP_HDR_EVENT),
	[SIP_NHDR_FROM]                = SIP_HDR_SHORTNAME(SIP_HDR_FROM),
	[SIP_NHDR_IDENTITY]            = SIP_HDR_SHORTNAME(SIP_HDR_IDENTITY),
	[SIP_NHDR_IDENTITY_INFO]       = SIP_HDR_SHORTNAME(SIP_HDR_IDENTITY_INFO),
	[SIP_NHDR_REFER_TO]            = SIP_HDR_SHORTNAME(SIP_HDR_REFER_TO),
	[SIP_NHDR_REFERRED_BY]         = SIP_HDR_SHORTNAME(SIP_HDR_REFERRED_BY),
	[SIP_NHDR_REJECT_CONTACT]      = SIP_HDR_SHORTNAME(SIP_HDR_REJECT_CONTACT),
	[SIP_NHDR_REQUEST_DISPOSITION] = SIP_HDR_SHORTNAME(SIP_HDR_REQUEST_DISPOSITION),
	[SIP_NHDR_SESSION_EXPIRES]     = SIP_HDR_SHORTNAME(SIP_HDR_SESSION_EXPIRES),
	[SIP_NHDR_SUBJECT]             = SIP_HDR_SHORTNAME(SIP_HDR_SUBJECT),
	[SIP_NHDR_SUPPORTED]           = SIP_HDR_SHORTNAME(SIP_HDR_SUPPORTED),
	[SIP_NHDR_TO]                  = SIP_HDR_SHORTNAME(SIP_HDR_TO),
	[SIP_NHDR_VIA]                 = SIP_HDR_SHORTNAME(SIP_HDR_VIA),
};
static const char * const *sip_hdr_name = sip_hdr_fullname;

static __attribute__ ((__pure__)) const char *find_alias(const char *name)
{
	const char *ret = "";
	int i;

	for (i = 0; !ret && i < arraysize(sip_hdr_fullname); i++) {
		if (!strcasecmp(sip_hdr_fullname[i], name)) {
			ret = sip_hdr_shortname[i];
			break;
		}
	}

	return ret;
}

static const char *sip_hdr_generic(const char *name)
{
	if (sip_hdr_name == sip_hdr_shortname) {
		const char *p;

		if ((p = find_alias(name)) && p[0])
			name = p;
	}

	return name;
}


/*!  Define SIP option tags, used in Require: and Supported: headers 
     We need to be aware of these properties in the phones to use 
    the replace: header. We should not do that without knowing
    that the other end supports it... 
    This is nothing we can configure, we learn by the dialog
    Supported: header on the REGISTER (peer) or the INVITE
    (other devices)
    We are not using many of these today, but will in the future.
    This is documented in RFC 3261
*/
#define SUPPORTED		1
#define NOT_SUPPORTED		0

#define SIP_OPT_REPLACES	(1 << 0)
#define SIP_OPT_100REL		(1 << 1)
#define SIP_OPT_TIMER		(1 << 2)
#define SIP_OPT_EARLY_SESSION	(1 << 3)
#define SIP_OPT_JOIN		(1 << 4)
#define SIP_OPT_PATH		(1 << 5)
#define SIP_OPT_PREF		(1 << 6)
#define SIP_OPT_PRECONDITION	(1 << 7)
#define SIP_OPT_PRIVACY		(1 << 8)
#define SIP_OPT_SDP_ANAT	(1 << 9)
#define SIP_OPT_SEC_AGREE	(1 << 10)
#define SIP_OPT_EVENTLIST	(1 << 11)
#define SIP_OPT_GRUU		(1 << 12)
#define SIP_OPT_TARGET_DIALOG	(1 << 13)

/*! \brief List of well-known SIP options. If we get this in a require,
   we should check the list and answer accordingly. */
static const struct cfsip_options {
    int id;            /*!< Bitmap ID */
    int supported;        /*!< Supported by CallWeaver ? */
    const char * const text;    /*!< Text id, as in standard */
} sip_options[] = {
	/* Replaces: header for transfer */
	{ SIP_OPT_REPLACES,	SUPPORTED,	"replaces" },	
	/* RFC3262: PRACK 100% reliability */
	{ SIP_OPT_100REL,	NOT_SUPPORTED,	"100rel" },	
	/* SIP Session Timers */
	{ SIP_OPT_TIMER,	NOT_SUPPORTED,	"timer" },
	/* RFC3959: SIP Early session support */
	{ SIP_OPT_EARLY_SESSION, NOT_SUPPORTED,	"early-session" },
	/* SIP Join header support */
	{ SIP_OPT_JOIN,		NOT_SUPPORTED,	"join" },
	/* RFC3327: Path support */
	{ SIP_OPT_PATH,		NOT_SUPPORTED,	"path" },
	/* RFC3840: Callee preferences */
	{ SIP_OPT_PREF,		NOT_SUPPORTED,	"pref" },
	/* RFC3312: Precondition support */
	{ SIP_OPT_PRECONDITION,	NOT_SUPPORTED,	"precondition" },
	/* RFC3323: Privacy with proxies*/
	{ SIP_OPT_PRIVACY,	NOT_SUPPORTED,	"privacy" },
	/* RFC4092: Usage of the SDP ANAT Semantics in the SIP */
	{ SIP_OPT_SDP_ANAT,	NOT_SUPPORTED,	"sdp-anat" },
	/* RFC3329: Security agreement mechanism */
	{ SIP_OPT_SEC_AGREE,	NOT_SUPPORTED,	"sec_agree" },
	/* SIMPLE events:  draft-ietf-simple-event-list-07.txt */
	{ SIP_OPT_EVENTLIST,	NOT_SUPPORTED,	"eventlist" },
	/* GRUU: Globally Routable User Agent URI's */
	{ SIP_OPT_GRUU,		NOT_SUPPORTED,	"gruu" },
	/* Target-dialog: draft-ietf-sip-target-dialog-00.txt */
	{ SIP_OPT_TARGET_DIALOG,NOT_SUPPORTED,	"target-dialog" },
};


/*! \brief SIP Methods we support
 * RFC3261: 20.5:
 *     All methods, including ACK and CANCEL, understood by the UA MUST be
 *     included in the list of methods in the Allow header field, when
 *     present.
 */
#define ALLOWED_METHODS "INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, SUBSCRIBE, NOTIFY, INFO, MESSAGE, REGISTER"

/*! \brief SIP Extensions we support */
#define SUPPORTED_EXTENSIONS "replaces" 

#define DEFAULT_SIP_PORT    5060    /*!< From RFC 3261 (former 2543) */
#define DEFAULT_SIP_PORT_STR	CW_CPP_DO(CW_CPP_MKSTR, DEFAULT_SIP_PORT)

static char default_useragent[CW_MAX_EXTENSION] = DEFAULT_USERAGENT;

#define DEFAULT_CONTEXT "default"
static char default_context[CW_MAX_CONTEXT] = DEFAULT_CONTEXT;
static char default_subscribecontext[CW_MAX_CONTEXT];

#define DEFAULT_VMEXTEN "callweaver"
static char global_vmexten[CW_MAX_EXTENSION] = DEFAULT_VMEXTEN;

static char default_language[MAX_LANGUAGE] = "";

#define DEFAULT_CALLERID "callweaver"
static char default_callerid[CW_MAX_EXTENSION] = DEFAULT_CALLERID;

static char default_fromdomain[CW_MAX_EXTENSION] = "";

#define DEFAULT_NOTIFYMIME "application/simple-message-summary"
static char default_notifymime[CW_MAX_EXTENSION] = DEFAULT_NOTIFYMIME;

static int global_notifyringing = 1;    /*!< Send notifications on ringing */

static int global_alwaysauthreject = 0;	/*!< Send 401 Unauthorized for all failing requests */

static int default_qualify = 0;        /*!< Default Qualify= setting */

static struct cw_flags global_flags = {0};        /*!< global SIP_ flags */
static struct cw_flags global_flags_page2 = {0};    /*!< more global SIP_ flags */

static int srvlookup = 0;        /*!< SRV Lookup on or off. Default is off, RFC behavior is on */

static int pedanticsipchecking = 0;    /*!< Extra checking ?  Default off */

static int autocreatepeer = 0;        /*!< Auto creation of peers at registration? Default off. */

static int relaxdtmf = 0;

static int global_rtptimeout = 0;

static int global_rtpholdtimeout = 0;

static int global_rtpkeepalive = 0;

static int global_reg_timeout = DEFAULT_REGISTRATION_TIMEOUT;    
static int global_regattempts_max = 0;

/* Object counters */
static int suserobjs = 0;
static int ruserobjs = 0;
static int speerobjs = 0;
static int rpeerobjs = 0;
static int apeerobjs = 0;
static int regobjs = 0;

static int global_allowguest = 1;    /*!< allow unauthenticated users/peers to connect? */

#define DEFAULT_MWITIME 10
static int global_mwitime = DEFAULT_MWITIME;    /*!< Time between MWI checks for peers */


/*! \brief This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = CW_PTHREADT_NULL;

/* T.38 channel status */
typedef enum {
    SIP_T38_OFFER_REJECTED		= -1,
    SIP_T38_STATUS_UNKNOWN		= 0,
    SIP_T38_OFFER_SENT_DIRECT		= 1,
    SIP_T38_OFFER_SENT_REINVITE		= 2,
    SIP_T38_OFFER_RECEIVED_DIRECT	= 3,
    SIP_T38_OFFER_RECEIVED_REINVITE 	= 4,
    SIP_T38_NEGOTIATED			= 5
} sip_t38_status_t;

/* T.38 set of flags */
#define T38FAX_FILL_BIT_REMOVAL            (1 << 0)     /*!< Default: 0 (unset)*/
#define T38FAX_TRANSCODING_MMR            (1 << 1)    /*!< Default: 0 (unset)*/
#define T38FAX_TRANSCODING_JBIG            (1 << 2)    /*!< Default: 0 (unset)*/
/* Rate management */
#define T38FAX_RATE_MANAGEMENT_TRANSFERED_TCF    (0 << 3)
#define T38FAX_RATE_MANAGEMENT_LOCAL_TCF    (1 << 3)    /*!< Unset for transferedTCF (UDPTL), set for localTCF (TPKT) */
/* UDP Error correction */
#define T38FAX_UDP_EC_FEC            (1 << 4)    /*!< Set for t38UDPFEC */
#define T38FAX_UDP_EC_REDUNDANCY        (2 << 4)    /*!< Set for t38UDPRedundancy */
/* T38 Spec version */
#define T38FAX_VERSION                (3 << 6)    /*!< two bits, 2 values so far, up to 4 values max */ 
#define T38FAX_VERSION_0            (0 << 6)    /*!< Version 0 */ 
#define T38FAX_VERSION_1            (1 << 6)    /*!< Version 1 */
/* Maximum Fax Rate */
#define T38FAX_RATE_2400            (1 << 8)    /*!< 2400 bps t38FaxRate */
#define T38FAX_RATE_4800            (1 << 9)    /*!< 4800 bps t38FaxRate */
#define T38FAX_RATE_7200            (1 << 10)    /*!< 7200 bps t38FaxRate */
#define T38FAX_RATE_9600            (1 << 11)    /*!< 9600 bps t38FaxRate */
#define T38FAX_RATE_12000            (1 << 12)    /*!< 12000 bps t38FaxRate */
#define T38FAX_RATE_14400            (1 << 13)    /*!< 14400 bps t38FaxRate */
#define T38FAX_RATE_33600            (1 << 14)    /*!< 33600 bps t38FaxRate */

/*! \brief Codecs that we support by default: */
static int global_capability = CW_FORMAT_ULAW | CW_FORMAT_ALAW | CW_FORMAT_GSM | CW_FORMAT_H263;
static int noncodeccapability = CW_RTP_DTMF;
static int global_t38_capability = T38FAX_VERSION_0 | T38FAX_RATE_2400 | T38FAX_RATE_4800 | T38FAX_RATE_7200 | T38FAX_RATE_9600 | T38FAX_RATE_14400; /* This is default: NO MMR and JBIG trancoding, NO fill bit removal, transfered TCF, UDP FEC, Version 0 and 9600 max fax rate */
static struct cw_sockaddr_net outboundproxyip;
static struct sockaddr_in stunserver_ip;

static int sipdebug = 0;

static struct {
	pthread_rwlock_t lock;
	struct cw_acl *acl;
} debugacl;

static int tos = 0;

static int videosupport = 0;

static int t38udptlsupport = 0;
static int t38rtpsupport = 0;
static int t38tcpsupport = 0;

static char global_musicclass[MAX_MUSICCLASS] = "";    /*!< Global music on hold class */
#define DEFAULT_REALM    "callweaver.org"
static char global_realm[MAXHOSTNAMELEN] = DEFAULT_REALM;     /*!< Default realm */
static char regcontext[CW_MAX_CONTEXT] = "";        /*!< Context for auto-extensions */

static struct sched_context *sched;
static cw_io_context_t io;

#define SIP_MAX_LINES         64            /*!< Max amount of lines in SIP attachment (like SDP) */

#define DEC_CALL_LIMIT    0
#define INC_CALL_LIMIT    1

static struct cw_codec_pref prefs;


/*! \brief sip_request: The data grabbed from the UDP socket */
struct sip_request {
	struct sip_request *next;            /*!< Next packet */
	enum sipmethod method;		/*!< Method of this request */
	unsigned int uriresp; 		/*!< The Request URI or Response Status */
	unsigned int seqno;		/*!< Sequence number according to CSeq header */
	enum sipmethod cseq_method;	/*!< Method according to CSeq header */
	unsigned int cseq;	/*!< Offset of CSeq value in the data */
	int callid;
	int tag;
	int taglen;

	unsigned int debug:1;	/*!< Debug flag for this packet */
	unsigned int free:1;	/*!< Free when done with this packet */
	unsigned int flags;	/*!< SIP_PKT Flags for this packet */

	struct sip_pvt *owner;            /*!< Owner call */
	int retransid;                /*!< Retransmission ID */
	int timer_a;                /*!< SIP timer A, retransmission timer */

	struct timespec txtime;		/*!< Time this request was originally transmitted */
	int txtime_i;			/*!< Offset to the timestamp header line in the pkt */

	struct cw_connection *conn;
	struct cw_sockaddr_net recvdaddr;
	struct cw_sockaddr_net ouraddr;

	unsigned int body_start;
	unsigned int hdr_start;

	/* sipsock_read() only clears request packets down to here */
	struct cw_dynstr pkt;
};

/*! \brief Parameters to the transmit_invite function */
struct sip_invite_param {
    struct cw_var_t *distinctive_ring;    /*!< Distinctive ring header */
    struct cw_var_t *uri_options;    /*!< URI options to add to the URI */
    struct cw_var_t *vxml_url;        /*!< VXML url for Cisco phones */
    struct cw_dynstr auth;        /*!< Authentication */
    const char *authheader;    /*!< Auth header */
    enum sip_auth_type auth_type;    /*!< Authentication type */
#ifdef OSP_SUPPORT
    struct cw_var_t *osptoken;        /*!< OSP token for this call */
#endif
};

struct sip_route {
    struct sip_route *next;
    char hop[0];
};

enum domain_mode {
    SIP_DOMAIN_AUTO,    /*!< This domain is auto-configured */
    SIP_DOMAIN_CONFIG,    /*!< This domain is from configuration */
};

struct domain {
    char domain[MAXHOSTNAMELEN];        /*!< SIP domain we are responsible for */
    char context[CW_MAX_EXTENSION];    /*!< Incoming context for this domain */
    enum domain_mode mode;            /*!< How did we find this domain? */
    CW_LIST_ENTRY(domain) list;        /*!< List mechanics */
};

static CW_LIST_HEAD_STATIC(domain_list, domain);    /*!< The SIP domain list */

int allow_external_domains;        /*!< Accept calls to external SIP domains? */

/*! \brief sip_auth: Creadentials for authentication to other SIP services */
struct sip_auth {
    char realm[CW_MAX_EXTENSION];  /*!< Realm in which these credentials are valid */
    char username[256];             /*!< Username */
    char secret[256];               /*!< Secret */
    char md5secret[256];            /*!< MD5Secret */
    struct sip_auth *next;          /*!< Next auth structure in list */
};

#define SIP_ALREADYGONE        (1 << 0)    /*!< Whether or not we've already been destroyed by our peer */
/*                         (1 << 1)    unused */
#define SIP_NOVIDEO        (1 << 2)    /*!< Didn't get video in invite, don't offer */
#define SIP_RINGING        (1 << 3)    /*!< Have sent 180 ringing */
#define SIP_PROGRESS_SENT    (1 << 4)    /*!< Have sent 183 message progress */
#define SIP_NEEDREINVITE    (1 << 5)    /*!< Do we need to send another reinvite? */
#define SIP_PENDINGBYE        (1 << 6)    /*!< Need to send bye after we ack? */
#define SIP_GOTREFER        (1 << 7)    /*!< Got a refer? */
#define SIP_PROMISCREDIR    (1 << 8)    /*!< Promiscuous redirection */
#define SIP_TRUSTRPID        (1 << 9)    /*!< Trust RPID headers? */
#define SIP_USEREQPHONE        (1 << 10)    /*!< Add user=phone to numeric URI. Default off */
#define SIP_REALTIME        (1 << 11)    /*!< Flag for realtime users */
#define SIP_USECLIENTCODE    (1 << 12)    /*!< Trust X-ClientCode info message */
#define SIP_OUTGOING        (1 << 13)    /*!< Is this an outgoing call? */
#define SIP_SELFDESTRUCT    (1 << 14)    
#define SIP_CAN_BYE		(1 << 15)	/*!< Can we send BYE for this dialog? */
/* --- Choices for DTMF support in SIP channel */
#define SIP_DTMF        (3 << 16)    /*!< three settings, uses two bits */
#define SIP_DTMF_RFC2833    (0 << 16)    /*!< RTP DTMF */
#define SIP_DTMF_INBAND        (1 << 16)    /*!< Inband audio, only for ULAW/ALAW */
#define SIP_DTMF_INFO        (2 << 16)    /*!< SIP Info messages */
#define SIP_DTMF_AUTO        (3 << 16)    /*!< AUTO switch between rfc2833 and in-band DTMF */
/* NAT settings */
#define SIP_NAT            (3 << 18)    /*!< four settings, uses two bits */
#define SIP_NAT_NEVER        (0 << 18)    /*!< No nat support */
#define SIP_NAT_RFC3581        (1 << 18)
#define SIP_NAT_ROUTE        (2 << 18)
#define SIP_NAT_ALWAYS        (3 << 18)
/* re-INVITE related settings */
#define SIP_REINVITE        (3 << 20)    /*!< two bits used */
#define SIP_CAN_REINVITE    (1 << 20)    /*!< allow peers to be reinvited to send media directly p2p */
#define SIP_REINVITE_UPDATE    (2 << 20)    /*!< use UPDATE (RFC3311) when reinviting this peer */
/* "insecure" settings */
#define SIP_INSECURE_PORT    (1 << 22)    /*!< don't require matching port for incoming requests */
#define SIP_INSECURE_INVITE    (1 << 23)    /*!< don't require authentication for incoming INVITEs */
/* Sending PROGRESS in-band settings */
#define SIP_PROG_INBAND        (3 << 24)    /*!< three settings, uses two bits */
#define SIP_PROG_INBAND_NEVER    (0 << 24)
#define SIP_PROG_INBAND_NO    (1 << 24)
#define SIP_PROG_INBAND_YES    (2 << 24)
/* Open Settlement Protocol authentication */
#define SIP_OSPAUTH        (3 << 26)    /*!< four settings, uses two bits */
#define SIP_OSPAUTH_NO        (0 << 26)
#define SIP_OSPAUTH_GATEWAY    (1 << 26)
#define SIP_OSPAUTH_PROXY    (2 << 26)
#define SIP_OSPAUTH_EXCLUSIVE    (3 << 26)
/* Call states */
#define SIP_CALL_ONHOLD        (1 << 28)     
#define SIP_CALL_LIMIT        (1 << 29)
/* Remote Party-ID Support */
#define SIP_SENDRPID        (1 << 30)
/* Did this connection increment the counter of in-use calls? */
#define SIP_INC_COUNT (1 << 31)

#define SIP_FLAGS_TO_COPY \
    (SIP_PROMISCREDIR | SIP_TRUSTRPID | SIP_SENDRPID | SIP_DTMF | SIP_REINVITE | \
     SIP_PROG_INBAND | SIP_OSPAUTH | SIP_USECLIENTCODE | SIP_NAT | \
     SIP_INSECURE_PORT | SIP_INSECURE_INVITE)


/* a new page of flags for peer */
#define SIP_PAGE2_RTCACHEFRIENDS    (1 << 0)
#define SIP_PAGE2_RTUPDATE        (1 << 1)
#define SIP_PAGE2_RTAUTOCLEAR        (1 << 2)
#define SIP_PAGE2_IGNOREREGEXPIRE    (1 << 3)
#define SIP_PAGE2_RT_FROMCONTACT     (1 << 4)
#define SIP_PAGE2_DYNAMIC	     (1 << 5)	/*!< Is this a dynamic peer? */

/* SIP request flags */
#define SIP_PKT_DEBUG             (1 << 0)    /*!< Debug this request */
#define SIP_PKT_WITH_TOTAG        (1 << 1)    /*!< This request has a to-tag */
#define FLAG_FATAL                (1 << 2)

static int global_rtautoclear = 120;

/*! \brief sip_pvt: PVT structures are used for each SIP conversation, ie. a call  */
struct sip_pvt {
    struct cw_object obj;
    struct cw_registry_entry *reg_entry;
    cw_mutex_t lock;            /*!< Channel private lock */
    char callid[80];            /*!< Global CallID */
    char randdata[80];            /*!< Random data */
    struct cw_codec_pref prefs;        /*!< codec prefs */
    unsigned int ocseq;            /*!< Current outgoing seqno */
    unsigned int icseq;            /*!< Current incoming seqno */
    cw_group_t callgroup;            /*!< Call group */
    cw_group_t pickupgroup;        /*!< Pickup group */
    int lastinvite;                /*!< Last Cseq of invite */
    unsigned int flags;            /*!< SIP_ flags */    
    int timer_t1;                /*!< SIP timer T1, ms (current RTT estimate) */
    int timer_t2;                /*!< SIP RFC timer T2, ms (maximum transmit interval for non-INVITES) */
    unsigned int sipoptions;        /*!< Supported SIP sipoptions on the other end */
    int capability;                /*!< Special capability (codec) */
    int jointcapability;            /*!< Supported capability at both ends (codecs ) */
    int peercapability;            /*!< Supported peer capability */
    int prefcodec;                /*!< Preferred codec (outbound only) */
    int noncodeccapability;
    int callingpres;            /*!< Calling presentation */
    int authtries;                /*!< Times we've tried to authenticate */
    int expiry;                /*!< How long we take to expire */
    int branch;                /*!< One random number */
    char tag[sizeof("01234567")]; /*!< Our tag */
    int tag_len;
    int sessionid;                /*!< SDP Session ID */
    int sessionversion;            /*!< SDP Session Version */

    int local;
    struct cw_connection *conn;
    struct cw_sockaddr_net ouraddr;            /*!< Our address as we know it */
    struct cw_sockaddr_net stunaddr;           /*!< Our address as seen externally (it may be changed by NAT) */
    struct cw_sockaddr_net peeraddr;          /*!< The peer address */
    struct cw_sockaddr_net redirip;         /*!< Where our RTP should be going if not to us */
    struct cw_sockaddr_net vredirip;        /*!< Where our Video RTP should be going if not to us */
    struct cw_sockaddr_net udptlredirip;    /*!< Where our T.38 UDPTL should be going if not to us */

    int redircodecs;            /*!< Redirect codecs */
    struct cw_channel *owner;        /*!< Who owns us */
    char exten[CW_MAX_EXTENSION];        /*!< Extension where to start */
    char refer_to[CW_MAX_EXTENSION];    /*!< Place to store REFER-TO extension */
    char referred_by[CW_MAX_EXTENSION];    /*!< Place to store REFERRED-BY extension */
    char refer_contact[CW_MAX_EXTENSION];    /*!< Place to store Contact info from a REFER extension */
    struct sip_pvt *refer_call;        /*!< Call we are referring */
    struct sip_route *route;        /*!< Head of linked list of routing steps (fm Record-Route) */
    int route_persistant;            /*!< Is this the "real" route? */
    char from[256];                /*!< The From: header */
    char useragent[256];            /*!< User agent in SIP request */
    char context[CW_MAX_CONTEXT];        /*!< Context for this call */
    char subscribecontext[CW_MAX_CONTEXT];    /*!< Subscribecontext */
    char fromdomain[MAXHOSTNAMELEN];    /*!< Domain to show in the from field */
    char fromuser[CW_MAX_EXTENSION];    /*!< User to show in the user field */
    char fromname[CW_MAX_EXTENSION];    /*!< Name to show in the user field */
    char tohost[MAXHOSTNAMELEN];        /*!< Host we should put in the "to" field */
    char language[MAX_LANGUAGE];        /*!< Default language for this call */
    char musicclass[MAX_MUSICCLASS];    /*!< Music on Hold class */
    char rdnis[256];            /*!< Referring DNIS */
    char theirtag[256];            /*!< Their tag */
    int theirtag_len;
    char username[256];            /*!< [user] name */
    char peername[256];            /*!< [peer] name, not set if [user] */
    char authname[256];            /*!< Who we use for authentication */
    char uri[256];                /*!< Original requested URI */
    char okcontacturi[256];            /*!< URI from the 200 OK on INVITE */
    char peersecret[256];            /*!< Password */
    char peermd5secret[256];
    struct sip_auth *peerauth;        /*!< Realm authentication */
    char cid_num[256];            /*!< Caller*ID */
    char cid_name[256];            /*!< Caller*ID */
    char fullcontact[128];            /*!< The Contact: that the UA registers with us */
    char accountcode[CW_MAX_ACCOUNT_CODE];    /*!< Account code */
    char our_contact[256];            /*!< Our contact header */
    char *rpid;                /*!< Our RPID header */
    char *rpid_from;            /*!< Our RPID From header */
    char realm[MAXHOSTNAMELEN];        /*!< Authorization realm */
    char nonce[256];            /*!< Authorization nonce */
    int noncecount;                /*!< Nonce-count */
    char opaque[256];            /*!< Opaque nonsense */
    char qop[80];                /*!< Quality of Protection, since SIP wasn't complicated enough yet. */
    char domain[MAXHOSTNAMELEN];        /*!< Authorization domain */
    char lastmsg[256];            /*!< Last Message sent/received */
    int amaflags;                /*!< AMA Flags */
    int pendinginvite;            /*!< Any pending invite */
#ifdef OSP_SUPPORT
    int osphandle;                /*!< OSP Handle for call */
    time_t ospstart;            /*!< OSP Start time */
    unsigned int osptimelimit;        /*!< OSP call duration limit */
#endif
    struct sip_request initreq;        /*!< Initial request */
    
    int maxtime;                /*!< Max time for first response */
    int initid;                /*!< Auto-congest ID if appropriate */
    int autokillid;                /*!< Auto-kill ID */
    time_t lastrtprx;            /*!< Last RTP received */
    time_t lastrtptx;            /*!< Last RTP sent */
    int rtptimeout;                /*!< RTP timeout time */
    int rtpholdtimeout;            /*!< RTP timeout when on hold */
    int rtpkeepalive;            /*!< Send RTP packets for keepalive */
    enum subscriptiontype subscribed;    /*!< Is this call a subscription?  */
    int stateid;
    int laststate;                          /*!< Last known extension state */
    int dialogver;
    
    struct cw_dsp *vad;            /*!< Voice Activation Detection dsp */
    
    struct sip_peer *peerpoke;        /*!< If this calls is to poke a peer, which one */
    struct sip_registry *registry;        /*!< If this is a REGISTER call, to which registry */
    struct cw_rtp *rtp;            /*!< RTP Session */
    struct cw_rtp *vrtp;            /*!< Video RTP session */
    struct sip_request *retrans;        /*!< Packets scheduled for re-transmission */
    struct cw_variable *chanvars;        /*!< Channel variables to set for call */
    struct sip_invite_param *options;    /*!< Options for INVITE */

#ifdef ENABLE_SRTP
    struct sip_srtp *srtp;
#endif

    struct cw_jb_conf jbconf;

    char ruri[256];                /*!< REAL Original requested URI */

    //struct cw_tpkt *tpkt;            /*!< T.38 TPKT session */
    cw_udptl_t *udptl;        /*!< T.38 UDPTL session */
    int t38capability;            /*!< Our T38 capability */
    int t38peercapability;            /*!< Peers T38 capability */
    int t38jointcapability;            /*!< Supported T38 capability at both ends */
    sip_t38_status_t t38state; 	/*!< T.38 state : 
				    0 - not enabled, 
					1 - offered from local - direct, 
					2 - offered from local - reinvite, 
					3 - offered from peer - direct, 
					4 - offered from peer - reinvite, 
					5 - negotiated (enabled) 
				*/
    struct cw_dsp *vadtx;            /*!< Voice Activation Detection dsp on TX */
    int udptl_active;            /*!<  */
};


/*! \brief Structure for SIP user data. User's place calls to us */
struct sip_user {
    /* Users who can access various contexts */
    struct cw_object obj;
    struct cw_registry_entry *reg_entry_byname;
    char name[80];        /*!< Name */
    char secret[80];        /*!< Password */
    char md5secret[80];        /*!< Password in md5 */
    char context[CW_MAX_CONTEXT];    /*!< Default context for incoming calls */
    char subscribecontext[CW_MAX_CONTEXT];    /* Default context for subscriptions */
    char cid_num[80];        /*!< Caller ID num */
    char cid_name[80];        /*!< Caller ID name */
    char accountcode[CW_MAX_ACCOUNT_CODE];    /* Account code */
    char language[MAX_LANGUAGE];    /*!< Default language for this user */
    char musicclass[MAX_MUSICCLASS];/*!< Music on Hold class */
    char useragent[256];        /*!< User agent in SIP request */
    struct cw_codec_pref prefs;    /*!< codec prefs */
    cw_group_t callgroup;        /*!< Call group */
    cw_group_t pickupgroup;    /*!< Pickup Group */
    unsigned int flags;        /*!< SIP flags */    
    unsigned int sipoptions;    /*!< Supported SIP options */
    struct cw_flags flags_page2;    /*!< SIP_PAGE2 flags */
    int amaflags;            /*!< AMA flags for billing */
    int callingpres;        /*!< Calling id presentation */
    int capability;            /*!< Codec capability */
    int inUse;            /*!< Number of calls in use */
    int call_limit;            /*!< Limit of concurrent calls */
    struct cw_acl *acl;        /*!< ACL setting */
    struct cw_variable *chanvars;    /*!< Variables to set for channel created by user */
};

/* Structure for SIP peer data, we place calls to peers if registered  or fixed IP address (host) */
struct sip_peer {
    struct cw_object obj;
    struct cw_registry_entry *reg_entry_byname, *reg_entry_byaddr;
    char name[80];        /*!< Name */
    char secret[80];        /*!< Password */
    char md5secret[80];        /*!< Password in MD5 */
    struct sip_auth *auth;        /*!< Realm authentication list */
    char context[CW_MAX_CONTEXT];    /*!< Default context for incoming calls */
    char subscribecontext[CW_MAX_CONTEXT];    /*!< Default context for subscriptions */
    char username[80];        /*!< Temporary username until registration */ 
    char accountcode[CW_MAX_ACCOUNT_CODE];    /*!< Account code */
    int amaflags;            /*!< AMA Flags (for billing) */
    char tohost[MAXHOSTNAMELEN];    /*!< If not dynamic, IP address or hostname */
    char proxyhost[MAXHOSTNAMELEN];    /*!< IP address or hostname of proxy (if any) */
    char regexten[CW_MAX_EXTENSION]; /*!< Extension to register (if regcontext is used) */
    char fromuser[80];        /*!< From: user when calling this peer */
    char fromdomain[MAXHOSTNAMELEN];    /*!< From: domain when calling this peer */
    char fullcontact[256];        /*!< Contact registered with us (not in sip.conf) */
    char cid_num[80];        /*!< Caller ID num */
    char cid_name[80];        /*!< Caller ID name */
    int callingpres;        /*!< Calling id presentation */
    int inUse;            /*!< Number of calls in use */
    int call_limit;            /*!< Limit of concurrent calls */
    char vmexten[CW_MAX_EXTENSION]; /*!< Dialplan extension for MWI notify message*/
    char mailbox[CW_MAX_EXTENSION]; /*!< Mailbox setting for MWI checks */
    char language[MAX_LANGUAGE];    /*!<  Default language for prompts */
    char musicclass[MAX_MUSICCLASS];/*!<  Music on Hold class */
    char useragent[256];        /*!<  User agent in SIP request (saved from registration) */
    struct cw_codec_pref prefs;    /*!<  codec prefs */
    int lastmsgssent;
    time_t    lastmsgcheck;        /*!<  Last time we checked for MWI */
    unsigned int flags;        /*!<  SIP flags */    
    unsigned int sipoptions;    /*!<  Supported SIP options */
    struct cw_flags flags_page2;    /*!<  SIP_PAGE2 flags */
    int expire;            /*!<  When to expire this peer registration */
    int capability;            /*!<  Codec capability */
    int rtptimeout;            /*!<  RTP timeout */
    int rtpholdtimeout;        /*!<  RTP Hold Timeout */
    int rtpkeepalive;        /*!<  Send RTP packets for keepalive */
    cw_group_t callgroup;        /*!<  Call group */
    cw_group_t pickupgroup;    /*!<  Pickup group */
    struct cw_sockaddr_net addr;    /*!<  IP address of peer */

    /* Qualification */
    int pokeexpire;            /*!<  When to expire poke (qualify= checking) */
    /* T1 & T2 are configurable per-peer because they are sometimes needed if
     * paths via geostationary satellites, moonbases etc. are involved. In which
     * case they need adjusting at both ends of course.
     */
    int timer_t1;            /*!<  How long last response took (in ms), or -1 for no response */
    int timer_t2;            /*!< SIP RFC timer T2, ms (maximum transmit interval for non-INVITES) */
    int maxms;            /*!<  Max ms we will accept for the host to be up, 0 to not monitor */

    struct cw_sockaddr_net defaddr;    /*!<  Default IP address, used until registration */
    struct cw_acl *acl;        /*!<  Access control list */
    struct cw_variable *chanvars;    /*!<  Variables to set for     channel created by user */
    int lastmsg;
};

/* States for outbound registrations (with register= lines in sip.conf */
#define REG_STATE_UNREGISTERED 0
#define REG_STATE_REGSENT      1
#define REG_STATE_AUTHSENT     2
#define REG_STATE_REGISTERED   3
#define REG_STATE_REJECTED     4
#define REG_STATE_TIMEOUT      5
#define REG_STATE_NOAUTH       6
#define REG_STATE_FAILED       7
#define REG_STATE_SHUTDOWN     8


/*! \brief sip_registry: Registrations with other SIP proxies */
struct sip_registry {
    struct cw_object obj;
    struct sip_registry *next;
    char name[80];        /*!< Name */
    int portno;            /*!<  Optional port override */
    char username[80];        /*!<  Who we are registering as */
    char authuser[80];        /*!< Who we *authenticate* as */
    char hostname[MAXHOSTNAMELEN];    /*!< Domain or host we register to */
    char secret[80];        /*!< Password in clear text */    
    char md5secret[80];        /*!< Password in md5 */
    char contact[256];        /*!< Contact extension */
    char random[80];
    int expire;            /*!< Sched ID of expiration */
    int regattempts;        /*!< Number of attempts (since the last success) */
    int timeout;             /*!< sched id of sip_reg_timeout */
    int refresh;            /*!< How often to refresh */
    struct sip_pvt *dialogue;        /*!< create a sip_pvt structure for each outbound "registration call" in progress */
    int regstate;            /*!< Registration state (see above) */
    int callid_valid;        /*!< 0 means we haven't chosen callid for this registry yet. */
    char callid[80];        /*!< Global CallID for this registry */
    unsigned int ocseq;        /*!< Sequence number we got to for REGISTERs for this registry */
     
                    	    /* Saved headers */
    char realm[MAXHOSTNAMELEN];    /*!< Authorization realm */
    char nonce[256];        /*!< Authorization nonce */
    char domain[MAXHOSTNAMELEN];    /*!< Authorization domain */
    char opaque[256];        /*!< Opaque nonsense */
    char qop[80];            /*!< Quality of Protection. */
    int noncecount;            /*!< Nonce-count */
 
    char lastmsg[256];        /*!< Last Message sent/received */
};

/* The registry list is only changed by config reload and thus is protected
 * by the reload lock rather than having a separate lock of its own.
 */
static struct sip_registry *regl;
static struct sip_registry **regl_last = &regl;


pthread_rwlock_t sip_reload_lock;

static struct cw_sockaddr_net externip;
static char externhost[MAXHOSTNAMELEN] = "";
static time_t externexpire = 0;
static int externrefresh = 10;
static struct cw_acl *localaddr;

/* The list of manual NOTIFY types we know how to send */
struct cw_config *notify_types;

static struct sip_auth *authl;          /*!< Authentication list */

static char *get_header(const struct sip_request *req, const char *name, size_t name_len, const char *alias, size_t alias_len);
static void transmit_error(struct sip_request *req, const char *status);
static void transmit_response(struct sip_pvt *p, const char *status, struct sip_request *req, int reliable);
static void transmit_response_with_sdp(struct sip_pvt *p, const char *status, struct sip_request *req, int retrans);
static void transmit_response_with_unsupported(struct sip_pvt *p, const char *status, struct sip_request *req, char *unsupported);
static void transmit_response_with_auth(struct sip_pvt *p, const char *status, struct sip_request *req, const char *rand, int reliable, const char *header, int stale);
static void transmit_ack(struct sip_pvt *p, struct sip_request *req, int newbranch);
static void transmit_request_with_auth(struct sip_pvt *p, enum sipmethod sipmethod, int inc, int reliable, int newbranch);
static int transmit_invite(struct sip_pvt *p, enum sipmethod sipmethod, int sendsdp, int init);
static void transmit_reinvite_with_sdp(struct sip_pvt *p);
static void transmit_info_with_digit(struct sip_pvt *p, char digit, unsigned int duration);
static void transmit_info_with_vidupdate(struct sip_pvt *p);
static int transmit_message_with_text(struct sip_pvt *p, const char *mimetype, const char *disposition, const char *text);
static int transmit_refer(struct sip_pvt *p, const char *dest);
static int sip_sipredirect(struct sip_pvt *p, const char *dest);
static struct sip_peer *temp_peer(const char *name);
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, const char *header, const char *respheader, enum sipmethod sipmethod, int init);
static void free_old_route(struct sip_route *route);
static int build_reply_digest(struct sip_pvt *p, enum sipmethod method, struct cw_dynstr *digest);
static int update_call_counter(struct sip_pvt *fup, int event);
static struct sip_peer *build_peer(const char *name, struct cw_variable *v, int realtime, int sip_running);
static struct sip_user *build_user(const char *name, struct cw_variable *v, int realtime);
static int expire_register(void *data);
static int callevents = 0;

static struct cw_channel *sip_request_call(const char *type, int format, void *data, int *cause);
static int sip_devicestate(void *data);
static int sip_sendtext(struct cw_channel *ast, const char *text);
static int sip_call(struct cw_channel *ast, const char *dest);
static int sip_hangup(struct cw_channel *ast);
static int sip_answer(struct cw_channel *ast);
static struct cw_frame *sip_read(struct cw_channel *ast);
static int sip_write(struct cw_channel *ast, struct cw_frame *frame);
static int sip_indicate(struct cw_channel *ast, int condition);
static int sip_transfer(struct cw_channel *ast, const char *dest);
static int sip_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);
static int sip_senddigit(struct cw_channel *ast, char digit);
static void clear_realm_authentication(struct sip_auth **authlist);                           /* Clear realm authentication list */
static struct sip_auth *add_realm_authentication(struct sip_auth *authlist, char *configuration, int lineno);   /* Add realm authentication in list */
static struct sip_auth *find_realm_authentication(struct sip_auth *authlist, char *realm);         /* Find authentication for a specific realm */
static int check_sip_domain(const char *domain, char *context, size_t len); /* Check if domain is one of our local domains */
static void append_date(struct sip_request *req);    /* Append date to SIP packet */
static const struct cfsubscription_types *find_subscription_type(enum subscriptiontype subtype);
static void transmit_state_notify(struct sip_pvt *p, int state, int full, int substate, int timeout);

static void transmit_response_with_t38_sdp(struct sip_pvt *p, const char *status, struct sip_request *req, int retrans);
static void transmit_reinvite_with_t38_sdp(struct sip_pvt *p);
static int sip_handle_t38_reinvite(struct cw_channel *chan, struct sip_pvt *pvt, int reinvite); /* T38 negotiation helper function */
static enum cw_bridge_result sip_bridge(struct cw_channel *c0, struct cw_channel *c1, int flag, struct cw_frame **fo,struct cw_channel **rc, int timeoutms); /* Function to bridge to SIP channels if T38 support enabled */
static const char *nat2str(int nat);
static int sip_poke_peer(void *data);


static int sipsock_read(struct cw_connection *conn);

static struct cw_connection_tech tech_sip = {
	.name = "SIP",
	.read = sipsock_read,
};



#ifdef ENABLE_SRTP
/*
 * SRTP sdescriptions
 * Specified in: draft-ietf-mmusic-sdescriptions-12.txt
 */

static struct sip_srtp *sip_srtp_alloc(void)
{
	struct sip_srtp *srtp = malloc(sizeof(*srtp));

	memset(srtp, 0, sizeof(*srtp));
	return srtp;
}

static void sip_srtp_destroy(struct sip_srtp *srtp)
{
	free(srtp->a_crypto);
	srtp->a_crypto = NULL;
}


static int setup_crypto(struct sip_pvt *p)
{
	if (!cw_srtp_is_registered())
		return -1;

	p->srtp = sip_srtp_alloc();
	if (!p->srtp)
		return -1;

	if (cw_srtp_get_random(p->srtp->local_key,
				sizeof(p->srtp->local_key)) < 0) {
		sip_srtp_destroy(p->srtp);
		p->srtp = NULL;
		return -1;
	}

	cw_base64encode(p->srtp->local_key64, p->srtp->local_key,
			 SRTP_MASTER_LEN, sizeof(p->srtp->local_key64));
	return 0;
}

static int set_crypto_policy(struct cw_srtp_policy *policy,
			     int suite_val, const unsigned char *master_key,
			     unsigned long ssrc, int inbound)
{
	const unsigned char *master_salt = NULL;
	master_salt = master_key + SRTP_MASTERKEY_LEN;
	if (cw_srtp_policy_set_master_key(policy,
					   master_key, SRTP_MASTERKEY_LEN,
					   master_salt, SRTP_MASTERSALT_LEN) < 0)
		return -1;


	if (cw_srtp_policy_set_suite(policy, suite_val)) {
		cw_log(CW_LOG_WARNING, "Could not set remote SRTP suite\n");
		return -1;
	}

	cw_srtp_policy_set_ssrc(policy, ssrc, inbound);

	return 0;
}

static int activate_crypto(struct sip_pvt *p, int suite_val,
			   unsigned char *remote_key)
{
	struct cw_srtp_policy *local_policy = NULL;
	struct cw_srtp_policy *remote_policy = NULL;
	int res = -1;
	struct sip_srtp *srtp = p->srtp;

	if (!srtp)
		return -1;

	local_policy = cw_srtp_policy_alloc();
	if (!local_policy)
		goto err;

	remote_policy = cw_srtp_policy_alloc();
	if (!remote_policy) {
		goto err;
	}

	if (set_crypto_policy(local_policy, suite_val, srtp->local_key,
			      cw_rtp_get_ssrc(p->rtp), 0) < 0)
		goto err;

	if (set_crypto_policy(remote_policy, suite_val, remote_key, 0, 1) < 0)
		goto err;

	if (cw_rtp_add_srtp_policy(p->rtp, local_policy)) {
		cw_log(CW_LOG_WARNING, "Could not set local SRTP policy\n");
		goto err;
	}

	if (cw_rtp_add_srtp_policy(p->rtp, remote_policy)) {
		cw_log(CW_LOG_WARNING, "Could not set remote SRTP policy\n");
		goto err;
	}


	cw_log(CW_LOG_DEBUG, "SRTP policy activated\n");
	res = 0;

err:
	if (local_policy)
		cw_srtp_policy_destroy(local_policy);

	if (remote_policy)
		cw_srtp_policy_destroy(remote_policy);
	return res;
}

static int process_crypto(struct sip_pvt *p, const char *attr)
{
	char *str = NULL;
	char *name = NULL;
	char *tag = NULL;
	char *suite = NULL;
	char *key_params = NULL;
	char *key_param = NULL;
	char *session_params = NULL;
	char *key_salt = NULL;
	char *lifetime = NULL;
	int found = 0;
	int attr_len = strlen(attr);
	int key_len = 0;
	unsigned char remote_key[SRTP_MASTER_LEN];
	int suite_val = 0;
	struct sip_srtp *srtp = p->srtp;

	if (!cw_srtp_is_registered())
		return -1;

	/* Crypto already accepted */
	if (srtp && srtp->a_crypto)
		return -1;

	str = strdupa(attr);

	name = strsep(&str, ":");
	tag = strsep(&str, " ");
	suite = strsep(&str, " ");
	key_params = strsep(&str, " ");
	session_params = strsep(&str, " ");

	if (!tag || !suite) {
		cw_log(CW_LOG_WARNING, "Unrecognized a=%s", attr);
		return -1;
	}

	if (session_params) {
		cw_log(CW_LOG_WARNING, "Unsupported crypto parameters: %s",
			session_params);
		return -1;
	}

	if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_80")) {
		suite_val = CW_AES_CM_128_HMAC_SHA1_80;
	} else if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_32")) {
		suite_val = CW_AES_CM_128_HMAC_SHA1_32;
	} else {
		cw_log(CW_LOG_WARNING, "Unsupported crypto suite: %s",
			suite);
		return -1;
	}

	while ((key_param = strsep(&key_params, ";"))) {
		char *method = NULL;
		char *info = NULL;

		method = strsep(&key_param, ":");
		info = strsep(&key_param, ";");

		if (!strcmp(method, "inline")) {
			key_salt = strsep(&info, "|");
			lifetime = strsep(&info, "|");

			if (lifetime) {
				cw_log(CW_LOG_NOTICE, "Crypto life time unsupported: %s\n",
					attr);
				continue;
			}

/* 			if (info || strncmp(lifetime, "2^", 2)) { */
/* 				cw_log(CW_LOG_NOTICE, "MKI unsupported: %s\n", */
/* 					attr); */
/* 				continue; */
/* 			} */

			found = 1;
			break;
		}
	}

	if (!found) {
		cw_log(CW_LOG_NOTICE, "SRTP crypto offer not acceptable\n");
		return -1;
	}

	if (!srtp) {
		setup_crypto(p);
		srtp = p->srtp;
	}

	key_len = cw_base64decode(remote_key, key_salt, sizeof(remote_key));
	if (key_len != SRTP_MASTER_LEN) {
		cw_log(CW_LOG_WARNING, "SRTP sdescriptions key %d != %d\n",
			key_len, SRTP_MASTER_LEN);
		return -1;
	}

	if (activate_crypto(p, suite_val, remote_key) < 0)
		return -1;

	srtp->a_crypto = malloc(attr_len+11);
	snprintf(srtp->a_crypto, attr_len+10,
		// "a=crypto:%s %s inline:%s\r\n",
		 "a=crypto:%s %s inline:%s",
		 tag, suite, srtp->local_key64);

	return 0;
}
#endif


static int peerbyname_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct sip_peer *peer_a = container_of(*objp_a, struct sip_peer, obj);
	const struct sip_peer *peer_b = container_of(*objp_b, struct sip_peer, obj);

	return strcasecmp(peer_a->name, peer_b->name);
}

static int peerbyname_object_match(struct cw_object *obj, const void *pattern)
{
	const struct sip_peer *peer = container_of(obj, struct sip_peer, obj);

	return !strcmp(peer->name, pattern);
}

struct cw_registry peerbyname_registry = {
	.name = "Peer (by name)",
	.qsort_compare = peerbyname_qsort_compare_by_name,
	.match = peerbyname_object_match,
};


static int peerbyaddr_qsort_compare_by_addr(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct sip_peer *peer_a = container_of(*objp_a, struct sip_peer, obj);
	const struct sip_peer *peer_b = container_of(*objp_b, struct sip_peer, obj);

	return cw_sockaddr_cmp(&peer_a->addr.sa, &peer_b->addr.sa, -1, 1);
}

static int peerbyaddr_object_match(struct cw_object *obj, const void *pattern)
{
	const struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	const struct sockaddr *sa = pattern;
	int withport = cw_sockaddr_get_port(sa);

	return (withport || cw_test_flag(peer, SIP_INSECURE_PORT))
			? !cw_sockaddr_cmp(&peer->addr.sa, sa, -1, withport)
			: 0;
}

struct cw_registry peerbyaddr_registry = {
	.name = "Peer (by addr)",
	.qsort_compare = peerbyaddr_qsort_compare_by_addr,
	.match = peerbyaddr_object_match,
};


static int userbyname_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct sip_user *user_a = container_of(*objp_a, struct sip_user, obj);
	const struct sip_user *user_b = container_of(*objp_b, struct sip_user, obj);

	return strcasecmp(user_a->name, user_b->name);
}

static int userbyname_object_match(struct cw_object *obj, const void *pattern)
{
	const struct sip_user *user = container_of(obj, struct sip_user, obj);

	return !strcmp(user->name, pattern);
}

struct cw_registry userbyname_registry = {
	.name = "User (by name)",
	.qsort_compare = userbyname_qsort_compare_by_name,
	.match = userbyname_object_match,
};


static unsigned int dialogue_hash(struct sip_pvt *dialogue)
{
	unsigned int hash = 0;
	char *p;

	for (p = dialogue->callid; *p; p++)
		hash = cw_hash_add(hash, *p);

	return hash;
}


static int dialogue_qsort_compare_by_name_and_theirtag(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct sip_pvt *dialogue_a = container_of(*objp_a, struct sip_pvt, obj);
	const struct sip_pvt *dialogue_b = container_of(*objp_b, struct sip_pvt, obj);
	int ret;

	if (!(ret = strcmp(dialogue_a->callid, dialogue_b->callid)))
		ret = strcmp(dialogue_a->theirtag, dialogue_b->theirtag);

	return ret;
}

struct dialogue_key {
	char *callid;
	char *tag;
	int taglen;
};

static int dialogue_object_match(struct cw_object *obj, const void *pattern)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	const struct dialogue_key *key = pattern;

	return !strcmp(dialogue->callid, key->callid);
}

struct cw_registry dialogue_registry = {
	.name = "SIP dialogue",
	.qsort_compare = dialogue_qsort_compare_by_name_and_theirtag,
	.match = dialogue_object_match,
};


static void dialogue_release(struct cw_object *obj)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);

	if (dialogue->rtp)
		cw_rtp_destroy(dialogue->rtp);

	if (dialogue->vrtp)
		cw_rtp_destroy(dialogue->vrtp);

	if (dialogue->udptl)
		cw_udptl_destroy(dialogue->udptl);

#if 0
	if (dialogue->tpkt) {
		cw_tpkt_destroy(dialogue->tpkt);
		dialogue->tpkt = NULL;
	}
#endif

	if (dialogue->route) {
		free_old_route(dialogue->route);
		dialogue->route = NULL;
	}

	if (dialogue->options) {
		if (dialogue->options->distinctive_ring)
			cw_object_put(dialogue->options->distinctive_ring);
		if (dialogue->options->vxml_url)
			cw_object_put(dialogue->options->vxml_url);
		if (dialogue->options->uri_options)
			cw_object_put(dialogue->options->uri_options);
#ifdef OSP_SUPPORT
		if (dialogue->options->osptoken)
			cw_object_put(dialogue->options->osptoken);
#endif
		cw_dynstr_free(&dialogue->options->auth);
		free(dialogue->options);
	}

	free(dialogue->rpid);
	free(dialogue->rpid_from);

	if (dialogue->chanvars)
		cw_variables_destroy(dialogue->chanvars);

#ifdef ENABLE_SRTP
	if (dialogue->srtp)
		sip_srtp_destroy(dialogue->srtp);
#endif

	cw_object_put(dialogue->conn);

	cw_mutex_destroy(&dialogue->lock);

	cw_object_destroy(dialogue);
	free(dialogue);
}


/*! \brief Definition of this channel for PBX channel registration */
static const struct cw_channel_tech sip_tech =
{
    .type = channeltype,
    .description = "Session Initiation Protocol (SIP)",
    .capabilities = ((CW_FORMAT_MAX_AUDIO << 1) - 1),
    .properties = CW_CHAN_TP_WANTSJITTER | CW_CHAN_TP_CREATESJITTER,
    .requester = sip_request_call,
    .devicestate = sip_devicestate,
    .call = sip_call,
    .hangup = sip_hangup,
    .answer = sip_answer,
    .read = sip_read,
    .write = sip_write,
    .write_video = sip_write,
    .indicate = sip_indicate,
    .transfer = sip_transfer,
    .fixup = sip_fixup,
    .send_digit = sip_senddigit,
    .bridge = sip_bridge,
//    .bridge = cw_rtp_bridge,
    .send_text = sip_sendtext,
};



static void sip_debug_ports(struct sip_pvt *p)
{
	if (option_debug > 8) {
		if (p->owner)
			cw_log(CW_LOG_DEBUG,"DEBUG PORTS CHANNEL %s\n", p->owner->name);

		if (p->udptl) {
			cw_log(CW_LOG_DEBUG, "DEBUG PORTS T.38 UDPTL is at port %#l@\n", cw_udptl_get_us(p->udptl));
		}

		if (p->rtp) {
			cw_log(CW_LOG_DEBUG, "DEBUG PORTS rtp is at port %#l@\n", cw_rtp_get_us(p->rtp));
		}
	}
}

/*! \brief  find_sip_method: Find SIP method from header
 * Strictly speaking, SIP methods are case SENSITIVE, but we don't check 
 * following Jon Postel's rule: Be gentle in what you accept, strict with what you send */
static enum sipmethod find_sip_method(const char *msg)
{
    int i;

    if (!cw_strlen_zero(msg))
    {
        for (i = 1; i < arraysize(sip_methods); i++)
        {
            if (!strcasecmp(sip_methods[i].text, msg)) 
                return (enum sipmethod)i;
        }
    }

    return SIP_UNKNOWN;
}

/*! \brief  parse_sip_options: Parse supported header in incoming packet */
static unsigned int parse_sip_options(struct sip_pvt *pvt, char *supported)
{
    char *next = NULL;
    char *sep = NULL;
    char *temp = cw_strdupa(supported);
    int i;
    unsigned int profile = 0;

    if (cw_strlen_zero(supported))
        return 0;

    if (option_debug > 2 && sipdebug)
        cw_log(CW_LOG_DEBUG, "Begin: parsing SIP \"Supported: %s\"\n", supported);

    next = temp;
    while (next)
    {
        char res = 0;

        if ((sep = strchr(next, ',')) != NULL)
        {
            *sep = '\0';
            sep++;
        }
        while (*next == ' ')    /* Skip spaces */
            next++;
        if (option_debug > 2 && sipdebug)
            cw_log(CW_LOG_DEBUG, "Found SIP option: -%s-\n", next);
        for (i = 0;  (i < (sizeof(sip_options) / sizeof(sip_options[0])))  &&  !res;  i++)
        {
            if (!strcasecmp(next, sip_options[i].text))
            {
                profile |= sip_options[i].id;
                res = 1;
                if (option_debug > 2  &&  sipdebug)
                    cw_log(CW_LOG_DEBUG, "Matched SIP option: %s\n", next);
            }
        }
        if (!res) 
            if (option_debug > 2 && sipdebug)
                cw_log(CW_LOG_DEBUG, "Found no match for SIP option: %s (Please file bug report!)\n", next);
        next = sep;
    }
    if (pvt)
    {
        pvt->sipoptions = profile;
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "* SIP extension value: %u for call %s\n", profile, pvt->callid);
    }
    return profile;
}

/*! \brief  sip_debug_test_addr: See if we pass debug IP filter */
static inline int sip_debug_test_addr(struct sockaddr *addr)
{
	int res = 0;

	while (pthread_rwlock_rdlock(&debugacl.lock) == EAGAIN)
		usleep(1000);

	if (unlikely(debugacl.acl))
		res = cw_acl_check(debugacl.acl, addr, 0);

	pthread_rwlock_unlock(&debugacl.lock);

	return res;
}

/*! \brief  sip_is_nat_needed: Check if we need NAT or STUN */
static inline int sip_is_nat_needed(struct sip_pvt *dialogue)
{
	unsigned int nat;
	int ret = 0;

	if (!(nat = cw_test_flag(dialogue, SIP_NAT)))
		nat = cw_test_flag(&global_flags, SIP_NAT);

	if (nat & SIP_NAT_ALWAYS)
		ret = 1;
	else if (nat & SIP_NAT_ROUTE)
		ret = !dialogue->local;

	if (option_debug > 5)
		cw_log(CW_LOG_DEBUG, "Nat is %sneeded (nat = %s, local = %d)\n", (ret ? "" : "not "), nat2str(nat), dialogue->local);

	return ret;
}

/*! \brief  sip_debug_test_pvt: Test PVT for debugging output */
static inline int sip_debug_test_pvt(struct sip_pvt *p) 
{
	return sip_debug_test_addr(&p->peeraddr.sa);
}


/*! \brief  __sip_xmit: Transmit SIP message */
static int __sip_xmit(struct cw_connection *conn, struct cw_sockaddr_net *from, struct cw_sockaddr_net *to, struct sip_request *msg)
{
	int res;

	res = cw_sendfromto(conn->sock, msg->pkt.data, msg->pkt.used, 0, &from->sa, sizeof(*from), &to->sa, sizeof(*to));

	if (res == msg->pkt.used)
		res = 0;
	else {
		cw_log(CW_LOG_WARNING, "sip_xmit of \"%.16s...\" (len %d) from %#ll@ to %#ll@ returned %d: %s\n", msg->pkt.data, msg->pkt.used, from, to, res, strerror(errno));
		res = -1;
	}

	return res;
}

static void sip_destroy(struct sip_pvt *p);


/*! \brief  cw_sip_ouraddrfor: NAT fix - decide which IP address to use for CallWeaver.org server? */
static int cw_sip_ouraddrfor(struct sip_pvt *dialogue, struct sockaddr *peer_sa, socklen_t peer_salen)
{
	struct cw_sockaddr_net ouraddr;
	struct sockaddr_in6 lpeer_sin6;
	int local, ret;

	local = 1;

	if (localaddr)
		local = cw_acl_check(localaddr, peer_sa, 0);

again:
	ouraddr.sa.sa_family = AF_UNSPEC;

	/* If the peer is non-local and we are using a specific externally facing address then use it */
	if (!local && (externip.sa.sa_family != AF_UNSPEC || externhost[0])) {
		time_t now = time(NULL);

		if (externhost[0] && (externip.sa.sa_family != peer_sa->sa_family || (externexpire && now >= externexpire))) {
			const struct addrinfo hints = {
				.ai_flags = AI_ADDRCONFIG | AI_IDN | (cw_sockaddr_is_mapped(peer_sa) ? AI_V4MAPPED : 0),
				.ai_family = peer_sa->sa_family,
				.ai_socktype = SOCK_DGRAM,
			};
			struct addrinfo *addrs;
			int err;

			externexpire = now + externrefresh;

			if (!(err = cw_getaddrinfo(externhost, "0", &hints, &addrs, NULL))) {
				memcpy(&externip, addrs->ai_addr, addrs->ai_addrlen);
				freeaddrinfo(addrs);
			} else
				cw_log(CW_LOG_WARNING, "%s: %s\n", externhost, gai_strerror(err));
		}

		if (externip.sa.sa_family == peer_sa->sa_family) {
			cw_log(CW_LOG_DEBUG, "Target address %#l@ is not local, using externip for our address\n", peer_sa);
			ouraddr.sa = externip.sa;
		}
	}

	/* If we still have nothing try see what the kernel's routing suggests.
	 * Note that if we are using multiple aliases on the same subnet outgoing
	 * dialogues will likely only use one of them. This is expected. It's the
	 * way networking works.
	 */
	if (ouraddr.sa.sa_family == AF_UNSPEC) {
		int s;

		if ((s = socket_cloexec(peer_sa->sa_family, SOCK_DGRAM, 0)) >= 0) {
			if (!connect(s, peer_sa, peer_salen)) {
				socklen_t slen = sizeof(ouraddr);
				getsockname(s, &ouraddr.sa, &slen);
			} else if (sipdebug)
				cw_log(CW_LOG_DEBUG, "connect %ll@: %s\n", peer_sa, strerror(errno));
			close(s);
		}
	}

	ret = 0;

	if (ouraddr.sa.sa_family == AF_UNSPEC || cw_sockaddr_cmp(&dialogue->ouraddr.sa, &ouraddr.sa, -1, 0)) {
		struct cw_connection *conn = NULL;

		ret = -1;

		/* Now try and find a connection suitable for using this local address.
		 * If we have something bound specifically we'll use that otherwise
		 * we'll look for something generic.
		 */
		if (ouraddr.sa.sa_family == AF_UNSPEC || !(conn = cw_connection_find(&tech_sip, &ouraddr.sa, 0))) {
			struct cw_sockaddr_net addr;

			memset(&addr, 0, sizeof(addr));
			addr.sa.sa_family = ouraddr.sa.sa_family;

			if (ouraddr.sa.sa_family == AF_UNSPEC || !(conn = cw_connection_find(&tech_sip, &addr.sa, 0))) {
				if (!cw_sockaddr_is_mapped(peer_sa)) {
					cw_sockaddr_map(&lpeer_sin6, (struct sockaddr_in *)peer_sa);
					peer_sa = (struct sockaddr *)&lpeer_sin6;
					peer_salen = sizeof(lpeer_sin6);
					goto again;
				}

				if (ouraddr.sa.sa_family != AF_UNSPEC)
					cw_log(CW_LOG_ERROR, "no sockets in family %d available to talk to %ll@\n", ouraddr.sa.sa_family, peer_sa);
				else
					cw_log(CW_LOG_ERROR, "no suitable connections for path from %ll@ to %ll@\n", &ouraddr.sa, peer_sa);
			}
		}

		if (conn) {
			cw_sockaddr_set_port(&ouraddr.sa, cw_sockaddr_get_port(&conn->addr));

			if (dialogue->conn)
				cw_object_put(dialogue->conn);

			dialogue->conn = conn;
			dialogue->stunaddr = dialogue->ouraddr = ouraddr;
			memcpy(&dialogue->peeraddr, peer_sa, peer_salen);
			dialogue->local = local;

			if (!local && stunserver_ip.sin_family != AF_UNSPEC) {
#if 0
				/* FIXME: only enable after we are able to defer message
				 * building to after STUN completion!
				 */
				dialogue->stunaddr.sa.sa_family = AF_UNSPEC;
#endif
                                cw_stun_bindrequest(conn->sock, &conn->addr, conn->addrlen, (struct sockaddr *)&stunserver_ip, sizeof(stunserver_ip), &dialogue->stunaddr.sin);
			}

			ret = 0;
		}
	}

	return ret;
}


/*! \brief  retrans_pkt: Retransmit SIP message if no answer */
static int retrans_pkt(void *data)
{
    struct sip_request *msg = data, **prev;

    cw_mutex_lock(&msg->owner->lock);

    /* It's possible we just waited while an ack was sent for a reply
     * to a previous transmission of this packet. If so the ack code
     * will have changed the method for this packet to unknown to
     * signal that no further retransmissions are required, however
     * it is still down to us to dequeue the packet once we're done
     * looking at it.
     */

    if (msg->method != SIP_UNKNOWN
    && ((msg->method == SIP_INVITE && (msg->timer_a < 0 || msg->timer_a < rfc_timer_b))
    || (msg->method != SIP_INVITE && msg->timer_a < RFC_TIMER_F)))
    {
        int reschedule;

	if (msg->timer_a >= 0) {
            /* Re-schedule using timer_a and timer_t1 */
            if (!msg->timer_a)
                msg->timer_a = 2 ;
            else
                msg->timer_a = 2 * msg->timer_a;	/* Double each time */

            reschedule = msg->timer_a * msg->owner->timer_t1;
            /* For non-invites, a maximum of T2 (normally 4 secs  as per RFC3261) */
            if (msg->method != SIP_INVITE && reschedule > msg->owner->timer_t2)
                reschedule = msg->owner->timer_t2;
        } else {
            /* As noted in RFC3581 the duration of an INVITE transaction is arbitrary
             * and may exceed the lifetime of an idle NAT mapping in the path. Therefore
             * INVITEs SHOULD be retransmitted periodically until a final response is
             * received - any provisional response serves only to halt the initial
             * fast retransmissions.
             */
            reschedule = SLOW_INVITE_RETRANS;
        }

        if (msg->owner && sip_debug_test_pvt(msg->owner))
            cw_verbose("SIP TIMER: #%d: Retransmitting (%sNAT) to %#l@ (peer %#l@):\n%s\n---\n",
                msg->retransid,
                (sip_is_nat_needed(msg->owner) ? "" : "no "),
                &msg->recvdaddr.sa, &msg->owner->peeraddr.sa,
                msg->pkt.data);
        if (sipdebug && option_debug > 3)
            cw_log(CW_LOG_DEBUG, "SIP TIMER: #%d: scheduling retransmission of %s for %d ms (t1 %d ms) \n", msg->retransid, sip_methods[msg->method].text, reschedule, msg->owner->timer_t1);

	if (msg->txtime_i) {
		int i;
		cw_clock_gettime(global_clock_monotonic, &msg->txtime);
		sprintf(&msg->pkt.data[msg->txtime_i], "Timestamp: %9lu.%09lu%n", msg->txtime.tv_sec, msg->txtime.tv_nsec, &i);
		msg->pkt.data[msg->txtime_i + i] = '\r';
	}

        __sip_xmit(msg->conn, &msg->ouraddr, &msg->recvdaddr, msg);

	/* We reschedule ourself here rather than letting the scheduler do it
	 * because the lock below coordinates with the acking code. Releasing
	 * the lock allows the acking code to process a response and attempt
	 * to deschedule retransmission. If we then come back here, return
	 * and allow the scheduler to reschedule us we're going to send
	 * an unwanted retransmission of a packet that we already acked a
	 * reply for.
	 */
	msg->retransid = cw_sched_modify(sched, msg->retransid, reschedule, retrans_pkt, msg);

        cw_mutex_unlock(&msg->owner->lock);
        return 0;
    }

    /* Dequeue the packet. It is then exclusively ours even if we drop the
     * lock thus allowing an attempt to ack a late reply.
     */
    for (prev = &msg->owner->retrans; *prev; prev = &(*prev)->next)
    {
        if (*prev == msg)
        {
            *prev = msg->next;
            break;
        }
    }

    /* If the method is now SIP_UNKNOWN we processed a response while waiting
     * for the lock so we just need to discard the message. Otherwise we've
     * reached the maximum retries for this packet without getting any response.
     */

    if (msg->method != SIP_UNKNOWN)
    {
        /* Whatever we thought we knew about this peer's RTT is clearly wrong.  */
        if (msg->owner) {
            msg->owner->timer_t1 = rfc_timer_t1;
            if (msg->owner->peerpoke) {
                msg->owner->peerpoke->timer_t1 = (msg->owner->peerpoke->maxms ? -1 : rfc_timer_t1);
                cw_log(CW_LOG_NOTICE, "%s: RTT %d\n", msg->owner->peerpoke->name, rfc_timer_t1);
            }
	}

        if (msg->owner && msg->method != SIP_OPTIONS)
        {
            if (cw_test_flag(msg, FLAG_FATAL) || sipdebug)    /* Tell us if it's critical or if we're debugging */
                cw_log(CW_LOG_WARNING, "Maximum retries exceeded on transmission %s for seqno %u (%s %s) SIP Timer T1=%d\n", msg->owner->callid, msg->seqno, (cw_test_flag(msg, FLAG_FATAL) ? "Critical" : "Non-critical"), (msg->method == SIP_RESPONSE ? "Response" : "Request"), msg->owner->timer_t1);
        }
        else
        {
            if (msg->method == SIP_OPTIONS && sipdebug)
                cw_log(CW_LOG_WARNING, "Cancelling retransmit of OPTIONs (call id %s) \n", msg->owner->callid);
        }

        if (cw_test_flag(msg, FLAG_FATAL))
        {
            while (msg->owner->owner  &&  cw_channel_trylock(msg->owner->owner))
            {
                cw_mutex_unlock(&msg->owner->lock);
                usleep(1);
                cw_mutex_lock(&msg->owner->lock);
            }
            if (msg->owner->owner)
            {
                cw_set_flag(msg->owner, SIP_ALREADYGONE);
                cw_log(CW_LOG_WARNING, "Hanging up call %s - no reply to our critical packet.\n", msg->owner->callid);
                cw_queue_hangup(msg->owner->owner);
                cw_channel_unlock(msg->owner->owner);
            }
            else
            {
                /* If no channel owner, destroy now */
                sip_destroy(msg->owner);
            }
        }
    }

    cw_mutex_unlock(&msg->owner->lock);
    cw_object_put(msg->owner);
    cw_dynstr_free(&msg->pkt);
    free(msg);
    return 0;
}


/*! \brief  __sip_reliable_xmit: transmit packet with retransmits */
static void __sip_reliable_xmit(struct sip_pvt *p, struct cw_connection *conn, struct cw_sockaddr_net *from, struct cw_sockaddr_net *to, struct sip_request *msg)
{
	msg->next = p->retrans;
	p->retrans = msg;

	msg->timer_a = 0;

	if (msg->method == SIP_INVITE) {
		/* Note this is a pending invite */
		p->pendinginvite = msg->seqno;
	}

	msg->ouraddr = *from;
	msg->recvdaddr = *to;

	msg->conn = cw_object_dup(conn);
	msg->owner = cw_object_dup(p);

	__sip_xmit(conn, from, to, msg);

	/* Schedule retransmission */
	/* Note: The first retransmission is at last RTT plus a bit to allow for (some) jitter
	 * and to avoid sending a retransmit at the exact moment we expect the reply to arrive
	 */
	msg->retransid = cw_sched_add_variable(sched, (p->timer_t1 != DEFAULT_RFC_TIMER_T1 ? p->timer_t1 + (p->timer_t1 >> 4) + 1 : DEFAULT_RFC_TIMER_T1), retrans_pkt, msg, 1);
}


/*! \brief  __sip_autodestruct: Kill a call (called by scheduler) */
static int __sip_autodestruct(void *data)
{
	struct sip_pvt *dialogue = data;

	cw_mutex_lock(&dialogue->lock);

	dialogue->autokillid = -1;

	/* If this is a subscription, tell the phone that we got a timeout */
	if (dialogue->subscribed) {
		transmit_state_notify(dialogue, CW_EXTENSION_DEACTIVATED, 1, 1, 1);    /* Send first notification */
		dialogue->subscribed = NONE;
		cw_mutex_unlock(&dialogue->lock);
		return 10000;    /* Reschedule this destruction so that we know that it's gone */
	}

	cw_log(CW_LOG_DEBUG, "Auto destroying call '%s'\n", dialogue->callid);

	if (dialogue->owner) {
		cw_log(CW_LOG_WARNING, "Autodestruct on call '%s' with owner in place\n", dialogue->callid);
		cw_queue_hangup(dialogue->owner);
	} else
		sip_destroy(dialogue);

	cw_mutex_unlock(&dialogue->lock);
	cw_object_put(dialogue);
	return 0;
}


/*! \brief  sip_scheddestroy: Schedule destruction of SIP call */
static int sip_scheddestroy(struct sip_pvt *dialogue, int ms)
{
	if (ms < 0) {
#if SIP_ASSUME_SYMMETRIC
		/* Although timer B is configurable that sets the retransmit interval
		 * for *us*. We're scheduling the destroy for after the remote side's
		 * retransmit interval has expired. Since we don't know their config
		 * we have to assume the max timer B value allowed by RFC3261.
		 * It should also be noted that paths may vary in each direction and
		 * really it is strictly the remote side's RTT estimate that we should
		 * be using here. However it doesn't seem too unreasonable to expect
		 * that the paths are not _that_ different?
		 * And, of course, we don't know _what_ the remote might be trying to
		 * send so we don't clip against timer T2.
		 * Oh, and, according to RFC3261 max timer B == timer F.
		 */
		ms = (dialogue->timer_t1 > 0 ? dialogue->timer_t1 : rfc_timer_t1) * DEFAULT_RFC_TIMER_B;
#else
		ms = rfc_timer_t1 * DEFAULT_RFC_TIMER_B;
#endif
	}

	if (sip_debug_test_pvt(dialogue))
		cw_verbose("Scheduling destruction of call '%s' in %dms (t1=%d)\n", dialogue->callid, ms, dialogue->timer_t1);

	if (dialogue->autokillid > -1) {
		if (!cw_sched_del(sched, dialogue->autokillid))
			dialogue->autokillid = cw_sched_add(sched, ms, __sip_autodestruct, dialogue);
	} else
		dialogue->autokillid = cw_sched_add(sched, ms, __sip_autodestruct, cw_object_dup(dialogue));

	return 0;
}


/*! \brief  sip_cancel_destroy: Cancel destruction of SIP call */
static int sip_cancel_destroy(struct sip_pvt *dialogue)
{
	if (dialogue->autokillid > -1 && !cw_sched_del(sched, dialogue->autokillid)) {
		cw_object_put(dialogue);
		dialogue->autokillid = -1;
	}

	return 0;
}


/*! \brief  retrans_stop: stop retransmission of the message for which we have just received a reply */
static void retrans_stop(struct sip_pvt *p, struct sip_request *reply, enum sipmethod sipmethod)
{
	struct sip_request **cur;

	for (cur = &p->retrans; *cur; cur = &(*cur)->next) {
		if (((*cur)->seqno == reply->seqno)
		&& ((!strncasecmp(sip_methods[sipmethod].text, (*cur)->pkt.data, sip_methods[sipmethod].len)))) {
			cw_log(CW_LOG_DEBUG, "%s: stopping retransmission of %s, seq %d\n", p->callid, sip_methods[sipmethod].text, reply->seqno);

			/* If we can delete the scheduled retransmit we own the packet
			 * and can go ahead and free it. Otherwise the scheduled retransmit
			 * has already fired and is waiting on the lock. In this case we
			 * just set the method to unknown so retrans_pkt will give up once
			 * it acquires the lock.
			 */
			if ((*cur)->retransid == -1 || !cw_sched_del(sched, (*cur)->retransid)) {
				struct timespec ts = { 0, 0 };
				char *s;

				/* If we have a timestamp header in the reply that's our transmit time
				 * and, possibly, the peer's delay so we can use it to calculate RTO.
				 * Otherwise we have to use our locally saved TX time.
				 * RFC3261 8.2.6.1 says that timestamp headers are only echoed back
				 * in provisional responses to INVITEs and the addition of a delay
				 * to the timestamp is optional.
				 */
				if ((s = get_header(reply, SIP_HDR_NOSHORT("Timestamp")))
				&& sscanf(s, "%*lu.%*lu %lu.%lu", &(*cur)->txtime.tv_sec, &(*cur)->txtime.tv_nsec, &ts.tv_sec, &ts.tv_nsec)) {
					if (((*cur)->txtime.tv_nsec += ts.tv_nsec) > 1000000000UL) {
						(*cur)->txtime.tv_sec++;
						(*cur)->txtime.tv_nsec -= 1000000000UL;
					}
					(*cur)->txtime.tv_sec += ts.tv_sec;
				} else if ((*cur)->timer_a != 0)
					(*cur)->txtime.tv_sec = (*cur)->txtime.tv_nsec = 0;

				if ((*cur)->txtime.tv_sec || (*cur)->txtime.tv_nsec) {
					cw_clock_gettime(global_clock_monotonic, &ts);

					/* It's tempting to do something similar to TCP's retransmission timer
					 * calulation (RFC6298) but SIP traffic is fairly sparse so we probably
					 * don't have enough traffic to usefully converge on an RTO. So we
					 * just use whatever the last measurement said.
					 */
					p->timer_t1 = cw_clock_diff_ms(&ts, &(*cur)->txtime);
					if (p->timer_t1 < 1)
						p->timer_t1 = 1;

					if (sipdebug) cw_log(CW_LOG_DEBUG, "%s: RTT %d\n", p->callid, p->timer_t1);
					if (p->peerpoke) {
						p->peerpoke->timer_t1 = p->timer_t1;
						if (sipdebug) cw_log(CW_LOG_DEBUG, "%s: RTT %d\n", p->peerpoke->name, p->timer_t1);
					}
				}

				if (sipmethod != SIP_INVITE || (*(reply->pkt.data + reply->uriresp) != '1')) {
					struct sip_request *old = *cur;

					if (sipdebug && option_debug > 3)
						cw_log(CW_LOG_DEBUG, "** SIP TIMER: Cancelling retransmit of %s - reply received\n", sip_methods[sipmethod].text);

					*cur = (*cur)->next;

					cw_object_put(old->owner);
					cw_dynstr_free(&old->pkt);
					free(old);
				} else {
					/* As noted in RFC3581 the duration of an INVITE transaction is arbitrary
					 * and may exceed the lifetime of an idle NAT mapping in the path. Therefore
					 * INVITEs SHOULD be retransmitted periodically until a final response is
					 * received - any provisional response serves only to halt the initial
					 * fast retransmissions.
					 */
					if (sipdebug && option_debug > 3)
						cw_log(CW_LOG_DEBUG, "** SIP TIMER: Changing to slow retransmit of %s - provisional reply received\n", sip_methods[sipmethod].text);

					(*cur)->timer_a = -1;
					(*cur)->retransid = cw_sched_modify(sched, (*cur)->retransid, SLOW_INVITE_RETRANS, retrans_pkt, *cur);
				}
				break;
			}

			(*cur)->method = SIP_UNKNOWN;
			break;
		}
	}
}

/*! \brief retrans_stop_all: Stop any retransmits of outstanding reliable-delivery messages */
static void retrans_stop_all(struct sip_pvt *p)
{
	struct sip_request **cur;

	cur = &p->retrans;
	while (*cur) {
		/* If we can delete the scheduled retransmit we own the packet
		 * and can go ahead and free it. Otherwise the scheduled retransmit
		 * has already fired and is waiting on the lock. In this case we
		 * just set the method to unknown so retrans_pkt will give up once
		 * it acquires the lock.
		 */
		if ((*cur)->retransid == -1 || !cw_sched_del(sched, (*cur)->retransid)) {
			struct sip_request *old = *cur;

			*cur = (*cur)->next;

			cw_object_put(old->owner);
			cw_dynstr_free(&old->pkt);
			free(old);
		} else {
			(*cur)->method = SIP_UNKNOWN;
			cur = &(*cur)->next;
		}
	}
}


static inline int CW_KEYCMP(const char *buf, const char *key, size_t key_len)
{
	return (buf[key_len] == ':' || isspace(buf[key_len])) && !strncasecmp(buf, key, key_len);
}
#define CW_HDRCMP2(buf, name, name_len, alias, alias_len) ({ \
	CW_KEYCMP(buf, name, name_len) || (alias_len && CW_KEYCMP(buf, alias, alias_len)); \
})
#define CW_HDRCMP(buf, name) CW_CPP_DO(CW_HDRCMP2, buf, name)

struct parse_request_state {
	int i;
	int state;
	int key, value;
	int limited;
	int content_length;
};

static inline void parse_request_init(struct parse_request_state *pstate, struct sip_request *req)
{
	memset(pstate, 0, sizeof(*pstate));
	pstate->content_length = -1;

	req->callid = req->tag = req->taglen = 0;
}

static void copy_request(struct sip_request *dst,struct sip_request *src);

static void build_callid(char *callid, int len, struct sockaddr *ouraddr, char *fromdomain);


/*! \brief  send_message: Send a SIP message (request or response) to the other part of the dialogue */
static int send_message(struct sip_pvt *p, struct cw_connection *conn, struct cw_sockaddr_net *from, struct cw_sockaddr_net *to, struct sip_request *msg, int reliable)
{
	int res = -1;

	if (!msg->pkt.error) {
		if (sip_debug_test_pvt(p))
			cw_verbose("%sTransmitting from %#l@ to %#l@:\n%s\n---\n",
				(reliable ? "Reliably " : ""),
				from, to,
				msg->pkt.data);

		if (reliable) {
			__sip_reliable_xmit(p, conn, from, to, msg);
			res = 0;
		} else {
			res = __sip_xmit(conn, from, to, msg);
			cw_dynstr_free(&msg->pkt);
			if (msg->free)
				free(msg);
		}
	}

	return res;
}


/* ************************************************************************ */

/*! \brief  get_in_brackets: Pick out text in brackets from character string */
/* returns pointer to terminated stripped string. modifies input string. */
static char *get_in_brackets(char *tmp)
{
    char *parse;
    char *first_quote;
    char *first_bracket;
    char *second_bracket;
    char last_char;

    parse = tmp;
    while (1)
    {
        first_quote = strchr(parse, '"');
        first_bracket = strchr(parse, '<');
        if (first_quote && first_bracket && (first_quote < first_bracket))
        {
            last_char = '\0';
            for (parse = first_quote + 1;  *parse;  parse++)
            {
                if ((*parse == '"')  &&  (last_char != '\\'))
                    break;
                last_char = *parse;
            }
            if (!*parse)
            {
                cw_log(CW_LOG_WARNING, "No closing quote found in '%s'\n", tmp);
                return tmp;
            }
            parse++;
            continue;
        }
        if (first_bracket)
        {
            second_bracket = strchr(first_bracket + 1, '>');
            if (second_bracket)
            {
                *second_bracket = '\0';
                return first_bracket + 1;
            }
            else
            {
                cw_log(CW_LOG_WARNING, "No closing bracket found in '%s'\n", tmp);
                return tmp;
            }
        }
        return tmp;
    }
}

/*! \brief  sip_sendtext: Send SIP MESSAGE text within a call */
/*      Called from PBX core text message functions */
static int sip_sendtext(struct cw_channel *chan, const char *text)
{
	struct sip_pvt *p = chan->tech_pvt;
	int res = -1;

	if (p) {
		if (sip_debug_test_pvt(p))
			cw_verbose("%s: sending text: %s\n", chan->name, text);

		res = transmit_message_with_text(p, "text/plain", NULL, text);
	}

	return res;
}

/*! \brief  realtime_update_peer: Update peer object in realtime storage */
/*! \brief Update peer object in realtime storage 
       If the CallWeaver system name is set in callweaver.conf, we will use
       that name and store that in the "regserver" field in the sippeers
       table to facilitate multi-server setups.
*/
static void realtime_update_peer(const char *peername, struct sockaddr *sa, const char *username, const char *fullcontact, int expiry, const char *useragent)
{
    char addrbuf[CW_MAX_ADDRSTRLEN];
    char regseconds[20] = "0";
    const char *sysname = cw_config[CW_SYSTEM_NAME];
    const char *syslabel = NULL;
    time_t nowtime;
    int port;

    time(&nowtime);
    nowtime += expiry;
    snprintf(regseconds, sizeof(regseconds), "%ld", nowtime);    /* Expiration time */

    cw_snprintf(addrbuf, sizeof(addrbuf), "%@%c%n%h@", sa, '\0', &port, sa);

    if (cw_strlen_zero(sysname)) /* No system name, disable this */
	sysname = NULL;
    else
	syslabel = "regserver";

    if (fullcontact)
	cw_update_realtime(
	    "sippeers", "name", peername, "ipaddr", addrbuf, "port", &addrbuf[port], "regseconds",
	    regseconds, "username", username, "useragent", useragent, "fullcontact", fullcontact, 
	    syslabel, sysname, NULL);
    else
	cw_update_realtime(
	    "sippeers", "name", peername, "ipaddr", addrbuf, "port", &addrbuf[port], "regseconds",
	    regseconds, "username", username, "useragent", useragent, syslabel, sysname, NULL);

}

/*! \brief  register_peer_exten: Automatically add peer extension to dial plan */
static void register_peer_exten(struct sip_peer *peer, int onoff)
{
    char multi[256];
    char *stringp, *ext;
    
    if (!cw_strlen_zero(regcontext))
    {
        cw_copy_string(multi, cw_strlen_zero(peer->regexten) ? peer->name : peer->regexten, sizeof(multi));
        stringp = multi;
        while ((ext = strsep(&stringp, "&")))
        {
            if (onoff)
                cw_add_extension(regcontext, 1, ext, 1, NULL, NULL, "NoOp", strdup(peer->name), free, channeltype);
            else
                cw_context_remove_extension(regcontext, ext, 1, NULL);
        }
    }
}


static void sip_peer_release(struct cw_object *obj)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);

	if (cw_test_flag(peer, SIP_SELFDESTRUCT))
		apeerobjs--;
	else if (cw_test_flag(peer, SIP_REALTIME))
		rpeerobjs--;
	else
		speerobjs--;

	if (peer->chanvars)
		cw_variables_destroy(peer->chanvars);

	cw_acl_free(peer->acl);

	clear_realm_authentication(&peer->auth);
	free(peer);
}

/*! \brief  update_peer: Update peer data in database (if used) */
static void update_peer(struct sip_peer *p, int expiry)
{
    int rtcachefriends = cw_test_flag(&(p->flags_page2), SIP_PAGE2_RTCACHEFRIENDS);
        
    if (cw_test_flag((&global_flags_page2), SIP_PAGE2_RTUPDATE) &&
        (cw_test_flag(p, SIP_REALTIME) || rtcachefriends))
    {
        realtime_update_peer(p->name, &p->addr.sa, p->username, rtcachefriends ? p->fullcontact : NULL, expiry, p->useragent);
    }
}


/*! \brief  realtime_peer: Get peer from realtime storage
 * Checks the "sippeers" realtime family from extconfig.conf */
static struct sip_peer *realtime_peer(const char *peername, struct sockaddr *sa)
{
    char addrbuf[CW_MAX_ADDRSTRLEN];
    struct sip_peer *peer = NULL;
    struct cw_variable *var;
    struct cw_variable *tmp;
    char *newpeername = (char *)peername;
    int port;

    /* First check on peer name */
    if (peername)
        var = cw_load_realtime("sippeers", "name", peername, NULL);
    else if (sa)
    {
        /* Then check on address first then look for registered hosts */
        cw_snprintf(addrbuf, sizeof(addrbuf), "%@%c%n%h@", sa, '\0', &port, sa);
        if (!(var = cw_load_realtime("sippeers", "host", addrbuf, NULL)))
	    var = cw_load_realtime("sippeers", "ipaddr", addrbuf, "port", &addrbuf[port], NULL);
    }
    else
        return NULL;

    if (!var)
        return NULL;

    tmp = var;
    /* If this is type=user, then skip this object. */
    while (tmp)
    {
        if (!strcasecmp(tmp->name, "type")
            &&
            !strcasecmp(tmp->value, "user"))
        {
            cw_variables_destroy(var);
            return NULL;
        }
        else if (!newpeername && !strcasecmp(tmp->name, "name"))
        {
            newpeername = tmp->value;
        }
        tmp = tmp->next;
    }
    
    if (!newpeername)
    {
        /* Did not find peer in realtime */
        cw_log(CW_LOG_WARNING, "Cannot find realtime peer %s %#l@\n", (peername ? peername : ""), sa);
        cw_variables_destroy(var);
        return (struct sip_peer *) NULL;
    }

    /* Peer found in realtime, now build it in memory */
    peer = build_peer(newpeername, var, !cw_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS), 1);
    if (!peer)
    {
        cw_variables_destroy(var);
        return (struct sip_peer *) NULL;
    }

    if (cw_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS))
    {
        /* Cache peer */
        cw_copy_flags((&peer->flags_page2),(&global_flags_page2), SIP_PAGE2_RTAUTOCLEAR|SIP_PAGE2_RTCACHEFRIENDS);
        if (cw_test_flag((&global_flags_page2), SIP_PAGE2_RTAUTOCLEAR))
        {
            if (peer->expire > -1)
                cw_sched_del(sched, peer->expire);
            peer->expire = cw_sched_add(sched, (global_rtautoclear) * 1000, expire_register, (void *)peer);
        }
    }
    else
    {
        cw_set_flag(peer, SIP_REALTIME);
    }
    cw_variables_destroy(var);

    return peer;
}

/*! \brief  find_peer: Locate peer by name or ip address 
 *    This is used on incoming SIP message to find matching peer on ip
    or outgoing message to find matching peer on name */
static struct sip_peer *find_peer(const char *peer, struct sockaddr *sa, int realtime)
{
	struct cw_object *obj;
	struct sip_peer *p = NULL;

	if (peer)
		obj = cw_registry_find(&peerbyname_registry, 1, cw_hash_string(0, peer), peer);
	else {
		unsigned int hash = cw_sockaddr_hash(sa, 0);

		if (!(obj = cw_registry_find(&peerbyaddr_registry, 1, hash, sa))) {
			uint16_t port = cw_sockaddr_get_port(sa);
			cw_sockaddr_set_port(sa, 0);
			obj = cw_registry_find(&peerbyaddr_registry, 1, hash, sa);
			cw_sockaddr_set_port(sa, port);
		}
	}

	if (obj)
		p = container_of(obj, struct sip_peer, obj);
	else if (realtime)
		p = realtime_peer(peer, sa);

	return p;
}


/*! \brief  sip_destroy_user: Remove user object from in-memory storage */
static void sip_user_release(struct cw_object *obj)
{
	struct sip_user *user = container_of(obj, struct sip_user, obj);

	cw_acl_free(user->acl);

	if (user->chanvars) {
		cw_variables_destroy(user->chanvars);
		user->chanvars = NULL;
	}

	if (cw_test_flag(user, SIP_REALTIME))
		ruserobjs--;
	else
		suserobjs--;

	free(user);
}


/*! \brief  realtime_user: Load user from realtime storage
 * Loads user from "sipusers" category in realtime (extconfig.conf)
 * Users are matched on From: user name (the domain in skipped) */
static struct sip_user *realtime_user(const char *username)
{
    struct cw_variable *var;
    struct cw_variable *tmp;
    struct sip_user *user = NULL;

    var = cw_load_realtime("sipusers", "name", username, NULL);

    if (!var)
        return NULL;

    tmp = var;
    while (tmp)
    {
        if (!strcasecmp(tmp->name, "type")
            &&
            !strcasecmp(tmp->value, "peer"))
        {
            cw_variables_destroy(var);
            return NULL;
        }
        tmp = tmp->next;
    }

    user = build_user(username, var, !cw_test_flag((&global_flags_page2), SIP_PAGE2_RTCACHEFRIENDS));
    
    if (!user)
    {
        /* No user found */
        cw_variables_destroy(var);
        return NULL;
    }

    if (cw_test_flag(&global_flags_page2, SIP_PAGE2_RTCACHEFRIENDS))
    {
        cw_set_flag(&user->flags_page2, SIP_PAGE2_RTCACHEFRIENDS);
        suserobjs++;
        user->reg_entry_byname = cw_registry_add(&userbyname_registry, cw_hash_string(0, user->name), &user->obj);
    }
    else
    {
        /* Move counter from s to r... */
        suserobjs--;
        ruserobjs++;
        cw_set_flag(user, SIP_REALTIME);
    }
    cw_variables_destroy(var);
    return user;
}


/*! \brief  find_user: Locate user by name 
 * Locates user by name (From: sip uri user name part) first
 * from in-memory list (static configuration) then from 
 * realtime storage (defined in extconfig.conf) */
static struct sip_user *find_user(const char *name, int realtime)
{
	struct cw_object *obj;
	struct sip_user *user = NULL;

	if ((obj = cw_registry_find(&userbyname_registry, 1, cw_hash_string(0, name), name)))
		user = container_of(obj, struct sip_user, obj);
	else if (realtime)
		user = realtime_user(name);

	return user;
}


/*! \brief  create_addr_from_peer: create address structure from peer reference */
static int create_addr_from_peer(struct sip_pvt *dialogue, struct sip_peer *peer)
{
	char *callhost;
	int ret = -1;

	/* If the peer is not dynamic we need to find it's address */
	if (!cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC)) {
		/* If we have a host find where to send traffic for it */
		/* FIXME: is this right? If we have a proxy traffic goes there but the message should
		 * use the original host's address, surely? Previously use of the proxy only appeared
		 * when sending poke OPTIONS so perhaps no one's ever used it?
		 */
		if (peer->proxyhost[0])
			cw_get_ip_or_srv(AF_UNSPEC, &peer->addr.sa, peer->proxyhost, (srvlookup ? "_sip._udp" : NULL));
		else if (peer->tohost[0])
			cw_get_ip_or_srv(AF_UNSPEC, &peer->addr.sa, peer->tohost, (srvlookup ? "_sip._udp" : NULL));

		/* If we still don't know fall back on the default address (if any) */
		if (peer->addr.sa.sa_family == AF_UNSPEC)
			peer->addr = peer->defaddr;

		/* And fill in the port if it wasn't otherwise determined */
		if (cw_sockaddr_get_port(&peer->addr.sa) == 0)
			cw_sockaddr_set_port(&peer->addr.sa, DEFAULT_SIP_PORT);
	}

	if (peer->addr.sa.sa_family == AF_UNSPEC)
		goto out;

	if (cw_sip_ouraddrfor(dialogue, &peer->addr.sa, sizeof(peer->addr)))
		goto out;

	cw_copy_flags(dialogue, peer, SIP_FLAGS_TO_COPY);
	dialogue->capability = peer->capability;
	dialogue->prefs = peer->prefs;
	cw_copy_string(dialogue->peername, peer->username, sizeof(dialogue->peername));
	cw_copy_string(dialogue->authname, peer->username, sizeof(dialogue->authname));
	cw_copy_string(dialogue->username, peer->username, sizeof(dialogue->username));
	cw_copy_string(dialogue->peersecret, peer->secret, sizeof(dialogue->peersecret));
	cw_copy_string(dialogue->peermd5secret, peer->md5secret, sizeof(dialogue->peermd5secret));

	if (!cw_strlen_zero(peer->tohost))
		cw_copy_string(dialogue->tohost, peer->tohost, sizeof(dialogue->tohost));
	else
		cw_snprintf(dialogue->tohost, sizeof(dialogue->tohost), "%#@", &dialogue->peeraddr.sa);

	cw_copy_string(dialogue->fullcontact, peer->fullcontact, sizeof(dialogue->fullcontact));
	if (!dialogue->initreq.pkt.used && !cw_strlen_zero(peer->fromdomain)) {
		if ((callhost = strchr(dialogue->callid, '@')))
			strncpy(callhost + 1, peer->fromdomain, sizeof(dialogue->callid) - (callhost - dialogue->callid) - 2);
	}
	if (!cw_strlen_zero(peer->fromdomain))
		cw_copy_string(dialogue->fromdomain, peer->fromdomain, sizeof(dialogue->fromdomain));
	if (!cw_strlen_zero(peer->fromuser))
		cw_copy_string(dialogue->fromuser, peer->fromuser, sizeof(dialogue->fromuser));
	if (!cw_strlen_zero(peer->language))
		cw_copy_string(dialogue->language, peer->language, sizeof(dialogue->language));
	dialogue->maxtime = peer->maxms;
	dialogue->callgroup = peer->callgroup;
	dialogue->pickupgroup = peer->pickupgroup;
	if ((cw_test_flag(dialogue, SIP_DTMF) == SIP_DTMF_RFC2833) || (cw_test_flag(dialogue, SIP_DTMF) == SIP_DTMF_AUTO))
		dialogue->noncodeccapability |= CW_RTP_DTMF;
	else
		dialogue->noncodeccapability &= ~CW_RTP_DTMF;
	cw_copy_string(dialogue->context, peer->context, sizeof(dialogue->context));

	dialogue->timer_t1 = (peer->timer_t1 > 0 ? peer->timer_t1 : rfc_timer_t1);
	dialogue->timer_t2 = peer->timer_t2;

	dialogue->rtptimeout = peer->rtptimeout;
	dialogue->rtpholdtimeout = peer->rtpholdtimeout;
	dialogue->rtpkeepalive = peer->rtpkeepalive;
	if (peer->call_limit)
		cw_set_flag(dialogue, SIP_CALL_LIMIT);

	ret = 0;

out:
	return ret;
}


/*! \brief  create_addr: create address structure from peer name
 *      Or, if peer not found, find it in the global DNS 
 *      returns TRUE (-1) on failure, FALSE on success */
static int create_addr(struct sip_pvt *dialogue, char *opeername, struct sip_peer *peer, int qualify)
{
	struct sip_peer *p;
	int ret = -1;

	if ((p = peer) || (p = find_peer(opeername, NULL, 1))) {
		if (qualify || !p->maxms || (p->timer_t1 > 0 && p->timer_t1 <= p->maxms))
			ret = create_addr_from_peer(dialogue, p);
		if (!peer)
			cw_object_put(p);
	} else if (opeername) {
		static const struct addrinfo hints = {
			.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_IDN,
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_DGRAM,
		};
		char host[MAXHOSTNAMELEN];
		struct addrinfo *addrs;
		char *peername, *port;
		char *server;
		int err;
		uint16_t portno;

		peername = opeername;
		if ((port = strchr(opeername, ':'))) {
			peername = cw_strdupa(opeername);
			port = peername + (port - opeername);
			*(port++) = '\0';
		}

		if (!port || !(portno = atoi(port)))
			portno = DEFAULT_SIP_PORT;

		cw_copy_string(dialogue->tohost, peername, sizeof(dialogue->tohost));
		server = peername;

		if (srvlookup) {
			char *service;
			int tportno;

			service = alloca(sizeof("_sip._udp.") - 1 + strlen(peername) + 1);
			sprintf(service, "_sip._udp.%s", peername);

			if (cw_get_srv(NULL, host, sizeof(host), &tportno, service) > 0) {
				server = host;
				portno = tportno;
			}
		}

		if (!(err = cw_getaddrinfo(server, NULL, &hints, &addrs, NULL))) {
			struct addrinfo *addr;

			for (addr = addrs; addr; addr = addr->ai_next) {
				cw_sockaddr_set_port(addr->ai_addr, portno);

				if (!cw_sip_ouraddrfor(dialogue, addr->ai_addr, addr->ai_addrlen)) {
					ret = 0;
					break;
				}
			}

			if (ret) {
				cw_log(CW_LOG_WARNING, "no path to %s (peer %s) with address(es):\n", server, peername);
				for (addr = addrs; addr; addr = addr->ai_next)
					cw_log(CW_LOG_WARNING, "    %ll@\n", addr->ai_addr);
			}

			freeaddrinfo(addrs);
		} else
			cw_log(CW_LOG_WARNING, "%s: %s\n", server, gai_strerror(err));
	}

	if (!ret) {
		/* FIXME: should these should happen after STUN completes? i.e. they should
		 * include NAT-mapped addresses?
		 */
		build_callid(dialogue->callid, sizeof(dialogue->callid), &dialogue->ouraddr.sa, dialogue->fromdomain);
	}

	return ret;
}

/*! \brief  auto_congest: Scheduled congestion on a call */
static int auto_congest(void *data)
{
	struct sip_pvt *dialogue = data;

	/* Since we are now running we can't be unscheduled therefore
	 * the reference to the dialogue is our solely responsibility.
	 */

	cw_mutex_lock(&dialogue->lock);

	dialogue->initid = -1;

	if (dialogue->owner && !cw_channel_trylock(dialogue->owner)) {
		cw_log(CW_LOG_NOTICE, "Auto-congesting %s\n", dialogue->owner->name);
		cw_queue_control(dialogue->owner, CW_CONTROL_CONGESTION);
		cw_channel_unlock(dialogue->owner);
	}

	cw_mutex_unlock(&dialogue->lock);

	cw_object_put(dialogue);
	return 0;
}


/*! \brief  sip_call: Initiate SIP call from PBX 
 *      used from the dial() application      */
static int sip_call(struct cw_channel *ast, const char *dest)
{
    int res;
    struct sip_pvt *p;
#ifdef OSP_SUPPORT
    struct cw_var_t *osphandle = NULL;
#endif    
    struct cw_object *obj;

    CW_UNUSED(dest);

    p = ast->tech_pvt;
    if ((ast->_state != CW_STATE_DOWN) && (ast->_state != CW_STATE_RESERVED))
    {
        cw_log(CW_LOG_WARNING, "sip_call called on %s, neither down nor reserved\n", ast->name);
        return -1;
    }

    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_ALERT_INFO, "ALERT_INFO")))
        p->options->distinctive_ring = container_of(obj, struct cw_var_t, obj);
    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_VXML_URL, "VXML_URL")))
        p->options->vxml_url = container_of(obj, struct cw_var_t, obj);
    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_SIP_URI_OPTIONS, "SIP_URI_OPTIONS")))
        p->options->uri_options = container_of(obj, struct cw_var_t, obj);

#ifdef ENABLE_SRTP
    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_SIP_SRTP_SDES, "SIP_SRTP_SDES"))) {
        if (cw_true(container_of(obj, struct cw_var_t, obj)->value)) {
            if (!cw_srtp_is_registered()) {
                cw_log(CW_LOG_WARNING, "SIP_SRTP_SDES set but SRTP is not available\n");
                return -1;
            }

            if (!p->srtp) {
                if (setup_crypto(p) < 0) {
                    cw_log(CW_LOG_WARNING, "SIP SRTP sdes setup failed\n");
                    return -1;
                }
            }
        }
    }
#endif

    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_T38CALL, "T38CALL"))) {
        cw_object_put_obj(obj);
        p->t38state = SIP_T38_OFFER_SENT_DIRECT;
        cw_log(CW_LOG_DEBUG, "%s: T38State change to %d\n", ast->name, SIP_T38_OFFER_SENT_DIRECT);
    }

#ifdef OSP_SUPPORT
    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_OSPTOKEN, "OSPTOKEN")))
        p->options->osptoken = container_of(obj, struct cw_var_t, obj);
    if ((obj = cw_registry_find(&ast->vars, 1, CW_KEYWORD_OSPHANDLE, "OSPHANDLE")))
        osphandle = container_of(obj, struct cw_var_t, obj);

    if (!p->options->osptoken || !osphandle || (sscanf(osphandle->value, "%d", &p->osphandle) != 1))
    {
        /* Force Disable OSP support */
        cw_log(CW_LOG_DEBUG, "Disabling OSP support for this call. osptoken = %s, osphandle = %s\n",
            (p->options->osptoken ? p->options->osptoken->value : "missing"),
            (osphandle ? osphandle->value : "missing"));

        if (p->options->osptoken)
            cw_object_put(p->options->osptoken);

        p->options->osptoken = NULL;
        p->osphandle = -1;
    }
    if (osphandle)
        cw_object_put(osphandle);
#endif

    res = 0;
    cw_set_flag(p, SIP_OUTGOING);
    cw_log(CW_LOG_DEBUG, "Outgoing Call for %s\n", p->username);
    res = update_call_counter(p, INC_CALL_LIMIT);
    if (res != -1)
    {
        p->callingpres = ast->cid.cid_pres;
        p->jointcapability = p->capability;
        p->t38jointcapability = p->t38capability;
        cw_log(CW_LOG_DEBUG,"Our T38 capability (%d), joint T38 capability (%d)\n", p->t38capability, p->t38jointcapability);
        res = transmit_invite(p, SIP_INVITE, 1, 2);
        if (p->maxtime)
        {
            /* Initialize auto-congest time */
            p->initid = cw_sched_add(sched, p->maxtime * 4, auto_congest, cw_object_dup(p));
        }
    }
    return res;
}


/*! \brief  sip_registry_release: Destroy registry object */
/*    Objects created with the register= statement in static configuration */
static void sip_registry_release(struct cw_object *obj)
{
	struct sip_registry *reg = container_of(obj, struct sip_registry, obj);

	regobjs--;
	free(reg);
}


/*! \brief   sip_destroy: Execute destrucion of call structure, release memory*/
static void sip_destroy(struct sip_pvt *dialogue)
{
	if (sip_debug_test_pvt(dialogue))
		cw_verbose("Destroying call '%s'\n", dialogue->callid);

	if (dialogue->stateid > -1)
		cw_extension_state_del(dialogue->stateid, NULL);

	/* If we can't delete scheduled tasks it means they are running and will
	 * release their dialogue reference when done. If we can delete the task
	 * it's up to us to drop the reference that was given it.
	 */

	if (dialogue->initid > -1 && !cw_sched_del(sched, dialogue->initid))
		cw_object_put(dialogue);

	if (dialogue->autokillid > -1 && !cw_sched_del(sched, dialogue->autokillid))
		cw_object_put(dialogue);

	retrans_stop_all(dialogue);

	if (dialogue->registry) {
		if (dialogue->registry->dialogue == dialogue) {
			cw_object_put(dialogue->registry->dialogue);
			dialogue->registry->dialogue = NULL;
		}
		cw_object_put(dialogue->registry);
	}

	/* Unlink us from the owner if we have one */
	/* FIXME: why? Is this necessary? */
	if (dialogue->owner) {
		cw_channel_lock(dialogue->owner);
		cw_log(CW_LOG_DEBUG, "Detaching from %s\n", dialogue->owner->name);
		dialogue->owner->tech_pvt = NULL;
		cw_channel_unlock(dialogue->owner);
	}

	if (dialogue->reg_entry)
		cw_registry_del(&dialogue_registry, dialogue->reg_entry);
}

/*! \brief  update_call_counter: Handle call_limit for SIP users 
 * Note: This is going to be replaced by app_groupcount 
 * Thought: For realtime, we should propably update storage with inuse counter... */
static int update_call_counter(struct sip_pvt *fup, int event)
{
    char name[256];
    int *inuse, *call_limit;
    int outgoing = cw_test_flag(fup, SIP_OUTGOING);
    struct sip_user *u = NULL;
    struct sip_peer *p = NULL;

    if (option_debug > 2)
        cw_log(CW_LOG_DEBUG, "Updating call counter for %s call\n", outgoing ? "outgoing" : "incoming");
    /* Test if we need to check call limits, in order to avoid 
       realtime lookups if we do not need it */
    if (!cw_test_flag(fup, SIP_CALL_LIMIT))
        return 0;

    cw_copy_string(name, fup->username, sizeof(name));

    /* Check the list of users */
    u = find_user(name, 1);
    if (!outgoing && u)
    {
        inuse = &u->inUse;
        call_limit = &u->call_limit;
        p = NULL;
    }
    else
    {
        /* Try to find peer */
        if (!p)
            p = find_peer(fup->peername, NULL, 1);
        if (p)
        {
            inuse = &p->inUse;
            call_limit = &p->call_limit;
            cw_copy_string(name, fup->peername, sizeof(name));
        }
        else
        {
            if (option_debug > 1)
                cw_log(CW_LOG_DEBUG, "%s is not a local user, no call limit\n", name);
            return 0;
        }
    }
    switch (event)
    {
    /* incoming and outgoing affects the inUse counter */
    case DEC_CALL_LIMIT:
        if ( *inuse > 0 ) {
	    if (cw_test_flag(fup,SIP_INC_COUNT)) {
	         (*inuse)--;
		cw_clear_flag(fup, SIP_INC_COUNT);
	    }
	}
        else
            *inuse = 0;
        if (option_debug > 1  ||  sipdebug)
            cw_log(CW_LOG_DEBUG, "Call %s %s '%s' removed from call limit %d\n", outgoing ? "to" : "from", u ? "user":"peer", name, *call_limit);
        break;
    case INC_CALL_LIMIT:
        if (*call_limit > 0 )
        {
            if (*inuse >= *call_limit)
            {
                cw_log(CW_LOG_ERROR, "Call %s %s '%s' rejected due to usage limit of %d\n", outgoing ? "to" : "from", u ? "user":"peer", name, *call_limit);
                if (u)
                    cw_object_put(u);
                else
                    cw_object_put(p);
                return -1; 
            }
        }
        (*inuse)++;
	cw_set_flag(fup,SIP_INC_COUNT);
        if (option_debug > 1  ||  sipdebug)
            cw_log(CW_LOG_DEBUG, "Call %s %s '%s' is %d out of %d\n", outgoing ? "to" : "from", u ? "user":"peer", name, *inuse, *call_limit);
        break;
    default:
        cw_log(CW_LOG_ERROR, "update_call_counter(%s, %d) called with no event!\n", name, event);
    }
    if (u)
        cw_object_put(u);
    else
        cw_object_put(p);
    return 0;
}


/*! \brief  hangup_sip2cause: Convert SIP hangup causes to CallWeaver hangup causes */
static int hangup_sip2cause(int cause)
{
    int ret;

    /* Possible values taken from causes.h */
    switch (cause)
    {
    case 603:	 /* Declined */
    case 403:    /* Not found */
    case 487:	 /* Call cancelled */
        ret = CW_CAUSE_CALL_REJECTED;
        break;
    case 404:    /* Not found */
        ret = CW_CAUSE_UNALLOCATED;
        break;
    case 408:    /* No reaction */
        ret = CW_CAUSE_NO_USER_RESPONSE;
        break;
    case 480:    /* No answer */
        ret = CW_CAUSE_NO_ANSWER;
        break;
    case 483:    /* Too many hops */
        ret = CW_CAUSE_NO_ANSWER;
        break;
    case 486:    /* Busy everywhere */
        ret = CW_CAUSE_BUSY;
        break;
    case 488:    /* No codecs approved */
        ret = CW_CAUSE_BEARERCAPABILITY_NOTAVAIL;
        break;
    case 500:    /* Server internal failure */
        ret = CW_CAUSE_FAILURE;
        break;
    case 501:    /* Call rejected */
        ret = CW_CAUSE_FACILITY_REJECTED;
        break;
    case 502:    
        ret = CW_CAUSE_DESTINATION_OUT_OF_ORDER;
        break;
    case 503:    /* Service unavailable */
        ret = CW_CAUSE_CONGESTION;
        break;
    default:
        ret = CW_CAUSE_NORMAL;
        break;
    }

    return ret;
}

/*! \brief  hangup_cause2sip: Convert CallWeaver hangup causes to SIP codes 
\verbatim
 Possible values from causes.h
        CW_CAUSE_NOTDEFINED    CW_CAUSE_NORMAL        CW_CAUSE_BUSY
        CW_CAUSE_FAILURE       CW_CAUSE_CONGESTION    CW_CAUSE_UNALLOCATED

    In addition to these, a lot of PRI codes is defined in causes.h 
    ...should we take care of them too ?
    
    Quote RFC 3398

   ISUP Cause value                        SIP response
   ----------------                        ------------
   1  unallocated number                   404 Not Found
   2  no route to network                  404 Not found
   3  no route to destination              404 Not found
   16 normal call clearing                 --- (*)
   17 user busy                            486 Busy here
   18 no user responding                   408 Request Timeout
   19 no answer from the user              480 Temporarily unavailable
   20 subscriber absent                    480 Temporarily unavailable
   21 call rejected                        403 Forbidden (+)
   22 number changed (w/o diagnostic)      410 Gone
   22 number changed (w/ diagnostic)       301 Moved Permanently
   23 redirection to new destination       410 Gone
   26 non-selected user clearing           404 Not Found (=)
   27 destination out of order             502 Bad Gateway
   28 address incomplete                   484 Address incomplete
   29 facility rejected                    501 Not implemented
   31 normal unspecified                   480 Temporarily unavailable
\endverbatim
*/
static const char *hangup_cause2sip(int cause)
{
    const char *ret;

    switch(cause)
    {
        case CW_CAUSE_UNALLOCATED:        /* 1 */
        case CW_CAUSE_NO_ROUTE_DESTINATION:    /* 3 IAX2: Can't find extension in context */
        case CW_CAUSE_NO_ROUTE_TRANSIT_NET:    /* 2 */
            ret = "404 Not Found";
            break;
        case CW_CAUSE_CONGESTION:        /* 34 */
        case CW_CAUSE_SWITCH_CONGESTION:    /* 42 */
            ret = "503 Service Unavailable";
            break;
        case CW_CAUSE_NO_USER_RESPONSE:    /* 18 */
            ret = "408 Request Timeout";
            break;
        case CW_CAUSE_NO_ANSWER:        /* 19 */
            ret = "480 Temporarily unavailable";
            break;
        case CW_CAUSE_CALL_REJECTED:        /* 21 */
            ret = "403 Forbidden";
            break;
        case CW_CAUSE_NUMBER_CHANGED:        /* 22 */
            ret = "410 Gone";
            break;
        case CW_CAUSE_NORMAL_UNSPECIFIED:    /* 31 */
            ret = "480 Temporarily unavailable";
            break;
        case CW_CAUSE_INVALID_NUMBER_FORMAT:
            ret = "484 Address incomplete";
            break;
        case CW_CAUSE_USER_BUSY:
            ret = "486 Busy here";
            break;
        case CW_CAUSE_FAILURE:
             ret = "500 Server internal failure";
            break;
        case CW_CAUSE_FACILITY_REJECTED:    /* 29 */
            ret = "501 Not Implemented";
            break;
        case CW_CAUSE_CHAN_NOT_IMPLEMENTED:
            ret = "503 Service Unavailable";
            break;
        /* Used in chan_iax2 */
        case CW_CAUSE_DESTINATION_OUT_OF_ORDER:
            ret = "502 Bad Gateway";
            break;
        case CW_CAUSE_BEARERCAPABILITY_NOTAVAIL:    /* Can't find codec to connect to host */
            ret = "488 Not Acceptable Here";
            break;
        case CW_CAUSE_NOTDEFINED:
        default:
            cw_log(CW_LOG_DEBUG, "CW hangup cause %d (no match found in SIP)\n", cause);
            ret = NULL;
            break;
    }

    return ret;
}


/*! \brief  sip_hangup: Hangup SIP call 
 * Part of PBX interface, called from cw_hangup */
static int sip_hangup(struct cw_channel *ast)
{
    struct sip_pvt *p = ast->tech_pvt;
    int needcancel = 0;
    int needdestroy = 0;
    //struct cw_flags locflags = {0};

    if (!p)
    {
        cw_log(CW_LOG_DEBUG, "Asked to hangup channel not connected\n");
        return 0;
    }
    if (option_debug)
        cw_log(CW_LOG_DEBUG, "Hangup call %s, SIP callid %s)\n", ast->name, p->callid);

    cw_mutex_lock(&p->lock);

#ifdef OSP_SUPPORT
    if ((p->osphandle > -1) && (ast->_state == CW_STATE_UP))
    {
        cw_osp_terminate(p->osphandle, CW_CAUSE_NORMAL, p->ospstart, time(NULL) - p->ospstart);
    }
#endif    
    cw_log(CW_LOG_DEBUG, "update_call_counter(%s) - decrement call limit counter\n", p->username);
    update_call_counter(p, DEC_CALL_LIMIT);
    /* Determine how to disconnect */
    if (p->owner != ast)
    {
        cw_log(CW_LOG_WARNING, "Huh?  We aren't the owner? Can't hangup call.\n");
        cw_mutex_unlock(&p->lock);
        return 0;
    }
    /* If the call is not UP, we need to send CANCEL instead of BYE */
    if (ast->_state != CW_STATE_UP)
        needcancel = 1;

    /* Disconnect */
    if (p->vad)
        cw_dsp_free(p->vad);
    if (p->vadtx)
        cw_dsp_free(p->vadtx);
    p->owner = NULL;
    ast->tech_pvt = NULL;

    /* Do not destroy this pvt until we have timeout or
       get an answer to the BYE or INVITE/CANCEL 
       If we get no answer during retransmit period, drop the call anyway.
       (Sorry, mother-in-law, you can't deny a hangup by sending
       603 declined to BYE...)
    */

    if (cw_test_flag(p, SIP_ALREADYGONE)) {
        needdestroy = 1;	/* Set destroy flag at end of this function */
    } else {
        sip_scheddestroy(p, -1);
    }

    /* Start the process if it's not already started */
    if (!cw_test_flag(p, SIP_ALREADYGONE) && p->initreq.pkt.used)
    {
        if (needcancel)
        {
            /* Outgoing call, not up */
            if (cw_test_flag(p, SIP_OUTGOING))
            {
		retrans_stop_all(p);

		/* are we allowed to send CANCEL yet? if not, mark
		   it pending */
		if (!cw_test_flag(p, SIP_CAN_BYE)) {
			cw_set_flag(p, SIP_PENDINGBYE);
			/* Do we need a timer here if we don't hear from them at all? */
		} else {
			/* Send a new request: CANCEL */
			transmit_request_with_auth(p, SIP_CANCEL, p->ocseq, 1, 0);
			/* Actually don't destroy us yet, wait for the 487 on our original 
			   INVITE, but do set an autodestruct just in case we never get it. */
		}
                if (p->initid != -1)
                {
                    /* channel still up - reverse dec of inUse counter
                       only if the channel is not auto-congested */
                    update_call_counter(p, INC_CALL_LIMIT);
                }
            }
            else
            {
                /* Incoming call, not up */
                const char *res;
            
		cw_set_flag(&p->initreq, FLAG_FATAL);
                if (ast->hangupcause && ((res = hangup_cause2sip(ast->hangupcause))))
                    transmit_response(p, res, &p->initreq, 1);
                else 
                    transmit_response(p, "603 Declined", &p->initreq, 1);
            }
        }
        else
        {
            /* Call is in UP state, send BYE */
            if (!p->pendinginvite)
            {
                /* Send a hangup */
                transmit_request_with_auth(p, SIP_BYE, 0, 1, 1);
            }
            else
            {
                /* Note we will need a BYE when this all settles out
                   but we can't send one while we have "INVITE" outstanding. */
                cw_set_flag(p, SIP_PENDINGBYE);    
                cw_clear_flag(p, SIP_NEEDREINVITE);    
            }
        }
    }

    if (needdestroy)
	sip_destroy(p);

    cw_mutex_unlock(&p->lock);
    cw_object_put(p);
    return 0;
}

/*! \brief Try setting codec suggested by the SIP_CODEC channel variable */
static void try_suggested_sip_codec(struct sip_pvt *p)
{
	struct cw_var_t *var;
	int fmt;

	if (!(var = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_SIP_CODEC, "SIP_CODEC")))
		return;

	fmt = cw_getformatbyname(var->value);
	if (fmt) {
		cw_log(CW_LOG_NOTICE, "Changing codec to '%s' for this call because of ${SIP_CODEC) variable\n",var->value);
		if (p->jointcapability & fmt) {
			p->jointcapability &= fmt;
			p->capability &= fmt;
		} else
			cw_log(CW_LOG_NOTICE, "Ignoring ${SIP_CODEC} variable because it is not shared by both ends.\n");
	} else
		cw_log(CW_LOG_NOTICE, "Ignoring ${SIP_CODEC} variable because of unrecognized/not configured codec (check allow/disallow in sip.conf): %s\n", var->value);

	cw_object_put(var);
	return;	
}


/*! \brief  sip_answer: Answer SIP call , send 200 OK on Invite 
 * Part of PBX interface */
static int sip_answer(struct cw_channel *ast)
{
    int res = 0;
    struct sip_pvt *p = ast->tech_pvt;

    cw_mutex_lock(&p->lock);
    if (ast->_state != CW_STATE_UP)
    {
#ifdef OSP_SUPPORT    
        time(&p->ospstart);
#endif
	try_suggested_sip_codec(p);	

        cw_setstate(ast, CW_STATE_UP);
        if (option_debug)
            cw_log(CW_LOG_DEBUG, "sip_answer(%s)\n", ast->name);

        if (p->t38state == SIP_T38_OFFER_RECEIVED_DIRECT)
        {
            p->t38state = SIP_T38_NEGOTIATED;
            cw_log(CW_LOG_DEBUG,"T38State change to %d on channel %s\n",p->t38state, ast->name);
            cw_log(CW_LOG_DEBUG,"T38mode enabled for channel %s\n", ast->name);
            transmit_response_with_t38_sdp(p, "200 OK", &p->initreq, 2);
            cw_channel_set_t38_status(ast, T38_NEGOTIATED);
        }
        else
            transmit_response_with_sdp(p, "200 OK", &p->initreq, 2);
    }
    cw_mutex_unlock(&p->lock);
    return res;
}

/*! \brief  sip_write: Send frame to media channel (rtp) */
static int sip_rtp_write(struct cw_channel *ast, struct cw_frame *frame, int *faxdetect)
{
    struct sip_pvt *p = ast->tech_pvt;
    int res = 0;

    CW_UNUSED(faxdetect);

    switch (frame->frametype)
    {
    case CW_FRAME_VOICE:
        if (!(frame->subclass & ast->nativeformats))
        {
            cw_log(CW_LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
                frame->subclass, ast->nativeformats, ast->readformat, ast->writeformat);
            return 0;
        }
        if (p)
        {
            cw_mutex_lock(&p->lock);
            if (p->rtp)
            {
                /* If channel is not up, activate early media session */
                if ((ast->_state != CW_STATE_UP) && !cw_test_flag(p, SIP_PROGRESS_SENT) && !cw_test_flag(p, SIP_OUTGOING))
                {
                    transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
                    cw_set_flag(p, SIP_PROGRESS_SENT);    
                }
                time(&p->lastrtptx);
                res =  cw_rtp_write(p->rtp, frame);

            }
            cw_mutex_unlock(&p->lock);
        }
        break;
    case CW_FRAME_VIDEO:
        if (p)
        {
            cw_mutex_lock(&p->lock);
            if (p->vrtp)
            {
                /* Activate video early media */
                if ((ast->_state != CW_STATE_UP) && !cw_test_flag(p, SIP_PROGRESS_SENT) && !cw_test_flag(p, SIP_OUTGOING))
                {
                    transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
                    cw_set_flag(p, SIP_PROGRESS_SENT);    
                }
                time(&p->lastrtptx);
                res =  cw_rtp_write(p->vrtp, frame);
            }
            cw_mutex_unlock(&p->lock);
        }
        break;
    case CW_FRAME_IMAGE:
        return 0;
        break;
    case CW_FRAME_MODEM:
        if (p)
        {
            cw_mutex_lock(&p->lock);
            if (p->udptl)
            {
                if ((ast->_state != CW_STATE_UP) && !cw_test_flag(p, SIP_PROGRESS_SENT) && !cw_test_flag(p, SIP_OUTGOING))
                {
                    transmit_response_with_t38_sdp(p, "183 Session Progress", &p->initreq, 0);
                    cw_set_flag(p, SIP_PROGRESS_SENT);    
                }
                res = cw_udptl_write(p->udptl, frame);
            }
            cw_mutex_unlock(&p->lock);
        }
        break;
    default: 
        cw_log(CW_LOG_WARNING, "Can't send %d type frames with SIP write\n", frame->frametype);
        return 0;
    }

    return res;
}

/*! \brief  sip_write: Send frame to media channel (rtp) */
static int sip_write(struct cw_channel *ast, struct cw_frame *frame)
{
    int res=0;
    int faxdetected = 0;

    res = sip_rtp_write(ast,frame,&faxdetected);

    return res;
}

/*! \brief  sip_fixup: Fix up a channel:  If a channel is consumed, this is called.
        Basically update any ->owner links -*/
static int sip_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
    struct sip_pvt *p = newchan->tech_pvt;

    if (!p) {
	cw_log(CW_LOG_WARNING, "No pvt after masquerade. Strange things may happen\n");
	return -1;
    }

    cw_mutex_lock(&p->lock);
    if (p->owner != oldchan)
    {
        cw_log(CW_LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
        cw_mutex_unlock(&p->lock);
        return -1;
    }
    p->owner = newchan;
    cw_mutex_unlock(&p->lock);
    return 0;
}

/*! \brief  sip_senddigit: Send DTMF character on SIP channel */
/*    within one call, we're able to transmit in many methods simultaneously */
static int sip_senddigit(struct cw_channel *ast, char digit)
{
    struct sip_pvt *p = ast->tech_pvt;
    int res = 0;

    cw_mutex_lock(&p->lock);

    switch (cw_test_flag(p, SIP_DTMF))
    {
    case SIP_DTMF_INFO:
        transmit_info_with_digit(p, digit, 100);
        break;
    case SIP_DTMF_RFC2833:
        if (p->rtp) {
            cw_rtp_sendevent(p->rtp, digit, 100);
            res = -2;
        }
        break;
    case SIP_DTMF_INBAND:
        res = -1;
        break;
    }

    cw_mutex_unlock(&p->lock);
    return res;
}


/*! \brief  sip_transfer: Transfer SIP call */
static int sip_transfer(struct cw_channel *ast, const char *dest)
{
    struct sip_pvt *p = ast->tech_pvt;
    int res;

    cw_mutex_lock(&p->lock);
    if (ast->_state == CW_STATE_RING)
        res = sip_sipredirect(p, dest);
    else
        res = transmit_refer(p, dest);
    cw_mutex_unlock(&p->lock);
    return res;
}

/*! \brief  sip_indicate: Play indication to user 
 * With SIP a lot of indications is sent as messages, letting the device play
   the indication - busy signal, congestion etc */
static int sip_indicate(struct cw_channel *ast, int condition)
{
    struct sip_pvt *p = ast->tech_pvt;
    int res = 0;

    cw_mutex_lock(&p->lock);
    switch (condition)
    {
    case CW_CONTROL_RINGING:
        if (ast->_state == CW_STATE_RING)
        {
            if (!cw_test_flag(p, SIP_PROGRESS_SENT)
                ||
                (cw_test_flag(p, SIP_PROG_INBAND) == SIP_PROG_INBAND_NEVER))
            {
                /* Send 180 ringing if out-of-band seems reasonable */
                transmit_response(p, "180 Ringing", &p->initreq, 0);
                cw_set_flag(p, SIP_RINGING);
                if (cw_test_flag(p, SIP_PROG_INBAND) != SIP_PROG_INBAND_YES)
                    break;
            }
            else
            {
                /* Well, if it's not reasonable, just send in-band */
            }
        }
        res = -1;
        break;
    case CW_CONTROL_BUSY:
        if (ast->_state != CW_STATE_UP)
        {
            transmit_response(p, "486 Busy Here", &p->initreq, 0);
            cw_set_flag(p, SIP_ALREADYGONE);    
            cw_softhangup_nolock(ast, CW_SOFTHANGUP_DEV);
            break;
        }
        res = -1;
        break;
    case CW_CONTROL_CONGESTION:
        if (ast->_state != CW_STATE_UP)
        {
            transmit_response(p, "503 Service Unavailable", &p->initreq, 0);
            cw_set_flag(p, SIP_ALREADYGONE);    
            cw_softhangup_nolock(ast, CW_SOFTHANGUP_DEV);
            break;
        }
        res = -1;
        break;
    case CW_CONTROL_PROCEEDING:
        if ((ast->_state != CW_STATE_UP) && !cw_test_flag(p, SIP_PROGRESS_SENT) && !cw_test_flag(p, SIP_OUTGOING))
        {
            transmit_response(p, "100 Trying", &p->initreq, 0);
            break;
        }
        res = -1;
        break;
    case CW_CONTROL_PROGRESS:
        if ((ast->_state != CW_STATE_UP) && !cw_test_flag(p, SIP_PROGRESS_SENT) && !cw_test_flag(p, SIP_OUTGOING))
        {
            transmit_response_with_sdp(p, "183 Session Progress", &p->initreq, 0);
            cw_set_flag(p, SIP_PROGRESS_SENT);    
            break;
        }
        res = -1;
        break;
    case CW_CONTROL_HOLD:    /* The other part of the bridge are put on hold */
        if (sipdebug)
            cw_log(CW_LOG_DEBUG, "Bridged channel now on hold%s\n", p->callid);
        res = -1;
        break;
    case CW_CONTROL_UNHOLD:    /* The other part of the bridge are back from hold */
        if (sipdebug)
            cw_log(CW_LOG_DEBUG, "Bridged channel is back from hold, let's talk! : %s\n", p->callid);
        res = -1;
        break;
    case CW_CONTROL_VIDUPDATE:    /* Request a video frame update */
        if (p->vrtp && !cw_test_flag(p, SIP_NOVIDEO))
        {
            transmit_info_with_vidupdate(p);
            break;
        }
        res = -1;
        break;
    case CW_CONTROL_FLASH:    /* Send a hook flash */
        switch (cw_test_flag(p, SIP_DTMF))
        {
        case SIP_DTMF_INFO:
            /* There is no standard for sending a hook flash
	     * via SIP INFO...
	     */
            transmit_info_with_digit(p, '!', 80);
            break;
        case SIP_DTMF_RFC2833:
            if (p->rtp) {
                /* Hook flashes are sent as RFC2833 events with a fixed
                 * length of 80ms and clocked out by 80ms of silence
                 */
                cw_rtp_sendevent(p->rtp, 'X', 80);
                cw_playtones_start(ast, 0, "!0/80", 0);
            }
            break;
        case SIP_DTMF_INBAND:
            res = -1;
            break;
        }
        break;
    case -1:
        res = -1;
        break;
    default:
        cw_log(CW_LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
        res = -1;
        break;
    }
    cw_mutex_unlock(&p->lock);
    return res;
}

static enum cw_bridge_result sip_bridge(struct cw_channel *c0, struct cw_channel *c1, int flag, struct cw_frame **fo,struct cw_channel **rc, int timeoutms)
{
    int res1 = 0;
    int res2 = 0;
    enum cw_bridge_result bridge_res;

    CW_UNUSED(timeoutms);

    cw_channel_lock(c0);
    if (c0->tech->bridge == sip_bridge)
    {
        res1 = cw_channel_get_t38_status(c0);
        cw_log(CW_LOG_DEBUG, "T38 on channel %s is: %s", c0->name, ( res1 == T38_NEGOTIATED )  ?  "enabled\n"  :  "not enabled\n");
    }
    cw_channel_unlock(c0);
    cw_channel_lock(c1);
    if (c1->tech->bridge == sip_bridge)
    {
        res2 = cw_channel_get_t38_status(c1);
        cw_log(CW_LOG_DEBUG, "T38 on channel %s is: %s", c1->name, ( res2 == T38_NEGOTIATED ) ?  "enabled\n"  :  "not enabled\n");
    }
    cw_channel_unlock(c1);

    /* We start trying rtp bridge. */ 
    if ( ( res1==res2) && ((res1 == T38_STATUS_UNKNOWN)  ||  (res1 == T38_OFFER_REJECTED))  ) {
        bridge_res=cw_rtp_bridge(c0, c1, flag, fo, rc, 0);

        if ( bridge_res == CW_BRIDGE_FAILED_NOWARN )
             return CW_BRIDGE_RETRY_NATIVE;
        else
             return bridge_res;
    }

    /* If they are both in T.38 mode try a native UDPTL bridge */
    if ( (res1 == T38_NEGOTIATED)  &&  (res2 == T38_NEGOTIATED) ) {
        return cw_udptl_bridge(c0, c1, flag, fo, rc);
    }

    /* If they are in mixed modes, don't try any bridging */
     if ( res1 != res2 )
         return CW_BRIDGE_RETRY;

     return CW_BRIDGE_FAILED;
}

/*! \brief  sip_new: Initiate a call in the SIP channel */
/*      called from sip_request_call (calls from the pbx ) */
static struct cw_channel *sip_new(struct sip_pvt *i, int state, char *title)
{
    struct cw_channel *tmp;
    struct cw_variable *v = NULL;
    int fmt;

    if (title)
        tmp = cw_channel_alloc(1, "SIP/%s-%04x", title, cw_random() & 0xffff);
    else if (strchr(i->fromdomain, ':'))
        tmp = cw_channel_alloc(1, "SIP/%s-%08x", strchr(i->fromdomain, ':') + 1, (int)(long)(i));
    else
        tmp = cw_channel_alloc(1, "SIP/%s-%08x", i->fromdomain, (int)(long)(i));

    if (tmp == NULL)
    {
        cw_log(CW_LOG_WARNING, "Unable to allocate SIP channel structure\n");
        return NULL;
    }

    cw_channel_lock(tmp);

    tmp->tech = &sip_tech;
    /* Select our native format based on codec preference until we receive
       something from another device to the contrary. */
//    cw_mutex_lock(&i->lock);
    if (i->jointcapability)
        tmp->nativeformats = cw_codec_choose(&i->prefs, i->jointcapability, 1);
    else if (i->capability)
        tmp->nativeformats = cw_codec_choose(&i->prefs, i->capability, 1);
    else
        tmp->nativeformats = cw_codec_choose(&i->prefs, global_capability, 1);
//    cw_mutex_unlock(&i->lock);
    fmt = cw_best_codec(tmp->nativeformats);

    tmp->type = channeltype;
    if (cw_test_flag(i, SIP_DTMF) ==  SIP_DTMF_INBAND)
    {
        i->vad = cw_dsp_new();
        cw_dsp_set_features(i->vad,   DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
        i->vadtx = cw_dsp_new();
        cw_dsp_set_features(i->vadtx, DSP_FEATURE_DTMF_DETECT | DSP_FEATURE_FAX_CNG_DETECT);
        if (relaxdtmf)
        {
            cw_dsp_digitmode(i->vad  , DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
            cw_dsp_digitmode(i->vadtx, DSP_DIGITMODE_DTMF | DSP_DIGITMODE_RELAXDTMF);
        }
    }
    if (i->rtp)
    {
        tmp->fds[0] = cw_rtp_fd(i->rtp);
        tmp->fds[1] = cw_rtcp_fd(i->rtp);
    }
    if (i->vrtp)
    {
        tmp->fds[2] = cw_rtp_fd(i->vrtp);
        tmp->fds[3] = cw_rtcp_fd(i->vrtp);
    }
    if (state == CW_STATE_RING)
        tmp->rings = 1;
    tmp->adsicpe = CW_ADSI_UNAVAILABLE;
    tmp->writeformat = fmt;
    tmp->rawwriteformat = fmt;
    tmp->readformat = fmt;
    tmp->rawreadformat = fmt;
    tmp->tech_pvt = cw_object_dup(i);

    tmp->callgroup = i->callgroup;
    tmp->pickupgroup = i->pickupgroup;
    tmp->cid.cid_pres = i->callingpres;
    if (!cw_strlen_zero(i->accountcode))
        cw_copy_string(tmp->accountcode, i->accountcode, sizeof(tmp->accountcode));
    if (i->amaflags)
        tmp->amaflags = i->amaflags;
    if (!cw_strlen_zero(i->language))
        cw_copy_string(tmp->language, i->language, sizeof(tmp->language));
    if (!cw_strlen_zero(i->musicclass))
        cw_copy_string(tmp->musicclass, i->musicclass, sizeof(tmp->musicclass));
    i->owner = tmp;
    cw_copy_string(tmp->context, i->context, sizeof(tmp->context));
    cw_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));

    if (!cw_strlen_zero(i->cid_num)) 
        tmp->cid.cid_num = strdup(i->cid_num);
    if (!cw_strlen_zero(i->cid_name))
        tmp->cid.cid_name = strdup(i->cid_name);
    if (!cw_strlen_zero(i->rdnis))
        tmp->cid.cid_rdnis = strdup(i->rdnis);
    if (!cw_strlen_zero(i->exten) && strcmp(i->exten, "s"))
        tmp->cid.cid_dnid = strdup(i->exten);

    tmp->priority = 1;
    if (!cw_strlen_zero(i->uri))
        pbx_builtin_setvar_helper(tmp, "SIPURI", i->uri);
    if (!cw_strlen_zero(i->uri))
        pbx_builtin_setvar_helper(tmp, "SIPRURI", i->ruri);
    if (!cw_strlen_zero(i->domain))
        pbx_builtin_setvar_helper(tmp, "SIPDOMAIN", i->domain);
    if (!cw_strlen_zero(i->useragent))
        pbx_builtin_setvar_helper(tmp, "SIPUSERAGENT", i->useragent);
    if (!cw_strlen_zero(i->callid))
        pbx_builtin_setvar_helper(tmp, "SIPCALLID", i->callid);
#ifdef OSP_SUPPORT
{
    char addrbuf[CW_MAX_ADDRSTRLEN];

    cw_snprintf(addrbuf, sizeof(addrbuf), "%#l@", &i->peeraddr.sa);
    pbx_builtin_setvar_helper(tmp, "OSPPEER", addrbuf);
}
#endif

    /* Set channel variables for this call from configuration */
    for (v = i->chanvars;  v;  v = v->next)
        pbx_builtin_setvar_helper(tmp, v->name, v->value);

    /* Configure the new channel jb */
    if (tmp != NULL  &&  i != NULL  &&  i->rtp != NULL)
        cw_jb_configure(tmp, &i->jbconf);

    cw_setstate(tmp, state);
    if (state != CW_STATE_DOWN && cw_pbx_start(tmp))
    {
        cw_log(CW_LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
        cw_hangup(tmp);
        tmp = NULL;
    }

    return tmp;
}


static char *get_sdp_iterate(int *i, struct sip_request *req, const char *name, size_t len, int sdp_end)
{
	char *ret = NULL;

	while (*i < sdp_end) {
		char *p = &req->pkt.data[*i];
		int l = strcspn(&req->pkt.data[*i], "\r\n");

		*i += l;
		while (*i < sdp_end && (req->pkt.data[*i] == '\r' || req->pkt.data[*i] == '\n' || req->pkt.data[*i] == '\0'))
			(*i)++;

		if (!strncasecmp(p, name, len)) {
			while (p[len] == ' ' || p[len] == '\t') len++;
			if (p[len] == '=') {
				for (ret = &p[len + 1]; ret[0] == ' ' || ret[0] == '\t'; ret++);
				p[l] = '\0';
				break;
			}
		}
	}

	return ret;
}


static char *get_sdp(struct sip_request *req, const char *name, size_t len, int sdp_start, int sdp_end)
{
	int i = sdp_start;

	return get_sdp_iterate(&i, req, name, len, sdp_end);
}


static char *__get_header(const struct sip_request *req, const char *name, size_t name_len, const char *alias, size_t alias_len, int *i)
{
	/* We don't return NULL, so get_header is always a valid pointer */
	char *ret = (char *)"";
	int len;

	while (*i < req->body_start) {
		char *p = &req->pkt.data[*i];
		int l = strlen(&req->pkt.data[*i]);

		*i += l;
		while (*i < req->body_start && !req->pkt.data[*i])
			(*i)++;

		if (((len = name_len),CW_KEYCMP(p, name, name_len))
		|| (((len = alias_len) && CW_KEYCMP(p, alias, alias_len)))) {
			while (p[len] == ' ' || p[len] == '\t') len++;
			if (p[len] == ':') {
				for (ret = &p[len + 1]; ret[0] == ' ' || ret[0] == '\t'; ret++);
				break;
			}
		}
	}

	return ret;
}


/*! \brief  get_header: Get header from SIP request */
static char *get_header(const struct sip_request *req, const char *name, size_t name_len, const char *alias, size_t alias_len)
{
	int start = req->hdr_start;
	return __get_header(req, name, name_len, alias, alias_len, &start);
}


/*! \brief  sip_rtp_read: Read RTP from network */
static struct cw_frame *sip_rtp_read(struct cw_channel *ast, struct sip_pvt *p, int *faxdetect)
{
    /* Retrieve audio/etc from channel.  Assumes p->lock is already held. */
    struct cw_frame *f;

    CW_UNUSED(faxdetect);

    if (!p->rtp)
    {
        /* We have no RTP allocated for this channel */
        return &cw_null_frame;
    }

    switch (ast->fdno)
    {
    case 0:
        if (p->udptl_active  &&  p->udptl)
            f = cw_udptl_read(p->udptl);      /* UDPTL for T.38 */
        else
            f = cw_rtp_read(p->rtp);          /* RTP Audio */
        break;
    case 1:
        f = cw_rtcp_read(ast, p->rtp);             /* RTCP Control Channel */
        break;
    case 2:
        f = cw_rtp_read(p->vrtp);             /* RTP Video */
        break;
    case 3:
        f = cw_rtcp_read(ast, p->vrtp);            /* RTCP Control Channel for video */
        break;
    default:
        f = &cw_null_frame;
    }
    /* Don't forward RFC2833 if we're not supposed to */
    if (f && (f->frametype == CW_FRAME_DTMF) && (cw_test_flag(p, SIP_DTMF) != SIP_DTMF_RFC2833))
        return &cw_null_frame;
    if (p->owner)
    {
        /* We already hold the channel lock */
        if (f->frametype == CW_FRAME_VOICE)
        {
            if (f->subclass != p->owner->nativeformats)
            {
		if (!(f->subclass & p->jointcapability)) {
		    cw_log(CW_LOG_DEBUG, "Bogus frame of format '%s' received from '%s'!\n",
		    cw_getformatname(f->subclass), p->owner->name);
		    return &cw_null_frame;
		}
                cw_log(CW_LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
                p->owner->nativeformats = f->subclass;
                cw_set_read_format(p->owner, p->owner->readformat);
                cw_set_write_format(p->owner, p->owner->writeformat);
            }
            if (f && (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_INBAND) && p->vad)
            {
                f = cw_dsp_process(p->owner, p->vad, f);
                if (f && f->frametype == CW_FRAME_DTMF)
                    cw_log(CW_LOG_DEBUG, "Detected inband DTMF '%c'\n", f->subclass);
            }
        }
    }
    return f;
}

/*! \brief  sip_read: Read SIP RTP from channel */
static struct cw_frame *sip_read(struct cw_channel *ast)
{
    struct cw_frame *fr;
    struct sip_pvt *p = ast->tech_pvt;
    int faxdetected = 0;

    cw_mutex_lock(&p->lock);
    fr = sip_rtp_read(ast, p, &faxdetected);
    time(&p->lastrtprx);
    cw_mutex_unlock(&p->lock);

    return fr;
}

/*! \brief  build_callid: Build SIP CALLID header */
static void build_callid(char *callid, int len, struct sockaddr *oursa, char *fromdomain)
{
	int x, res;

	for (x = 0;  x < 4;  x++) {
		int val = cw_random();
		res = snprintf(callid, len, "%08x", val);
		len -= res;
		callid += res;
	}
	if (!cw_strlen_zero(fromdomain))
		snprintf(callid, len, "@%s", fromdomain);
	else {
		/* It's not really important that we really use our right address
		 * here but it's supposed to be an address specific to us.
		 */
		cw_snprintf(callid, len, "@%#@", oursa);
	}
}

static void make_our_tag(struct sip_pvt *p)
{
    p->tag_len = snprintf(p->tag, sizeof(p->tag), "%08lx", cw_random());
}


static void init_msg(struct sip_request *msg, size_t size)
{
	memset(msg, 0, offsetof(typeof(*msg), pkt));
	cw_dynstr_init(&msg->pkt, size, 1024);
}


/*! \brief  sip_alloc: Allocate SIP_PVT structure and set defaults */
static struct sip_pvt *sip_alloc(void)
{
    struct sip_pvt *p;

    if ((p = calloc(1, sizeof(struct sip_pvt))) == NULL)
        return NULL;

    cw_object_init(p, CW_OBJECT_CURRENT_MODULE, 1);
    p->obj.release = dialogue_release;

    cw_mutex_init(&p->lock);

    p->initid = -1;
    p->autokillid = -1;
    p->subscribed = NONE;
    p->stateid = -1;
    p->prefs = prefs;

    p->timer_t1 = rfc_timer_t1;
    p->timer_t2 = rfc_timer_t2;

#ifdef OSP_SUPPORT
    p->osphandle = -1;
    p->osptimelimit = 0;
#endif

    init_msg(&p->initreq, 0);

    p->branch = cw_random();
    make_our_tag(p);
    p->ocseq = 1;

    sip_debug_ports(p);

    cw_copy_flags(p, &global_flags, SIP_FLAGS_TO_COPY);
    /* Assign default music on hold class */
    strcpy(p->musicclass, global_musicclass);
    p->capability = global_capability;
    if ((cw_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833)  ||  (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_AUTO))
        p->noncodeccapability |= CW_RTP_DTMF;
    strcpy(p->context, default_context);

    /* Assign default jb conf to the new sip_pvt */
    memcpy(&p->jbconf, &global_jbconf, sizeof(struct cw_jb_conf));

    return p;
}


static void sip_alloc_media(struct sip_pvt *dialogue)
{
	int natneeded = sip_is_nat_needed(dialogue);

	dialogue->rtptimeout = global_rtptimeout;
	dialogue->rtpholdtimeout = global_rtpholdtimeout;
	dialogue->rtpkeepalive = global_rtpkeepalive;

	if ((dialogue->rtp = cw_rtp_new_with_bindaddr(&dialogue->ouraddr.sa))) {
		cw_rtp_settos(dialogue->rtp, tos);
		cw_rtp_setnat(dialogue->rtp, natneeded);
	}

	if (videosupport && (dialogue->vrtp = cw_rtp_new_with_bindaddr(&dialogue->ouraddr.sa))) {
		cw_rtp_settos(dialogue->vrtp, tos);
		cw_rtp_setnat(dialogue->vrtp, natneeded);
	}

	if (t38udptlsupport && (dialogue->udptl = cw_udptl_new_with_sock_info(&dialogue->rtp->sock_info[0]))) {
		cw_udptl_settos(dialogue->udptl, tos);
		cw_udptl_setnat(dialogue->udptl, natneeded);

		dialogue->t38capability = global_t38_capability | T38FAX_RATE_MANAGEMENT_TRANSFERED_TCF;

		switch (cw_udptl_get_preferred_error_correction_scheme(dialogue->udptl)) {
			case UDPTL_ERROR_CORRECTION_FEC:
				dialogue->t38capability |= T38FAX_UDP_EC_FEC;
				break;
			case UDPTL_ERROR_CORRECTION_REDUNDANCY:
				dialogue->t38capability |= T38FAX_UDP_EC_REDUNDANCY;
				break;
		}

		dialogue->t38jointcapability = dialogue->t38capability;

		if (option_debug)
			cw_log(CW_LOG_DEBUG,"Our T38 capability (%d)\n", dialogue->t38capability);
	}

	dialogue->udptl_active = 0;
}


/*! \brief  sip_register: Parse register=> line in sip.conf and add to registry */
static int sip_register(char *value, int lineno)
{
    struct sip_registry *reg;
    char copy[256];
    char *username = NULL;
    char *host = NULL;
    char *secret = NULL;
    char *authuser = NULL;
    char *porta = NULL;
    const char *contact = NULL;
    char *stringp = NULL;
    
    if (!value)
        return -1;
    cw_copy_string(copy, value, sizeof(copy));
    stringp=copy;
    username = stringp;
    host = strrchr(stringp, '@');
    if (host)
    {
        *host = '\0';
        host++;
    }
    if (cw_strlen_zero(username) || cw_strlen_zero(host))
    {
        cw_log(CW_LOG_WARNING, "Format for registration is user[:secret[:authuser]]@host[:port][/contact] at line %d\n", lineno);
        return -1;
    }
    stringp=username;
    username = strsep(&stringp, ":");
    if (username)
    {
        secret = strsep(&stringp, ":");
        if (secret)
            authuser = strsep(&stringp, ":");
    }
    stringp = host;
    host = strsep(&stringp, "/");
    if (host)
        contact = strsep(&stringp, "/");
    if (cw_strlen_zero(contact))
        contact = "s";
    stringp=host;
    host = strsep(&stringp, ":");
    porta = strsep(&stringp, ":");

    if (porta  &&  !atoi(porta))
    {
        cw_log(CW_LOG_WARNING, "%s is not a valid port number at line %d\n", porta, lineno);
        return -1;
    }
    reg = calloc(1, sizeof(struct sip_registry));
    if (!reg)
    {
        cw_log(CW_LOG_ERROR, "Out of memory. Can't allocate SIP registry entry\n");
        return -1;
    }

    regobjs++;

    cw_object_init(reg, NULL, 1);
    reg->obj.release = sip_registry_release;

    cw_copy_string(reg->contact, contact, sizeof(reg->contact));
    if (username)
        cw_copy_string(reg->username, username, sizeof(reg->username));
    if (host)
        cw_copy_string(reg->hostname, host, sizeof(reg->hostname));
    if (authuser)
        cw_copy_string(reg->authuser, authuser, sizeof(reg->authuser));
    if (secret)
        cw_copy_string(reg->secret, secret, sizeof(reg->secret));
    reg->expire = -1;
    reg->timeout =  -1;
    reg->refresh = default_expiry;
    reg->portno = porta ? atoi(porta) : 0;
    reg->callid_valid = 0;
    reg->ocseq = 1;

    reg->next = NULL;
    *regl_last = reg;
    regl_last = &reg->next;

    return 0;
}


/*! \brief  parse_request: Parse a SIP message */
static int parse_request(struct parse_request_state *state, struct sip_request *req)
{
	int j;

	for (state->i = 0; req->pkt.data[state->i]; state->i++) {
		switch (state->state) {
			case 0: /* Start of message, scanning method, looking for white space */
				while (req->pkt.data[state->i] && req->pkt.data[state->i] != ' ' && req->pkt.data[state->i] != '\t')
					state->i++;
				if (!req->pkt.data[state->i])
					break;
				req->pkt.data[state->i] = '\0';
				req->method = find_sip_method(req->pkt.data);
				state->state = 1;
				break;
			case 1: /* First line, looking for start of URI or response */
				while (req->pkt.data[state->i] == ' ' || req->pkt.data[state->i] == '\t')
					state->i++;
				if (req->method != SIP_RESPONSE && req->pkt.data[state->i] == '<')
					state->i++;
				if (!req->pkt.data[state->i])
					break;
				req->uriresp = state->i;
				state->state = 2;
				/* Fall through */
			case 2: /* First line, looking for end of line */
				while (req->pkt.data[state->i] && req->pkt.data[state->i] != '\n')
					state->i++;
				if (!req->pkt.data[state->i])
					break;
				j = state->i;
				if (req->pkt.data[j - 1] == '\r')
					j--;
				while (j && (req->pkt.data[j - 1] == ' ' || req->pkt.data[j - 1] == '\t'))
					j--;
				if (req->method != SIP_RESPONSE) {
					if (j >= sizeof("SIP/2.0") - 1 && !strncmp(req->pkt.data + j - (sizeof("SIP/2.0") - 1), "SIP/2.0", sizeof("SIP/2.0") - 1))
						j -= sizeof("SIP/2.0") - 1;
					while (j && (req->pkt.data[j - 1] == ' ' || req->pkt.data[j - 1] == '\t'))
						j--;
					if (j && req->pkt.data[j - 1] == '>')
						j--;
				}
				req->hdr_start = state->i + 1;
				req->pkt.data[state->i] = req->pkt.data[j] = '\0';
				state->state = 3;
				break;

			case 3: /* Header: start of line */
				if (req->pkt.data[state->i] == '\r')
					break;
				if (req->pkt.data[state->i] == '\n') {
					/* Now we process any mime content */
					req->body_start = state->i + 1;
					state->state = 7;
					state->limited = 0;
					break;
				}
				state->key = state->i;
				state->state = 4;
				/* Fall through - the key could be null */
			case 4: /* Header: in key, looking for ':' */
				while (req->pkt.data[state->i] && req->pkt.data[state->i] != ':' && req->pkt.data[state->i] != '\r' && req->pkt.data[state->i] != '\n')
					state->i++;
				if (!req->pkt.data[state->i])
					break;
				/* End of header name, skip spaces to value */
				state->state = 5;
				if (req->pkt.data[state->i] == ':')
					break;
				/* Fall through all the way - no colon, null value - want end of line */
			case 5: /* Header: skipping spaces before value */
				while (req->pkt.data[state->i] && (req->pkt.data[state->i] == ' ' || req->pkt.data[state->i] == '\t'))
					state->i++;
				if (!req->pkt.data[state->i])
					break;
				state->value = state->i;
				state->state = 6;
				/* Fall through - we are on the start of the value and it may be blank */
			case 6: /* Header: in value, looking for end of line */
				while (req->pkt.data[state->i] && req->pkt.data[state->i] != '\n')
					state->i++;
				if (!req->pkt.data[state->i] || req->pkt.data[state->i + 1] == ' ' || req->pkt.data[state->i + 1] == '\t')
					break;
				if (state->i && req->pkt.data[state->i - 1] == '\r')
					req->pkt.data[state->i - 1] = '\0';
				req->pkt.data[state->i] = '\0';
				state->state = 3;
				if (CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_CALL_ID)) {
					req->callid = state->value;
				} else if (CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_NOSHORT("CSeq"))) {
					if (sscanf(&req->pkt.data[state->value], "%u %n", &req->seqno, &j)) {
						req->cseq_method = find_sip_method(&req->pkt.data[state->value + j]);
					} else
						req->method = SIP_UNKNOWN;
					req->cseq = state->value;
				} else if (CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_CONTENT_LENGTH)) {
					state->content_length = atol(&req->pkt.data[state->value]);
				} else if ((req->method != SIP_RESPONSE && CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_FROM))
				|| (req->method == SIP_RESPONSE && CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_TO))) {
					char *p;

					if ((p = strcasestr(&req->pkt.data[state->value], ";tag="))) {
						p += sizeof(";tag=") - 1;
						req->tag = p - req->pkt.data;
						for (req->taglen = 0; p[req->taglen] && p[req->taglen] != ';'; req->taglen++);
					}
				} else if (CW_HDRCMP(&req->pkt.data[state->key], SIP_HDR_NOSHORT("Warning"))) {
					if (!strncmp(&req->pkt.data[state->value], "392 ", 4)) {
						/* Sip EXpress router uses 392 to send "noisy feedback"
						 * giving source address and URI details seen. It is
						 * _too_ noisy though :-)
						 */
					} else {
						/* FIXME: should we avoid logging 304 and 305 (one or
						 * more media types/formats not available)?
						 */
						cw_log(CW_LOG_WARNING, "%s\n", &req->pkt.data[state->value]);
					}
				}
				break;

			case 7: /* Body */
				if (!state->content_length)
					goto done;
				j = req->pkt.used - state->i;
				if (j > state->content_length)
					j = state->content_length;
				state->content_length -= j;
				goto done;
		}
	}
done:

	/* If we ran out of data before finding all the content we have to ignore this message */
	if (state->content_length > 0) {
		cw_log(CW_LOG_WARNING, "Insufficient data for content-Length (len = %d, missing = %d)\n", req->pkt.used, state->content_length);
		return -1;
	}

	/* If we consumed all content but didn't end on a null there was data left over */
#if 0
	/* FIXME: Disabled until we have killed all non-incoming uses of parse_request */
	if (state->content_length == 0 && req->pkt.data[state->i])
		cw_log(CW_LOG_WARNING, "Ignoring data beyond given content-Length (len = %d, i = %d): \"%s\" ... \"%.16s...\"\n", req->pkt.used, state->i, req->pkt.data, req->pkt.data + state->i);
#endif

	/* If state is anything other than 7 (start of body line) the last line was missing a newline
	 * so is possibly truncated.
	 */
	if (state->state != 7) {
		cw_log(CW_LOG_WARNING, "Last line has no newline and may be truncated - ignoring message (state %d, hdr=\"%s\", val=\"%s\")\n", state->state, &req->pkt.data[state->key], &req->pkt.data[state->value]);
		return -1;
	}

	return 0;
}


/*! \brief  process_sdp: Process SIP SDP and activate RTP channels */
static int process_sdp(struct sip_pvt *p, struct sip_request *req, int ignore, int sdp_start, int sdp_end)
{
	static const struct addrinfo hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
	};
	struct addrinfo *addrs;
	int host, err;
	char chr;
    struct cw_sockaddr_net addr;
    char *m;
    char *c;
    char *a;
    char *s;
    char *codecs;
    struct cw_channel *bridgepeer = NULL;
    struct cw_var_t *var;
    int len = -1;
    int portno = -1;
    int vportno = -1;
    int peercapability, peernoncodeccapability;
    int vpeercapability=0, vpeernoncodeccapability=0;
    unsigned int codec;
    int destiterator = 0;
    int iterator;
    int sendonly = 0;
    int x,y;
    int debug=sip_debug_test_pvt(p);
#ifdef ENABLE_SRTP
    int secure_audio = 0;
#endif
    int ec_found;
    int udptlportno = -1;
    int peert38capability = 0;
    int old = 0;
    int t38_disabled;

	CW_UNUSED(ignore);

	/*  FIXME: is this even possible? */
	if (!p->rtp) {
		cw_log(CW_LOG_ERROR, "Got SDP but have no RTP session allocated.\n");
		return -1;
	}

	/* Update our last rtprx when we receive an SDP, too */
	/*  FIXME: why? How is this used? SDP is not RTP? */
	time(&p->lastrtprx);
	time(&p->lastrtptx);

	m = get_sdp(req, "m", 1, sdp_start, sdp_end);
	destiterator = sdp_start;
	c = get_sdp_iterate(&destiterator, req, "c", 1, sdp_end);

	if (!m || !c) {
		cw_log(CW_LOG_WARNING, "Insufficient information for SDP (m = '%s', c = '%s')\n", m, c);
		return -1;
	}

	if (!sscanf(c, "IN IP%c %n", &chr, &host) || (chr != '4' && chr != '6')) {
		cw_log(CW_LOG_WARNING, "Invalid host in c= line, '%s'\n", c);
		return -1;
	}

    iterator = sdp_start;
    cw_set_flag(p, SIP_NOVIDEO);    
    while ((m = get_sdp_iterate(&iterator, req, "m", 1, sdp_end)))
    {
        char protocol[5] = "";
        int found = 0;

        len = -1;
        if ((sscanf(m, "audio %d/%d RTP/%4s %n", &x, &y, protocol, &len) == 3)
        || (sscanf(m, "audio %d RTP/%4s %n", &x, protocol, &len) == 2)) {
            if (!strcmp(protocol, "SAVP")) {
#ifdef ENABLE_SRTP
                secure_audio = 1;
#else
                continue;
#endif
            } else if (strcmp(protocol, "AVP")) {
                cw_log(CW_LOG_WARNING, "Unknown SDP media protocol in offer: %s\n", protocol);
                continue;
            }

            if (len < 0) {
                cw_log(CW_LOG_WARNING, "Unknown SDP media type in offer: %s\n", m);
                continue;
            }

            found = 1;
            portno = x;
            /* Scan through the RTP payload types specified in a "m=" line: */
            cw_rtp_pt_clear(p->rtp);
            codecs = m + len;
            while (!cw_strlen_zero(codecs))
            {
                if (sscanf(codecs, "%u%n", &codec, &len) != 1)
                {
                    cw_log(CW_LOG_WARNING, "Error in codec string '%s'\n", codecs);
                    return -1;
                }
                if (debug)
                    cw_verbose("Found RTP audio format %u\n", codec);
                cw_rtp_set_m_type(p->rtp, codec);
                codecs = cw_skip_blanks(codecs + len);
            }
        }
	
	t38_disabled = 0;
	if ((var = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_T38_DISABLE, "T38_DISABLE"))) {
		t38_disabled = 1;
		cw_object_put(var);
	}

        if (p->udptl && t38udptlsupport && !t38_disabled
            && 
                ((sscanf(m, "image %d udptl t38 %n", &x, &len) == 1)
                ||
                (sscanf(m, "image %d UDPTL t38 %n", &x, &len) == 1)))
        {
            if (debug) {
                cw_verbose("Got T.38 offer in SDP\n");
                sip_debug_ports(p);
            }
            found = 1;
            udptlportno = x;

            cw_log(CW_LOG_DEBUG, "Activating UDPTL on response %s (1)\n", p->callid);
            p->udptl_active = 1;
            cw_channel_set_t38_status(p->owner,T38_NEGOTIATED);

            if (p->owner  &&  p->lastinvite)
            {
                p->t38state = SIP_T38_OFFER_RECEIVED_REINVITE; /* T38 Offered in re-invite from remote party */
                cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state,p->owner ? p->owner->name : "<none>" );
            }
            else
            {
                p->t38state = SIP_T38_OFFER_RECEIVED_DIRECT; /* T38 Offered directly from peer in first invite */
                cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state,p->owner ? p->owner->name : "<none>");
            }                
        }
        else
        {
            cw_log(CW_LOG_DEBUG, "Activating RTP on response %s (1)\n", p->callid);
            p->udptl_active = 0;
        }
        if (p->vrtp)
            cw_rtp_pt_clear(p->vrtp);  /* Must be cleared in case no m=video line exists */

#ifdef ENABLE_SRTP
        len = -1;
#endif
        if (p->vrtp && (sscanf(m, "video %d RTP/AVP %n", &x, &len) == 1))
        {
#ifdef ENABLE_SRTP
            if (len < 0) {
                cw_log(CW_LOG_WARNING, "Unknown SDP media type in offer: %s\n", m);
                continue;
            }
#endif
            found = 1;
            cw_clear_flag(p, SIP_NOVIDEO);    
            vportno = x;
            /* Scan through the RTP payload types specified in a "m=" line: */
            codecs = m + len;
            while (!cw_strlen_zero(codecs))
            {
                if (sscanf(codecs, "%u%n", &codec, &len) != 1)
                {
                    cw_log(CW_LOG_WARNING, "Error in codec string '%s'\n", codecs);
                    return -1;
                }
                if (debug)
                    cw_verbose("Found RTP video format %u\n", codec);
                cw_rtp_set_m_type(p->vrtp, codec);
                codecs = cw_skip_blanks(codecs + len);
            }
        }
        if (!found )
            cw_log(CW_LOG_WARNING, "Unknown or ignored SDP media type in offer: %s\n", m);
    }
    if (portno == -1  &&  vportno == -1  &&  udptlportno == -1)
    {
        /* No acceptable offer found in SDP */
        return -2;
    }

	/* Check for Media-description-level-address for audio.
	 * If there isn't one we use the main address.
	 */
	if (pedanticsipchecking) {
		if ((c = get_sdp_iterate(&destiterator, req, "c", 1, sdp_end))) {
			int host2;

			if (!sscanf(c, "IN IP%c %n", &chr, &host2) || (chr != '4' && chr != '6'))
				cw_log(CW_LOG_WARNING, "Invalid host in c= line, '%s'\n", c);

			host = host2;
		}
	}

	/*  N.B. We ignore the IPv[46] indicator and go by what the address looks like */
	if ((err = cw_getaddrinfo(&c[host], "0", &hints, &addrs, NULL))) {
		cw_log(CW_LOG_WARNING, "Unable to lookup host \"%s\" in c= line: %s\n", &c[host], gai_strerror(err));
		return -1;
	}
	memcpy(&addr.sa, addrs->ai_addr, addrs->ai_addrlen);

	/* Setup audio port number */
	if (portno != -1) {
		if (p->rtp && portno) {
			/* N.B. we set the port first because set_peer sets both RTP and RTCP destinations */
			cw_sockaddr_set_port(addrs->ai_addr, portno);
			cw_rtp_set_peer(p->rtp, addrs->ai_addr);
			if (debug)
				cw_log(CW_LOG_DEBUG,"Peer audio RTP is at %#l@\n", cw_rtp_get_peer(p->rtp));
		}
	}

	/* Check for Media-description-level-address for video.
	 * If there isn't one we use whatever we used for audio.
	 * FIXME: is that the right thing to do?
	 */
	if (pedanticsipchecking) {
		if ((c = get_sdp_iterate(&destiterator, req, "c", 1, sdp_end))) {
			if (!sscanf(c, "IN IP%c %n", &chr, &host) || (chr != '4' && chr != '6'))
				cw_log(CW_LOG_WARNING, "Invalid host in c= line, '%s'\n", c);
			else {
				freeaddrinfo(addrs);

				/*  N.B. We ignore the IPv[46] indicator and go by what the address looks like */
				if ((err = cw_getaddrinfo(&c[host], "0", &hints, &addrs, NULL))) {
					cw_log(CW_LOG_WARNING, "Unable to lookup host \"%s\" in c= line: %s\n", &c[host], gai_strerror(err));
					return -1;
				}
			}
		}
	}

	/* Setup video port number */
	if (vportno != -1) {
		if (p->vrtp && vportno) {
			/* N.B. we set the port first because set_peer sets both RTP and RTCP destinations */
			cw_sockaddr_set_port(addrs->ai_addr, vportno);
			cw_rtp_set_peer(p->vrtp, addrs->ai_addr);
			if (debug)
				cw_log(CW_LOG_DEBUG, "Peer video RTP is at %#l@\n", cw_rtp_get_peer(p->vrtp));
		}
	}

	/* Setup UDPTL port number */
	if (udptlportno != -1 ) {
		if (p->udptl && t38udptlsupport && udptlportno) {
			/* FIXME: we're using the same address as audio here. Is that right?
			 * Should we look for another media-description-level address?
			 */
			cw_udptl_set_peer(p->udptl, cw_rtp_get_peer(p->rtp));
			cw_sockaddr_set_port(cw_udptl_get_peer(p->udptl), udptlportno);
			if (debug)
				cw_log(CW_LOG_DEBUG, "Peer T.38 UDPTL is at %#l@\n", cw_udptl_get_peer(p->udptl));
		}
	}

	freeaddrinfo(addrs);

    /* Next, scan through each "a=rtpmap:" line, noting each
     * specified RTP payload type (with corresponding MIME subtype):
     */
    iterator = sdp_start;
    while ((a = get_sdp_iterate(&iterator, req, "a", 1, sdp_end)))
    {
        char *mimeSubtype = cw_strdupa(a); /* ensures we have enough space */
        
	if (!strcasecmp(a, "sendonly") || !strcasecmp(a, "inactive"))
        {
            sendonly = 1;
            continue;
        }
#ifdef ENABLE_SRTP
        if (!strncasecmp(a, "crypto:", 7)) {
            process_crypto(p, a);
            continue;
        }
#endif
        if (!strcasecmp(a, "sendrecv"))
              sendonly = 0;
        if (sscanf(a, "rtpmap: %u %[^/]/", &codec, mimeSubtype) != 2)
            continue;
        if (debug)
            cw_verbose("Found description format %s\n", mimeSubtype);
        /* Note: should really look at the 'freq' and '#chans' params too */
        cw_rtp_set_rtpmap_type(p->rtp, codec, "audio", mimeSubtype);
        if (p->vrtp)
            cw_rtp_set_rtpmap_type(p->vrtp, codec, "video", mimeSubtype);
    }
#ifdef ENABLE_SRTP
    if (secure_audio && !(p->srtp && p->srtp->a_crypto))
    {
        cw_log(CW_LOG_WARNING, "Can't provide secure audio requested in SDP offer\n");
        return -2;
    }
#endif
    if (udptlportno != -1)
    {
        /* Scan through the a= lines for T38 attributes and set appropriate fileds */
        iterator = sdp_start;
        old = 0;
        int found = 0;
    
        ec_found = UDPTL_ERROR_CORRECTION_REDUNDANCY;
        while ((a = get_sdp_iterate(&iterator, req, "a", 1, sdp_end)))
        {
            if (old  &&  (iterator-old != 1))
                break;
            old = iterator;
            
            if (sscanf(a, "T38FaxMaxBuffer:%d", &x) == 1)
            {
                found = 1;
                cw_log(CW_LOG_DEBUG,"MaxBufferSize:%d\n",x);
            }
            else if (sscanf(a, "T38MaxBitRate:%d", &x) == 1)
            {
                found = 1;
                cw_log(CW_LOG_DEBUG,"T38MaxBitRate: %d\n",x);
                switch (x)
                {
                case 33600:
                    peert38capability |= T38FAX_RATE_33600 | T38FAX_RATE_14400 | T38FAX_RATE_12000 | T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 14400:
                    peert38capability |= T38FAX_RATE_14400 | T38FAX_RATE_12000 | T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 12000:
                    peert38capability |= T38FAX_RATE_12000 | T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 9600:
                    peert38capability |= T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 7200:
                    peert38capability |= T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 4800:
                    peert38capability |= T38FAX_RATE_4800 | T38FAX_RATE_2400;
                    break;
                case 2400:
                    peert38capability |= T38FAX_RATE_2400;
                    break;
                }
            }
            else if (sscanf(a, "T38FaxVersion:%d", &x) == 1)
            {
                found = 1;
                cw_log(CW_LOG_DEBUG,"FaxVersion: %d\n",x);
                if (x == 0)
                    peert38capability |= T38FAX_VERSION_0;
                else if (x == 1)
                    peert38capability |= T38FAX_VERSION_1;
            }
            else if (sscanf(a, "T38FaxMaxDatagram:%d", &x) == 1)
            {
                found = 1;
                cw_log(CW_LOG_DEBUG,"FaxMaxDatagram: %d\n",x);
                cw_udptl_set_far_max_datagram(p->udptl, x);
                cw_udptl_set_local_max_datagram(p->udptl, x);
            }
            else if (!strncmp(a, "T38FaxFillBitRemoval", sizeof("T38FaxFillBitRemoval") - 1))
            {
                found = 1;
                x = 0;
                if (a[sizeof("T38FaxFillBitRemoval") - 1] == '\0'
                || !strcmp(a + sizeof("T38FaxFillBitRemoval") - 1, ":1"))
                {
                    x = 1;
                    peert38capability |= T38FAX_FILL_BIT_REMOVAL;
                }
                cw_log(CW_LOG_DEBUG, "FillBitRemoval: %d\n", x);
            }
            else if (!strncmp(a, "T38FaxTranscodingMMR", sizeof("T38FaxTranscodingMMR") - 1))
            {
                found = 1;
                x = 0;
                if (a[sizeof("T38FaxTranscodingMMR") - 1] == '\0'
                || !strcmp(a + sizeof("T38FaxTranscodingMMR") - 1, ":1"))
                {
                    x = 1;
                    peert38capability |= T38FAX_TRANSCODING_MMR;
                }
                cw_log(CW_LOG_DEBUG, "Transcoding MMR: %d\n", x);
            }
            else if (!strncmp(a, "T38FaxTranscodingJBIG", sizeof("T38FaxTranscodingJBIG") - 1))
            {
                found = 1;
                x = 0;
                if (a[sizeof("T38FaxTranscodingJBIG") - 1] == '\0'
                || !strcmp(a + sizeof("T38FaxTranscodingJBIG") - 1, ":1"))
                {
                    x = 1;
                    peert38capability |= T38FAX_TRANSCODING_JBIG;
                }
                cw_log(CW_LOG_DEBUG, "Transcoding JBIG: %d\n", x);
            }
            else if (!strncmp(a, "T38FaxRateManagement:", sizeof("T38FaxRateManagement:") - 1))
            {
                found = 1;
                s = a + sizeof("T38FaxRateManagement:") - 1;
                cw_log(CW_LOG_DEBUG,"RateMangement: %s\n", s);
                if (!strcasecmp(s, "localTCF"))
                    peert38capability |= T38FAX_RATE_MANAGEMENT_LOCAL_TCF;
                else if (!strcasecmp(s, "transferredTCF"))
                    peert38capability |= T38FAX_RATE_MANAGEMENT_TRANSFERED_TCF;
            }
            else if (!strncmp(a, "T38FaxUdpEC:", sizeof("T38FaxUdpEC:") - 1))
            {
                found = 1;
                s = a + sizeof("T38FaxUdpEC:") - 1;
                cw_log(CW_LOG_DEBUG,"UDP EC: %s\n", s);
                if (strcasecmp(s, "t38UDPFEC") == 0)
                {
                    peert38capability |= T38FAX_UDP_EC_FEC;
                    ec_found = UDPTL_ERROR_CORRECTION_FEC;
                }
                else if (strcasecmp(s, "t38UDPRedundancy") == 0)
                {
                    peert38capability |= T38FAX_UDP_EC_REDUNDANCY;
                    ec_found = UDPTL_ERROR_CORRECTION_REDUNDANCY;
                }
                else
                {
                    ec_found = UDPTL_ERROR_CORRECTION_NONE;
                }
            }
            else if ((sscanf(a, "T38VendorInfo:%d %d %d", &x, &x, &x) == 3))
            {
                found = 1;
            }
        }
        cw_udptl_set_error_correction_scheme(p->udptl, ec_found);
        if (found)
        {
            /* Some cisco equipment returns nothing beside c= and m= lines in 200 OK T38 SDP */ 
            p->t38peercapability = peert38capability;
            p->t38jointcapability = (peert38capability & 0xFF); /* Put everything beside supported speeds settings */
            peert38capability &= (T38FAX_RATE_14400 | T38FAX_RATE_12000 | T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400); /* Mask speeds only */ 
            p->t38jointcapability |= (peert38capability & p->t38capability); /* Put the lower of our's and peer's speed */
        }
        if (debug)
        {
            cw_log(CW_LOG_DEBUG,"Our T38 capability = (%d), peer T38 capability (%d), joint T38 capability (%d)\n",
                    p->t38capability,
                    p->t38peercapability,
                    p->t38jointcapability);
        }
    }
    else
    {
        p->t38state = SIP_T38_STATUS_UNKNOWN;
        cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state,p->owner ? p->owner->name : "<none>");
    }

    /* Now gather all of the codecs that were asked for: */
    cw_rtp_get_current_formats(p->rtp,
                &peercapability, &peernoncodeccapability);
    if (p->vrtp)
        cw_rtp_get_current_formats(p->vrtp,
                &vpeercapability, &vpeernoncodeccapability);
    p->jointcapability = p->capability & (peercapability | vpeercapability);
    p->peercapability = (peercapability | vpeercapability);
    p->noncodeccapability = noncodeccapability & peernoncodeccapability;
    
    if (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_AUTO)
    {
        cw_clear_flag(p, SIP_DTMF);
        if (p->noncodeccapability & CW_RTP_DTMF)
        {
            /* XXX Would it be reasonable to drop the DSP at this point? XXX */
            cw_set_flag(p, SIP_DTMF_RFC2833);
        }
        else
        {
            cw_set_flag(p, SIP_DTMF_INBAND);
        }
    }
    
    if (debug)
    {
        struct cw_dynstr ds = CW_DYNSTR_INIT;

        cw_dynstr_printf(&ds, "Capabilities: us - ");
        cw_getformatname_multiple(&ds, p->capability);
        cw_dynstr_printf(&ds, ", peer - audio=");
        cw_getformatname_multiple(&ds, peercapability);
        cw_dynstr_printf(&ds, "/video=");
        cw_getformatname_multiple(&ds, vpeercapability);
        cw_dynstr_printf(&ds, ", combined - ");
        cw_getformatname_multiple(&ds, p->jointcapability);
        cw_dynstr_printf(&ds, "\n");
        if (!ds.error)
            cw_verbose(ds.data);

        cw_dynstr_reset(&ds);

        cw_dynstr_printf(&ds, "Non-codec capabilities: us - ");
        cw_rtp_lookup_mime_multiple(&ds, noncodeccapability, 0);
        cw_dynstr_printf(&ds, ", peer - ");
        cw_rtp_lookup_mime_multiple(&ds, peernoncodeccapability, 0);
        cw_dynstr_printf(&ds, ", combined - ");
        cw_rtp_lookup_mime_multiple(&ds, p->noncodeccapability, 0);
        cw_dynstr_printf(&ds, "\n");
        if (!ds.error)
            cw_verbose(ds.data);

        cw_dynstr_free(&ds);
    }
    if (!p->jointcapability)
    {
        cw_log(CW_LOG_DEBUG, "No compatible codecs!\n");
        return -1;
    }

    if (!p->owner)     /* There's no open channel owning us */
        return 0;

    if (!(p->owner->nativeformats & p->jointcapability))
    {
        struct cw_dynstr ds = CW_DYNSTR_INIT;

        cw_dynstr_printf(&ds, "Oooh, we need to change our formats since our peer supports only ");
        cw_getformatname_multiple(&ds, p->jointcapability);
        cw_dynstr_printf(&ds, " and not ");
        cw_getformatname_multiple(&ds, p->owner->nativeformats);
        cw_dynstr_printf(&ds, "\n");
        if (!ds.error)
            cw_log(CW_LOG_DEBUG, "%s", ds.data);

        cw_dynstr_free(&ds);

        p->owner->nativeformats = cw_codec_choose(&p->prefs, p->jointcapability, 1);
        cw_set_read_format(p->owner, p->owner->readformat);
        cw_set_write_format(p->owner, p->owner->writeformat);
    }
    if ((bridgepeer = cw_bridged_channel(p->owner)))
    {
        /* We have a bridge */
        /* Turn on/off music on hold if we are holding/unholding */
        if (cw_sockaddr_is_specific(&addr.sa) && !sendonly)
        {
            cw_moh_stop(bridgepeer);
        
            /* Activate a re-invite */
            cw_queue_frame(p->owner, &cw_null_frame);
        }
        else
        {
            /* No address for RTP, we're on hold */
            
            cw_moh_start(bridgepeer, NULL);
            if (sendonly)
                cw_rtp_stop(p->rtp);
            /* Activate a re-invite */
            cw_queue_frame(p->owner, &cw_null_frame);
        }
        cw_object_put(bridgepeer);
    }

    /* Manager Hold and Unhold events must be generated, if necessary */
    if (cw_sockaddr_is_specific(&addr.sa) && !sendonly)
    {
        if (callevents && cw_test_flag(p, SIP_CALL_ONHOLD))
        {
            cw_manager_event(EVENT_FLAG_CALL, "Unhold",
                2,
                cw_msg_tuple("Channel",  "%s", p->owner->name),
                cw_msg_tuple("Uniqueid", "%s", p->owner->uniqueid)
            );
        }
        cw_clear_flag(p, SIP_CALL_ONHOLD);
    }
    else
    {            
        /* No address for RTP, we're on hold */
        if (callevents && !cw_test_flag(p, SIP_CALL_ONHOLD))
        {
            cw_manager_event(EVENT_FLAG_CALL, "Hold",
                2,
                cw_msg_tuple("Channel",  "%s", p->owner->name),
                cw_msg_tuple("Uniqueid", "%s", p->owner->uniqueid)
            );
        }
        cw_set_flag(p, SIP_CALL_ONHOLD);
    }

    return 0;
}


struct cw_mime_process_action {
	const char *content_type;
	int content_type_len;
	int (*action)(struct sip_pvt *dialogue, struct sip_request *req, int ignore, int start, int end);
};


static const struct cw_mime_process_action mime_sdp_actions[] = {
	{ "application/sdp", sizeof("application/sdp") - 1, process_sdp }
};


static int mime_parse(struct sip_pvt *dialogue, struct sip_request *req, int ignore, const struct cw_mime_process_action *actions, int nactions, const char *content_type, int start, int end)
{
	size_t content_type_len;
	int processed = 0;

	for (content_type_len = 0; content_type[content_type_len] && content_type[content_type_len] >= 32 && !isspace(content_type[content_type_len]) && !strchr("()<>@,;:\\\"/[]?=", content_type[content_type_len]); content_type_len++);
	if (content_type[content_type_len] == '/')
		for (content_type_len++; content_type[content_type_len] && content_type[content_type_len] >= 32 && !isspace(content_type[content_type_len]) && !strchr("()<>@,;:\\\"/[]?=", content_type[content_type_len]); content_type_len++);

	if (content_type_len == sizeof("multipart/mixed") - 1 && !strncasecmp(content_type, "multipart/mixed", sizeof("multipart/mixed") - 1)) {
		const char *p, *key, *value;
		size_t key_len, value_len;

		value = p = &content_type[content_type_len];
		value_len = 0;
		while (*p && *p != ';') p++;
		while (*p) {
			do { p++; } while (isspace(*p));
			key = p;
			while (*p && *p != '=') p++;
			for (key_len = p - key; key_len && isspace(key[key_len - 1]); key_len--);
			if (*p)
				do { p++; } while (*p && isspace(*p));
			if (*p != '"') {
				value = p;
				while (*p && *p != ';') p++;
				for (value_len = p - value; value_len && isspace(value[value_len - 1]); value_len--);
			} else {
				p++;
				value = p;
				while (*p && *p != '"') p += (p[0] == '\\' && p[1] ? 2 : 1);
				for (value_len = p - value; value_len && isspace(value[value_len - 1]); value_len--);
				while (*p && *p != ';') p++;
			}

			if (key_len == sizeof("boundary") - 1 && !strncasecmp(key, "boundary", sizeof("boundary") - 1))
				break;
		}

		p = &req->pkt.data[start];
		start = -1;
		end = -1;
		content_type = "";

		while (1) {
			const char *q;
			int r;

			while ((q = strpbrk(p, "\r\n"))) {
				if (p[0] == '-' && p[1] == '-' && !strncmp(&p[2], value, value_len) && (p[value_len] == '\r' || p[value_len] == '\n' || isspace(p[value_len]) || !strncmp(&p[value_len], "--\r\n", 4)))
					break;
				p = (q[0] == '\r' ? &q[2] : &q[1]);
			}

			/* N.B. When parsing nested multiparts we do not keep watching for the outer
			 * boundary marker and thus require correctly formatted objects.
			 * This is a technical violation of RFC 2046 5.1.2 :-(.
			 */
			if ((r = mime_parse(dialogue, req, ignore, actions, nactions, content_type, start, p - req->pkt.data)) < 0)
				break;
			processed += r;

			if (!q || p[value_len] == '-')
				break;

			p = (q[0] == '\r' ? &q[2] : &q[1]);
			/* RFC 2045 5.2 says the default content-type is "text-plain; charset=us-ascii"
			 * however RFC 3261 7.4.1 says the default charset is "UTF-8". We do not do
			 * explicit charset processing at all.
			 */
			content_type = "text/plain";

			while ((q = strpbrk(p, "\r\n"))) {
				if (p == q)
					break;
				if (!strncasecmp(p, "Content-type:", 13))
					content_type = &p[13];
				p = (q[0] == '\r' ? &q[2] : &q[1]);
			}

			if (!q)
				break;

			p = (q[0] == '\r' ? &q[2] : &q[1]);
			start = p - req->pkt.data;
		}
	} else {
		int i;

		for (i = 0; i < nactions; i++) {
			if ((content_type_len == actions[i].content_type_len && !strncasecmp(content_type, actions[i].content_type, content_type_len))
			|| (actions[i].content_type_len <= 0 && content_type_len >= -actions[i].content_type_len && !strncasecmp(content_type, actions[i].content_type, -actions[i].content_type_len))) {
				processed = (actions[i].action(dialogue, req, ignore, req->body_start, req->pkt.used) ? -1 : 1);
				break;
			}
		}
	}

	return processed;
}


static int mime_process(struct sip_pvt *dialogue, struct sip_request *req, int ignore, const struct cw_mime_process_action *actions, int nactions)
{
	const char *content_type;

	if (!(content_type = get_header(req, SIP_HDR_CONTENT_TYPE))) {
		/* RFC 2045 5.2: default content-type is "text-plain; charset=us-ascii" */
		content_type = "text/plain";
	}

	return mime_parse(dialogue, req, ignore, actions, nactions, content_type, req->body_start, req->pkt.used);
}



/*! \brief  add_header_contentLength: Add 'Content-Length' header to SIP message */
static int add_header_contentLength(struct sip_request *req, int content_len)
{
	size_t base;
	int offset;

	base = req->pkt.used;
	cw_dynstr_printf(&req->pkt, "%s: %n%-10d\r\n", sip_hdr_name[SIP_NHDR_CONTENT_LENGTH], &offset, content_len);

	return base + offset;
}


/*! \brief  add_header_contentLen: Add 'Content-Length' header to SIP message */
static void update_header_contentLength(struct sip_request *req, int offset, int len)
{
	if (offset >= 0 && !req->pkt.error)
		snprintf(req->pkt.data + offset, req->pkt.size - offset - 1, "%-10d", len);
}


/*! \brief  copy_header: Copy one header from one request to another */
static void copy_header(struct sip_request *req, const struct sip_request *orig, const char *name, size_t name_len, const char *alias, size_t alias_len)
{
	char *tmp;

	if ((tmp = get_header(orig, name, name_len, alias, alias_len)) && tmp[0])
		cw_dynstr_printf(&req->pkt, "%s: %s\r\n", ((sip_hdr_name == sip_hdr_fullname || !alias_len) ? name : alias), tmp);
	else
		cw_log(CW_LOG_NOTICE, "No header '%s' present to copy\n", name);
}


/*! \brief  copy_all_header: Copy all headers from one request to another */
static void copy_all_header(struct sip_request *req, const struct sip_request *orig, const char *name, size_t name_len, const char *alias, size_t alias_len)
{
	const char *dstname = ((sip_hdr_name == sip_hdr_fullname || !alias_len) ? name : alias);
	char *p;
	int start = req->hdr_start;
    
	while ((p = __get_header(orig, name, name_len, alias, alias_len, &start)) && p[0])
		cw_dynstr_printf(&req->pkt, "%s: %s\r\n", dstname, p);
}


/*! \brief  copy_via_headers: Copy SIP VIA Headers from the request to the response */
/*    If the client indicates that it wishes to know the port we received from,
    it adds ;rport without an argument to the topmost via header. We need to
    add the port number (from our point of view) to that parameter.
    We always add ;received=<ip address> to the topmost via header.
    Received: RFC 3261, rport RFC 3581 */
static void copy_via_headers(struct sip_request *resp, const struct sip_request *req)
{
	char *oh;
	int start = req->hdr_start;

	if ((oh = __get_header(req, SIP_HDR_VIA, &start)) && oh[0]) {
		/* Only check for empty rport in topmost via header */
		char *rport, *end;

		/* RFC3261 ABNF:
		 * the only place an IPv6 is allowed to appear without being enclosed
		 * in "[...]" is in the "received" parameter of a "Via" header. This may
		 * be an oversight, it may not. The jury is out and RFC5118 says either
		 * SHOULD be accepted.
		 * Here we go with the current RFC3261 and use bare IPv6 addresses. If this
		 * should change simply change "received=%@" to "received=%#@" in the below
		 * two formats.
		 */
		if ((rport = strstr(oh, ";rport")) && rport[6] != '=' && (!(end = strchr(oh, ',')) || rport < end)) {
			ptrdiff_t n = rport - oh;

			for (rport += sizeof(";rport") - 1; rport[0] && rport[0] != ';' && rport[0] != ','; rport++);

			cw_dynstr_tprintf(&resp->pkt, 3,
				cw_fmtval("%s: %.*s", sip_hdr_name[SIP_NHDR_VIA], n, oh),
				cw_fmtval(";received=%@;rport=%#h@", &req->recvdaddr.sa, &req->recvdaddr.sa),
				cw_fmtval("%s\r\n", rport)
			);
		} else
			/* We should *always* add a received to the topmost via */
			cw_dynstr_printf(&resp->pkt, "%s: %s;received=%@\r\n", sip_hdr_name[SIP_NHDR_VIA], oh, &req->recvdaddr.sa);
	}

	while ((oh = __get_header(req, SIP_HDR_VIA, &start)) && oh[0])
		cw_dynstr_printf(&resp->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_VIA], oh);
}

/*! \brief  add_route: Add route header into request per learned route */
static void add_route(struct sip_request *req, const struct sip_route *route)
{
	if (route) {
		cw_dynstr_printf(&req->pkt, "Route: <%s>", route->hop);
		route = route->next;

		while (route) {
			cw_dynstr_printf(&req->pkt, ",<%s>", route->hop);
			route = route->next;
		}

		cw_dynstr_printf(&req->pkt, "\r\n");
	}
}


/*! \brief  set_destination: Set destination from SIP URI */
static void set_destination(struct sip_pvt *p, char *uri)
{
	struct cw_sockaddr_net peeraddr;
	char *h, *maddr, *host;
	int n;
	uint16_t portno;

	/* Parse uri to h (host) and port - uri is already just the part inside the <> */
	/* general form we are expecting is sip[s]:username[:password]@host[:port][;...] */

	/* Find and parse hostname */
	if ((h = strchr(uri, '@')))
		h++;
	else {
		if (strncasecmp(uri, "sip:", 4) == 0)
			h = uri + 4;
		else if (strncasecmp(uri, "sips:", 5) == 0)
			h = uri + 5;
	}

	if (h) {
		n = 0;
		if (h[0] == '[')
			n = strcspn(++h, "];>");
		n += strcspn(&h[n], ":;>");

		/* Is "port" present after the hostname? If not default to DEFAULT_SIP_PORT */
		portno = DEFAULT_SIP_PORT;
		if (h[n] == ':')
			portno = atoi(&h[n + 1]);

		/* Maybe there's an "maddr=" to override address? */
		if ((maddr = strstr(&h[n], ";maddr="))) {
			h = maddr + sizeof(";maddr=") - 1;
			n = strcspn(h, ";>");
		}

		host = alloca(n + 1);
		memcpy(host, h, n);
		host[n] = '\0';

		if (!(n = cw_get_ip_or_srv(AF_UNSPEC, &peeraddr.sa, host, "_sip._udp"))) {
			cw_sockaddr_set_port(&peeraddr.sa, portno);
			cw_sip_ouraddrfor(p, &peeraddr.sa, sizeof(peeraddr));
		} else
			cw_log(CW_LOG_ERROR, "%s: %s\n", host, gai_strerror(n));
	}
}


/*! \brief  init_req: Initialize SIP request */
static void init_req(struct sip_request *req, enum sipmethod sipmethod, const char *recip)
{
	init_msg(req, 4096);
	req->method = sipmethod;
	cw_dynstr_printf(&req->pkt, "%s %s SIP/2.0\r\n", sip_methods[sipmethod].text, recip);
}


/*! \brief  respprep: Prepare SIP response packet */
static struct sip_request *respprep(struct sip_request *resp, struct sip_pvt *p, const char *msg, const struct sip_request *req)
{
	const char *ot;
	size_t base;
	int offset;

	if (!resp) {
		if (!(resp = malloc(sizeof(*resp))))
			goto out;
		resp->free = 1;
	}

	init_msg(resp, 4096);

	resp->method = SIP_RESPONSE;
	resp->seqno = req->seqno;

	cw_dynstr_printf(&resp->pkt, "SIP/2.0 %s\r\n", msg);
	copy_via_headers(resp, req);

	if (msg[0] == '2')
		copy_all_header(resp, req, SIP_HDR_NOSHORT("Record-Route"));

	copy_header(resp, req, SIP_HDR_CALL_ID);
	copy_header(resp, req, SIP_HDR_FROM);

	ot = get_header(req, SIP_HDR_TO);
	if (!strcasestr(ot, ";tag=") && strncmp(msg, "100 ", 4)) {
		/* Add the proper tag if we don't have it already.  If they have specified
		   their tag, use it.  Otherwise, use our own tag */
		cw_dynstr_printf(&resp->pkt, "%s: %s;tag=%s\r\n", sip_hdr_name[SIP_NHDR_TO], ot,
			(!cw_strlen_zero(p->theirtag) && cw_test_flag(p, SIP_OUTGOING) ? p->theirtag : p->tag));
	} else
		cw_dynstr_printf(&resp->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_TO], ot);

	base = resp->pkt.used;
	cw_dynstr_tprintf(&resp->pkt, 4,
		cw_fmtval("CSeq: %n%s\r\n",       &offset, req->pkt.data + req->cseq),
		cw_fmtval("User-Agent: %s\r\n",   default_useragent),
		cw_fmtval("Allow: %s\r\n",        ALLOWED_METHODS),
		cw_fmtval("Max-Forwards: %s\r\n", DEFAULT_MAX_FORWARDS)
	);
	resp->cseq = base + offset;

	if (msg[0] == '2' && (req->method == SIP_SUBSCRIBE || req->method == SIP_REGISTER)) {
		/* For registration responses, we also need expiry and contact info */
		cw_dynstr_printf(&resp->pkt, "Expires: %d\r\n", p->expiry);
		/* Only add contact if we have an expiry time */
		if (p->expiry)
			cw_dynstr_printf(&resp->pkt, "%s: %s;expires=%d\r\n", sip_hdr_name[SIP_NHDR_CONTACT], p->our_contact, p->expiry);
	} else if (msg[0] != '4' && p->our_contact[0])
		cw_dynstr_printf(&resp->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTACT], p->our_contact);

out:
	return resp;
}

/*! \brief  reqprep: Initialize a SIP request packet */
static struct sip_request *reqprep(struct sip_pvt *p, struct sip_request *req, const struct sip_request *orig, enum sipmethod sipmethod, unsigned int seqno, int newbranch)
{
	char stripped[80];
	char *c, *n;
	char *ot, *of;
	int is_strict = 0;    /* Strict routing flag */
	int needtag;

	if (!req) {
		if (!(req = malloc(sizeof(*req))))
			goto out;
		req->free = 1;
	}

	snprintf(p->lastmsg, sizeof(p->lastmsg), "Tx: %s", sip_methods[sipmethod].text);
    
	if (newbranch)
		p->branch ^= cw_random();

	/* Check for strict or loose router */
	if (p->route && !cw_strlen_zero(p->route->hop) && strstr(p->route->hop,";lr") == NULL)
		is_strict = 1;

	if (sipmethod == SIP_CANCEL)
		c = p->initreq.pkt.data + p->initreq.uriresp;    /* Use original URI */
	else if (sipmethod == SIP_ACK) {
		/* Use URI from Contact: in 200 OK (if INVITE)
		(we only have the contacturi on INVITEs) */
		if (!cw_strlen_zero(p->okcontacturi))
			c = is_strict ? p->route->hop : p->okcontacturi;
		else
			c = p->initreq.pkt.data + p->initreq.uriresp;
	} else if (!cw_strlen_zero(p->okcontacturi))
		c = is_strict ? p->route->hop : p->okcontacturi; /* Use for BYE or REINVITE */
	else if (!cw_strlen_zero(p->uri))
		c = p->uri;
	else {
		/* We have no URI, use To: or From:  header as URI (depending on direction) */
		if (cw_test_flag(p, SIP_OUTGOING))
			c = get_header(orig, SIP_HDR_TO);
		else
			c = get_header(orig, SIP_HDR_FROM);
		cw_copy_string(stripped, c, sizeof(stripped));
		c = get_in_brackets(stripped);
		if ((n = strchr(c, ';')))
			*n = '\0';
	}

	init_req(req, sipmethod, c);

	if (!seqno)
		seqno = ++p->ocseq;
	req->seqno = seqno;

	/* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
	/* Work around buggy UNIDEN UIP200 firmware by not asking for rport unnecessarily */
	cw_dynstr_printf(&req->pkt, "%s: SIP/2.0/UDP %#l@;branch=z9hG4bK%08x%s\r\n", sip_hdr_name[SIP_NHDR_VIA], &p->stunaddr.sa, p->branch, ((cw_test_flag(p, SIP_NAT) & SIP_NAT_RFC3581) ? ";rport" : ""));

	if (p->route) {
		if (is_strict)
			add_route(req, p->route->next);
		else
			add_route(req, p->route);
	}

	ot = get_header(orig, SIP_HDR_TO);
	of = get_header(orig, SIP_HDR_FROM);

	/* Add tag *unless* this is a CANCEL, in which case we need to send it exactly
	   as our original request, including tag (or presumably lack thereof) */
	needtag = (!strcasestr(ot, ";tag=") && sipmethod != SIP_CANCEL);
	if (cw_test_flag(p, SIP_OUTGOING))
		cw_dynstr_tprintf(&req->pkt, 2,
			cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_FROM], of),
			cw_fmtval("%s: %s%s%s\r\n", sip_hdr_name[SIP_NHDR_TO], ot,
				(needtag && p->theirtag[0] ? ";tag=" : ""),
				(needtag && p->theirtag[0] ? p->theirtag : ""))
		);
	else
		cw_dynstr_tprintf(&req->pkt, 2,
			cw_fmtval("%s: %s%s%s\r\n", sip_hdr_name[SIP_NHDR_FROM], ot,
				(needtag ? ";tag=" : ""),
				(needtag ? p->tag : "")),
			cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_TO], of)
		);

	copy_header(req, orig, SIP_HDR_CALL_ID);

	cw_dynstr_tprintf(&req->pkt, 4,
		cw_fmtval("%s: %s\r\n",           sip_hdr_name[SIP_NHDR_CONTACT], p->our_contact),
		cw_fmtval("CSeq: %u %s\r\n",      seqno, sip_methods[sipmethod].text),
		cw_fmtval("User-Agent: %s\r\n",   default_useragent),
		cw_fmtval("Max-Forwards: %s\r\n", DEFAULT_MAX_FORWARDS)
	);

	if (p->rpid)
		cw_dynstr_printf(&req->pkt, "Remote-Party-ID: %s\r\n", p->rpid);

out:
	return req;
}


/*! \brief  transmit_response: Base transmit response function */
static void transmit_response(struct sip_pvt *p, const char *status, struct sip_request *req, int reliable)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = respprep((reliable ? NULL : &tmpmsg), p, status, req))) {
		add_header_contentLength(msg, 0);

		if (status[0] != '1') {
			/* If we are cancelling an incoming invite for some reason, add information
			 * about the reason why we are doing this in clear text
			 */
			if (p->owner && p->owner->hangupcause)
				cw_dynstr_printf(&msg->pkt, "X-CallWeaver-HangupCause: %s\r\n", cw_cause2str(p->owner->hangupcause));

			/* If reliable transmission is called for we note the time so that
			 * we can determine the RTT if the first transmission is ACKed.
			 */
			if (reliable)
				cw_clock_gettime(global_clock_monotonic, &msg->txtime);
		} else if (!memcmp(status, "100 ", 4)) {
			char *s;

			/* RFC3261 8.2.6:
			 * When a 100 (Trying) response is generated, any Timestamp header field
			 * present in the request MUST be copied into this 100 (Trying) response.
			 * If there is a delay in generating the response, the UAS SHOULD add a
			 * delay value into the Timestamp value in the response. This value MUST
			 * contain the difference between the time of sending of the response and
			 * receipt of the request, measured in seconds.
			 */
			if ((s = get_header(req, SIP_HDR_NOSHORT("Timestamp"))) && s[0]) {
				if (req->txtime.tv_sec || req->txtime.tv_nsec) {
					struct timespec ts;

					cw_clock_gettime(global_clock_monotonic, &ts);
					cw_dynstr_printf(&msg->pkt, "Timestamp: %s %lu.%09lu\r\n", s, ts.tv_sec, ts.tv_nsec);
				} else {
					cw_dynstr_printf(&msg->pkt, "Timestamp: %s\r\n", s);
				}
			}
		}

		cw_dynstr_printf(&msg->pkt, "\r\n");

		send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, msg, reliable);
	}
}

//TODO
/*! \brief  transmit_error: Transmit response, no retransmits, using temporary pvt */
static void transmit_error(struct sip_request *req, const char *status)
{
	struct sip_pvt dialogue;

	memset(&dialogue, 0, sizeof(dialogue));

	dialogue.branch = cw_random();
	/* FIXME: do we _need_ to add a tag in error responses? */
	make_our_tag(&dialogue);
	dialogue.ocseq = 1;

	cw_copy_flags(&dialogue, &global_flags, SIP_NAT);

	transmit_response(&dialogue, status, req, 0);
}


/*! \brief  transmit_response_with_unsupported: Transmit response, no retransmits */
static void transmit_response_with_unsupported(struct sip_pvt *p, const char *status, struct sip_request *req, char *unsupported)
{
	struct sip_request msg;

	respprep(&msg, p, status, req);
	append_date(&msg);
	cw_dynstr_printf(&msg.pkt, "Unsupported: %s\r\n", unsupported);
	send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, &msg, 0);
}

/*! \brief  append_date: Append date to SIP message */
static void append_date(struct sip_request *req)
{
	struct tm tm;
	size_t space = 32;
	time_t t;
	int n;

	time(&t);
	gmtime_r(&t, &tm);
	cw_dynstr_printf(&req->pkt, "Date: ");
	do {
		cw_dynstr_need(&req->pkt, space);
	} while (!(n = strftime(req->pkt.data + req->pkt.used, req->pkt.size - req->pkt.used, "%a, %d %b %Y %T GMT", &tm)) && !req->pkt.error);
	req->pkt.used += n;
	cw_dynstr_printf(&req->pkt, "\r\n");
}

/*! \brief  transmit_response_with_date: Append date and content length before transmitting response */
static void transmit_response_with_date(struct sip_pvt *p, const char *status, struct sip_request *req)
{
	struct sip_request msg;

	respprep(&msg, p, status, req);
	append_date(&msg);
	add_header_contentLength(&msg, 0);
	cw_dynstr_printf(&msg.pkt, "\r\n");
	send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, &msg, 0);
}

/*! \brief  transmit_response_with_allow: Append Accept header, content length before transmitting response */
static void transmit_response_with_allow(struct sip_pvt *p, const char *status, struct sip_request *req, int reliable)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = respprep((reliable ? NULL : &tmpmsg), p, status, req))) {
		cw_dynstr_printf(&msg->pkt, "Accept: application/sdp\r\n");
		add_header_contentLength(msg, 0);
		cw_dynstr_printf(&msg->pkt, "\r\n");
		send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, msg, reliable);
	}
}

/* transmit_response_with_auth: Respond with authorization request */
static void transmit_response_with_auth(struct sip_pvt *p, const char *status, struct sip_request *req, const char *randdata, int reliable, const char *header, int stale)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = respprep((reliable ? NULL : &tmpmsg), p, status, req))) {
		/* Stale means that they sent us correct authentication, but based it on an old challenge (nonce) */
		cw_dynstr_printf(&msg->pkt, "%s: Digest algorithm=MD5, realm=\"%s\", nonce=\"%s\"%s\r\n", sip_hdr_generic(header), global_realm, randdata, stale ? ", stale=true" : "");
		add_header_contentLength(msg, 0);
		cw_dynstr_printf(&msg->pkt, "\r\n");
		send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, msg, reliable);
	}
}


static void add_codec_to_sdp(const struct sip_pvt *p, struct sip_request *resp, int codec, int sample_rate, int debug)
{
	int rtp_code;

	if (debug)
		cw_verbose("Adding codec 0x%x (%s) to SDP\n", codec, cw_getformatname(codec));

	if ((rtp_code = cw_rtp_lookup_code(p->rtp, 1, codec)) != -1) {
		cw_dynstr_printf(&resp->pkt, "a=rtpmap:%d %s/%d\r\n", rtp_code, cw_rtp_lookup_mime_subtype(1, codec), sample_rate);

		if (codec == CW_FORMAT_G729A) {
			/* Indicate that we don't support VAD (G.729 annex B) */
			cw_dynstr_printf(&resp->pkt, "a=fmtp:%d annexb=no\r\n", rtp_code);
		}
	}
}

static void add_noncodec_to_sdp(const struct sip_pvt *p, struct sip_request *resp,
                int format, int sample_rate, int debug)
{
	int rtp_code;

	if (debug)
		cw_verbose("Adding non-codec 0x%x (%s) to SDP\n", format, cw_rtp_lookup_mime_subtype(0, format));

	if ((rtp_code = cw_rtp_lookup_code(p->rtp, 0, format)) != -1) {
		cw_dynstr_printf(&resp->pkt, "a=rtpmap:%d %s/%d", rtp_code, cw_rtp_lookup_mime_subtype(0, format), sample_rate);

		if (format == CW_RTP_DTMF) {
			/* Indicate we support DTMF and FLASH... */
			cw_dynstr_printf(&resp->pkt, "a=fmtp:%d 0-16", rtp_code);
		}
	}
}

/*! \brief  t38_get_rate: Get Max T.38 Transmision rate from T38 capabilities */
static int t38_get_rate(int t38cap)
{
    int maxrate = (t38cap & (T38FAX_RATE_14400 | T38FAX_RATE_12000 | T38FAX_RATE_9600 | T38FAX_RATE_7200 | T38FAX_RATE_4800 | T38FAX_RATE_2400));

    if (maxrate & T38FAX_RATE_14400)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 14400 found\n");
        return 14400;
    }
    else if (maxrate & T38FAX_RATE_12000)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 12000 found\n");
        return 12000;
    }
    else if (maxrate & T38FAX_RATE_9600)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 9600 found\n");
        return 9600;
    }
    else if (maxrate & T38FAX_RATE_7200)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 7200 found\n");
        return 7200;
    }
    else if (maxrate & T38FAX_RATE_4800)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 4800 found\n");
        return 4800;
    }
    else if (maxrate & T38FAX_RATE_2400)
    {
        cw_log(CW_LOG_DEBUG, "T38MaxFaxRate 2400 found\n");
        return 2400;
    }
    else
    {
        cw_log(CW_LOG_DEBUG, "Strange, T38MaxFaxRate NOT found in peers T38 SDP.\n");
        return 0;
    }
}

/*! \brief  add_t38_sdp: Add T.38 Session Description Protocol message */
static void add_t38_sdp(struct sip_request *resp, struct sip_pvt *p)
{
	struct cw_sockaddr_net udptldest;
	unsigned int sdp_start;
	int cl_index;
	int x = 0;
	int debug;

	if (!p->udptl) {
		cw_log(CW_LOG_WARNING, "No way to add SDP without an UDPTL structure\n");
		return;
	}

	debug = sip_debug_test_pvt(p);

	if (!p->sessionid) {
		p->sessionid = getpid();
		p->sessionversion = p->sessionid;
	} else {
		p->sessionversion++;
	}

	/* Determine T.38 UDPTL destination */
	if (cw_sockaddr_is_specific(&p->udptlredirip.sa))
		cw_sockaddr_copy(&udptldest.sa, &p->udptlredirip.sa);
	else {
		udptldest = p->stunaddr;
		cw_sockaddr_set_port(&udptldest.sa, cw_sockaddr_get_port(cw_udptl_get_us(p->udptl)));
	}

	if (debug)
		cw_verbose("T.38 UDPTL is at %#l@\n", &udptldest.sa);

	/* We break with the "recommendation" and send our IP, in order that our
	   peer doesn't have to cw_gethostbyname() us */

	if (debug) {
		cw_log(CW_LOG_DEBUG, "Our T38 capability (%d), peer T38 capability (%d), joint capability (%d)\n",
			p->t38capability,
			p->t38peercapability,
			p->t38jointcapability);
	}

	cw_dynstr_printf(&resp->pkt, "%s: application/sdp\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE]);
	cl_index = add_header_contentLength(resp, 0);
	cw_dynstr_printf(&resp->pkt, "\r\n");
	sdp_start = resp->pkt.used;

	x = cw_udptl_get_local_max_datagram(p->udptl);
	cw_dynstr_tprintf(&resp->pkt, 15,
		cw_fmtval("v=0\r\n"),
		cw_fmtval("o=root %d %d IN IP%c %@\r\n", p->sessionid, p->sessionversion, (udptldest.sa.sa_family == AF_INET ? '4' : '6'), &udptldest.sa),
		cw_fmtval("s=session\r\n"),
		cw_fmtval("c=IN IP%c %@\r\n", (udptldest.sa.sa_family == AF_INET ? '4' : '6'), &udptldest.sa),
		cw_fmtval("t=0 0\r\n"),
		cw_fmtval("m=image %#h@ udptl t38\r\n", &udptldest.sa),
		cw_fmtval("%s", ((p->t38jointcapability & T38FAX_VERSION) == T38FAX_VERSION_0 ? "a=T38FaxVersion:0\r\n" : "")),
		cw_fmtval("%s",  ((p->t38jointcapability & T38FAX_VERSION) == T38FAX_VERSION_1 ? "a=T38FaxVersion:1\r\n" : "")),
		cw_fmtval("%s", ((p->t38jointcapability & T38FAX_FILL_BIT_REMOVAL) ? "a=T38FaxFillBitRemoval\r\n" : "")),
		cw_fmtval("%s", ((p->t38jointcapability & T38FAX_TRANSCODING_MMR) ? "a=T38FaxTranscodingMMR\r\n" : "")),
		cw_fmtval("%s", ((p->t38jointcapability & T38FAX_TRANSCODING_JBIG) ? "a=T38FaxTranscodingJBIG\r\n" : "")),
		cw_fmtval("%s\r\n", ((p->t38jointcapability & T38FAX_RATE_MANAGEMENT_LOCAL_TCF) ? "a=T38FaxRateManagement:localTCF" : "a=T38FaxRateManagement:transferredTCF")),
		cw_fmtval("%s", (((p->t38capability & (T38FAX_UDP_EC_FEC | T38FAX_UDP_EC_REDUNDANCY))) ? ((p->t38capability & T38FAX_UDP_EC_FEC) ? "a=T38FaxUdpEC:t38UDPFEC\r\n" : "a=T38FaxUdpEC:t38UDPRedundancy\r\n" ) : "")),
		cw_fmtval("a=T38FaxMaxBuffer:%d\r\n", x),
		cw_fmtval("a=T38FaxMaxDatagram:%d\r\n", x)
#if 0
		cw_fmtval("a=T38VendorInfo:0 0 0\r\n")
#endif
	);

	if ((x = t38_get_rate(p->t38jointcapability)))
		cw_dynstr_printf(&resp->pkt, "a=T38MaxBitRate:%d\r\n", x);

	update_header_contentLength(resp, cl_index, resp->pkt.used - sdp_start);

	/* Update lastrtprx when we send our SDP */
	p->lastrtptx = time(&p->lastrtprx);
}


/*! \brief  add_sdp: Add Session Description Protocol message */
static void add_sdp(struct sip_request *resp, struct sip_pvt *p)
{
	char m_audio[256];
	char m_video[256];
	char buf[256];
	struct cw_sockaddr_net dest;
	struct cw_sockaddr_net vdest;
#ifdef ENABLE_SRTP
	struct sip_srtp *srtp = p->srtp;
#endif
	const char *protocol = NULL;
	unsigned int sdp_start;
	int cl_index;
	int pref_codec;
	int alreadysent = 0;
	int x;
	int rtp_code;
	int capability;
	int debug;

	debug = sip_debug_test_pvt(p);

	if (!p->rtp) {
		cw_log(CW_LOG_WARNING, "No way to add SDP without an RTP structure\n");
		return;
	}
	capability = p->capability;

	if (!p->sessionid) {
		p->sessionid = getpid();
		p->sessionversion = p->sessionid;
	} else
		p->sessionversion++;

	if (cw_sockaddr_is_specific(&p->redirip.sa)) {
		cw_sockaddr_copy(&dest.sa, &p->redirip.sa);
		if (p->redircodecs)
			capability = p->redircodecs;
	} else {
		dest = p->stunaddr;
		cw_sockaddr_set_port(&dest.sa, cw_sockaddr_get_port(cw_rtp_get_us(p->rtp)));
	}

	/* Determine video destination */
	if (p->vrtp) {
		if (cw_sockaddr_is_specific(&p->vredirip.sa))
			cw_sockaddr_copy(&vdest.sa, &p->vredirip.sa);
		else {
			cw_sockaddr_copy(&vdest.sa, &p->stunaddr.sa);
			cw_sockaddr_set_port(&vdest.sa, cw_sockaddr_get_port(cw_rtp_get_us(p->vrtp)));
		}
	}

	sip_debug_ports(p);

	if (debug) {
		cw_verbose("We're at %#l@\n", &dest.sa);
		if (p->vrtp)
			cw_verbose("Video is at %#l@\n", &vdest.sa);
	}

	protocol = "AVP";
#ifdef ENABLE_SRTP
	if (srtp)
		protocol = "SAVP";
#endif

	/* We break with the "recommendation" and send our IP, in order that our
	   peer doesn't have to cw_gethostbyname() us */

	cw_dynstr_printf(&resp->pkt, "%s: application/sdp\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE]);
	cl_index = add_header_contentLength(resp, 0);
	cw_dynstr_printf(&resp->pkt, "\r\n");
	sdp_start = resp->pkt.used;

	cw_dynstr_tprintf(&resp->pkt, 5,
		cw_fmtval("v=0\r\n"),
		cw_fmtval("o=root %d %d IN IP%c %@\r\n", p->sessionid, p->sessionversion, (dest.sa.sa_family == AF_INET ? '4' : '6'), &dest.sa),
		cw_fmtval("s=session\r\n"),
		cw_fmtval("c=IN IP%c %@\r\n", (dest.sa.sa_family == AF_INET ? '4' : '6'), &dest.sa),
		cw_fmtval("t=0 0\r\n")
	);

	cw_snprintf(m_audio, sizeof(m_audio), "m=audio %#h@ RTP/%s", &dest.sa, protocol);
	cw_snprintf(m_video, sizeof(m_video), "m=video %#h@ RTP/AVP", &vdest.sa);

	/* Prefer the codec we were requested to use, first, no matter what */
	if (capability & p->prefcodec) {
		if ((rtp_code = cw_rtp_lookup_code(p->rtp, 1, p->prefcodec)) != -1) {
			sprintf(buf," %d", rtp_code);
			strcat((p->prefcodec <= CW_FORMAT_MAX_AUDIO ? m_audio : m_video), buf);
		}
		alreadysent |= p->prefcodec;
	}

	/* Start by sending our preferred codecs */
	for (x = 0;  x < 32;  x++) {
		if (!(pref_codec = cw_codec_pref_index(&p->prefs, x)))
			break;

		if ((capability & pref_codec) && !(alreadysent & pref_codec)) {
			if ((rtp_code = cw_rtp_lookup_code(p->rtp, 1, pref_codec)) != -1) {
				sprintf(buf, " %d", rtp_code);
				strcat((pref_codec <= CW_FORMAT_MAX_AUDIO ? m_audio : m_video), buf);
			}
			alreadysent |= pref_codec;
		}
	}

	if (t38rtpsupport)
		strcat(m_audio, " 122");

	/* Now send any other common codecs, and non-codec formats: */
	for (x = 1; x <= ((videosupport && p->vrtp) ? CW_FORMAT_MAX_VIDEO : CW_FORMAT_MAX_AUDIO); x <<= 1) {
		if ((capability & x) && !(alreadysent & x)) {
			if ((rtp_code = cw_rtp_lookup_code(p->rtp, 1, x)) != -1) {
				sprintf(buf, " %d", rtp_code);
				strcat((x <= CW_FORMAT_MAX_AUDIO ? m_audio : m_video), buf);
			}
		}
	}

	for (x = 1;  x <= CW_RTP_MAX;  x <<= 1) {
		if (!(p->noncodeccapability & x))
			continue;
		if ((rtp_code = cw_rtp_lookup_code(p->rtp, 0, x)) != -1) {
			sprintf(buf, " %d", rtp_code);
			strcat(m_audio, buf);
		}
	}

	cw_dynstr_printf(&resp->pkt, "%s\r\n", m_audio);
	alreadysent = 0;

#ifdef ENABLE_SRTP
	if (srtp) {
		if (srtp->a_crypto)
			cw_dynstr_printf(&resp->pkt, "%s\r\n", srtp->a_crypto);
		else
			cw_dynstr_printf(&resp->pkt, "a=crypto:1 AES_CM_128_HMAC_SHA1_80 inline:%s\r\n", srtp->local_key64);
	}
#endif

	/* **************************************************************************** */
	/* Prefer the codec we were requested to use, first, no matter what */
	if (capability & p->prefcodec) {
		add_codec_to_sdp(p, resp, p->prefcodec, ((p->prefcodec <= CW_FORMAT_MAX_AUDIO) ? 8000 : 90000), debug);
		alreadysent |= p->prefcodec;
	}

	/* Start by sending our preferred codecs */
	for (x = 0;  x < 32;  x++) {
		if (!(pref_codec = cw_codec_pref_index(&p->prefs, x)))
			break;

		if ((capability & pref_codec) && !(alreadysent & pref_codec)) {
			add_codec_to_sdp(p, resp, pref_codec, ((pref_codec <= CW_FORMAT_MAX_AUDIO) ? 8000 : 90000), debug);
			alreadysent |= pref_codec;
		}
	}

	if (t38rtpsupport) {
		/* TODO: Improve this? */
		cw_dynstr_printf(&resp->pkt, "a=rtpmap:122 t38/8000\r\n");
	}

	/* Now send any other common codecs, and non-codec formats: */
	for (x = 1;  x <= ((videosupport && p->vrtp)  ?  CW_FORMAT_MAX_VIDEO  :  CW_FORMAT_MAX_AUDIO);  x <<= 1) {
		if ((capability & x) && !(alreadysent & x))
			add_codec_to_sdp(p, resp, x, ((x <= CW_FORMAT_MAX_AUDIO) ? 8000 : 90000), debug);
	}

	for (x = 1;  x <= CW_RTP_MAX;  x <<= 1) {
		if ((p->noncodeccapability & x))
			add_noncodec_to_sdp(p, resp, x, 8000, debug);
	}

	cw_dynstr_printf(&resp->pkt, "a=silenceSupp:off - - - -\r\n");

	if ((p->vrtp) && (!cw_test_flag(p, SIP_NOVIDEO)) && (capability & VIDEO_CODEC_MASK)) {
		/* only if video response is appropriate */
		cw_dynstr_printf(&resp->pkt, "%s\r\n", m_video);
	}

	update_header_contentLength(resp, cl_index, resp->pkt.used - sdp_start);

	/* Update lastrtprx when we send our SDP */
	p->lastrtptx = time(&p->lastrtprx);
}


/*! \brief  copy_request: copy SIP request (mostly used to save request for responses) */
static void copy_request(struct sip_request *dst, struct sip_request *src)
{
	cw_dynstr_free(&dst->pkt);

	memcpy(dst, src, sizeof(struct sip_request));
	cw_dynstr_clone(&dst->pkt, &src->pkt);
}


static void copy_and_parse_request(struct sip_request *dst, struct sip_request *src)
{
	struct parse_request_state pstate;

	cw_dynstr_free(&dst->pkt);

	memcpy(dst, src, offsetof(typeof(*dst), pkt));
	cw_dynstr_clone(&dst->pkt, &src->pkt);
	parse_request_init(&pstate, dst);
	parse_request(&pstate, dst);
}


/*! \brief  transmit_response_with_sdp: Used for 200 OK and 183 early media */
static void transmit_response_with_sdp(struct sip_pvt *p, const char *status, struct sip_request *req, int reliable)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = respprep((reliable ? NULL : &tmpmsg), p, status, req))) {
		/* If reliable transmission is called for we note the time so that
		 * we can determine the RTT if the first transmission is ACKed.
		 */
		if (reliable)
			cw_clock_gettime(global_clock_monotonic, &msg->txtime);

		if (p->rtp) {
			cw_rtp_offered_from_local(p->rtp, 0);
			try_suggested_sip_codec(p);
			add_sdp(msg, p);
		} else
			cw_log(CW_LOG_ERROR, "Can't add SDP to response, since we have no RTP session allocated. Call-ID %s\n", p->callid);

		send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, msg, reliable);
	}
}

/*! \brief  transmit_response_with_t38_sdp: Used for 200 OK and 183 early media */
static void transmit_response_with_t38_sdp(struct sip_pvt *p, const char *status, struct sip_request *req, int reliable)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = respprep((reliable ? NULL : &tmpmsg), p, status, req))) {
		/* If reliable transmission is called for we note the time so that
		 * we can determine the RTT if the first transmission is ACKed.
		 */
		if (reliable)
			cw_clock_gettime(global_clock_monotonic, &msg->txtime);

		if (p->udptl) {
			cw_udptl_offered_from_local(p->udptl, 0);
			add_t38_sdp(msg, p);
		} else
			cw_log(CW_LOG_ERROR, "Can't add SDP to response, since we have no UDPTL session allocated. Call-ID %s\n", p->callid);

		send_message(p, req->conn, &req->ouraddr, &req->recvdaddr, msg, reliable);
	}
}


/*! \brief  transmit_reinvite_with_sdp: Transmit reinvite with SDP :-) */
/*     A re-invite is basically a new INVITE with the same CALL-ID and TAG as the
    INVITE that opened the SIP dialogue 
    We reinvite so that the audio stream (RTP) go directly between
    the SIP UAs. SIP Signalling stays with * in the path.
*/
static void transmit_reinvite_with_sdp(struct sip_pvt *p)
{
	struct sip_request *msg;

	if ((msg = reqprep(p, NULL, &p->initreq, (cw_test_flag(p, SIP_REINVITE_UPDATE) ? SIP_UPDATE : SIP_INVITE), 0, 1))) {
		/* Note the time so that we can determine the RTT if the first transmission is ACKed. */
		clock_gettime(global_clock_monotonic, &msg->txtime);

		cw_dynstr_printf(&msg->pkt, "Allow: " ALLOWED_METHODS "\r\n");
		if (sipdebug)
			cw_dynstr_printf(&msg->pkt, "X-callweaver-info: SIP re-invite (RTP bridge)\r\n");
		cw_rtp_offered_from_local(p->rtp, 1);
		add_sdp(msg, p);

		/* Use this as the basis */
		copy_and_parse_request(&p->initreq, msg);

		p->lastinvite = p->ocseq;
		cw_set_flag(p, SIP_OUTGOING);

		cw_log(CW_LOG_DEBUG, "Activating UDPTL on reinvite %s (b)\n", p->callid);
		if (t38udptlsupport  &&  p->udptl ) {
			p->udptl_active = 1;
			cw_channel_set_t38_status(p->owner, T38_NEGOTIATED);
		}

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

/*! \brief  transmit_reinvite_with_t38_sdp: Transmit reinvite with T38 SDP */
/*     A re-invite is basically a new INVITE with the same CALL-ID and TAG as the
    INVITE that opened the SIP dialogue 
    We reinvite so that the T38 processing can take place.
    SIP Signalling stays with * in the path.
*/
static void transmit_reinvite_with_t38_sdp(struct sip_pvt *p)
{
	struct sip_request *msg;

	if ((msg = reqprep(p, NULL, &p->initreq, (cw_test_flag(p, SIP_REINVITE_UPDATE) ? SIP_UPDATE : SIP_INVITE), 0, 1))) {
		/* Note the time so that we can determine the RTT if the first transmission is ACKed. */
		cw_clock_gettime(global_clock_monotonic, &msg->txtime);

		cw_dynstr_printf(&msg->pkt, "Allow: " ALLOWED_METHODS "\r\n");
		if (sipdebug)
			cw_dynstr_printf(&msg->pkt, "X-callweaver-info: SIP re-invite (T38 switchover)\r\n");
		cw_udptl_offered_from_local(p->udptl, 1);
		add_t38_sdp(msg, p);

		/* Use this as the basis */
		copy_and_parse_request(&p->initreq, msg);

		p->lastinvite = p->ocseq;
		cw_set_flag(p, SIP_OUTGOING);

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

/*! \brief  extract_uri: Check Contact: URI of SIP message */
static void extract_uri(struct sip_pvt *p, struct sip_request *req)
{
    char stripped[256];
    char *c, *n;
    cw_copy_string(stripped, get_header(req, SIP_HDR_CONTACT), sizeof(stripped));
    c = get_in_brackets(stripped);
    n = strchr(c, ';');
    if (n)
        *n = '\0';
    if (!cw_strlen_zero(c))
        cw_copy_string(p->uri, c, sizeof(p->uri));
}

/*! \brief  build_contact: Build contact header - the contact header we send out */
static void build_contact(struct sip_pvt *p)
{
	if (cw_sockaddr_get_port(&p->stunaddr.sa) != 5060)
		cw_snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s%s%#l@;transport=udp>", p->exten, (cw_strlen_zero(p->exten) ? "" : "@"), &p->stunaddr.sa);
	else
		cw_snprintf(p->our_contact, sizeof(p->our_contact), "<sip:%s%s%#@>", p->exten, (cw_strlen_zero(p->exten) ? "" : "@"), &p->stunaddr.sa);
}

/*! \brief  build_rpid: Build the Remote Party-ID & From using callingpres options */
static void build_rpid(struct sip_pvt *p)
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	const char *privacy = NULL;
	const char *screen = NULL;
	const char *clid;
	const char *clin;

	if (p->rpid || p->rpid_from)
		return;

	clid = (p->owner && p->owner->cid.cid_num ? p->owner->cid.cid_num : default_callerid);
	clin = (p->owner && !cw_strlen_zero(p->owner->cid.cid_name) ? p->owner->cid.cid_name : clid);

	switch (p->callingpres) {
		case CW_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
			privacy = "off";
			screen = "no";
			break;
		case CW_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
			privacy = "off";
			screen = "yes";
			break;
		case CW_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
			privacy = "off";
			screen = "no";
			break;
		case CW_PRES_ALLOWED_NETWORK_NUMBER:
			privacy = "off";
			screen = "yes";
			break;
		case CW_PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
			privacy = "full";
			screen = "no";
			break;
		case CW_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
			privacy = "full";
			screen = "yes";
			break;
		case CW_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
			privacy = "full";
			screen = "no";
			break;
		case CW_PRES_PROHIB_NETWORK_NUMBER:
			privacy = "full";
			screen = "yes";
			break;
		case CW_PRES_NUMBER_NOT_AVAILABLE:
			break;
		default:
			cw_log(CW_LOG_WARNING, "Unsupported callingpres (%d)\n", p->callingpres);
			if ((p->callingpres & CW_PRES_RESTRICTION) != CW_PRES_ALLOWED)
				privacy = "full";
			else
				privacy = "off";
			screen = "no";
			break;
	}

	cw_dynstr_init(&ds, 0, 1);

	if (cw_strlen_zero(p->fromdomain))
		cw_dynstr_printf(&ds, "\"%s\" <sip:%s@%#@>;tag=%s", clin, (cw_strlen_zero(p->fromuser) ? clid : p->fromuser), &p->stunaddr.sa, p->tag);
	else
		cw_dynstr_printf(&ds, "\"%s\" <sip:%s@%s>;tag=%s", clin, (cw_strlen_zero(p->fromuser) ? clid : p->fromuser), p->fromdomain, p->tag);
	p->rpid_from = cw_dynstr_steal(&ds);

	if (cw_strlen_zero(p->fromdomain))
		cw_dynstr_printf(&ds, "\"%s\" <sip:%s@%#@>", clin, clid, &p->stunaddr.sa);
	else
		cw_dynstr_printf(&ds, "\"%s\" <sip:%s@%s>", clin, clid, p->fromdomain);
	if (p->callingpres != CW_PRES_NUMBER_NOT_AVAILABLE)
		cw_dynstr_printf(&ds, ";privacy=%s;screen=%s", privacy, screen);
	p->rpid = cw_dynstr_steal(&ds);

	cw_dynstr_free(&ds);
}

/*! \brief  initreqprep: Initiate new SIP request to peer/user */
static struct sip_request *initreqprep(struct sip_request *req, struct sip_pvt *p, enum sipmethod sipmethod)
{
    struct cw_dynstr uri_ds = CW_DYNSTR_INIT_STATIC(p->uri);
    char from[256];
    char to[256];
    struct cw_dynstr tmp = CW_DYNSTR_INIT;
    struct cw_dynstr tmp2 = CW_DYNSTR_INIT;
    const char *l = NULL, *n = NULL;
    int x;

    if (!req) {
        if (!(req = malloc(sizeof(*req))))
            goto out;
        req->free = 1;
    }

    snprintf(p->lastmsg, sizeof(p->lastmsg), "Init: %s", sip_methods[sipmethod].text);

    if (p->owner)
    {
        l = p->owner->cid.cid_num;
        n = p->owner->cid.cid_name;
    }
    /* if we are not sending RPID and user wants his callerid restricted */
    if (!cw_test_flag(p, SIP_SENDRPID) && ((p->callingpres & CW_PRES_RESTRICTION) != CW_PRES_ALLOWED))
    {
        l = CALLERID_UNKNOWN;
        n = l;
    }
    if (cw_strlen_zero(l))
        l = default_callerid;
    if (cw_strlen_zero(n))
        n = l;
    /* Allow user to be overridden */
    if (!cw_strlen_zero(p->fromuser))
        l = p->fromuser;
    else /* Save for any further attempts */
        cw_copy_string(p->fromuser, l, sizeof(p->fromuser));

    /* Allow user to be overridden */
    if (!cw_strlen_zero(p->fromname))
        n = p->fromname;
    else /* Save for any further attempts */
        cw_copy_string(p->fromname, n, sizeof(p->fromname));

    if (pedanticsipchecking)
    {
        n = cw_uri_encode(n, &tmp, 0);
        l = cw_uri_encode(l, &tmp2, 0);
    }

    if (cw_strlen_zero(p->fromdomain)) {
        if (cw_sockaddr_get_port(&p->stunaddr.sa) != 5060)
            cw_snprintf(from, sizeof(from), "\"%s\" <sip:%s@%#l@>;tag=%s", n, l, &p->stunaddr.sa, p->tag);
        else
            cw_snprintf(from, sizeof(from), "\"%s\" <sip:%s@%#@>;tag=%s", n, l, &p->stunaddr.sa, p->tag);
    } else {
        if (cw_sockaddr_get_port(&p->stunaddr.sa) != 5060)
            cw_snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s:%#h@>;tag=%s", n, l, p->fromdomain, &p->stunaddr.sa, p->tag);
        else
            cw_snprintf(from, sizeof(from), "\"%s\" <sip:%s@%s>;tag=%s", n, l, p->fromdomain, p->tag);
    }

    cw_dynstr_free(&tmp);
    cw_dynstr_free(&tmp2);

    /* If we're calling a registered SIP peer, use the fullcontact to dial to the peer */
    if (!cw_strlen_zero(p->fullcontact))
    {
        /* If we have full contact, trust it */
        cw_dynstr_printf(&uri_ds, "%s", p->fullcontact);
    }
    else
    {
        /* Otherwise, use the username while waiting for registration */
        cw_dynstr_printf(&uri_ds, "sip:");
        if (!cw_strlen_zero(p->username))
        {
            if (pedanticsipchecking)
                cw_uri_encode(p->username, &uri_ds, 0);
            cw_dynstr_printf(&uri_ds, "%s@", (pedanticsipchecking ? "" : p->username));
        }
        cw_dynstr_printf(&uri_ds, "%s", p->tohost);
        if ((x = cw_sockaddr_get_port(&p->peeraddr.sa)) != 5060)
            cw_dynstr_printf(&uri_ds, ":%d", x);

        if (cw_test_flag(p, SIP_USEREQPHONE))
        {
            for (x = (p->username[0] == '+' ? 1 : 0); isdigit(p->username[x]); x++);
            if (!p->username[x])
                cw_dynstr_printf(&uri_ds, ";user=phone");
        }
    }

    /* If custom URI options have been provided, append them */
    if (p->options && p->options->uri_options)
        cw_dynstr_printf(&uri_ds, ";%s", p->options->uri_options->value);

    /* FIXME: at this stage we should check uri_ds.error and if it is
     * set return an error to the caller. Or we could just make p->uri
     * malloc'd data and steal uri_ds.data.
     */

    /* If there is a VXML URL append it to the SIP URL */
    if (p->options && p->options->vxml_url)
        snprintf(to, sizeof(to), "<%s>;%s", p->uri, p->options->vxml_url->value);
    else
        snprintf(to, sizeof(to), "<%s>", p->uri);

    if (sipmethod == SIP_NOTIFY && !cw_strlen_zero(p->theirtag)) { 
    	/* If this is a NOTIFY, use the From: tag in the subscribe (RFC 3265) */
	snprintf(to, sizeof(to), "<sip:%s>;tag=%s", p->uri, p->theirtag);
    } else if (p->options && p->options->vxml_url) {
	/* If there is a VXML URL append it to the SIP URL */
	snprintf(to, sizeof(to), "<%s>;%s", p->uri, p->options->vxml_url->value);
    } else {
    	snprintf(to, sizeof(to), "<%s>", p->uri);
    }

    init_req(req, sipmethod, p->uri);
    req->seqno = ++p->ocseq;

    /* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
    /* Work around buggy UNIDEN UIP200 firmware by not asking for rport unnecessarily */
    cw_dynstr_printf(&req->pkt, "%s: SIP/2.0/UDP %#l@;branch=z9hG4bK%08x%s\r\n", sip_hdr_name[SIP_NHDR_VIA], &p->stunaddr.sa, p->branch, ((cw_test_flag(p, SIP_NAT) & SIP_NAT_RFC3581) ? ";rport" : ""));

    /* Build Remote Party-ID and From */
    if (cw_test_flag(p, SIP_SENDRPID) && (sipmethod == SIP_INVITE)) {
        build_rpid(p);
        cw_dynstr_printf(&req->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_FROM], p->rpid_from);
    } else {
        cw_dynstr_printf(&req->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_FROM], from);
    }
    cw_copy_string(p->exten, l, sizeof(p->exten));
    build_contact(p);
    cw_dynstr_tprintf(&req->pkt, 6,
        cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_TO], to),
        cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTACT], p->our_contact),
        cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_CALL_ID], p->callid),
        cw_fmtval("CSeq: %u %s\r\n", req->seqno, sip_methods[sipmethod].text),
        cw_fmtval("User-Agent: %s\r\n", default_useragent),
        cw_fmtval("Max-Forwards: " DEFAULT_MAX_FORWARDS "\r\n")
    );
    if (p->rpid)
        cw_dynstr_printf(&req->pkt, "Remote-Party-ID: %s\r\n", p->rpid);

out:
    return req;
}

/*! \brief  transmit_invite: Build REFER/INVITE/OPTIONS message and transmit it */
static int transmit_invite(struct sip_pvt *p, enum sipmethod sipmethod, int sdp, int init)
{
	struct sip_request *msg;
	int res = -1;

	if (init) {
		/* Bump branch even on initial requests */
		p->branch ^= cw_random();
	}

	if (init > 1) {
		if (!(msg = initreqprep(NULL, p, sipmethod)))
			goto out;
	} else {
		if (!(msg = reqprep(p, NULL, &p->initreq, sipmethod, 0, 1)))
			goto out;
	}

	if (p->options && p->options->auth.used)
		cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_generic(p->options->authheader), p->options->auth.data);

	append_date(msg);
	if (sipmethod == SIP_REFER) {
		/* Call transfer */
		if (!cw_strlen_zero(p->refer_to))
			cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_REFER_TO], p->refer_to);
		if (!cw_strlen_zero(p->referred_by))
			cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_REFERRED_BY], p->referred_by);
	}
#ifdef OSP_SUPPORT
	if (sipmethod != SIP_OPTIONS && p->options && p->options->osptoken && !cw_strlen_zero(p->options->osptoken->value)) {
		cw_log(CW_LOG_DEBUG, "Adding OSP Token: %s\n", p->options->osptoken->value);
		cw_dynstr_printf(&msg->pkt, "P-OSP-Auth-Token: %s\r\n", p->options->osptoken->value);
	}
#endif
	if (p->options && p->options->distinctive_ring && !cw_strlen_zero(p->options->distinctive_ring->value))
		cw_dynstr_printf(&msg->pkt, "Alert-Info: %s\r\n", p->options->distinctive_ring->value);

	cw_dynstr_printf(&msg->pkt, "Allow: " ALLOWED_METHODS "\r\n");

	cw_clock_gettime(global_clock_monotonic, &msg->txtime);
	msg->txtime_i = msg->pkt.used;
	cw_dynstr_printf(&msg->pkt, "Timestamp: %9lu.%09lu\r\n", msg->txtime.tv_sec, msg->txtime.tv_nsec);

	if (p->owner) {
		char buf[] = "SIPADDHEADER01";
		struct cw_object *obj;
		int i = 2;

		while ((obj = cw_registry_find(&p->owner->vars, 1, cw_hash_var_name(buf), buf))) {
			struct cw_var_t *var = container_of(obj, struct cw_var_t, obj);
			char *content;

			if ((content = strchr(var->value, ':'))) {
				char *header;

				if ((header = malloc((content - var->value) + 1))) {
					memcpy(header, var->value, content - var->value);
					header[(content - var->value) + 1] = '\0';

					do {
						content++;
					} while (isblank(*content));

					cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_generic(header), content);

					if (sipdebug)
						cw_log(CW_LOG_DEBUG, "Adding SIP Header \"%s\" with content :%s: \n", header, content);

					free(header);
				} else
					cw_log(CW_LOG_ERROR, "Out of memory!\n");
			}

			cw_object_put_obj(obj);

			sprintf(buf + sizeof(buf) - 3, "%.2d", i++);
		}
	}
	if (sdp  &&  p->udptl  &&  (p->t38state == SIP_T38_OFFER_SENT_DIRECT)) {
		cw_udptl_offered_from_local(p->udptl, 1);
		cw_log(CW_LOG_DEBUG, "T38 is in state %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
		add_t38_sdp(msg, p);
	} else if (sdp  &&  p->rtp) {
		cw_rtp_offered_from_local(p->rtp, 1);
		add_sdp(msg, p);
	} else {
		add_header_contentLength(msg, 0);
		cw_dynstr_printf(&msg->pkt, "\r\n");
	}

	/* Use this as the basis */
	if (!p->initreq.pkt.used)
		copy_and_parse_request(&p->initreq, msg);

	p->lastinvite = p->ocseq;

	res = send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, (init ? 2 : 1));

out:
	return res;
}

/*! \brief  transmit_state_notify: Used in the SUBSCRIBE notification subsystem -*/
static void transmit_state_notify(struct sip_pvt *p, int state, int full, int substate, int timeout)
{
	char from[256], to[256];
	struct cw_dynstr hint = CW_DYNSTR_INIT;
	char *c, *a, *mfrom, *mto;
	struct sip_request *msg;
	const char *statestring = "terminated";
	const struct cfsubscription_types *subscriptiontype;
	enum state { NOTIFY_OPEN, NOTIFY_INUSE, NOTIFY_CLOSED } local_state = NOTIFY_OPEN;
	const char *pidfstate = "--";
	const char *pidfnote= "Ready";
	int cl_index, body_start;

	CW_UNUSED(substate);
	CW_UNUSED(timeout);

	switch (state) {
		case (CW_EXTENSION_RINGING | CW_EXTENSION_INUSE):
			if (global_notifyringing)
				statestring = "early";
			else
				statestring = "confirmed";
			local_state = NOTIFY_INUSE;
			pidfstate = "busy";
			pidfnote = "Ringing";
			break;
		case CW_EXTENSION_RINGING:
			statestring = "early";
			local_state = NOTIFY_INUSE;
			pidfstate = "busy";
			pidfnote = "Ringing";
			break;
		case CW_EXTENSION_INUSE:
			statestring = "confirmed";
			local_state = NOTIFY_INUSE;
			pidfstate = "busy";
			pidfnote = "On the phone";
			break;
		case CW_EXTENSION_BUSY:
			statestring = "confirmed";
			local_state = NOTIFY_CLOSED;
			pidfstate = "busy";
			pidfnote = "On the phone";
			break;
		case CW_EXTENSION_UNAVAILABLE:
			statestring = "confirmed";
			local_state = NOTIFY_CLOSED;
			pidfstate = "away";
			pidfnote = "Unavailable";
			break;
		case CW_EXTENSION_NOT_INUSE:
		default:
			/* Default setting */
			break;
	}

	subscriptiontype = find_subscription_type(p->subscribed);
   
	/* Check which device/devices we are watching  and if they are registered */
	if (cw_get_hint(&hint, NULL, NULL, p->context, p->exten) && !hint.error) {
		/* If they are not registered, we will override notification and show no availability */
		if (cw_device_state(hint.data) == CW_DEVICE_UNAVAILABLE) {
			local_state = NOTIFY_CLOSED;
			pidfstate = "away";
			pidfnote = "Not online";
		}
	}
	cw_dynstr_free(&hint);

	cw_copy_string(from, get_header(&p->initreq, SIP_HDR_FROM), sizeof(from));
	c = get_in_brackets(from);
	if (strncasecmp(c, "sip:", 4)) {
		cw_log(CW_LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return;
	}
	if ((a = strchr(c, ';')))
		*a = '\0';
	mfrom = c;

	cw_copy_string(to, get_header(&p->initreq, SIP_HDR_TO), sizeof(to));
	c = get_in_brackets(to);
	if (strncasecmp(c, "sip:", 4)) {
		cw_log(CW_LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
		return;
	}
	if ((a = strchr(c, ';')))
		*a = '\0';
	mto = c;

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_NOTIFY, 0, 1))) {
		cw_dynstr_tprintf(&msg->pkt, 2,
			cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_EVENT], subscriptiontype->event),
			cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE], subscriptiontype->mediatype)
		);

		switch(state) {
			case CW_EXTENSION_DEACTIVATED:
				if (p->subscribed == TIMEOUT)
					cw_dynstr_printf(&msg->pkt, "Subscription-State: terminated;reason=timeout\r\n");
				else {
					cw_dynstr_tprintf(&msg->pkt, 2,
						cw_fmtval("Subscription-State: terminated;reason=probation\r\n"),
						cw_fmtval("Retry-After: 60\r\n")
					);
				}
				break;
			case CW_EXTENSION_REMOVED:
				cw_dynstr_printf(&msg->pkt, "Subscription-State: terminated;reason=noresource\r\n");
				break;
			default:
				cw_dynstr_printf(&msg->pkt, "Subscription-State: %s\r\n", (p->expiry ? "active" : "terminated;reason=timeout"));
		}

		cl_index = add_header_contentLength(msg, 0);
		cw_dynstr_printf(&msg->pkt, "\r\n");
		body_start = msg->pkt.used;

		switch (p->subscribed) {
			case XPIDF_XML:
			case CPIM_PIDF_XML:
				cw_dynstr_tprintf(&msg->pkt, 9,
					cw_fmtval("<?xml version=\"1.0\"?>\n"),
					cw_fmtval("<!DOCTYPE presence PUBLIC \"-//IETF//DTD RFCxxxx XPIDF 1.0//EN\" \"xpidf.dtd\">\n"),
					cw_fmtval("<presence>\n"),
					cw_fmtval("<presentity uri=\"%s;method=SUBSCRIBE\" />\n", mfrom),
					cw_fmtval("<atom id=\"%s\">\n", p->exten),
					cw_fmtval("<address uri=\"%s;user=ip\" priority=\"0.800000\">\n", mto),
					cw_fmtval("<status status=\"%s\" />\n", (local_state ==  NOTIFY_OPEN ? "open" : (local_state == NOTIFY_INUSE ? "inuse" : "closed"))),
					cw_fmtval("<msnsubstatus substatus=\"%s\" />\n", (local_state == NOTIFY_OPEN ? "online" : (local_state == NOTIFY_INUSE ? "onthephone" : "offline"))),
					cw_fmtval("</address>\n</atom>\n</presence>\n")
				);
				break;
			case PIDF_XML: /* Eyebeam supports this format */
				cw_dynstr_tprintf(&msg->pkt, 15,
					cw_fmtval("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"),
					cw_fmtval("<presence xmlns=\"urn:ietf:params:xml:ns:pidf\" \nxmlns:pp=\"urn:ietf:params:xml:ns:pidf:person\"\nxmlns:es=\"urn:ietf:params:xml:ns:pidf:rpid:status:rpid-status\"\nxmlns:ep=\"urn:ietf:params:xml:ns:pidf:rpid:rpid-person\"\nentity=\"%s\">\n", mfrom),
					cw_fmtval("<pp:person><status>\n"),
					cw_fmtval("%s", (pidfstate[0] != '-' ? "<ep:activities><ep:" : "")),
					cw_fmtval("%s", (pidfstate[0] != '-' ? pidfstate : "")),
					cw_fmtval("%s", (pidfstate[0] != '-' ? "/></ep:activities>\n" : "")),
					cw_fmtval("</status></pp:person>\n"),
					cw_fmtval("<note>%s</note>\n", pidfnote),
					cw_fmtval("<tuple id=\"%s\">\n", p->exten),
					cw_fmtval("<contact priority=\"1\">%s</contact>\n", mto),
					cw_fmtval("%s", (pidfstate[0] == 'b' ? "<status><basic>open</basic></status>\n" : "")),
					cw_fmtval("%s", (pidfstate[0] != 'b' ? "<status><basic>" : "")),
					cw_fmtval("%s", (pidfstate[0] != 'b' ? (local_state != NOTIFY_CLOSED ? "open" : "closed") : "")),
					cw_fmtval("%s", (pidfstate[0] != 'b' ? "</basic></status>\n" : "")),
					cw_fmtval("</tuple>\n</presence>\n")
				);
				break;
			case DIALOG_INFO_XML: /* SNOM subscribes in this format */
				cw_dynstr_tprintf(&msg->pkt, 7,
					cw_fmtval("<?xml version=\"1.0\"?>\n"),
					cw_fmtval("<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"%d\" state=\"%s\" entity=\"%s\">\n", p->dialogver++, full ? "full":"partial", mto),
					cw_fmtval("<dialog id=\"%s\"", p->exten),
					cw_fmtval("%s", ((state & CW_EXTENSION_RINGING) && global_notifyringing ? " direction=\"recipient\"" : "")),
					cw_fmtval(">\n"),
					cw_fmtval("<state>%s</state>\n", statestring),
					cw_fmtval("</dialog>\n</dialog-info>\n")
				);
				break;
			case NONE:
			default:
				break;
		}

		update_header_contentLength(msg, cl_index, msg->pkt.used - body_start);

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

#if 0
/* Currently unused */

/*! \brief  transmit_notify_with_mwi: Notify user of messages waiting in voicemail */
/*      Notification only works for registered peers with mailbox= definitions
 *      in sip.conf
 *      We use the SIP Event package message-summary
 *      MIME type defaults to  "application/simple-message-summary";
 */
static void transmit_notify_with_mwi(struct sip_pvt *p, int newmsgs, int oldmsgs, char *vmexten)
{
	char tmp[500];
	struct sip_request *msg;

	if ((msg = initreqprep(NULL, p, SIP_NOTIFY))) {
		cw_dynstr_tprintf(&msg->pkt, 2,
			cw_fmtval("%s: message-summary\r\n", sip_hdr_name[SIP_NHDR_EVENT]),
			cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE], default_notifymime)
		);

		res = snprintf(tmp, sizeof(tmp), "Messages-Waiting: %s\r\n"
			"Message-Account: sip:%s@%s\r\n"
			"Voice-Message: %d/%d (0/0)\r\n",
			(newmsgs ? "yes" : "no"),
			(!cw_strlen_zero(vmexten) ? vmexten : global_vmexten),
			p->fromdomain,
			newmsgs, oldmsgs);

		add_header_contentLength(msg, res);
		cw_dynstr_printf(&msg->pkt, "\r\n%s", tmp);

		if (!p->initreq.hdr.used) {
			/* Use this as the basis */
			copy_and_parse_request(&p->initreq, msg);
		}

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}
#endif

/*! \brief  transmit_sip_request: Transmit SIP request */
static void transmit_sip_request(struct sip_pvt *p, struct sip_request *req)
{
	if (!p->initreq.pkt.used) {
		/* Use this as the basis */
		copy_and_parse_request(&p->initreq, req);
	}

	send_message(p, p->conn, &p->ouraddr, &p->peeraddr, req, 0);
}

/*! \brief  transmit_notify_with_sipfrag: Notify a transferring party of the status of transfer */
/*      Apparently the draft SIP REFER structure was too simple, so it was decided that the
 *      status of transfers also needed to be sent via NOTIFY instead of just the 202 Accepted
 *      that had worked heretofore.
 */
static void transmit_notify_with_sipfrag(struct sip_pvt *p, int cseq)
{
	struct sip_request *msg;

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_NOTIFY, 0, 1))) {
		cw_dynstr_tprintf(&msg->pkt, 3,
			cw_fmtval("%s: refer;id=%d\r\n", sip_hdr_name[SIP_NHDR_EVENT], cseq),
			cw_fmtval("Subscription-state: terminated;reason=noresource\r\n"),
			cw_fmtval("%s: message/sipfrag;version=2.0\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE])
		);

		add_header_contentLength(msg, sizeof("SIP/2.0 200 OK\r\n") - 1);
		cw_dynstr_printf(&msg->pkt, "\r\nSIP/2.0 200 OK\r\n");

		if (!p->initreq.pkt.used) {
			/* Use this as the basis */
			copy_and_parse_request(&p->initreq, msg);
		}

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

static const char *regstate2str(int regstate)
{
    switch (regstate)
    {
    case REG_STATE_FAILED:
        return "Failed";
    case REG_STATE_UNREGISTERED:
        return "Unregistered";
    case REG_STATE_REGSENT:
        return "Request Sent";
    case REG_STATE_AUTHSENT:
        return "Auth. Sent";
    case REG_STATE_REGISTERED:
        return "Registered";
    case REG_STATE_REJECTED:
        return "Rejected";
    case REG_STATE_TIMEOUT:
        return "Timeout";
    case REG_STATE_NOAUTH:
        return "No Authentication";
    default:
        return "Unknown";
    }
}

static int transmit_register(struct sip_registry *r, enum sipmethod sipmethod, const struct cw_dynstr *auth, const char *authheader);

/*! \brief  __sip_do_register: Register with SIP proxy */
static void *__sip_do_register(void *data)
{
    struct sip_registry *r = data;

    transmit_register(r, SIP_REGISTER, NULL, NULL);

    return NULL;
}

/*! \brief  sip_reregister: Update registration with SIP Proxy*/
static int sip_reregister(void *data) 
{
    /* if we are here, we know that we need to reregister. */
    struct sip_registry *r = data;
    pthread_t tid;

    if (sipdebug)
        cw_log(CW_LOG_NOTICE, "   -- Re-registration for  %s@%s\n", r->username, r->hostname);

    cw_pthread_create(&tid, &global_attr_detached, __sip_do_register, r);
    return 0;
}

/*! \brief  sip_reg_timeout: Registration timeout, register again */
static int sip_reg_timeout(void *data)
{
    /* if we are here, our registration timed out, so we'll just do it over */
    struct sip_registry *r = data;
    struct sip_pvt *p;

    /* Since we are now running we can't be unscheduled therefore
     * even if we get a response handle_response_register will do
     * nothing.
     */

    cw_log(CW_LOG_NOTICE, "   -- Registration for '%s@%s' timed out, trying again (Attempt #%d)\n", r->username, r->hostname, r->regattempts); 
    if (r->dialogue)
    {
        /* Unlink us, destroy old dialogue. */
        p = r->dialogue;
        cw_mutex_lock(&p->lock);
        if (p->registry)
            cw_object_put(p->registry);
        r->dialogue = NULL;
        sip_destroy(p);
        cw_mutex_unlock(&p->lock);
        cw_object_put(p);
    }

    if (r->regstate != REG_STATE_SHUTDOWN) {
        /* If we have a limit, stop registration and give up */
        if (global_regattempts_max && (r->regattempts > global_regattempts_max)) {
            /* Ok, enough is enough. Don't try any more */
            cw_log(CW_LOG_NOTICE, "   -- Giving up forever trying to register '%s@%s'\n", r->username, r->hostname);
            r->regstate = REG_STATE_FAILED;
        } else
            r->regstate=REG_STATE_UNREGISTERED;

        cw_manager_event(EVENT_FLAG_SYSTEM, "Registry",
		4,
		cw_msg_tuple("Channel",  "%s", "SIP"),
		cw_msg_tuple("Username", "%s", r->username),
		cw_msg_tuple("Domain",   "%s", r->hostname),
		cw_msg_tuple("Status",   "%s", regstate2str(r->regstate))
	);

	if (r->regstate != REG_STATE_FAILED) {
            r->timeout = -1;
	    /* transmit_regsiter inherits our reference and sets a new timeout */
            transmit_register(r, SIP_REGISTER, NULL, NULL);
        }
    } else
        cw_object_put(r);

    return 0;
}

/*! \brief  transmit_register: Transmit register to SIP proxy or UA */
static int transmit_register(struct sip_registry *r, enum sipmethod sipmethod, const struct cw_dynstr *auth, const char *authheader)
{
    struct sip_request *msg;
    struct sip_pvt *p;
    int has_at;
    int res = -1;

    /* exit if we are already in process with this registrar ?*/
    if ( r == NULL || ((auth==NULL) && (r->regstate==REG_STATE_REGSENT || r->regstate==REG_STATE_AUTHSENT)))
    {
        cw_log(CW_LOG_NOTICE, "Strange, trying to register %s@%s when registration already pending\n", r->username, r->hostname);
        return 0;
    }

    if (r->dialogue)
    {
        /* We have a registration */
        if (!auth)
        {
            cw_log(CW_LOG_WARNING, "Already have a REGISTER going on to %s@%s?? \n", r->username, r->hostname);
            return 0;
        }
        p = cw_object_dup(r->dialogue);
#if 0
	/* Forget their old tag, so we don't match tags when getting response.
	 * This is strictly incorrect since the tag should be constant throughout
	 * a dialogue (in this case register,unauth'd,reg-with-auth,ok-or-fail)
	 * but there are SIP implementations that have had this wrong in the past.
	 * DEPRECATED: This should be removed at some point...
	 * FIXME: if we actually need to do this we need to unregister and reregister p
	 */
        p->theirtag[0]='\0';
#endif
    }
    else
    {
        /* Allocate SIP packet for registration */
        p = sip_alloc();
        if (!p)
        {
            cw_log(CW_LOG_WARNING, "Unable to allocate registration call\n");
            return 0;
        }

        /* Find address to hostname */
        if (create_addr(p, r->hostname, NULL, 0))
        {
            /* we have what we hope is a temporary network error,
             * probably DNS.  We need to reschedule a registration try */
            r->regattempts++;
            if (r->timeout > -1)
            {
                if (!cw_sched_del(sched, r->timeout))
                    cw_object_put(r);
                cw_log(CW_LOG_WARNING, "Still have a registration timeout for %s@%s (create_addr() error), %d\n", r->username, r->hostname, r->timeout);
                r->timeout = cw_sched_add(sched, global_reg_timeout*1000, sip_reg_timeout, r);
            }
            else
            {
                cw_log(CW_LOG_WARNING, "Probably a DNS error for registration to %s@%s, trying REGISTER again (after %d seconds)\n", r->username, r->hostname, global_reg_timeout);
                r->timeout = cw_sched_add(sched, global_reg_timeout*1000, sip_reg_timeout, r);
            }
            sip_destroy(p);
            cw_object_put(p);
            return 0;
        }

        /* RFC3261: 10.2
	 * All registrations from a UAC SHOULD use the same Call-ID header field value
	 * for registrations sent to a particular registrar.
	 */
        if (r->callid_valid)
            cw_copy_string(p->callid, r->callid, sizeof(p->callid));
        else
        {
            cw_copy_string(r->callid, p->callid, sizeof(r->callid));
            r->callid_valid = 1;
        }

        if (r->portno) {
            cw_sockaddr_set_port(&p->peeraddr.sa, r->portno);
	} else {
            /* Set registry port to the port set from the peer definition/srv or default */
            r->portno = cw_sockaddr_get_port(&p->peeraddr.sa);
        }

        cw_set_flag(p, SIP_OUTGOING);    /* Registration is outgoing call */
        r->dialogue = cw_object_get(p);            /* Save pointer to SIP packet */
        p->registry = cw_object_dup(r);    /* Add pointer to registry in packet */
        if (!cw_strlen_zero(r->secret))    /* Secret (password) */
            cw_copy_string(p->peersecret, r->secret, sizeof(p->peersecret));
        if (!cw_strlen_zero(r->md5secret))
            cw_copy_string(p->peermd5secret, r->md5secret, sizeof(p->peermd5secret));
        /* User name in this realm  
        - if authuser is set, use that, otherwise use username */
        if (!cw_strlen_zero(r->authuser))
        {    
            cw_copy_string(p->peername, r->authuser, sizeof(p->peername));
            cw_copy_string(p->authname, r->authuser, sizeof(p->authname));
        }
        else
        {
            if (!cw_strlen_zero(r->username))
            {
                cw_copy_string(p->peername, r->username, sizeof(p->peername));
                cw_copy_string(p->authname, r->username, sizeof(p->authname));
                cw_copy_string(p->fromuser, r->username, sizeof(p->fromuser));
            }
        }
        if (!cw_strlen_zero(r->username))
            cw_copy_string(p->username, r->username, sizeof(p->username));
        /* Save extension in packet */
        cw_copy_string(p->exten, r->contact, sizeof(p->exten));

        build_contact(p);

        p->reg_entry = cw_registry_add(&dialogue_registry, dialogue_hash(p), &p->obj);
    }

    /* Fromdomain is what we are registering to, regardless of actual
       host name from SRV */
    if (!cw_strlen_zero(p->fromdomain)) {
	if (r->portno && r->portno != DEFAULT_SIP_PORT)
	    snprintf(p->uri, sizeof(p->uri), "sip:%s:%d", p->fromdomain, r->portno);
	else
	    snprintf(p->uri, sizeof(p->uri), "sip:%s", p->fromdomain);
    } else {
	if (r->portno && r->portno != DEFAULT_SIP_PORT)
	    snprintf(p->uri, sizeof(p->uri), "sip:%s:%d", r->hostname, r->portno);
	else
	    snprintf(p->uri, sizeof(p->uri), "sip:%s", r->hostname);
    }

    p->branch ^= cw_random();

    if ((msg = malloc(sizeof(*msg)))) {
        init_req(msg, sipmethod, p->uri);

	r->ocseq++;
        msg->seqno = p->ocseq = r->ocseq;

        has_at = (strchr(r->username, '@') != NULL);
        cw_dynstr_tprintf(&msg->pkt, 7,
            /* z9hG4bK is a magic cookie.  See RFC 3261 section 8.1.1.7 */
            /* Work around buggy UNIDEN UIP200 firmware by not asking for rport unnecessarily */
            cw_fmtval("%s: SIP/2.0/UDP %#l@;branch=z9hG4bK%08x%s\r\n",
                sip_hdr_name[SIP_NHDR_VIA], &p->stunaddr.sa, p->branch, ((cw_test_flag(p, SIP_NAT) & SIP_NAT_RFC3581) ? ";rport" : "")),
            cw_fmtval("%s: <sip:%s%s%s>;tag=%s\r\n",
                sip_hdr_name[SIP_NHDR_FROM], r->username, (has_at ? "" : "@"), (has_at ? "" : p->tohost), p->tag),
            cw_fmtval("%s: <sip:%s%s%s>%s%s\r\n",
                sip_hdr_name[SIP_NHDR_TO],
                r->username,
	        (has_at ? "" : "@"), (has_at ? "" : p->tohost),
	        (p->theirtag[0] ? ";tag=" : ""),
	        (p->theirtag[0] ? p->theirtag : "")),
            cw_fmtval("%s: %s\r\n",          sip_hdr_name[SIP_NHDR_CALL_ID], p->callid),
            cw_fmtval("CSeq: %u %s\r\n",     r->ocseq, sip_methods[sipmethod].text),
            cw_fmtval("User-Agent: %s\r\n", default_useragent),
            cw_fmtval("Max-Forwards: " DEFAULT_MAX_FORWARDS "\r\n")
        );

        if (auth) {
            msg->pkt.error |= auth->error;
            cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_generic(authheader), auth->data);
        }

        else if (!cw_strlen_zero(r->nonce))
        {
            struct cw_dynstr digest = CW_DYNSTR_INIT;

            /* We have auth data to reuse, build a digest header! */
            if (sipdebug)
                cw_log(CW_LOG_DEBUG, "   >>> Re-using Auth data for %s@%s\n", r->username, r->hostname);
            cw_copy_string(p->realm, r->realm, sizeof(p->realm));
            cw_copy_string(p->nonce, r->nonce, sizeof(p->nonce));
            cw_copy_string(p->domain, r->domain, sizeof(p->domain));
            cw_copy_string(p->opaque, r->opaque, sizeof(p->opaque));
            cw_copy_string(p->qop, r->qop, sizeof(p->qop));
            p->noncecount = r->noncecount++;

            if(!build_reply_digest(p, sipmethod, &digest)) {
                msg->pkt.error |= digest.error;
                cw_dynstr_printf(&msg->pkt, "Authorization: %s\r\n", digest.data);
	        cw_dynstr_free(&digest);
	    } else
                cw_log(CW_LOG_NOTICE, "No authorization available for authentication of registration to %s@%s\n", r->username, r->hostname);
        }

        cw_dynstr_tprintf(&msg->pkt, 3,
            cw_fmtval("Expires: %d\r\n", default_expiry),
            cw_fmtval("%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTACT], p->our_contact),
            cw_fmtval("%s: registration\r\n", sip_hdr_name[SIP_NHDR_EVENT])
        );
        add_header_contentLength(msg, 0);
	cw_dynstr_printf(&msg->pkt, "\r\n");
        copy_and_parse_request(&p->initreq, msg);

        r->regstate = (auth ? REG_STATE_AUTHSENT : REG_STATE_REGSENT);
        r->regattempts++;    /* Another attempt */

        if (option_debug > 3)
            cw_verbose("REGISTER attempt %d to %s@%s\n", r->regattempts, r->username, r->hostname);

	msg->free = 1;

	cw_set_flag(msg, FLAG_FATAL);

        res = send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 2);
    }

    /* set up a timeout */
    if (auth == NULL)
    {
        if (r->timeout == -1 || cw_sched_del(sched, r->timeout))
            cw_object_dup(r);
        cw_log(CW_LOG_DEBUG, "Scheduled a registration timeout for %s id  #%d \n", r->hostname, r->timeout);
        r->timeout = cw_sched_add(sched, global_reg_timeout * 1000, sip_reg_timeout, r);
    } else
        cw_object_put(r);

    cw_object_put(p);
    return res;
}

/*! \brief  transmit_message_with_text: Transmit text with SIP MESSAGE method */
static int transmit_message_with_text(struct sip_pvt *p, const char *mimetype, const char *disposition, const char *text)
{
	struct sip_request *msg;
	int res = -1;

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_MESSAGE, 0, 1))) {
		cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE], mimetype);
		if (disposition)
			cw_dynstr_printf(&msg->pkt, "Content-Disposition: %s\r\n", disposition);

		/* FIXME: we should convert \n's to \r\n's? */
		if (!text) text = "";
		add_header_contentLength(msg, strlen(text) + sizeof("\r\n") - 1);
		cw_dynstr_printf(&msg->pkt, "\r\n%s\r\n", text);

		res = send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}

	return res;
}

/*! \brief  transmit_refer: Transmit SIP REFER message */
static int transmit_refer(struct sip_pvt *p, const char *dest)
{
	char from[256];
	char referto[256];
	struct sip_request *msg;
	char *of, *c;
	int res = -1;

	if (cw_test_flag(p, SIP_OUTGOING))
		of = get_header(&p->initreq, SIP_HDR_TO);
	else
		of = get_header(&p->initreq, SIP_HDR_FROM);
	cw_copy_string(from, of, sizeof(from));
	of = get_in_brackets(from);
	cw_copy_string(p->from, of, sizeof(p->from));
	if (strncasecmp(of, "sip:", 4))
		cw_log(CW_LOG_NOTICE, "From address missing 'sip:', using it anyway\n");
	else
		of += 4;
	/* Get just the username part */
	if ((c = strchr(dest, '@')))
		c = NULL;
	else if ((c = strchr(of, '@'))) {
		*c = '\0';
		c++;
	}
	if (c)
		snprintf(referto, sizeof(referto), "<sip:%s@%s>", dest, c);
	else
		snprintf(referto, sizeof(referto), "<sip:%s>", dest);

	/* save in case we get 407 challenge */
	cw_copy_string(p->refer_to, referto, sizeof(p->refer_to));
	cw_copy_string(p->referred_by, p->our_contact, sizeof(p->referred_by));

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_REFER, 0, 1))) {
		cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_REFER_TO], referto);
		if (!cw_strlen_zero(p->our_contact))
			cw_dynstr_printf(&msg->pkt, "%s: %s\r\n", sip_hdr_name[SIP_NHDR_REFERRED_BY], p->our_contact);
		cw_dynstr_printf(&msg->pkt, "\r\n");

		res = send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}

	return res;
}

/*! \brief  transmit_info_with_digit: Send SIP INFO dtmf message, see Cisco documentation on cisco.com */
static void transmit_info_with_digit(struct sip_pvt *p, char digit, unsigned int duration)
{
	char tmp[256];
	struct sip_request *msg;
	int len;

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_INFO, 0, 1))) {
		len = snprintf(tmp, sizeof(tmp), "Signal=%c\r\nDuration=%u\r\n", digit, duration);
		cw_dynstr_tprintf(&msg->pkt, 4,
			cw_fmtval("%s: application/dtmf-relay\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE]),
			cw_fmtval("%s: %d\r\n", sip_hdr_name[SIP_NHDR_CONTENT_LENGTH], len),
			cw_fmtval("\r\n"),
			cw_fmtval("%s\r\n", tmp)
		);

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

/*! \brief  transmit_info_with_vidupdate: Send SIP INFO with video update request */
static void transmit_info_with_vidupdate(struct sip_pvt *p)
{
	static const char data[] =
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n"
		" <media_control>\r\n"
		"  <vc_primitive>\r\n"
		"   <to_encoder>\r\n"
		"    <picture_fast_update\r\n"
		"    </picture_fast_update>\r\n"
		"   </to_encoder>\r\n"
		"  </vc_primitive>\r\n"
		" </media_control>";
	struct sip_request *msg;

	if ((msg = reqprep(p, NULL, &p->initreq, SIP_INFO, 0, 1))) {
		cw_dynstr_tprintf(&msg->pkt, 4,
			cw_fmtval("%s: application/media_control+xml\r\n", sip_hdr_name[SIP_NHDR_CONTENT_TYPE]),
			cw_fmtval("%s: %d\r\n", sip_hdr_name[SIP_NHDR_CONTENT_LENGTH], sizeof(data) - 1),
			cw_fmtval("\r\n"),
			cw_fmtval("%s\r\n", data)
		);

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, 1);
	}
}

/*! \brief  transmit_request: transmit generic SIP request */
static void transmit_ack(struct sip_pvt *p, struct sip_request *req, int newbranch)
{
	struct sip_request msg;

	reqprep(p, &msg, req, SIP_ACK, req->seqno, newbranch);
	add_header_contentLength(&msg, 0);
	cw_dynstr_printf(&msg.pkt, "\r\n");

	send_message(p, p->conn, &p->ouraddr, &p->peeraddr, &msg, 0);
}

/*! \brief  transmit_request_with_auth: Transmit SIP request, auth added */
static void transmit_request_with_auth(struct sip_pvt *p, enum sipmethod sipmethod, int seqno, int reliable, int newbranch)
{
	struct sip_request tmpmsg, *msg;

	if ((msg = reqprep(p, (reliable ? NULL : &tmpmsg), &p->initreq, sipmethod, seqno, newbranch))) {
		if (*p->realm) {
			struct cw_dynstr digest = CW_DYNSTR_INIT;

			if (!build_reply_digest(p, sipmethod, &digest)) {
				msg->pkt.error |= digest.error;
				cw_dynstr_printf(&msg->pkt, "%sAuthorization: %s\r\n", (p->options && p->options->auth_type == WWW_AUTH ? "" : "Proxy-"), digest.data);
				cw_dynstr_free(&digest);
			} else
				cw_log(CW_LOG_WARNING, "No authentication available for call %s\n", p->callid);
		}

		/* If we are hanging up and know a cause for that, send it in clear text to make debugging easier. */
		if (sipmethod == SIP_BYE && p->owner && p->owner->hangupcause)
			cw_dynstr_printf(&msg->pkt, "X-CallWeaver-HangupCause: %s\r\n", cw_cause2str(p->owner->hangupcause));

		add_header_contentLength(msg, 0);
		cw_dynstr_printf(&msg->pkt, "\r\n");

		send_message(p, p->conn, &p->ouraddr, &p->peeraddr, msg, reliable);
	}
}

static void destroy_association(struct sip_peer *peer)
{
    if (!cw_test_flag((&global_flags_page2), SIP_PAGE2_IGNOREREGEXPIRE))
    {
        if (cw_test_flag(&(peer->flags_page2), SIP_PAGE2_RT_FROMCONTACT))
        {
            cw_update_realtime("sippeers", "name", peer->name, "fullcontact", "", "port", "", "username", "", 
			     "regserver", "", NULL);
        }
        else
        {
            cw_db_del("SIP/Registry", peer->name);
        }
    }
}

/*! \brief  expire_register: Expire registration of SIP peer */
static int expire_register(void *data)
{
	struct sip_peer *peer = data;

	peer->expire = -1;

	if (peer->pokeexpire != -1 && !cw_sched_del(sched, peer->pokeexpire)) {
		peer->pokeexpire = -1;
		cw_object_put(peer);
	}

	if (peer->reg_entry_byaddr) {
		cw_registry_del(&peerbyaddr_registry, peer->reg_entry_byaddr);
		peer->reg_entry_byaddr = NULL;
	}
	memset(&peer->addr, 0, sizeof(peer->addr));

	register_peer_exten(peer, 0);
	destroy_association(peer);
	cw_device_state_changed("SIP/%s", peer->name);

	cw_manager_event(EVENT_FLAG_SYSTEM, "PeerStatus",
		3,
		cw_msg_tuple("Peer",       "SIP/%s", peer->name),
		cw_msg_tuple("PeerStatus", "%s",     "Unregistered"),
		cw_msg_tuple("Cause",      "%s",     "Expired")
	);

	if (cw_test_flag(peer, SIP_SELFDESTRUCT) || cw_test_flag((&peer->flags_page2), SIP_PAGE2_RTAUTOCLEAR)) {
		if (!cw_test_flag(peer, SIP_ALREADYGONE))
			cw_registry_del(&peerbyname_registry, peer->reg_entry_byname);
	}

	cw_object_put(peer);
	return 0;
}

/*! \brief  reg_source_db: Get registration details from CallWeaver DB */
static void reg_source_db(struct sip_peer *peer, int sip_running)
{
	static const struct addrinfo hints = {
		.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICHOST | AI_NUMERICSERV,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
	};
	struct addrinfo *addrs;
	int err;
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	int expiry;
	char *scan, *addr, *expiry_str, *username, *contact;

	if (cw_test_flag(&(peer->flags_page2), SIP_PAGE2_RT_FROMCONTACT))
		return;

	if (!cw_db_get("SIP/Registry", peer->name, &ds) && !ds.error) {
		addr = scan = ds.data;
		if (scan[0] == '[')
			while (*scan && *(scan++) != ']');
		while (*scan && *scan != ':') scan++;
		if (scan[0] == ':') {
			scan++;
			while (*scan && *scan != ':') scan++;
			*(scan++) = '\0';
		}
		expiry_str = strsep(&scan, ":");
		username = strsep(&scan, ":");
		contact = scan;    /* Contact include sip: and has to be the last part of the database entry as long as we use : as a separator */

		if (!(err = cw_getaddrinfo(addr, "0", &hints, &addrs, NULL))) {
			memcpy(&peer->addr, addrs->ai_addr, addrs->ai_addrlen);
			freeaddrinfo(addrs);

			expiry = atoi(expiry_str);

			if (username)
				cw_copy_string(peer->username, username, sizeof(peer->username));
			if (contact)
				cw_copy_string(peer->fullcontact, contact, sizeof(peer->fullcontact));

			if (option_verbose > 3)
				cw_verbose(VERBOSE_PREFIX_3 "SIP Seeding peer from cwdb: '%s' at %s@%s (%#l@) for %d\n",
						peer->name, peer->username, addr, &peer->addr.sa, expiry);

			if (sip_running) {
				/* SIP is already up, so schedule a poke in the near future */
				if (peer->pokeexpire == -1)
					peer->pokeexpire = cw_sched_add(sched, cw_random() % 5000 + 1, sip_poke_peer, cw_object_dup(peer));
			}

			peer->expire = cw_sched_add(sched, (expiry + 10) * 1000, expire_register, cw_object_dup(peer));
			register_peer_exten(peer, 1);
		} else
			cw_log(CW_LOG_WARNING, "%s: %s\n", addr, gai_strerror(err));
	}

	cw_dynstr_free(&ds);
}

/*! \brief  parse_ok_contact: Parse contact header for 200 OK on INVITE */
static char *parse_ok_contact(struct sip_pvt *pvt, struct sip_request *req)
{
	char *uri;

	if ((uri = get_header(req, SIP_HDR_CONTACT)) && uri[0]) {
		uri = get_in_brackets(cw_strdupa(uri));

		/* Save full contact to call pvt for later bye or re-invite */
		cw_copy_string(pvt->fullcontact, uri, sizeof(pvt->fullcontact));

		/* Save URI for later ACKs, BYE or RE-invites */
		cw_copy_string(pvt->okcontacturi, uri, sizeof(pvt->okcontacturi));
	}

	return uri;
}


enum parse_register_result {
    PARSE_REGISTER_FAILED,
    PARSE_REGISTER_UPDATE,
    PARSE_REGISTER_QUERY,
};

/*! \brief  parse_register_contact: Parse contact header and save registration */
static enum parse_register_result parse_register_contact(struct sip_pvt *pvt, struct sip_peer *p, struct sip_request *req)
{
    char contact[256]; 
    char data[CW_MAX_ADDRSTRLEN + 1 + 10 + 1 + sizeof(p->username) + 1 + sizeof(p->fullcontact) + 1];
    struct cw_sockaddr_net oldsin;
    char *expires = get_header(req, SIP_HDR_NOSHORT("Expires"));
    int expiry = atoi(expires);
    char *c, *n;
    char *useragent;

    if (cw_strlen_zero(expires))
    {
        /* No expires header */
        expires = strcasestr(get_header(req, SIP_HDR_CONTACT), ";expires=");
        if (expires)
        {
            char *ptr;
        
            if ((ptr = strchr(expires, ';')))
                *ptr = '\0';
            if (sscanf(expires + 9, "%d", &expiry) != 1)
                expiry = default_expiry;
        }
        else
        {
            /* Nothing has been specified */
            expiry = default_expiry;
        }
    }
    /* Look for brackets */
    cw_copy_string(contact, get_header(req, SIP_HDR_CONTACT), sizeof(contact));
    if (strchr(contact, '<') == NULL)
    {
        /* No <, check for ; and strip it */
        char *ptr = strchr(contact, ';');    /* This is Header options, not URI options */
    
        if (ptr)
            *ptr = '\0';
    }
    c = get_in_brackets(contact);

    /* if they did not specify Contact: or Expires:, they are querying
       what we currently have stored as their contact address, so return
       it
    */
    if (cw_strlen_zero(c) && cw_strlen_zero(expires))
    {
        if ((p->expire > -1) && !cw_strlen_zero(p->fullcontact))
        {
            /* tell them when the registration is going to expire */
            pvt->expiry = cw_sched_when(sched, p->expire);
        }
        return PARSE_REGISTER_QUERY;
    }

    if (!strcasecmp(c, "*") || !expiry)
    {
        /* Unregister this peer */
        /* This means remove all registrations and return OK */
        if (p->expire != -1 && !cw_sched_del(sched, p->expire))
            expire_register(p);

        return PARSE_REGISTER_UPDATE;
    }
    cw_copy_string(p->fullcontact, c, sizeof(p->fullcontact));
    /* For the 200 OK, we should use the received contact */
    snprintf(pvt->our_contact, sizeof(pvt->our_contact) - 1, "<%s>", c);
    /* Make sure it's a SIP URL */
    if (strncasecmp(c, "sip:", 4))
        cw_log(CW_LOG_NOTICE, "'%s' is not a valid SIP contact (missing sip:) trying to use anyway\n", c);
    else
        c += 4;
    /* Ditch q */
    n = strchr(c, ';');
    if (n)
        *n = '\0';
    /* Grab host */
    n = strchr(c, '@');
    if (!n)
    {
        n = c;
        c = NULL;
    }
    else
    {
        *n = '\0';
        n++;
    }

    oldsin = p->addr;

    if (!sip_is_nat_needed(pvt) )
    {
        static const struct addrinfo hints = {
            .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_IDN,
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
	};
	struct addrinfo *addrs;
	int err;

	if (!(err = cw_getaddrinfo(n, DEFAULT_SIP_PORT_STR, &hints, &addrs, NULL))) {
            memcpy(&p->addr, addrs->ai_addr, addrs->ai_addrlen);
            freeaddrinfo(addrs);
        } else {
            cw_log(CW_LOG_WARNING, "%s: %s\n", c, gai_strerror(err));
            return PARSE_REGISTER_FAILED;
        }
    }
    else
    {
        /* Don't trust the contact field.  Just use what they came to us with */
        /* FIXME: why? aren't we just working around a host that should have
	 * used STUN to figure out an externally visible address? How long will
	 * this be valid for?
	 */
        p->addr = req->recvdaddr;
    }

    if (c)    /* Overwrite the default username from config at registration */
        cw_copy_string(p->username, c, sizeof(p->username));
    else
        p->username[0] = '\0';

    p->timer_t1 = pvt->timer_t1;

    /* Save SIP options profile */
    p->sipoptions = pvt->sipoptions;

    /* Save User agent */
    useragent = get_header(req, SIP_HDR_NOSHORT("User-Agent"));
    if (useragent && useragent[0] && strcasecmp(useragent, p->useragent))
    {
        cw_copy_string(p->useragent, useragent, sizeof(p->useragent));
        if (option_verbose > 4)
            cw_verbose(VERBOSE_PREFIX_3 "Saved useragent \"%s\" for peer %s\n",p->useragent,p->name);
    }

    cw_snprintf(data, sizeof(data), "%#@:%#h@:%d:%s:%s", &p->addr.sa, &p->addr.sa, expiry, p->username, p->fullcontact);
    if (!cw_test_flag((&p->flags_page2), SIP_PAGE2_RT_FROMCONTACT) && !cw_test_flag(&(p->flags_page2), SIP_PAGE2_RTCACHEFRIENDS))
        cw_db_put("SIP/Registry", p->name, data);

    cw_manager_event(EVENT_FLAG_SYSTEM, "PeerStatus",
        2,
        cw_msg_tuple("Peer",       "SIP/%s", p->name),
        cw_msg_tuple("PeerStatus", "%s",     "Registered")
    );

    if (cw_sockaddr_cmp(&p->addr.sa, &oldsin.sa, -1, 1))
    {
        if (p->reg_entry_byaddr)
            cw_registry_del(&peerbyaddr_registry, p->reg_entry_byaddr);

        p->reg_entry_byaddr = cw_registry_add(&peerbyaddr_registry, cw_sockaddr_hash(&p->addr.sa, 0), &p->obj);

        if (option_verbose > 3)
            cw_verbose(VERBOSE_PREFIX_3 "Registered SIP '%s' at %#l@ expires %d\n", p->name, &p->addr.sa, expiry);
    }

    if (p->pokeexpire == -1)
        p->pokeexpire = cw_sched_add(sched, 1, sip_poke_peer, cw_object_dup(p));

    register_peer_exten(p, 1);

    if (expiry < 1 || expiry > max_expiry)
        expiry = max_expiry;

    pvt->expiry = expiry;

    if (p->expire != -1 && !cw_sched_del(sched, p->expire))
    {
        p->expire = -1;
        cw_object_put(p);
    }

    if (p->expire == -1 && !cw_test_flag(p, SIP_REALTIME))
        p->expire = cw_sched_add(sched, (expiry + 10) * 1000, expire_register, cw_object_dup(p));

    return PARSE_REGISTER_UPDATE;
}

/*! \brief  free_old_route: Remove route from route list */
static void free_old_route(struct sip_route *route)
{
    struct sip_route *next;
    while (route)
    {
        next = route->next;
        free(route);
        route = next;
    }
}

/*! \brief  list_route: List all routes - mostly for debugging */
static void list_route(struct sip_route *route)
{
    if (!route)
    {
        cw_verbose("list_route: no route\n");
        return;
    }
    while (route)
    {
        cw_verbose("list_route: hop: <%s>\n", route->hop);
        route = route->next;
    }
}

/*! \brief  build_route: Build route list from Record-Route header */
static void build_route(struct sip_pvt *p, struct sip_request *req, int backwards)
{
    struct sip_route *thishop, *head, *tail;
    int start = req->hdr_start;
    int len;
    char *rr, *contact, *c;

    /* Once a persistant route is set, don't fool with it */
    if (p->route && p->route_persistant)
    {
        cw_log(CW_LOG_DEBUG, "build_route: Retaining previous route: <%s>\n", p->route->hop);
        return;
    }

    if (p->route)
    {
        free_old_route(p->route);
        p->route = NULL;
    }
    
    p->route_persistant = backwards;
    
    /* We build up head, then assign it to p->route when we're done */
    head = NULL;  tail = head;
    /* 1st we pass through all the hops in any Record-Route headers */
    for (;;)
    {
        /* Each Record-Route header */
        rr = __get_header(req, SIP_HDR_NOSHORT("Record-Route"), &start);
        if (*rr == '\0')
            break;
        for (;;)
        {
            /* Each route entry */
            /* Find < */
            rr = strchr(rr, '<');
            if (!rr)
                break; /* No more hops */
            ++rr;
            len = strcspn(rr, ">") + 1;
            /* Make a struct route */
            thishop = malloc(sizeof(struct sip_route) + len);
            if (thishop)
            {
                cw_copy_string(thishop->hop, rr, len);
                cw_log(CW_LOG_DEBUG, "build_route: Record-Route hop: <%s>\n", thishop->hop);
                /* Link in */
                if (backwards)
                {
                    /* Link in at head so they end up in reverse order */
                    thishop->next = head;
                    head = thishop;
                    /* If this was the first then it'll be the tail */
                    if (!tail) tail = thishop;
                }
                else
                {
                    thishop->next = NULL;
                    /* Link in at the end */
                    if (tail)
                        tail->next = thishop;
                    else
                        head = thishop;
                    tail = thishop;
                }
            }
            rr += len;
        }
    }

    /* Only append the contact if we are dealing with a strict router */
    if (!head  ||  (!cw_strlen_zero(head->hop) && strstr(head->hop,";lr") == NULL))
    {
        /* 2nd append the Contact: if there is one */
        /* Can be multiple Contact headers, comma separated values - we just take the first */
        contact = get_header(req, SIP_HDR_CONTACT);
        if (!cw_strlen_zero(contact))
        {
            cw_log(CW_LOG_DEBUG, "build_route: Contact hop: %s\n", contact);
            /* Look for <: delimited address */
            c = strchr(contact, '<');
            if (c)
            {
                /* Take to > */
                ++c;
                len = strcspn(c, ">") + 1;
            }
            else
            {
                /* No <> - just take the lot */
                c = contact;
                len = strlen(contact) + 1;
            }
            thishop = malloc(sizeof(struct sip_route) + len);
            if (thishop)
            {
                cw_copy_string(thishop->hop, c, len);
                thishop->next = NULL;
                /* Goes at the end */
                if (tail)
                    tail->next = thishop;
                else
                    head = thishop;
            }
        }
    }

    /* Store as new route */
    p->route = head;

    /* For debugging dump what we ended up with */
    if (sip_debug_test_pvt(p))
        list_route(p->route);
}

#ifdef OSP_SUPPORT
/*! \brief  check_osptoken: Validate OSP token for user authorization */
static int check_osptoken (struct sip_pvt *p, char *token)
{
    char tmp[80];

    /* FIXME: under what circumstances would we not have a peer addr? */
    if (p->peeraddr.sa_family != AF_INET
    || cw_osp_validate(NULL, token, &p->osphandle, &p->osptimelimit, p->cid_num, &p->peeraddr.sin.sin_addr, p->exten) < 1)
        return (-1);
    snprintf (tmp, sizeof (tmp), "%d", p->osphandle);
    pbx_builtin_setvar_helper (p->owner, "_OSPHANDLE", tmp);
    return (0);
}
#endif

/*! \brief  check_auth: Check user authorization from peer definition */
/*      Some actions, like REGISTER and INVITEs from peers require
        authentication (if peer have secret set) */
static int check_auth(struct sip_pvt *p, struct sip_request *req, char *randdata, int randlen, char *username, char *secret, char *md5secret, enum sipmethod sipmethod, char *uri, int reliable, int ignore)
{
    int res = -1;
    const char *response = "407 Proxy Authentication Required";
    const char *reqheader = "Proxy-Authorization";
    const char *respheader = "Proxy-Authenticate";
    const char *authtoken;
#ifdef OSP_SUPPORT
    const char *osptoken;
#endif
    /* Always OK if no secret */
    if (cw_strlen_zero(secret) && cw_strlen_zero(md5secret)
#ifdef OSP_SUPPORT
        && !cw_test_flag(p, SIP_OSPAUTH)
        && global_allowguest != 2
#endif
        )
        return 0;
    if (sipmethod == SIP_REGISTER || sipmethod == SIP_SUBSCRIBE || sipmethod == SIP_INVITE)
    {
        /* On a REGISTER, we have to use 401 and its family of headers instead of 407 and its family
           of headers -- GO SIP!  Whoo hoo!  Two things that do the same thing but are used in
           different circumstances! What a surprise. */
        response = "401 Unauthorized";
        reqheader = "Authorization";
        respheader = "WWW-Authenticate";
    }
#ifdef OSP_SUPPORT
    else {
        cw_log(CW_LOG_DEBUG, "Checking OSP Authentication!\n");
        osptoken = get_header (req, SIP_HDR_NOSHORT("P-OSP-Auth-Token"));
        switch (cw_test_flag (p, SIP_OSPAUTH))
        {
            case SIP_OSPAUTH_NO:
                break;
            case SIP_OSPAUTH_GATEWAY:
                if (cw_strlen_zero (osptoken))
                {
                    if (cw_strlen_zero (secret) && cw_strlen_zero (md5secret))
                    {
                        return (0);
                    }
                }
                else
                {
                    return (check_osptoken (p, osptoken));
                }
                break;
            case SIP_OSPAUTH_PROXY:
                if (cw_strlen_zero (osptoken))
                {
                    return (0);
                } 
                else {
                    return (check_osptoken (p, osptoken));
                }
                break;
            case SIP_OSPAUTH_EXCLUSIVE:
                if (cw_strlen_zero (osptoken))
                {
                    return (-1);
                }
                else
                {
                    return (check_osptoken (p, osptoken));
                }
                break;
            default:
                return (-1);
        }
     }
#endif    
    authtoken =  get_header(req, SIP_HDR_VNOSHORT(reqheader));
    if (ignore && !cw_strlen_zero(randdata) && cw_strlen_zero(authtoken))
    {
        /* This is a retransmitted invite/register/etc, don't reconstruct authentication
           information */
        if (!cw_strlen_zero(randdata))
        {
            if (!reliable)
            {
                /* Resend message if this was NOT a reliable delivery.   Otherwise the
                   retransmission should get it */
                transmit_response_with_auth(p, response, req, randdata, reliable, respheader, 0);
                /* Schedule auto destroy in 15 seconds */
                sip_scheddestroy(p, -1);
            }
            res = 1;
        }
    }
    else if (cw_strlen_zero(randdata) || cw_strlen_zero(authtoken))
    {
        snprintf(randdata, randlen, "%08lx", cw_random());
        transmit_response_with_auth(p, response, req, randdata, reliable, respheader, 0);
        /* Schedule auto destroy in 15 seconds */
        sip_scheddestroy(p, -1);
        res = 1;
    }
    else
    {
        char buf[256] = "";
        char a1_hash[2 * CW_MAX_BINARY_MD_SIZE + 1];
        char a2_hash[2 * CW_MAX_BINARY_MD_SIZE + 1];
        const char *ua_hash = NULL;
        int ua_hash_len = 0;
        const char *usednonce = randdata;
        int usednonce_len = randlen;

	while (*authtoken && *authtoken == ' ') authtoken++;

	if (!strncasecmp(authtoken, "digest", sizeof("digest") - 1))
		authtoken += sizeof("digest") - 1;

        while (*authtoken)
        {
            const char *key, *val;
            int lkey, lval;

	    while (*authtoken && (*authtoken == ' ' || *authtoken == ',')) authtoken++;

            key = authtoken;
            for (lkey = 0; *authtoken && *authtoken != '='; lkey++,authtoken++);

            if (*authtoken && *(++authtoken) == '"')
	    {
	        val = ++authtoken;
                for (lval = 0; *authtoken && *authtoken != '"'; lval++,authtoken++);
                if (*authtoken) authtoken++;
	    }
	    else
	    {
                val = authtoken;
                for (lval = 0; *authtoken && *authtoken != ','; lval++,authtoken++);
                while (*authtoken == ' ') lval--,authtoken--;
	    }

	    switch (lkey)
	    {
	        case sizeof("uri")-1:
	            if (!strncasecmp(key, "uri", sizeof("uri")-1))
                        snprintf(buf, sizeof(buf), "%s:%.*s", sip_methods[sipmethod].text, lval, val);
	            break;

	        case sizeof("nonce")-1:
	            if (!strncasecmp(key, "nonce", sizeof("nonce")-1))
		    {
                        usednonce = val;
                        usednonce_len = lval;
		    }
	            break;

	        case sizeof("response")-1:
	        /* case sizeof("username")-1: */
	            if (!strncasecmp(key, "response", sizeof("response")-1))
		    {
	                ua_hash = val;
	                ua_hash_len = lval;
		    }
                    else if (!strncasecmp(key, "username", sizeof("username")-1))
		    {
                        /* Verify that digest username matches  the username we auth as */
                        if (strlen(username) != lval || strncmp(username, val, lval))
                            return -2;
		    }
	            break;
	    }
        }

        if (!*buf)
            snprintf(buf, sizeof(buf), "%s:%s", sip_methods[sipmethod].text, uri);
        cw_md5_hash(a2_hash, buf);

        if (!cw_strlen_zero(md5secret))
            snprintf(a1_hash, sizeof(a1_hash), "%s", md5secret);
        else
        {
            snprintf(buf, sizeof(buf), "%s:%s:%s", username, global_realm, secret);
            cw_md5_hash(a1_hash, buf);
        }

        snprintf(buf, sizeof(buf), "%s:%.*s:%s", a1_hash, usednonce_len, usednonce, a2_hash);
        cw_md5_hash(a1_hash, buf);

        /* Verify nonce from request matches our nonce.  If not, send 401 with new nonce */
        if (strlen(randdata) != usednonce_len || strncasecmp(randdata, usednonce, usednonce_len))
        {
            snprintf(randdata, randlen, "%08lx", cw_random());
            if (ua_hash && strlen(a1_hash) == ua_hash_len && !strncasecmp(ua_hash, a1_hash, ua_hash_len))
            {
                if (sipdebug)
                    cw_log(CW_LOG_NOTICE, "stale nonce received from '%s'\n", get_header(req, SIP_HDR_TO));
                /* We got working auth token, based on stale nonce . */
                transmit_response_with_auth(p, response, req, randdata, reliable, respheader, 1);
            }
            else
            {
                /* Everything was wrong, so give the device one more try with a new challenge */
                if (sipdebug)
                    cw_log(CW_LOG_NOTICE, "Bad authentication received from '%s'\n", get_header(req, SIP_HDR_TO));
                transmit_response_with_auth(p, response, req, randdata, reliable, respheader, 0);
            }

            sip_scheddestroy(p, -1);
            return 1;
        } 
        /* a1_hash now has the expected response, compare the two */
        if (ua_hash && strlen(a1_hash) == ua_hash_len && !strncasecmp(ua_hash, a1_hash, ua_hash_len))
        {
            /* Auth is OK */
            res = 0;
        }
    }
    /* Failure */
    return res;
}

/*! \brief  cb_extensionstate: Callback for the devicestate notification (SUBSCRIBE) support subsystem */
/*    If you add an "hint" priority to the extension in the dial plan,
      you will get notifications on device state changes */
static int cb_extensionstate(char *context, char* exten, int state, void *data)
{
    struct sip_pvt *p = data;

    CW_UNUSED(context);

    switch (state)
    {
    case CW_EXTENSION_DEACTIVATED:    /* Retry after a while */
    case CW_EXTENSION_REMOVED:    /* Extension is gone */
        if (p->autokillid > -1)
            sip_cancel_destroy(p);    /* Remove subscription expiry for renewals */
        sip_scheddestroy(p, -1);
        cw_verbose(VERBOSE_PREFIX_2 "Extension state: Watcher for hint %s %s. Notify User %s\n", exten, state == CW_EXTENSION_DEACTIVATED ? "deactivated" : "removed", p->username);
        p->stateid = -1;
        p->subscribed = NONE;
        break;
    default:    /* Tell user */
        p->laststate = state;
        break;
    }
    if (p->subscribed != NONE)	/* Only send state NOTIFY if we know the format */
    	transmit_state_notify(p, state, 1, 1, 0);

    if (option_verbose > 1)
        cw_verbose(VERBOSE_PREFIX_1 "Extension Changed %s new state %s for Notify User %s\n", exten, cw_extension_state2str(state), p->username);
    return 0;
}

/*! \brief Send a fake 401 Unauthorized response when the administrator
  wants to hide the names of local users/peers from fishers
*/
static void transmit_fake_auth_response(struct sip_pvt *p, struct sip_request *req, char *randdata, int randlen, int reliable)
{
	snprintf(randdata, randlen, "%08lx", cw_random());
	transmit_response_with_auth(p, "401 Unauthorized", req, randdata, reliable, "WWW-Authenticate", 0);
}


/*! \brief  register_verify: Verify registration of user */
static int register_verify(struct sip_pvt *p, struct sip_request *req, char *uri, int ignore)
{
    int res = -3;
    struct sip_peer *peer;
    char tmp[256];
    char *name, *c;
    char *t;
    char *domain;

    if (uri == NULL) {
        cw_log(CW_LOG_WARNING, "register_verify: URI is NULL!\n");
        transmit_response_with_date(p, "503 Bad Request", req);
        return -3;
    }

    /* Terminate URI */
    t = uri;
    while(*t && !isspace(*t) && (*t != ';'))
        t++;
    *t = '\0';
    
    cw_copy_string(tmp, get_header(req, SIP_HDR_TO), sizeof(tmp));
    if (pedanticsipchecking)
        cw_uri_decode(tmp);

    c = get_in_brackets(tmp);
    /* Ditch ;user=phone */
    name = strchr(c, ';');
    if (name)
        *name = '\0';

    if (!strncasecmp(c, "sip:", 4))
        name = c + 4;
    else
    {
        name = c;
        cw_log(CW_LOG_NOTICE, "Invalid to address: '%s' (missing \"sip:\") from %#l@ (peer %#l@) trying to use anyway...\n", c, &req->recvdaddr.sa, &p->peeraddr.sa);
    }

    /* Strip off the domain name */
    if ((c = strchr(name, '@')))
    {
        *c++ = '\0';
        domain = c;
        if ((c = strchr(domain, ':')))    /* Remove :port */
            *c = '\0';
        if (!CW_LIST_EMPTY(&domain_list))
        {
            if (!check_sip_domain(domain, NULL, 0))
            {
                transmit_response(p, "404 Not found (unknown domain)", &p->initreq, 0);
                return -3;
            }
        }
    }

    cw_copy_string(p->exten, name, sizeof(p->exten));
    build_contact(p);

    if ((peer = find_peer(name, NULL, 1)) && !cw_acl_check(peer->acl, &req->recvdaddr.sa, 1))
    {
        cw_object_put(peer);
	peer = NULL;
    }

    if (peer)
    {
        if (!cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC))
        {
            cw_log(CW_LOG_ERROR, "Peer '%s' is trying to register, but not configured as host=dynamic\n", peer->name);
        }
        else
        {
            cw_copy_flags(p, peer, SIP_NAT);
            transmit_response(p, "100 Trying", req, 0);
            if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), peer->name, peer->secret, peer->md5secret, SIP_REGISTER, uri, 0, ignore)))
            {
                sip_cancel_destroy(p);
                switch (parse_register_contact(p, peer, req))
                {
                case PARSE_REGISTER_FAILED:
                    cw_log(CW_LOG_WARNING, "Failed to parse contact info\n");
                    transmit_response_with_date(p, "400 Bad Request", req);
		    peer->lastmsgssent = -1;
		    res = 0;
                    break;
                case PARSE_REGISTER_QUERY:
                    transmit_response_with_date(p, "200 OK", req);
                    peer->lastmsgssent = -1;
                    res = 0;
                    break;
                case PARSE_REGISTER_UPDATE:
                    update_peer(peer, p->expiry);
                    /* Say OK and ask subsystem to retransmit msg counter */
                    transmit_response_with_date(p, "200 OK", req);
                    peer->lastmsgssent = -1;
                    res = 0;
                    break;
                }
            } 
        }
    }
    if (!peer  &&  autocreatepeer)
    {
        /* Create peer if we have autocreate mode enabled */
        peer = temp_peer(name);
        if (peer)
        {
            peer->reg_entry_byname = cw_registry_add(&peerbyname_registry, cw_hash_string(0, peer->name), &peer->obj);
            sip_cancel_destroy(p);
            switch (parse_register_contact(p, peer, req))
            {
            case PARSE_REGISTER_FAILED:
                cw_log(CW_LOG_WARNING, "Failed to parse contact info\n");
                peer->lastmsgssent = -1;
                res = 0;
                break;
            case PARSE_REGISTER_QUERY:
                transmit_response_with_date(p, "200 OK", req);
                peer->lastmsgssent = -1;
                res = 0;
                break;
            case PARSE_REGISTER_UPDATE:
                /* Say OK and ask subsystem to retransmit msg counter */
                transmit_response_with_date(p, "200 OK", req);
                cw_manager_event(EVENT_FLAG_SYSTEM, "PeerStatus",
                        2,
                        cw_msg_tuple("Peer",       "SIP/%s", peer->name),
                        cw_msg_tuple("PeerStatus", "%s",     "Registered")
                );
                peer->lastmsgssent = -1;
                res = 0;
                break;
            }
        }
    }
    if (!res)
    {
        cw_device_state_changed("SIP/%s", peer->name);
    }
    if (res < 0)
    {
        switch (res)
        {
        case -1:
            /* Wrong password in authentication. Go away, don't try again until you fixed it */
            transmit_response(p, "403 Forbidden (Bad auth)", &p->initreq, 0);
            break;
        case -2:
            /* Username and digest username does not match. 
               CallWeaver uses the From: username for authentication. We need the
               users to use the same authentication user name until we support
               proper authentication by digest auth name */
            transmit_response(p, "403 Authentication user name does not match account name", &p->initreq, 0);
            break;
        case -3:
	    if (global_alwaysauthreject) {
		transmit_fake_auth_response(p, &p->initreq, p->randdata, sizeof(p->randdata), 1);
	    } else {
		/* URI not found */
		transmit_response(p, "404 Not found", &p->initreq, 0);
	    }
            res = -2;
            break;
        }
        if (option_debug > 1)
        {
            cw_log(CW_LOG_DEBUG, "SIP REGISTER attempt failed for %s : %s\n",
                peer->name,
                (res == -1) ? "Bad password" : ((res == -2 ) ? "Bad digest user" : "Peer not found"));
        }
    }
    if (peer)
        cw_object_put(peer);

    return res;
}

/*! \brief  get_rdnis: get referring dnis */
static int get_rdnis(struct sip_pvt *p, struct sip_request *oreq)
{
    char tmp[256], *c, *a;
    struct sip_request *req;
    
    req = oreq;
    if (!req)
        req = &p->initreq;
    cw_copy_string(tmp, get_header(req, SIP_HDR_NOSHORT("Diversion")), sizeof(tmp));
    if (cw_strlen_zero(tmp))
        return 0;
    c = get_in_brackets(tmp);
    if (strncasecmp(c, "sip:", 4))
    {
        cw_log(CW_LOG_WARNING, "Huh?  Not an RDNIS SIP header (%s)?\n", c);
        return -1;
    }
    c += 4;
    if ((a = strchr(c, '@')) || (a = strchr(c, ';')))
    {
        *a = '\0';
    }
    if (sip_debug_test_pvt(p))
        cw_verbose("RDNIS is %s\n", c);
    cw_copy_string(p->rdnis, c, sizeof(p->rdnis));

    return 0;
}

/*! \brief  get_destination: Find out who the call is for */
static int get_destination(struct sip_pvt *p, struct sip_request *oreq)
{
    char tmp[256] = "", tmpf[256];
    const char *user;
    char *uri, *a, *from, *domain, *opts;
    struct sip_request *req;
    char *colon;
    
    req = oreq;
    if (!req)
        req = &p->initreq;
    if (req->uriresp)
        cw_copy_string(tmp, req->pkt.data + req->uriresp, sizeof(tmp));
    uri = get_in_brackets(tmp);
    
    cw_copy_string(tmpf, get_header(req, SIP_HDR_FROM), sizeof(tmpf));

    from = get_in_brackets(tmpf);
    
    if (strncasecmp(uri, "sip:", 4))
    {
        cw_log(CW_LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", uri);
        return -1;
    }
    uri += 4;
    if (!cw_strlen_zero(from))
    {
        if (strncasecmp(from, "sip:", 4))
        {
            cw_log(CW_LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", from);
            return -1;
        }
        from += 4;
    }
    else
        from = NULL;

    if (pedanticsipchecking)
    {
        cw_uri_decode(uri);
        cw_uri_decode(from);
    }

    /* Get the target domain first and user */
    if ((domain = strchr(uri, '@'))) {
	*domain++ = '\0';
	user = uri;
    } else {
	/* No user portion present */
	domain = uri;
	user = "s";
    }

    /* Strip port from domain if present */
    if ((colon = strchr(domain, ':'))) {
	*colon = '\0';
    }

    /* Strip any params or options from user */
    if ((opts = strchr(user, ';'))) {
	*opts = '\0';
    }

    cw_copy_string(p->domain, domain, sizeof(p->domain));

    if (!CW_LIST_EMPTY(&domain_list))
    {
        char domain_context[CW_MAX_EXTENSION];

        domain_context[0] = '\0';
        if (!check_sip_domain(p->domain, domain_context, sizeof(domain_context)))
        {
            if (!allow_external_domains && (req->method == SIP_INVITE || req->method == SIP_REFER))
            {
                cw_log(CW_LOG_DEBUG, "Got SIP %s to non-local domain '%s'; refusing request.\n", sip_methods[req->method].text, p->domain);
                return -2;
            }
        }
        /* If we have a context defined, overwrite the original context */
        if (!cw_strlen_zero(domain_context))
            cw_copy_string(p->context, domain_context, sizeof(p->context));
    }

    if (from)
    {
        if ((a = strchr(from, ';')))
            *a = '\0';
        if ((a = strchr(from, '@')))
        {
            *a = '\0';
            cw_copy_string(p->fromdomain, a + 1, sizeof(p->fromdomain));
        }
        else
            cw_copy_string(p->fromdomain, from, sizeof(p->fromdomain));
    }
    if (sip_debug_test_pvt(p))
        cw_verbose("Looking for %s in %s (domain %s)\n", uri, p->context, p->domain);

    /* Return 0 if we have a matching extension */
    if (cw_exists_extension(NULL, p->context, uri, 1, from)
        ||
        !strcmp(uri, cw_pickup_ext()))
    {
        if (!oreq)
            cw_copy_string(p->exten, uri, sizeof(p->exten));
        return 0;
    }

    /* Return 1 for overlap dialling support */
    if (cw_canmatch_extension(NULL, p->context, uri, 1, from)
        ||
        !strncmp(uri, cw_pickup_ext(),strlen(uri)))
    {
        return 1;
    }
    
    return -1;
}

/*! \brief  get_sip_pvt_byid_locked: Lock interface lock and find matching pvt lock  */
static struct sip_pvt *get_sip_pvt_byid_locked(struct dialogue_key *key)
{
	struct cw_object *obj;
	struct sip_pvt *dialogue = NULL;
	char *p;
	unsigned int hash;

	hash = 0;
	for (p = key->callid; *p; p++)
		hash = cw_hash_add(hash, *p);
	for (p = key->tag; *p; p++)
		hash = cw_hash_add(hash, *p);

	while ((obj = cw_registry_find(&dialogue_registry, 1, hash, key))) {
		dialogue = container_of(obj, struct sip_pvt, obj);

		cw_mutex_lock(&dialogue->lock);

		if (!dialogue->owner || !cw_channel_trylock(dialogue->owner))
			break;

		cw_mutex_unlock(&dialogue->lock);
		cw_object_put(dialogue);
		usleep(1);
	}

	return (obj ? dialogue : NULL);
}

/*! \brief  get_refer_info: Call transfer support (the REFER method) */
static int get_refer_info(struct sip_pvt *sip_pvt, struct sip_request *outgoing_req, const char *transfercontext)
{
    char *p_refer_to = NULL, *p_referred_by = NULL, *h_refer_to = NULL, *h_referred_by = NULL, *h_contact = NULL;
    char *refer_to = NULL, *referred_by = NULL, *ptr = NULL;
    struct sip_request *req = NULL;
    struct sip_pvt *sip_pvt_ptr = NULL;
    struct cw_channel *chan = NULL, *peer = NULL;
    struct dialogue_key replace_key;

    req = outgoing_req;

    if (!req)
    {
        req = &sip_pvt->initreq;
    }
    
    if (!((p_refer_to = get_header(req, SIP_HDR_REFER_TO)) && (h_refer_to = cw_strdupa(p_refer_to))))
    {
        cw_log(CW_LOG_WARNING, "No Refer-To Header That's illegal\n");
        return -1;
    }

    refer_to = get_in_brackets(h_refer_to);

    if (!((p_referred_by = get_header(req, SIP_HDR_REFERRED_BY)) && (h_referred_by = cw_strdupa(p_referred_by))))
    {
        cw_log(CW_LOG_WARNING, "No Referred-By Header That's not illegal\n");
        return -1;
    }
    else
    {
        if (pedanticsipchecking)
        {
            cw_uri_decode(h_referred_by);
        }
        referred_by = get_in_brackets(h_referred_by);
    }
    h_contact = get_header(req, SIP_HDR_CONTACT);
    
    if (strncasecmp(refer_to, "sip:", 4))
    {
        cw_log(CW_LOG_WARNING, "Refer-to: Huh?  Not a SIP header (%s)?\n", refer_to);
        return -1;
    }

    if (strncasecmp(referred_by, "sip:", 4))
    {
        cw_log(CW_LOG_WARNING, "Referred-by: Huh?  Not a SIP header (%s) Ignoring?\n", referred_by);
        referred_by = NULL;
    }

    if (refer_to)
        refer_to += 4;

    if (referred_by)
        referred_by += 4;
    
    replace_key.callid = NULL;

    if ((ptr = strchr(refer_to, '?')))
    {
        /* Search for arguments */
        *ptr = '\0';
        ptr++;
        if (!strncasecmp(ptr, "REPLACES=", 9))
        {
            char *p;

            replace_key.callid = cw_strdupa(ptr + 9);
            replace_key.tag = NULL;
            replace_key.taglen = 0;

            cw_uri_decode(replace_key.callid);

            for (p = replace_key.callid; (ptr = strchr(p, ';')); p = ptr + 1) {
                *ptr = '\0';
                if (!strcasecmp(p, "to-tag=")) {
                    replace_key.tag = p + sizeof("to-tag=") - 1;
                    replace_key.taglen = (int)(ptr - p);
		}
            }
        }
    }
    
    if ((ptr = strchr(refer_to, '@')))    /* Skip domain (should be saved in SIPDOMAIN) */
        *ptr = '\0';
    if ((ptr = strchr(refer_to, ';'))) 
        *ptr = '\0';
    
    if (referred_by)
    {
        if ((ptr = strchr(referred_by, '@')))
            *ptr = '\0';
        if ((ptr = strchr(referred_by, ';'))) 
            *ptr = '\0';
    }

    if (sip_debug_test_pvt(sip_pvt))
    {
        cw_verbose("Transfer to %s in %s\n", refer_to, transfercontext);
        if (referred_by)
            cw_verbose("Transfer from %s in %s\n", referred_by, sip_pvt->context);
    }
    if (replace_key.callid)
    {    
        /* This is a supervised transfer */
        cw_copy_string(sip_pvt->refer_to, "", sizeof(sip_pvt->refer_to));
        cw_copy_string(sip_pvt->referred_by, "", sizeof(sip_pvt->referred_by));
        cw_copy_string(sip_pvt->refer_contact, "", sizeof(sip_pvt->refer_contact));
        sip_pvt->refer_call = NULL;
        if ((sip_pvt_ptr = get_sip_pvt_byid_locked(&replace_key)))
        {
            sip_pvt->refer_call = sip_pvt_ptr;
            if (sip_pvt->refer_call == sip_pvt)
            {
                cw_log(CW_LOG_NOTICE, "Supervised transfer attempted to transfer into same call id (%s)!\n", sip_pvt->callid);
                sip_pvt->refer_call = NULL;
            }
            else
            {
                cw_mutex_unlock(&sip_pvt_ptr->lock);
                return 0;
            }
            cw_mutex_unlock(&sip_pvt_ptr->lock);
	    cw_object_put(sip_pvt_ptr);
        }
        else
        {
            cw_log(CW_LOG_NOTICE, "Supervised transfer requested, but unable to find callid '%s tag= %s'.  Both legs must reside on CallWeaver box to transfer at this time.\n", replace_key.callid, replace_key.tag);
            /* XXX The refer_to could contain a call on an entirely different machine, requiring an 
                  INVITE with a replaces header -anthm XXX */
            /* The only way to find out is to use the dialplan - oej */
        }
    }
    else if (cw_exists_extension(NULL, transfercontext, refer_to, 1, NULL) || !strcmp(refer_to, cw_parking_ext()))
    {
        /* This is an unsupervised transfer (blind transfer) */
        
        cw_log(CW_LOG_DEBUG,"Unsupervised transfer to (Refer-To): %s\n", refer_to);
        if (referred_by)
            cw_log(CW_LOG_DEBUG,"Transferred by  (Referred-by: ) %s \n", referred_by);
        cw_log(CW_LOG_DEBUG,"Transfer Contact Info %s (REFER_CONTACT)\n", h_contact);
        cw_copy_string(sip_pvt->refer_to, refer_to, sizeof(sip_pvt->refer_to));
        if (referred_by)
            cw_copy_string(sip_pvt->referred_by, referred_by, sizeof(sip_pvt->referred_by));
        if (h_contact)
        {
            cw_copy_string(sip_pvt->refer_contact, h_contact, sizeof(sip_pvt->refer_contact));
        }
        sip_pvt->refer_call = NULL;
        if ((chan = sip_pvt->owner) && (peer = cw_bridged_channel(sip_pvt->owner)))
        {
            pbx_builtin_setvar_helper(peer, "SIPREFERTO", get_header(req, SIP_HDR_REFER_TO));
            pbx_builtin_setvar_helper(chan, "SIPREFERTO", get_header(req, SIP_HDR_REFER_TO));
            pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", peer->name);
            pbx_builtin_setvar_helper(peer, "BLINDTRANSFER", chan->name);
            cw_object_put(peer);
        }
        return 0;
    }
    else if (cw_canmatch_extension(NULL, transfercontext, refer_to, 1, NULL))
    {
        return 1;
    }

    return -1;
}

/*! \brief  get_also_info: Call transfer support (old way, deprecated) */
static int get_also_info(struct sip_pvt *p, struct sip_request *oreq)
{
    char tmp[256], *c, *a;
    struct sip_request *req;
    
    req = oreq;
    if (!req)
        req = &p->initreq;
    cw_copy_string(tmp, get_header(req, SIP_HDR_NOSHORT("Also")), sizeof(tmp));
    
    c = get_in_brackets(tmp);
    
        
    if (strncasecmp(c, "sip:", 4))
    {
        cw_log(CW_LOG_WARNING, "Huh?  Not a SIP header (%s)?\n", c);
        return -1;
    }
    c += 4;
    if ((a = strchr(c, '@')))
        *a = '\0';
    if ((a = strchr(c, ';'))) 
        *a = '\0';
    
    if (sip_debug_test_pvt(p))
    {
        cw_verbose("Looking for %s in %s\n", c, p->context);
    }
    if (cw_exists_extension(NULL, p->context, c, 1, NULL))
    {
        /* This is an unsupervised transfer */
        cw_log(CW_LOG_DEBUG,"Assigning Extension %s to REFER-TO\n", c);
        cw_copy_string(p->refer_to, c, sizeof(p->refer_to));
        cw_copy_string(p->referred_by, "", sizeof(p->referred_by));
        cw_copy_string(p->refer_contact, "", sizeof(p->refer_contact));
        p->refer_call = NULL;
        return 0;
    }
    else if (cw_canmatch_extension(NULL, p->context, c, 1, NULL))
    {
        return 1;
    }

    return -1;
}


/*! \brief  get_calleridname: Get caller id name from SIP headers */
static char *get_calleridname(char *input, char *output, size_t outputsize)
{
    char *end = strchr(input, '<');
    char *tmp = strchr(input, '\"');
    int bytes = 0;
    int maxbytes = outputsize - 1;

    if (!end || (end == input)) return NULL;
    /* move away from "<" */
    end--;
    /* we found "name" */
    if (tmp && tmp < end)
    {
        end = strchr(tmp+1, '\"');
        if (!end) return NULL;
        bytes = (int) (end - tmp);
        /* protect the output buffer */
        if (bytes > maxbytes)
            bytes = maxbytes;
        cw_copy_string(output, tmp + 1, bytes);
    }
    else
    {
        /* we didn't find "name" */
        /* clear the empty characters in the begining*/
        input = cw_skip_blanks(input);
        /* clear the empty characters in the end */
        while(*end && isspace(*end) && end > input)
            end--;
        if (end >= input)
        {
            bytes = (int) (end - input) + 2;
            /* protect the output buffer */
            if (bytes > maxbytes)
            {
                bytes = maxbytes;
            }
            cw_copy_string(output, input, bytes);
        }
        else
            return NULL;
    }
    return output;
}

/*! \brief  get_rpid_num: Get caller id number from Remote-Party-ID header field 
 *    Returns true if number should be restricted (privacy setting found)
 *    output is set to NULL if no number found
 */
static int get_rpid_num(char *input,char *output, int maxlen)
{
    char *start;
    char *end;

    start = strchr(input, ':');
    if (!start)
    {
        output[0] = '\0';
        return 0;
    }
    start++;

    /* we found "number" */
    cw_copy_string(output,start,maxlen);
    output[maxlen-1] = '\0';

    end = strchr(output, '@');
    if (end)
        *end = '\0';
    else
        output[0] = '\0';
    if (strstr(input,"privacy=full") || strstr(input,"privacy=uri"))
        return CW_PRES_PROHIB_USER_NUMBER_NOT_SCREENED;

    return 0;
}


/*! \brief  check_user_full: Check if matching user or peer is defined */
/*     Match user on From: user name and peer on IP/port */
/*    This is used on first invite (not re-invites) and subscribe requests */
static int check_user_full(struct sip_pvt *p, struct sip_request *req, enum sipmethod sipmethod, char *uri, int reliable, int ignore, char *mailbox, int mailboxlen)
{
    struct sip_user *user = NULL;
    struct sip_peer *peer;
    char *of, from[256], *c;
    char *rpid,rpid_num[50];
    int res = 0;
    char *t;
    char calleridname[50];
    int debug = sip_debug_test_addr(&req->recvdaddr.sa);
    struct cw_variable *tmpvar = NULL, *v = NULL;
    char *uri2 = cw_strdupa(uri);

    /* Terminate URI */
    t = uri2;
    while(*t && !isspace(*t) && (*t != ';'))
        t++;
    *t = '\0';
    of = get_header(req, SIP_HDR_FROM);
    if (pedanticsipchecking)
        cw_uri_decode(of);

    cw_copy_string(from, of, sizeof(from));
    
    memset(calleridname,0,sizeof(calleridname));
    get_calleridname(from, calleridname, sizeof(calleridname));
    if (calleridname[0])
        cw_copy_string(p->cid_name, calleridname, sizeof(p->cid_name));

    rpid = get_header(req, SIP_HDR_NOSHORT("Remote-Party-ID"));
    memset(rpid_num,0,sizeof(rpid_num));
    if (!cw_strlen_zero(rpid)) 
        p->callingpres = get_rpid_num(rpid,rpid_num, sizeof(rpid_num));

    of = get_in_brackets(from);
    if (cw_strlen_zero(p->exten))
    {
        t = uri2;
        if (!strncasecmp(t, "sip:", 4))
            t+= 4;
        cw_copy_string(p->exten, t, sizeof(p->exten));
        t = strchr(p->exten, '@');
        if (t)
            *t = '\0';
        if (cw_strlen_zero(p->our_contact))
            build_contact(p);
    }
    /* save the URI part of the From header */
    cw_copy_string(p->from, of, sizeof(p->from));
    if (strncasecmp(of, "sip:", 4))
        cw_log(CW_LOG_NOTICE, "From address missing 'sip:', using it anyway\n");
    else
        of += 4;
    /* Get just the username part */
    if ((c = strchr(of, '@')))
    {
        *c = '\0';
        if ((c = strchr(of, ':')))
            *c = '\0';
        cw_copy_string(p->cid_num, of, sizeof(p->cid_num));
        cw_shrink_phone_number(p->cid_num);
    }
    if (cw_strlen_zero(of))
        return 0;

    if (!mailbox)    /* If it's a mailbox SUBSCRIBE, don't check users */
        user = find_user(of, 1);

    /* Find user based on user name in the from header */

    if (user && cw_acl_check(user->acl, &req->recvdaddr.sa, 1))
    {
        cw_copy_flags(p, user, SIP_FLAGS_TO_COPY);
        /* copy channel vars */
        for (v = user->chanvars ; v ; v = v->next)
        {
            if ((tmpvar = cw_variable_new(v->name, v->value)))
            {
                tmpvar->next = p->chanvars; 
                p->chanvars = tmpvar;
            }
        }
        p->prefs = user->prefs;
        /* replace callerid if rpid found, and not restricted */
        if (!cw_strlen_zero(rpid_num) && cw_test_flag(p, SIP_TRUSTRPID))
        {
            if (*calleridname)
                cw_copy_string(p->cid_name, calleridname, sizeof(p->cid_name));
            cw_copy_string(p->cid_num, rpid_num, sizeof(p->cid_num));
            cw_shrink_phone_number(p->cid_num);
        }

        if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), user->name, user->secret, user->md5secret, sipmethod, uri2, reliable, ignore)))
        {
            sip_cancel_destroy(p);
            cw_copy_flags(p, user, SIP_FLAGS_TO_COPY);
            /* Copy SIP extensions profile from INVITE */
            if (p->sipoptions)
                user->sipoptions = p->sipoptions;

            /* If we have a call limit, set flag */
            if (user->call_limit)
                cw_set_flag(p, SIP_CALL_LIMIT);
            if (!cw_strlen_zero(user->context))
                cw_copy_string(p->context, user->context, sizeof(p->context));
            if (!cw_strlen_zero(user->cid_num) && !cw_strlen_zero(p->cid_num))
            {
                cw_copy_string(p->cid_num, user->cid_num, sizeof(p->cid_num));
                cw_shrink_phone_number(p->cid_num);
            }
            if (!cw_strlen_zero(user->cid_name) && !cw_strlen_zero(p->cid_num)) 
                cw_copy_string(p->cid_name, user->cid_name, sizeof(p->cid_name));
            cw_copy_string(p->username, user->name, sizeof(p->username));
            cw_copy_string(p->peersecret, user->secret, sizeof(p->peersecret));
            cw_copy_string(p->subscribecontext, user->subscribecontext, sizeof(p->subscribecontext));
            cw_copy_string(p->peermd5secret, user->md5secret, sizeof(p->peermd5secret));
            cw_copy_string(p->accountcode, user->accountcode, sizeof(p->accountcode));
            cw_copy_string(p->language, user->language, sizeof(p->language));
            cw_copy_string(p->musicclass, user->musicclass, sizeof(p->musicclass));
            p->amaflags = user->amaflags;
            p->callgroup = user->callgroup;
            p->pickupgroup = user->pickupgroup;
            /* Copy callingpres only if the RPID did not indicate any privacy/screening parameters */
            if (p->callingpres == 0)
                p->callingpres = user->callingpres;
            p->capability = user->capability;
            p->jointcapability = user->capability;
            if (p->peercapability)
                p->jointcapability &= p->peercapability;
            if ((cw_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833) || (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_AUTO))
                p->noncodeccapability |= CW_RTP_DTMF;
            else
                p->noncodeccapability &= ~CW_RTP_DTMF;
            if (p->t38peercapability)
                p->t38jointcapability &= p->t38peercapability;
        }
        if (user && debug)
            cw_verbose("Found user '%s'\n", user->name);
    }
    else
    {
        if (user)
        {
            if (!mailbox && debug)
                cw_verbose("Found user '%s', but fails host access\n", user->name);
            cw_object_put(user);
        }
        user = NULL;
    }

    if (!user)
    {
        /* If we didn't find a user match, check for peers */
        if (sipmethod == SIP_SUBSCRIBE)
            /* For subscribes, match on peer name only */
            peer = find_peer(of, NULL, 1);
        else {
            /* Look for peer based on the address we received data from.
             * If peer is registered from this address or has this as a default
             * address, this call is from the peer .
             */
            peer = find_peer(NULL, &req->recvdaddr.sa, 1);
	}

        if (peer)
        {
            if (debug)
                cw_verbose("Found peer '%s'\n", peer->name);
            /* Take the peer */
            cw_copy_flags(p, peer, SIP_FLAGS_TO_COPY);

            /* Copy SIP extensions profile to peer */
            if (p->sipoptions)
                peer->sipoptions = p->sipoptions;

            /* replace callerid if rpid found, and not restricted */
            if (!cw_strlen_zero(rpid_num) && cw_test_flag(p, SIP_TRUSTRPID))
            {
                if (*calleridname)
                    cw_copy_string(p->cid_name, calleridname, sizeof(p->cid_name));
                cw_copy_string(p->cid_num, rpid_num, sizeof(p->cid_num));
                cw_shrink_phone_number(p->cid_num);
            }
            cw_copy_string(p->peersecret, peer->secret, sizeof(p->peersecret));
            p->peersecret[sizeof(p->peersecret)-1] = '\0';
            cw_copy_string(p->subscribecontext, peer->subscribecontext, sizeof(p->subscribecontext));
            cw_copy_string(p->peermd5secret, peer->md5secret, sizeof(p->peermd5secret));
            p->peermd5secret[sizeof(p->peermd5secret)-1] = '\0';
            /* Copy callingpres only if the RPID did not indicate any privacy/screening parameters */
            if (p->callingpres == 0)
                p->callingpres = peer->callingpres;

            p->timer_t1 = peer->timer_t1;
	    p->timer_t2 = peer->timer_t2;

            if (cw_test_flag(peer, SIP_INSECURE_INVITE))
            {
                /* Pretend there is no required authentication */
                p->peersecret[0] = '\0';
                p->peermd5secret[0] = '\0';
            }
            if (!(res = check_auth(p, req, p->randdata, sizeof(p->randdata), peer->name, p->peersecret, p->peermd5secret, sipmethod, uri, reliable, ignore)))
            {
                cw_copy_flags(p, peer, SIP_FLAGS_TO_COPY);
                /* If we have a call limit, set flag */
                if (peer->call_limit)
                    cw_set_flag(p, SIP_CALL_LIMIT);
                cw_copy_string(p->peername, peer->name, sizeof(p->peername));
                cw_copy_string(p->authname, peer->name, sizeof(p->authname));
                /* copy channel vars */
                for (v = peer->chanvars ; v ; v = v->next)
                {
                    if ((tmpvar = cw_variable_new(v->name, v->value)))
                    {
                        tmpvar->next = p->chanvars; 
                        p->chanvars = tmpvar;
                    }
                }
                if (mailbox)
                    snprintf(mailbox, mailboxlen, ",%s,", peer->mailbox);
                if (!cw_strlen_zero(peer->username))
                {
                    cw_copy_string(p->username, peer->username, sizeof(p->username));
                    /* Use the default username for authentication on outbound calls */
                    cw_copy_string(p->authname, peer->username, sizeof(p->authname));
                }
                if (!cw_strlen_zero(peer->cid_num) && !cw_strlen_zero(p->cid_num))
                {
                    cw_copy_string(p->cid_num, peer->cid_num, sizeof(p->cid_num));
                    cw_shrink_phone_number(p->cid_num);
                }
                if (!cw_strlen_zero(peer->cid_name) && !cw_strlen_zero(p->cid_name)) 
                    cw_copy_string(p->cid_name, peer->cid_name, sizeof(p->cid_name));
                cw_copy_string(p->fullcontact, peer->fullcontact, sizeof(p->fullcontact));
                if (!cw_strlen_zero(peer->context))
                    cw_copy_string(p->context, peer->context, sizeof(p->context));
                cw_copy_string(p->peersecret, peer->secret, sizeof(p->peersecret));
                cw_copy_string(p->peermd5secret, peer->md5secret, sizeof(p->peermd5secret));
                cw_copy_string(p->language, peer->language, sizeof(p->language));
                cw_copy_string(p->accountcode, peer->accountcode, sizeof(p->accountcode));
                p->amaflags = peer->amaflags;
                p->callgroup = peer->callgroup;
                p->pickupgroup = peer->pickupgroup;
                p->capability = peer->capability;
                p->prefs = peer->prefs;
                p->jointcapability = peer->capability;
                if (p->peercapability)
                    p->jointcapability &= p->peercapability;
                if ((cw_test_flag(p, SIP_DTMF) == SIP_DTMF_RFC2833) || (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_AUTO))
                    p->noncodeccapability |= CW_RTP_DTMF;
                else
                    p->noncodeccapability &= ~CW_RTP_DTMF;
                if (p->t38peercapability)
                    p->t38jointcapability &= p->t38peercapability;
            }
            cw_object_put(peer);
        }
        else
        { 
            if (debug)
                cw_verbose("Found no matching peer or user for %#l@\n", &req->recvdaddr.sa);

            /* do we allow guests? */
            if (!global_allowguest) {
		if (global_alwaysauthreject)
			res = -4; /* reject with fake authorization request */
		else
			res = -1; /* we don't want any guests, authentication will fail */
#ifdef OSP_SUPPORT            
            } else if (global_allowguest == 2)
            {
                cw_copy_flags(p, &global_flags, SIP_OSPAUTH);
                res = check_auth(p, req, p->randdata, sizeof(p->randdata), "", "", "", sipmethod, uri, reliable, ignore); 
#endif
            }
        }

    }

    if (user)
        cw_object_put(user);

    return res;
}


/*! \brief  receive_message: Receive SIP MESSAGE method messages */
/*    We only handle messages within current calls currently */
/*    Reference: RFC 3428 */
static void receive_message(struct sip_pvt *p, struct sip_request *req)
{
	struct cw_frame f;
	char *content_type;

	content_type = get_header(req, SIP_HDR_CONTENT_TYPE);

	/* We want text/plain. It may have a charset specifier after it but we don't want text/plainfoo[;charset=...] */
	if (strncmp(content_type, "text/plain", 10) || (content_type[10] && !(content_type[10] == ' ' || content_type[10] == ';'))) {
		/* No text/plain attachment */
		transmit_response(p, "415 Unsupported Media Type", req, 0); /* Good enough, or? */
		sip_destroy(p);
		return;
	}

	if (p->owner) {
		if (sip_debug_test_pvt(p))
			cw_verbose("Message received: '%s'\n", &req->pkt.data[req->body_start]);

		memset(&f, 0, sizeof(f));
		f.frametype = CW_FRAME_TEXT;
		f.subclass = 0;
		f.offset = 0;
		f.data = &req->pkt.data[req->body_start];
		f.datalen = req->pkt.used - req->body_start;
		cw_queue_frame(p->owner, &f);
		transmit_response(p, "202 Accepted", req, 0); /* We respond 202 accepted, since we relay the message */
	} else { /* Message outside of a call, we do not support that */
		cw_log(CW_LOG_WARNING,"Received message to %s from %s, dropped it...\n  Content-Type:%s\n  Message: %s\n", get_header(req, SIP_HDR_TO), get_header(req, SIP_HDR_FROM), content_type, &req->pkt.data[req->body_start]);
		transmit_response(p, "405 Method Not Allowed", req, 0); /* Good enough, or? */
	}
	sip_destroy(p);
	return;
}


#define FORMAT  "%-25.25s %-15.15s %-15.15s \n"
#define FORMAT2 "%-25.25s %15d %15d \n"
#define FORMAT3 "%-25.25s %15d %-15.15s \n"

struct sip_show_inuse_args {
	struct cw_dynstr *ds_p;
	int showall;
};

static int sip_show_inuse_user(struct cw_object *obj, void *data)
{
	struct sip_user *user = container_of(obj, struct sip_user, obj);
	struct sip_show_inuse_args *args = data;

        if (user->call_limit)
            cw_dynstr_printf(args->ds_p, FORMAT2, user->name, user->inUse, user->call_limit);
	else if (args->showall)
            cw_dynstr_printf(args->ds_p, FORMAT3, user->name, user->inUse, "N/A");

	return 0;
}

static int sip_show_inuse_peer(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct sip_show_inuse_args *args = data;

	if (peer->call_limit)
		cw_dynstr_printf(args->ds_p, FORMAT2, peer->name, peer->inUse, peer->call_limit);
	else if (args->showall)
		cw_dynstr_printf(args->ds_p, FORMAT3, peer->name, peer->inUse, "N/A");

	return 0;
}

/*! \brief  sip_show_inuse: CLI Command to show calls within limits set by 
      call_limit */
static int sip_show_inuse(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    struct sip_show_inuse_args args = {
        .ds_p = ds_p,
	.showall = 0,
    };

    if (argc < 3) 
        return RESULT_SHOWUSAGE;

    if (argc == 4 && !strcmp(argv[3],"all")) 
            args.showall = 1;
    
    cw_dynstr_printf(ds_p, FORMAT, "* User name", "In use", "Limit");
    cw_registry_iterate_ordered(&userbyname_registry, sip_show_inuse_user, &args);

    cw_dynstr_printf(ds_p, FORMAT, "* Peer name", "In use", "Limit");
    cw_registry_iterate_ordered(&peerbyname_registry, sip_show_inuse_peer, &args);

    return RESULT_SUCCESS;
}
#undef FORMAT
#undef FORMAT2
#undef FORMAT3

/*! \brief  nat2str: Convert NAT setting to text string */
static const char *nat2str(int nat)
{
    switch (nat)
    {
    case SIP_NAT_NEVER:
        return "No";
    case SIP_NAT_ROUTE:
        return "Route";
    case SIP_NAT_ALWAYS:
        return "Always";
    case SIP_NAT_RFC3581:
        return "RFC3581";
    default:
        return "Unknown";
    }
}

/*! \brief  peer_status: Report Peer status as text */
static const char *peer_status(struct sip_peer *peer)
{
	const char *status = "Unmonitored";

	if (peer->addr.sa.sa_family == AF_UNSPEC)
		status = "Unregistered";
	else if (peer->timer_t1 < 0)
		status = "UNREACHABLE";
	else if (peer->maxms && peer->timer_t1 > peer->maxms)
		status = "LAGGED";
	else
		status = "OK";

	return status;
}


struct sip_show_users_args {
	struct cw_dynstr *ds_p;
	int havepattern;
	regex_t regexbuf;
};

#define FORMAT  "%-25.25s  %-15.15s  %-15.15s  %-15.15s  %-5.5s%-10.10s\n"

static int sip_show_users_one(struct cw_object *obj, void *data)
{
	struct sip_user *user = container_of(obj, struct sip_user, obj);
	struct sip_show_users_args *args = data;

	if (!args->havepattern || !regexec(&args->regexbuf, user->name, 0, NULL, 0)) {
		cw_dynstr_printf(args->ds_p, FORMAT, user->name,
			user->secret,
			user->accountcode,
			user->context,
			(user->acl ? "Yes" : "No"),
			nat2str(cw_test_flag(user, SIP_NAT)));
	}
	return 0;
}

/*! \brief  sip_show_users: CLI Command 'SIP Show Users' */
static int sip_show_users(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_show_users_args args = {
		.ds_p = ds_p,
		.havepattern = 0,
	};

	switch (argc) {
		case 5:
			if (!strcasecmp(argv[3], "like")) {
				if (regcomp(&args.regexbuf, argv[4], REG_EXTENDED | REG_NOSUB))
					return RESULT_SHOWUSAGE;
				args.havepattern = 1;
		} else
				return RESULT_SHOWUSAGE;
			break;
		case 3:
			break;
		default:
			return RESULT_SHOWUSAGE;
	}

	cw_dynstr_printf(ds_p, FORMAT, "Username", "Secret", "Accountcode", "Def.Context", "ACL", "NAT");

	cw_registry_iterate_ordered(&userbyname_registry, sip_show_users_one, &args);

	if (args.havepattern)
		regfree(&args.regexbuf);

    return RESULT_SUCCESS;
#undef FORMAT
}

static char mandescr_show_peers[] =
"Description: Lists SIP peers in text format with details on current status.\n"
"Variables: \n"
"  ActionID: <id>    Action ID for this transaction. Will be returned.\n";


#define SHOW_PEERS_FORMAT_HEADER  "%-25.25s  %-20.20s %-3.3s %-3.3s %-3.3s %-8s %-12s %-7s\n"
#define SHOW_PEERS_FORMAT_DETAIL  "%-25.25s  %-20@ %-3.3s %-3.3s %-3.3s %-8h@ %-12s %-d\n"

struct sip_show_peers_args {
	struct cw_dynstr *ds_p;
	regex_t regexbuf;
	int havepattern;
	int total_peers;
	int peers_online;
	int peers_offline;
};

static int sip_show_peers_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct sip_show_peers_args *args = data;
	char name[256];

	if (!args->havepattern || !regexec(&args->regexbuf, peer->name, 0, NULL, 0)) {
		if (!cw_strlen_zero(peer->username))
			snprintf(name, sizeof(name), "%s/%s", peer->name, peer->username);

		if ((peer->maxms && peer->timer_t1 < 0) || peer->addr.sa.sa_family == AF_UNSPEC)
			args->peers_offline++;
		else
			args->peers_online++;

		cw_dynstr_printf(args->ds_p, SHOW_PEERS_FORMAT_DETAIL,
			name,
			&peer->addr.sa,
			(cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) ? "D" : ""), /* Dynamic or not? */
			((cw_test_flag(peer, SIP_NAT) & SIP_NAT_ROUTE) ? "N" : ""),       /* NAT=yes? */
			(peer->acl ? "A" : ""),                                           /* permit/deny */
			&peer->addr.sa,
			peer_status(peer), peer->timer_t1);

		args->total_peers++;
	}

	return 0;
}

/*! \brief  sip_show_peers: Execute sip show peers command */
static int sip_show_peers(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_show_peers_args args = {
		.ds_p = ds_p,
		.havepattern = 0,
		.peers_online = 0,
		.peers_offline = 0,
		.total_peers = 0,
	};

	switch (argc) {
		case 5:
			if (!strcasecmp(argv[3], "like")) {
				if (regcomp(&args.regexbuf, argv[4], REG_EXTENDED | REG_NOSUB))
					return RESULT_SHOWUSAGE;
				args.havepattern = 1;
			} else
				return RESULT_SHOWUSAGE;
			break;
		case 3:
			break;
		default:
			return RESULT_SHOWUSAGE;
	}

	cw_dynstr_printf(ds_p, SHOW_PEERS_FORMAT_HEADER, "Name/username", "Host", "Dyn", "Nat", "ACL", "Port", "Status", "RTT(ms)");

	cw_registry_iterate_ordered(&peerbyname_registry, sip_show_peers_one, &args);

	cw_dynstr_printf(ds_p,"%d sip peers [%d online , %d offline]\n", args.total_peers, args.peers_online, args.peers_offline);

	if (args.havepattern)
		regfree(&args.regexbuf);

	return RESULT_SUCCESS;
}


struct manager_sip_show_peers_args {
	struct cw_dynstr ds;
	struct mansession *sess;
	const struct message *req;
	int total;
};

static int manager_sip_show_peers_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct manager_sip_show_peers_args *args = data;
	struct cw_manager_message *msg = NULL;

	cw_manager_msg(&msg, 10,
		cw_msg_tuple("Event",          "%s",   "PeerEntry"),
		cw_msg_tuple("ChannelType",    "%s",   "SIP"),
		cw_msg_tuple("ObjectName",     "%s",   peer->name),
		cw_msg_tuple("ChanObjectType", "%s",   "peer"),
		cw_msg_tuple("IPaddress",      "%@",   &peer->addr.sa),
		cw_msg_tuple("IPport",         "%h@",  &peer->addr.sa),
		cw_msg_tuple("Dynamic",        "%s",   (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) ? "yes" : "no")),
		cw_msg_tuple("Natsupport",     "%s",   ((cw_test_flag(peer, SIP_NAT) & SIP_NAT_ROUTE) ? "yes" : "no")),
		cw_msg_tuple("Status",         "%s",   peer_status(peer)),
		cw_msg_tuple("RTT",            "%dms", peer->timer_t1)
	);

	if (msg) {
		cw_acl_print(&args->ds, peer->acl);
		cw_manager_msg(&msg, 1, cw_msg_tuple("ACL", "%s", args->ds.data));
		cw_dynstr_reset(&args->ds);
	}

	args->total++;

	if (msg)
		return cw_manager_send(args->sess, args->req, &msg);

	return -1;
}
/*! \brief  manager_sip_show_peers: Show SIP peers in the manager API */
static struct cw_manager_message *manager_sip_show_peers(struct mansession *sess, const struct message *req)
{
	struct manager_sip_show_peers_args args = {
		.ds = CW_DYNSTR_INIT,
		.sess = sess,
		.req = req,
		.total = 0,
	};
	struct cw_manager_message *msg;

	if ((msg = cw_manager_response("Success", "Peer status list will follow")) && !cw_manager_send(sess, req, &msg)) {
		cw_registry_iterate_ordered(&peerbyname_registry, manager_sip_show_peers_one, &args);

		cw_manager_msg(&msg, 2,
			cw_msg_tuple("Event", "%s", "PeerListComplete"),
			cw_msg_tuple("ListItems", "%d", args.total)
		);

		cw_dynstr_free(&args.ds);
	}

	return msg;
}

#undef FORMAT
#undef FORMAT2


/*! \brief  dtmfmode2str: Convert DTMF mode to printable string */
static const char *dtmfmode2str(int mode)
{
    switch (mode)
    {
    case SIP_DTMF_RFC2833:
        return "rfc2833";
    case SIP_DTMF_INFO:
        return "info";
    case SIP_DTMF_INBAND:
        return "inband";
    case SIP_DTMF_AUTO:
        return "auto";
    }
    return "<error>";
}

/*! \brief  insecure2str: Convert Insecure setting to printable string */
static const char *insecure2str(int port, int invite)
{
    if (port && invite)
        return "port,invite";
    else if (port)
        return "port";
    else if (invite)
        return "invite";
    else
        return "no";
}


struct sip_prune_realtime_args {
	char *name;
	regex_t regexbuf;
	int pruned;
};

static int sip_prune_realtime_peer(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct sip_prune_realtime_args *args = data;

	if (!args->name || !regexec(&args->regexbuf, peer->name, 0, NULL, 0)) {
		if (cw_test_flag(&peer->flags_page2, SIP_PAGE2_RTCACHEFRIENDS)) {
			cw_registry_del(&peerbyname_registry, peer->reg_entry_byname);
			if (peer->reg_entry_byaddr)
				cw_registry_del(&peerbyaddr_registry, peer->reg_entry_byaddr);
			args->pruned++;
		}
	}

	return 0;
}

static int sip_prune_realtime_user(struct cw_object *obj, void *data)
{
	struct sip_user *user = container_of(obj, struct sip_user, obj);
	struct sip_prune_realtime_args *args = data;

	if (!args->name || !regexec(&args->regexbuf, user->name, 0, NULL, 0)) {
		if (cw_test_flag(&user->flags_page2, SIP_PAGE2_RTCACHEFRIENDS)) {
			cw_registry_del(&userbyname_registry, user->reg_entry_byname);
			args->pruned++;
		}
	}

	return 0;
}

/*! \brief  sip_prune_realtime: Remove temporary realtime objects from memory (CLI) */
static int sip_prune_realtime(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_prune_realtime_args args = {
		.name = NULL,
	};
	struct sip_peer *peer;
	struct sip_user *user;
	int pruneuser = 0;
	int prunepeer = 0;
	int multi = 0;

	switch (argc) {
		case 4:
			if (!strcasecmp(argv[3], "user") || !strcasecmp(argv[3], "peer") || !strcasecmp(argv[3], "like"))
				return RESULT_SHOWUSAGE;
			if (!strcasecmp(argv[3], "all"))
				multi = pruneuser = prunepeer = 1;
			else {
				pruneuser = prunepeer = 1;
				args.name = argv[3];
			}
			break;
		case 5:
			if (!strcasecmp(argv[4], "like") || !strcasecmp(argv[3], "all"))
				return RESULT_SHOWUSAGE;
			if (!strcasecmp(argv[3], "like")) {
				multi = pruneuser = prunepeer = 1;
				args.name = argv[4];
			} else if (!strcasecmp(argv[3], "user")) {
				pruneuser = 1;
				if (!strcasecmp(argv[4], "all"))
					multi = 1;
				else
					args.name = argv[4];
			} else if (!strcasecmp(argv[3], "peer")) {
				prunepeer = 1;
				if (!strcasecmp(argv[4], "all"))
					multi = 1;
				else
					args.name = argv[4];
			} else
				return RESULT_SHOWUSAGE;
			break;
		case 6:
			if (strcasecmp(argv[4], "like"))
				return RESULT_SHOWUSAGE;
			if (!strcasecmp(argv[3], "user")) {
				pruneuser = 1;
				args.name = argv[5];
			} else if (!strcasecmp(argv[3], "peer")) {
				prunepeer = 1;
				args.name = argv[5];
			} else
				return RESULT_SHOWUSAGE;
			break;
		default:
			return RESULT_SHOWUSAGE;
	}

	if (multi && args.name) {
		if (regcomp(&args.regexbuf, args.name, REG_EXTENDED | REG_NOSUB))
			return RESULT_SHOWUSAGE;
	}

	if (multi) {
		if (prunepeer) {
			args.pruned = 0;

			cw_registry_iterate(&peerbyname_registry, sip_prune_realtime_peer, &args);

			if (args.pruned)
				cw_dynstr_printf(ds_p, "%d peers pruned.\n", args.pruned);
			else
				cw_dynstr_printf(ds_p, "No peers found to prune.\n");
		}
		if (pruneuser) {
			args.pruned = 0;

			cw_registry_iterate(&userbyname_registry, sip_prune_realtime_user, &args);

			if (args.pruned)
				cw_dynstr_printf(ds_p, "%d users pruned.\n", args.pruned);
			else
				cw_dynstr_printf(ds_p, "No users found to prune.\n");
		}
	} else {
		if (prunepeer) {
			if ((peer = find_peer(args.name, NULL, 0))) {
				if (!cw_test_flag(&peer->flags_page2, SIP_PAGE2_RTCACHEFRIENDS))
					cw_dynstr_printf(ds_p, "Peer '%s' is not a Realtime peer, cannot be pruned.\n", args.name);
				else {
					cw_dynstr_printf(ds_p, "Peer '%s' pruned.\n", args.name);
					cw_registry_del(&peerbyname_registry, peer->reg_entry_byname);
					if (peer->reg_entry_byaddr)
						cw_registry_del(&peerbyaddr_registry, peer->reg_entry_byaddr);
				}
				cw_object_put(peer);
			} else
				cw_dynstr_printf(ds_p, "Peer '%s' not found.\n", args.name);
		}
		if (pruneuser) {
			if ((user = find_user(args.name, 0))) {
				if (!cw_test_flag(&user->flags_page2, SIP_PAGE2_RTCACHEFRIENDS))
					cw_dynstr_printf(ds_p, "User '%s' is not a Realtime user, cannot be pruned.\n", args.name);
				else {
					cw_dynstr_printf(ds_p, "User '%s' pruned.\n", args.name);
					cw_registry_del(&userbyname_registry, user->reg_entry_byname);
				}
				cw_object_put(user);
			} else
				cw_dynstr_printf(ds_p, "User '%s' not found.\n", args.name);
		}
	}

	return RESULT_SUCCESS;
}

/*! \brief  print_codec_to_cli: Print codec list from preference to CLI/manager */
static void print_codec_to_cli(struct cw_dynstr *ds_p, struct cw_codec_pref *pref)
{
    int x, codec;

    for (x = 0;  x < 32;  x++)
    {
        codec = cw_codec_pref_index(pref, x);
        if (!codec)
            break;
        cw_dynstr_printf(ds_p, "%s", cw_getformatname(codec));
        if (x < 31  &&  cw_codec_pref_index(pref, x + 1))
            cw_dynstr_printf(ds_p, ",");
    }
    if (!x)
        cw_dynstr_printf(ds_p, "none");
}

static const char *domain_mode_to_text(const enum domain_mode mode)
{
    switch (mode)
    {
    case SIP_DOMAIN_AUTO:
        return "[Automatic]";
    case SIP_DOMAIN_CONFIG:
        return "[Configured]";
    }

    return "";
}

/*! \brief  sip_show_domains: CLI command to list local domains */
#define FORMAT "%-40.40s %-20.20s %-16.16s\n"
static int sip_show_domains(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    struct domain *d;

    CW_UNUSED(argc);
    CW_UNUSED(argv);

    if (CW_LIST_EMPTY(&domain_list))
    {
        cw_dynstr_printf(ds_p, "SIP Domain support not enabled.\n\n");
        return RESULT_SUCCESS;
    }
    else
    {
        cw_dynstr_printf(ds_p, FORMAT, "Our local SIP domains:", "Context", "Set by");
        CW_LIST_LOCK(&domain_list);
        CW_LIST_TRAVERSE(&domain_list, d, list)
            cw_dynstr_printf(ds_p, FORMAT, d->domain, cw_strlen_zero(d->context) ? "(default)": d->context,
                domain_mode_to_text(d->mode));
        CW_LIST_UNLOCK(&domain_list);
        cw_dynstr_printf(ds_p, "\n");
        return RESULT_SUCCESS;
    }
}
#undef FORMAT


static char mandescr_show_peer[] =
"Description: Show one SIP peer with details on current status.\n"
"Variables: \n"
"  Peer: <name>           The peer name you want to check.\n";

static int sip_show_peer(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    char callgroup[256], pickupgroup[256];
    char cbuf[256];
    struct sip_peer *peer;
    struct cw_variable *v;
    struct sip_auth *auth;
    int x = 0, load_realtime = 0;

    if (argc < 4)
        return RESULT_SHOWUSAGE;

    load_realtime = (argc == 5 && !strcmp(argv[4], "load")) ? 1 : 0;
    peer = find_peer(argv[3], NULL, load_realtime);
    if (peer)
    {
        cw_dynstr_tprintf(ds_p, 41,
            cw_fmtval("\n\n"),
            cw_fmtval("  * Name           : %s\n", peer->name),
            cw_fmtval("  Secret           : %s\n", (cw_strlen_zero(peer->secret) ? "<Not set>" : "<Set>")),
            cw_fmtval("  MD5Secret        : %s\n", (cw_strlen_zero(peer->md5secret) ? "<Not set>" : "<Set>")),
            cw_fmtval("  Context          : %s\n", peer->context),
            cw_fmtval("  Subscr.Cont.     : %s\n", (cw_strlen_zero(peer->subscribecontext) ? "<Not set>" : peer->subscribecontext)),
            cw_fmtval("  Language         : %s\n", peer->language),
            cw_fmtval("  Accountcode      : %s\n", (!cw_strlen_zero(peer->accountcode) ? peer->accountcode : "")),
            cw_fmtval("  AMA flags        : %s\n", cw_cdr_flags2str(peer->amaflags)),
            cw_fmtval("  CallingPres      : %s\n", cw_describe_caller_presentation(peer->callingpres)),
            cw_fmtval("  Callerid         : %s\n", cw_callerid_merge(cbuf, sizeof(cbuf), peer->cid_name, peer->cid_num, "")),
            cw_fmtval("  FromUser         : %s\n", (!cw_strlen_zero(peer->fromuser) ? peer->fromuser : "")),
            cw_fmtval("  FromDomain       : %s\n", (!cw_strlen_zero(peer->fromdomain) ? peer->fromdomain : "")),
            cw_fmtval("  Callgroup        : %s\n", cw_print_group(callgroup, sizeof(callgroup), peer->callgroup)),
            cw_fmtval("  Pickupgroup      : %s\n", cw_print_group(pickupgroup, sizeof(pickupgroup), peer->pickupgroup)),
            cw_fmtval("  VoiceMailbox     : %s\n", peer->mailbox),
            cw_fmtval("  VM Extension     : %s\n", peer->vmexten),
            cw_fmtval("  LastMsgsSent     : %d\n", peer->lastmsgssent),
            cw_fmtval("  Call limit       : %d\n", peer->call_limit),
            cw_fmtval("  Dynamic          : %c\n", (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) ? 'Y' : 'N')),
            cw_fmtval("  Expire           : %ld\n", cw_sched_when(sched, peer->expire)),
            cw_fmtval("  Insecure         : %s\n", insecure2str(cw_test_flag(peer, SIP_INSECURE_PORT), cw_test_flag(peer, SIP_INSECURE_INVITE))),
            cw_fmtval("  Nat              : %s\n", nat2str(cw_test_flag(peer, SIP_NAT))),
            cw_fmtval("  CanReinvite      : %c\n", (cw_test_flag(peer, SIP_CAN_REINVITE) ? 'Y' : 'N')),
            cw_fmtval("  PromiscRedir     : %c\n", (cw_test_flag(peer, SIP_PROMISCREDIR) ? 'Y' : 'N')),
            cw_fmtval("  User=Phone       : %c\n", (cw_test_flag(peer, SIP_USEREQPHONE) ? 'Y' : 'N')),
            cw_fmtval("  Trust RPID       : %c\n", (cw_test_flag(peer, SIP_TRUSTRPID) ? 'Y' : 'N')),
            cw_fmtval("  Send RPID        : %c\n", (cw_test_flag(peer, SIP_SENDRPID) ? 'Y' : 'N')),
            cw_fmtval("  DTMFmode         : %s\n", dtmfmode2str(cw_test_flag(peer, SIP_DTMF))),
            cw_fmtval("  LastMsg          : %d\n", peer->lastmsg),
            cw_fmtval("  ToHost           : %s\n", peer->tohost),
            cw_fmtval("  Address-IP       : %@\n", &peer->addr.sa),
            cw_fmtval("  Address-port     : %h@\n", &peer->addr.sa),
            cw_fmtval("  Default-addr-IP  : %@\n", &peer->defaddr.sa),
            cw_fmtval("  Default-addr-port: %h@\n", &peer->defaddr.sa),
            cw_fmtval("  Default-Username : %s\n", peer->username),
            cw_fmtval("  Status           : %s\n", peer_status(peer)),
            cw_fmtval("  RTT              : %dms\n", peer->timer_t1),
            cw_fmtval("  Useragent        : %s\n", peer->useragent),
            cw_fmtval("  Reg-Contact      : %s\n", peer->fullcontact),
            cw_fmtval("  Codecs           : ")
	);

        cw_getformatname_multiple(ds_p, peer->capability);

        cw_dynstr_printf(ds_p, "\n  ACL              : ");
        cw_acl_print(ds_p, peer->acl);

        cw_dynstr_printf(ds_p, "\n  Codec Order      : (");
        print_codec_to_cli(ds_p, &peer->prefs);
        cw_dynstr_printf(ds_p, ")\n");

        for (auth = peer->auth; auth; auth = auth->next) {
            cw_dynstr_printf(ds_p,
                  "  Realm-auth        : Realm %-15.15s User %-10.20s %s\n",
                  auth->realm, auth->username,
                  (!cw_strlen_zero(auth->secret) ? "<Secret set>"
                      : (!cw_strlen_zero(auth->md5secret) ? "<MD5secret set>" : "<Not set>"))
            );
        }

        if (peer->sipoptions)
        {
            cw_dynstr_printf(ds_p, "  SIP Options      : ");
            for (x = 0;  (x < (sizeof(sip_options)/sizeof(sip_options[0])));  x++)
            {
                if (peer->sipoptions & sip_options[x].id)
                    cw_dynstr_printf(ds_p, "%s ", sip_options[x].text);
            }
            cw_dynstr_printf(ds_p, "\n");
        }

        if (peer->chanvars)
        {
            cw_dynstr_printf(ds_p, "  Variables        :\n");
            for (v = peer->chanvars;  v;  v = v->next)
                cw_dynstr_printf(ds_p, "                     %s = %s\n", v->name, v->value);
        }

        cw_object_put(peer);
    }
    else
        cw_dynstr_printf(ds_p,"Peer %s not found.\n", argv[3]);

    cw_dynstr_printf(ds_p,"\n");

    return RESULT_SUCCESS;
}

/*! \brief  manager_sip_show_peer: Show SIP peers in the manager API  */
static struct cw_manager_message *manager_sip_show_peer(struct mansession *sess, const struct message *req)
{
	char callgroup[256], pickupgroup[256];
	char cbuf[256];
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	struct sip_peer *peer;
	struct sip_auth *auth;
	struct cw_codec_pref *pref;
	struct cw_variable *v;
	struct cw_manager_message *msg;
	char *peer_s = cw_manager_msg_header(req, "Peer");
	int x = 0, codec = 0;

	CW_UNUSED(sess);

	if (!cw_strlen_zero(peer_s)) {
		if ((peer = find_peer(peer_s, NULL, 0))) {
			if ((msg = cw_manager_response("Success", NULL))) {
				cw_manager_msg(&msg, 37,
					cw_msg_tuple("Channeltype",       "%s",    "SIP"),
					cw_msg_tuple("ObjectName",        "%s",    peer->name),
					cw_msg_tuple("ChanObjectType",    "%s",    "peer"),
					cw_msg_tuple("SecretExist",       "%c",    (cw_strlen_zero(peer->secret) ? 'N' : 'Y')),
					cw_msg_tuple("MD5SecretExist",    "%c",    (cw_strlen_zero(peer->md5secret) ? 'N' : 'Y')),
					cw_msg_tuple("Context",           "%s",    peer->context),
					cw_msg_tuple("Language",          "%s",    peer->language),
					cw_msg_tuple("Accountcode",       "%s",    (!cw_strlen_zero(peer->accountcode) ? peer->accountcode : "")),
					cw_msg_tuple("AMAflags",          "%s",    cw_cdr_flags2str(peer->amaflags)),
					cw_msg_tuple("CID-CallingPres",   "%s",    cw_describe_caller_presentation(peer->callingpres)),
					cw_msg_tuple("Callerid",          "%s",    cw_callerid_merge(cbuf, sizeof(cbuf), peer->cid_name, peer->cid_num, "")),
					cw_msg_tuple("SIP-FromUser",      "%s",    (!cw_strlen_zero(peer->fromuser) ? peer->fromuser : "")),
					cw_msg_tuple("SIP-FromDomain",    "%s",    (!cw_strlen_zero(peer->fromdomain) ? peer->fromdomain : "")),
					cw_msg_tuple("Callgroup",         "%s",    cw_print_group(callgroup, sizeof(callgroup), peer->callgroup)),
					cw_msg_tuple("Pickupgroup",       "%s",    cw_print_group(pickupgroup, sizeof(pickupgroup), peer->pickupgroup)),
					cw_msg_tuple("VoiceMailbox",      "%s",    peer->mailbox),
					cw_msg_tuple("LastMsgsSent",      "%d",    peer->lastmsgssent),
					cw_msg_tuple("Call limit",        "%d",    peer->call_limit),
					cw_msg_tuple("Dynamic",           "%s",    (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) ? "Y" : "N")),
					cw_msg_tuple("RegExpire",         "%ld"  , cw_sched_when(sched, peer->expire)),
					cw_msg_tuple("SIP-AuthInsecure",  "%s",    insecure2str(cw_test_flag(peer, SIP_INSECURE_PORT), cw_test_flag(peer, SIP_INSECURE_INVITE))),
					cw_msg_tuple("SIP-NatSupport",    "%s",    nat2str(cw_test_flag(peer, SIP_NAT))),
					cw_msg_tuple("SIP-CanReinvite",   "%c",    (cw_test_flag(peer, SIP_CAN_REINVITE) ? 'Y' : 'N')),
					cw_msg_tuple("SIP-PromiscRedir",  "%c",    (cw_test_flag(peer, SIP_PROMISCREDIR) ? 'Y' : 'N')),
					cw_msg_tuple("SIP-UserPhone",     "%c",    (cw_test_flag(peer, SIP_USEREQPHONE) ? 'Y' : 'N')),
					cw_msg_tuple("SIP-DTMFmode",      "%s",    dtmfmode2str(cw_test_flag(peer, SIP_DTMF))),
					cw_msg_tuple("SIPLastMsg",        "%d",    peer->lastmsg),
					cw_msg_tuple("ToHost",            "%s",    peer->tohost),
					cw_msg_tuple("Address-IP",        "%@",    &peer->addr.sa),
					cw_msg_tuple("Address-port",      "%h@",   &peer->addr.sa),
					cw_msg_tuple("Default-addr-IP",   "%@",    &peer->defaddr.sa),
					cw_msg_tuple("Default-addr-port", "%h@",   &peer->defaddr.sa),
					cw_msg_tuple("Default-Username",  "%s",    peer->username),
					cw_msg_tuple("Status",            "%s",    peer_status(peer)),
					cw_msg_tuple("RTT",               "%dms",  peer->timer_t1),
					cw_msg_tuple("SIP-Useragent",     "%s",    peer->useragent),
					cw_msg_tuple("Reg-Contact",       "%s",    peer->fullcontact)
				);

				if (msg) {
					cw_acl_print(&ds, peer->acl);
					cw_manager_msg(&msg, 1, cw_msg_tuple("ACL", "%s", ds.data));
					cw_dynstr_reset(&ds);
				}

				if (msg) {
					cw_getformatname_multiple(&ds, peer->capability);
					cw_manager_msg(&msg, 1, cw_msg_tuple("Codecs", "%s", ds.data));
					cw_dynstr_reset(&ds);
				}

				if (msg) {
					pref = &peer->prefs;
					for (x = 0;  x < 32;  x++) {
						if (!(codec = cw_codec_pref_index(pref, x)))
							break;
						cw_dynstr_printf(&ds, ",%s", cw_getformatname(codec));
					}

					cw_manager_msg(&msg, 1, cw_msg_tuple("CodecOrder", "%s", ds.data));
				}

				cw_dynstr_free(&ds);

				if (msg) {
					for (auth = peer->auth; msg && auth; auth = auth->next) {
						cw_manager_msg(&msg, 1,
							cw_msg_tuple("Realm-auth", "Realm %-15.15s User %-10.20s %s", auth->realm, auth->username, (!cw_strlen_zero(auth->secret) ? "<Secret set>" : (!cw_strlen_zero(auth->md5secret) ? "<MD5secret set>" : "<Not set>"))));
					}
				}

				if (msg && peer->chanvars) {
					for (v = peer->chanvars; msg && v; v = v->next)
						cw_manager_msg(&msg, 1, cw_msg_tuple("ChanVariable", "%s,%s", v->name, v->value));
				}

				cw_object_put(peer);
			}
		} else
			msg = cw_manager_response("Error", "Peer not found");
	} else
		msg = cw_manager_response("Error", "Peer: <name> missing.\n");

	return msg;
}


/*! \brief  sip_show_user: Show one user in detail */
static int sip_show_user(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    char callgroup[256], pickupgroup[256];
    char cbuf[256];
    struct sip_user *user;
    struct cw_codec_pref *pref;
    struct cw_variable *v;
    int x = 0, codec = 0, load_realtime = 0;

    if (argc < 4)
        return RESULT_SHOWUSAGE;

    /* Load from realtime storage? */
    load_realtime = (argc == 5  &&  !strcmp(argv[4], "load"))  ?  1  :  0;

    user = find_user(argv[3], load_realtime);
    if (user)
    {
        cw_dynstr_tprintf(ds_p, 13,
            cw_fmtval("\n\n"),
            cw_fmtval("  * Name       : %s\n", user->name),
            cw_fmtval("  Secret       : %s\n", cw_strlen_zero(user->secret)?"<Not set>":"<Set>"),
            cw_fmtval("  MD5Secret    : %s\n", cw_strlen_zero(user->md5secret)?"<Not set>":"<Set>"),
            cw_fmtval("  Context      : %s\n", user->context),
            cw_fmtval("  Language     : %s\n", user->language),
            cw_fmtval("  Accountcode  : %s\n", (!cw_strlen_zero(user->accountcode) ? user->accountcode : "")),
            cw_fmtval("  AMA flags    : %s\n", cw_cdr_flags2str(user->amaflags)),
            cw_fmtval("  CallingPres  : %s\n", cw_describe_caller_presentation(user->callingpres)),
            cw_fmtval("  Call limit   : %d\n", user->call_limit),
            cw_fmtval("  Callgroup    : %s\n", cw_print_group(callgroup, sizeof(callgroup), user->callgroup)),
            cw_fmtval("  Pickupgroup  : %s\n", cw_print_group(pickupgroup, sizeof(pickupgroup), user->pickupgroup)),
            cw_fmtval("  Callerid     : %s\n", cw_callerid_merge(cbuf, sizeof(cbuf), user->cid_name, user->cid_num, "<unspecified>"))
	);

        cw_dynstr_printf(ds_p, "  ACL          : ");
        cw_acl_print(ds_p, user->acl);

        cw_dynstr_printf(ds_p, "\n  Codec Order  : (");
        pref = &user->prefs;
        for (x = 0;  x < 32;  x++)
        {
            codec = cw_codec_pref_index(pref,x);
            if (!codec)
                break;
            cw_dynstr_printf(ds_p, "%s", cw_getformatname(codec));
            if (x < 31 && cw_codec_pref_index(pref,x+1))
                cw_dynstr_printf(ds_p, ",");
        }
        cw_dynstr_printf(ds_p, ")\n");

        if (user->chanvars)
        {
            cw_dynstr_printf(ds_p, "  Variables    :\n");
            for (v = user->chanvars;  v;  v = v->next)
                cw_dynstr_printf(ds_p, "                 %s = %s\n", v->name, v->value);
        }
        cw_dynstr_printf(ds_p,"\n");

        cw_object_put(user);
    }
    else
    {
        cw_dynstr_printf(ds_p,"User %s not found.\n\n", argv[3]);
    }

    return RESULT_SUCCESS;
}

/*! \brief  sip_show_registry: Show SIP Registry (registrations with other SIP proxies */
static int sip_show_registry(struct cw_dynstr *ds_p, int argc, char *argv[])
{
#define FORMAT2 "%-30.30s  %-12.12s  %8.8s %-20.20s\n"
#define FORMAT  "%-30.30s  %-12.12s  %8d %-20.20s\n"
    char host[80];
    struct sip_registry *reg;

    CW_UNUSED(argv);

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    cw_dynstr_printf(ds_p, FORMAT2, "Host", "Username", "Refresh", "State");

    pthread_rwlock_rdlock(&sip_reload_lock);

    for (reg = regl; reg; reg = reg->next) {
        snprintf(host, sizeof(host), "%s:%d", reg->hostname, (reg->portno ? reg->portno : DEFAULT_SIP_PORT));
        cw_dynstr_printf(ds_p, FORMAT, host, reg->username, reg->refresh, regstate2str(reg->regstate));
    }

    pthread_rwlock_unlock(&sip_reload_lock);

    return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

/*! \brief  sip_show_settings: List global settings for the SIP channel */
static int sip_show_settings(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    int realtimepeers = 0;
    int realtimeusers = 0;

    CW_UNUSED(argv);

    realtimepeers = cw_check_realtime("sippeers");
    realtimeusers = cw_check_realtime("sipusers");

    if (argc != 3)
        return RESULT_SHOWUSAGE;

    cw_dynstr_tprintf(ds_p, 24,
        cw_fmtval("\n\nGlobal Settings:\n"),
        cw_fmtval("----------------\n"),
        cw_fmtval("  Videosupport:           %s\n", videosupport ? "Yes" : "No"),
        cw_fmtval("  T.38 UDPTL Support:     %s\n", t38udptlsupport ? "Yes" : "No"),
        cw_fmtval("  AutoCreatePeer:         %s\n", autocreatepeer ? "Yes" : "No"),
        cw_fmtval("  Allow unknown access:   %s\n", global_allowguest ? "Yes" : "No"),
        cw_fmtval("  Promsic. redir:         %s\n", cw_test_flag(&global_flags, SIP_PROMISCREDIR) ? "Yes" : "No"),
        cw_fmtval("  SIP domain support:     %s\n", CW_LIST_EMPTY(&domain_list) ? "No" : "Yes"),
        cw_fmtval("  Call to non-local dom.: %s\n", allow_external_domains ? "Yes" : "No"),
        cw_fmtval("  URI user is phone no:   %s\n", cw_test_flag(&global_flags, SIP_USEREQPHONE) ? "Yes" : "No"),
        cw_fmtval("  Our auth realm          %s\n", global_realm),
        cw_fmtval("  Realm. auth:            %s\n", authl ? "Yes": "No"),
        cw_fmtval("  User Agent:             %s\n", default_useragent),
        cw_fmtval("  MWI checking interval:  %d secs\n", global_mwitime),
        cw_fmtval("  Reg. context:           %s\n", cw_strlen_zero(regcontext) ? "(not set)" : regcontext),
        cw_fmtval("  Caller ID:              %s\n", default_callerid),
        cw_fmtval("  From: Domain:           %s\n", default_fromdomain),
        cw_fmtval("  Call Events:            %s\n", callevents ? "On" : "Off"),
        cw_fmtval("  IP ToS:                 0x%x\n", tos),
#ifdef OSP_SUPPORT
        cw_fmtval("  OSP Support:            Yes\n"),
#else
        cw_fmtval("  OSP Support:            No\n"),
#endif
        cw_fmtval("  SIP realtime:           %s\n", (!realtimepeers && !realtimeusers ? "Disabled" : "Enabled")),

        cw_fmtval("\nGlobal Signalling Settings:\n"),
        cw_fmtval("---------------------------\n"),
        cw_fmtval("  Codecs:                 ")
    );
    print_codec_to_cli(ds_p, &prefs);
    cw_dynstr_tprintf(ds_p, 24,
        cw_fmtval("\n"),
        cw_fmtval("  Relax DTMF:             %s\n", relaxdtmf ? "Yes" : "No"),
        cw_fmtval("  Compact SIP headers:    %s\n", (sip_hdr_name == sip_hdr_shortname ? "Yes" : "No")),
        cw_fmtval("  RTP Timeout:            %d %s\n", global_rtptimeout, global_rtptimeout ? "" : "(Disabled)" ),
        cw_fmtval("  RTP Hold Timeout:       %d %s\n", global_rtpholdtimeout, global_rtpholdtimeout ? "" : "(Disabled)"),
        cw_fmtval("  MWI NOTIFY mime type:   %s\n", default_notifymime),
        cw_fmtval("  DNS SRV lookup:         %s\n", srvlookup ? "Yes" : "No"),
        cw_fmtval("  Pedantic SIP support:   %s\n", pedanticsipchecking ? "Yes" : "No"),
        cw_fmtval("  Reg. max duration:      %d secs\n", max_expiry),
        cw_fmtval("  Reg. default duration:  %d secs\n", default_expiry),
        cw_fmtval("  Outbound reg. timeout:  %d secs\n", global_reg_timeout),
        cw_fmtval("  Outbound reg. attempts: %d\n", global_regattempts_max),
        cw_fmtval("  Notify ringing state:   %s\n", global_notifyringing ? "Yes" : "No"),
        cw_fmtval("\nDefault Settings:\n"),
        cw_fmtval("-----------------\n"),
        cw_fmtval("  Context:                %s\n", default_context),
        cw_fmtval("  Nat:                    %s\n", nat2str(cw_test_flag(&global_flags, SIP_NAT))),
        cw_fmtval("  DTMF:                   %s\n", dtmfmode2str(cw_test_flag(&global_flags, SIP_DTMF))),
        cw_fmtval("  Qualify:                %d\n", default_qualify),
        cw_fmtval("  Use ClientCode:         %s\n", cw_test_flag(&global_flags, SIP_USECLIENTCODE) ? "Yes" : "No"),
        cw_fmtval("  Progress inband:        %s\n", (cw_test_flag(&global_flags, SIP_PROG_INBAND) == SIP_PROG_INBAND_NEVER) ? "Never" : (cw_test_flag(&global_flags, SIP_PROG_INBAND) == SIP_PROG_INBAND_NO) ? "No" : "Yes" ),
        cw_fmtval("  Language:               %s\n", cw_strlen_zero(default_language) ? "(Defaults to English)" : default_language),
        cw_fmtval("  Musicclass:             %s\n", global_musicclass),
        cw_fmtval("  Voice Mail Extension:   %s\n", global_vmexten)
    );

    if (realtimepeers || realtimeusers)
    {
        cw_dynstr_tprintf(ds_p, 8,
            cw_fmtval("\nRealtime SIP Settings:\n"),
            cw_fmtval("----------------------\n"),
            cw_fmtval("  Realtime Peers:         %s\n", (realtimepeers ? "Yes" : "No")),
            cw_fmtval("  Realtime Users:         %s\n", (realtimeusers ? "Yes" : "No")),
            cw_fmtval("  Cache Friends:          %s\n", (cw_test_flag(&global_flags_page2, SIP_PAGE2_RTCACHEFRIENDS) ? "Yes" : "No")),
            cw_fmtval("  Update:                 %s\n", (cw_test_flag(&global_flags_page2, SIP_PAGE2_RTUPDATE) ? "Yes" : "No")),
            cw_fmtval("  Ignore Reg. Expire:     %s\n", (cw_test_flag(&global_flags_page2, SIP_PAGE2_IGNOREREGEXPIRE) ? "Yes" : "No")),
            cw_fmtval("  Auto Clear:             %d\n", global_rtautoclear)
        );
    }

    cw_dynstr_printf(ds_p, "\n----\n");
    return RESULT_SUCCESS;
}

/*! \brief  subscription_type2str: Show subscription type in string format */
static const char *subscription_type2str(enum subscriptiontype subtype)
{
    int i;

    for (i = 1; (i < (sizeof(subscription_types) / sizeof(subscription_types[0]))); i++)
    {
        if (subscription_types[i].type == subtype)
        {
            return subscription_types[i].text;
        }
    }
    return subscription_types[0].text;
}

/*! \brief  find_subscription_type: Find subscription type in array */
static const struct cfsubscription_types *find_subscription_type(enum subscriptiontype subtype)
{
    int i;

    for (i = 1; (i < (sizeof(subscription_types) / sizeof(subscription_types[0]))); i++)
    {
        if (subscription_types[i].type == subtype)
        {
            return &subscription_types[i];
        }
    }
    return &subscription_types[0];
}


struct sip_show_channels_args {
	struct cw_dynstr *ds_p;
	int numchans;
};

#define SHOW_CHANNELS_FORMAT_HEADER "%-15.15s  %-10.10s  %-11.11s  %-11.11s  %-4.4s  %-7.7s  %-15.15s\n"
#define SHOW_CHANNELS_FORMAT_DETAIL  "%-15l@  %-10.10s  %-11.11s  %5.5d/%5.5d  %-4.4s  %-7.7s  %-15.15s\n"

static int sip_show_channels_one(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct sip_show_channels_args *args = data;

	if (dialogue->subscribed == NONE) {
		cw_dynstr_printf(args->ds_p, SHOW_CHANNELS_FORMAT_DETAIL,
			&dialogue->peeraddr.sa,
			(cw_strlen_zero(dialogue->username)
				? (cw_strlen_zero(dialogue->cid_num) ? "(None)" : dialogue->cid_num)
				: dialogue->username),
			dialogue->callid,
			dialogue->ocseq, dialogue->icseq,
			(dialogue->t38state == SIP_T38_NEGOTIATED
				? "T38"
				: (cw_getformatname(dialogue->owner ? dialogue->owner->nativeformats : 0))),
			(cw_test_flag(dialogue, SIP_CALL_ONHOLD) ? "Yes" : "No"),
			dialogue->lastmsg);
		args->numchans++;
	}

	return 0;
}

/*! \brief  sip_show_channels: Show active SIP channels */
static int sip_show_channels(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_show_channels_args args = {
		.ds_p = ds_p,
		.numchans = 0,
	};

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_dynstr_printf(ds_p, SHOW_CHANNELS_FORMAT_HEADER, "Peer", "User/ANR", "Call ID", "Seq (Tx/Rx)", "Format", "Hold", "Last Message");

	cw_registry_iterate_ordered(&dialogue_registry, sip_show_channels_one, &args);

	cw_dynstr_printf(ds_p, "%d active SIP channel%s\n", args.numchans, (args.numchans != 1) ? "s" : "");

	return RESULT_SUCCESS;
}

#define SHOW_CHANNELS_SUBS_FORMAT_HEADER "%-15.15s  %-10.10s  %-11.11s  %-15.15s  %-13.13s  %-15.15s\n"
#define SHOW_CHANNELS_SUBS_FORMAT_DETAIL "%-15l@  %-10.10s  %-11.11s  %-15.15s  %-13.13s  %-15.15s\n"

static int sip_show_channels_subs_one(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct sip_show_channels_args *args = data;

	if (dialogue->subscribed != NONE) {
		cw_dynstr_printf(args->ds_p, SHOW_CHANNELS_SUBS_FORMAT_DETAIL,
			&dialogue->peeraddr.sa,
			(cw_strlen_zero(dialogue->username)
				? (cw_strlen_zero(dialogue->cid_num) ? "(None)" : dialogue->cid_num)
				: dialogue->username),
			dialogue->callid,
			dialogue->exten,
			cw_extension_state2str(dialogue->laststate),
			subscription_type2str(dialogue->subscribed));
		args->numchans++;
	}

	return 0;
}
 
/*! \brief  sip_show_subscriptions: Show active SIP subscriptions */
static int sip_show_subscriptions(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_show_channels_args args = {
		.ds_p = ds_p,
		.numchans = 0,
	};

	CW_UNUSED(argv);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	cw_dynstr_printf(ds_p, SHOW_CHANNELS_SUBS_FORMAT_HEADER, "Peer", "User", "Call ID", "Extension", "Last state", "Type");

	cw_registry_iterate_ordered(&dialogue_registry, sip_show_channels_subs_one, &args);

	cw_dynstr_printf(ds_p, "%d active SIP subscription%s\n", args.numchans, (args.numchans != 1) ? "s" : "");

	return RESULT_SUCCESS;
}



struct complete_sipch_args {
	struct cw_dynstr *ds_p;
	const char *prefix;
	int prefix_len;
};

static int complete_sipch_one(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct complete_sipch_args *args = data;

        if (!strncasecmp(args->prefix, dialogue->callid, args->prefix_len))
		cw_dynstr_printf(args->ds_p, "%s\n", dialogue->callid);

	return 0;
}

/*! \brief  complete_sipch: Support routine for 'sip show channel' CLI */
static void complete_sipch(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
	struct complete_sipch_args args = {
		.ds_p = ds_p,
		.prefix = argv[lastarg],
		.prefix_len = lastarg_len,
	};

	cw_registry_iterate(&dialogue_registry, complete_sipch_one, &args);
}


struct complete_sip_peer_args {
	struct cw_dynstr *ds_p;
	char *word;
	int word_len;
	int flags2;
};

static int complete_sip_peer_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct complete_sip_peer_args *args = data;

	if (!strncasecmp(args->word, peer->name, args->word_len)) {
		if (!args->flags2 || cw_test_flag(&peer->flags_page2, args->flags2))
			cw_dynstr_printf(args->ds_p, "%s\n", peer->name);
	}

	return 0;
}

/*! \brief  complete_sip_peer: Do completion on peer name */
static void complete_sip_peer(struct cw_dynstr *ds_p, char *word, int word_len, int flags2)
{
	struct complete_sip_peer_args args = {
		.ds_p = ds_p,
		.word = word,
		.word_len = word_len,
		.flags2 = flags2,
	};

	cw_registry_iterate(&peerbyname_registry, complete_sip_peer_one, &args);
}

/*! \brief  complete_sip_show_peer: Support routine for 'sip show peer' CLI */
static void complete_sip_show_peer(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 3)
        complete_sip_peer(ds_p, argv[3], lastarg_len, 0);
}


/*! \brief  complete_sip_debug_peer: Support routine for 'sip debug peer' CLI */
static void complete_sip_debug_peer(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 3)
         complete_sip_peer(ds_p, argv[3], lastarg_len, 0);
}


struct complete_sip_user_args {
	struct cw_dynstr *ds_p;
	char *word;
	int word_len;
	int flags2;
};

static int complete_sip_user_one(struct cw_object *obj, void *data)
{
	struct sip_user *user = container_of(obj, struct sip_user, obj);
	struct complete_sip_user_args *args = data;

	if (!strncasecmp(args->word, user->name, args->word_len)) {
		if (!args->flags2 || cw_test_flag(&user->flags_page2, args->flags2))
			cw_dynstr_printf(args->ds_p, "%s\n", user->name);
	}

	return 0;
}

/*! \brief  complete_sip_user: Do completion on user name */
static void complete_sip_user(struct cw_dynstr *ds_p, char *word, int word_len, int flags2)
{
	struct complete_sip_user_args args = {
		.ds_p = ds_p,
		.word = word,
		.word_len = word_len,
		.flags2 = flags2,
	};

	cw_registry_iterate(&userbyname_registry, complete_sip_user_one, &args);
}


/*! \brief  complete_sip_show_user: Support routine for 'sip show user' CLI */
static void complete_sip_show_user(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 3)
        complete_sip_user(ds_p, argv[3], lastarg_len, 0);
}


/*! \brief  complete_sipnotify: Support routine for 'sip notify' CLI */
static void complete_sipnotify(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 2)
    {
        char *cat;

        /* do completion for notify type */

        if (notify_types)
	{
            for (cat = cw_category_browse(notify_types, NULL); cat; cat = cw_category_browse(notify_types, cat))
            {
                if (!strncasecmp(argv[2], cat, lastarg_len))
                    cw_dynstr_printf(ds_p, "%s\n", cat);
            }
        }
    }
    else if (lastarg > 2)
        complete_sip_peer(ds_p, argv[lastarg], lastarg_len, 0);
}

/*! \brief  complete_sip_prune_realtime_peer: Support routine for 'sip prune realtime peer' CLI */
static void complete_sip_prune_realtime_peer(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 4)
        complete_sip_peer(ds_p, argv[4], lastarg_len, SIP_PAGE2_RTCACHEFRIENDS);
}

/*! \brief  complete_sip_prune_realtime_user: Support routine for 'sip prune realtime user' CLI */
static void complete_sip_prune_realtime_user(struct cw_dynstr *ds_p, char *argv[], int lastarg, int lastarg_len)
{
    if (lastarg == 4)
        complete_sip_user(ds_p, argv[4], lastarg_len, SIP_PAGE2_RTCACHEFRIENDS);
}


struct sip_show_channel_args {
	struct cw_dynstr *ds_p;
	int found;
	const char *prefix;
	size_t prefix_len;
};

static int sip_show_channel_one(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct sip_show_channel_args *args = data;

	if (!strncasecmp(dialogue->callid, args->prefix, args->prefix_len)) {
		cw_dynstr_printf(args->ds_p,"\n");

		if (dialogue->subscribed != NONE)
			cw_dynstr_printf(args->ds_p, "  * Subscription (type: %s)\n", subscription_type2str(dialogue->subscribed));
		else
			cw_dynstr_printf(args->ds_p, "  * SIP Call\n");

		cw_dynstr_tprintf(args->ds_p, 14,
			cw_fmtval("  Our Address (local):    %l@\n", &dialogue->ouraddr.sa),
			cw_fmtval("  Our Address (external): %l@\n", &dialogue->stunaddr.sa),
			cw_fmtval("  Peer Address:           %l@\n", &dialogue->peeraddr.sa),

			cw_fmtval("  Direction:              %s\n", (cw_test_flag(dialogue, SIP_OUTGOING) ? "Outgoing" : "Incoming")),
			cw_fmtval("  Call-ID:                %s\n", dialogue->callid),
			cw_fmtval("  Our Codec Capability:   %d\n", dialogue->capability),
			cw_fmtval("  Non-Codec Capability:   %d\n", dialogue->noncodeccapability),
			cw_fmtval("  Their Codec Capability: %d\n", dialogue->peercapability),
			cw_fmtval("  Joint Codec Capability: %d\n", dialogue->jointcapability),
			cw_fmtval("  Format:                 %s\n", cw_getformatname(dialogue->owner ? dialogue->owner->nativeformats : 0)),
			cw_fmtval("  NAT Support:            %s\n", nat2str(cw_test_flag(dialogue, SIP_NAT))),
			cw_fmtval("  Our Tag:                %s\n", dialogue->tag),
			cw_fmtval("  Their Tag:              %s\n", dialogue->theirtag),
			cw_fmtval("  SIP User agent:         %s\n", dialogue->useragent)
		);

		if (cw_sockaddr_is_specific(&dialogue->redirip.sa))
			cw_dynstr_printf(args->ds_p, "  Audio IP:               %l@ (Outside bridge)\n", &dialogue->redirip.sa);
		else
			cw_dynstr_printf(args->ds_p, "  Audio IP:               %l@ (local)\n", &dialogue->stunaddr.sa);

		if (!cw_strlen_zero(dialogue->username))
			cw_dynstr_printf(args->ds_p, "  Username:               %s\n", dialogue->username);
		if (!cw_strlen_zero(dialogue->peername))
			cw_dynstr_printf(args->ds_p, "  Peername:               %s\n", dialogue->peername);
		if (!cw_strlen_zero(dialogue->uri))
			cw_dynstr_printf(args->ds_p, "  Original uri:           %s\n", dialogue->uri);
		if (!cw_strlen_zero(dialogue->cid_num))
			cw_dynstr_printf(args->ds_p, "  Caller-ID:              %s\n", dialogue->cid_num);

		cw_dynstr_tprintf(args->ds_p, 7,
			cw_fmtval("  Last Message:           %s\n", dialogue->lastmsg),
			cw_fmtval("  Promiscuous Redir:      %s\n", (cw_test_flag(dialogue, SIP_PROMISCREDIR) ? "Yes" : "No")),
			cw_fmtval("  Route:                  %s\n", (dialogue->route ? dialogue->route->hop : "N/A")),
			cw_fmtval("  T38 State:              %d\n", dialogue->t38state),
			cw_fmtval("  DTMF Mode:              %s\n", dtmfmode2str(cw_test_flag(dialogue, SIP_DTMF))),
			cw_fmtval("  On HOLD:                %s\n", (cw_test_flag(dialogue, SIP_CALL_ONHOLD) ? "Yes" : "No")),
			cw_fmtval("  SIP Options:            ")
		);

		if (dialogue->sipoptions) {
			int x;

			for (x = 0 ; (x < (sizeof(sip_options) / sizeof(sip_options[0]))); x++) {
				if ((dialogue->sipoptions & sip_options[x].id))
					cw_dynstr_printf(args->ds_p, "%s ", sip_options[x].text);
			}
		} else
			cw_dynstr_printf(args->ds_p, "(none)\n");

		cw_dynstr_printf(args->ds_p, "\n\n");
		args->found++;
	}

	return 0;
}

/*! \brief  sip_show_channel: Show details of one call */
static int sip_show_channel(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct sip_show_channel_args args = {
		.ds_p = ds_p,
		.found = 0,
	};

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	args.prefix = argv[3];
	args.prefix_len = strlen(argv[3]);

	cw_registry_iterate_ordered(&dialogue_registry, sip_show_channel_one, &args);

	if (!args.found)
		cw_dynstr_printf(ds_p, "No such SIP Call ID starting with '%s'\n", argv[3]);

	return RESULT_SUCCESS;
}


static int info_dtmf(struct sip_pvt *dialogue, struct sip_request *req, int ignore, int start, int end)
{
	struct cw_frame f;
	int i, j, event, duration;
	char c;

	if (!ignore && dialogue->owner) {
		duration = event = -1;

		for (i = start; i < end; ) {
			if (sscanf(&req->pkt.data[i], "Signal = %n%c", &j, &c) > 0 || sscanf(&req->pkt.data[i], "d = %n%c", &j, &c) > 0) {
				i += j;
				if (c == '*')
					event = 10;
				else if (c == '#')
					event = 11;
				else if (c >= 'A' && c <= 'D')
					event = 12 + c - 'A';
				else if (c == '!')
					event = 16;
				else
					event = atoi(&req->pkt.data[i]);
			} else
				sscanf(&req->pkt.data[i], "Duration = %d", &duration);

			for (i += strcspn(&req->pkt.data[i], "\r\n"); i < end && strchr("\r\n", req->pkt.data[i]); i++);
		}


		if (event >= 0) {
			if (event <= 16) {
				if (event != 16)
					cw_fr_init_ex(&f, CW_FRAME_DTMF, "0123456789*#ABCD"[event]);
				else
					cw_fr_init_ex(&f, CW_FRAME_CONTROL, CW_CONTROL_FLASH);

				f.duration = (duration > 0 ? duration : 100);
				cw_queue_frame(dialogue->owner, &f);

				if (sipdebug && option_verbose > 2)
					cw_verbose("* DTMF-relay event received: %c%s duration %d\n",
						(event == 16 ? 'F' : f.subclass),
						(event == 16 ? "LASH" : ""),
						f.duration
					);
			}
		} else
			cw_log(CW_LOG_WARNING, "Unable to retrieve DTMF signal from INFO message from %s\n", dialogue->callid);
	}

	return 0;
}


static int info_mediaxml(struct sip_pvt *dialogue, struct sip_request *req, int ignore, int start, int end)
{
	CW_UNUSED(req);
	CW_UNUSED(start);
	CW_UNUSED(end);

	/* Eh, we'll just assume it's a fast picture update for now */
	if (dialogue->owner && !ignore)
		cw_queue_control(dialogue->owner, CW_CONTROL_VIDUPDATE);

	return 0;
}


static int info_unknown(struct sip_pvt *dialogue, struct sip_request *req, int ignore, int start, int end)
{
	CW_UNUSED(dialogue);
	CW_UNUSED(req);
	CW_UNUSED(ignore);
	CW_UNUSED(start);
	CW_UNUSED(end);

	return -1;
}


/*! \brief  handle_request_info: Receive SIP INFO Message */
/*    Doesn't read the duration of the DTMF signal */
static void handle_request_info(struct sip_pvt *dialogue, struct sip_request *req, int ignore)
{
	static const struct cw_mime_process_action actions[] = {
		{ "application/dtmf-relay",                sizeof("application/dtmf-relay") - 1,                info_dtmf },
		{ "application/vnd.nortelnetworks.digits", sizeof("application/vnd.nortelnetworks.digits") - 1, info_dtmf },
		{ "application/media_control+xml",         sizeof("application/media_control+xml") - 1,         info_mediaxml },
		{ "",                                      0,                                                   info_unknown }
	};
	int processed;

	/* RFC 2976 2.2:
	 *
	 * A 200 OK response MUST be sent by a UAS for an INFO request with no message
	 * body if the INFO request was successfully received for an existing call.
	 *
	 * If a server receives an INFO request with a body it understands, but it has
	 * no knowledge of INFO associated processing rules for the body, the body MAY
	 * be rendered and displayed to the user. The INFO is responded to with a
	 * 200 OK.
	 *
	 * If the INFO request contains a body that the server does not understand then, in the
	 * absence of INFO associated processing rules for the body, the server MUST respond
	 * with a 415 Unsupported Media Type message.
	 */
	processed = 0;
	if (req->body_start == req->pkt.used) {
		const char *cmc;

		/* Client Matter Code (from SNOM phone) */
		if (!(cmc = get_header(req, SIP_HDR_NOSHORT("X-ClientCode"))) || !cmc[0]
		|| cw_test_flag(dialogue, SIP_USECLIENTCODE)) {
			if (cmc && cmc[0] && !ignore) {
				struct cw_channel *bchan;

				if (dialogue->owner && dialogue->owner->cdr)
					cw_cdr_setuserfield(dialogue->owner, cmc);
				if (dialogue->owner && (bchan = cw_bridged_channel(dialogue->owner))) {
					if (bchan->cdr)
						cw_cdr_setuserfield(bchan, cmc);
					cw_object_put(bchan);
				}
			}
			transmit_response(dialogue, "200 OK", req, 0);
		} else
			transmit_response(dialogue, "403 Unauthorized", req, 0);
	} else if ((processed = mime_process(dialogue, req, ignore, actions, arraysize(actions))) >= 0)
		transmit_response(dialogue, "200 OK", req, 0);
	else {
		if (!ignore)
			cw_log(CW_LOG_WARNING, "Unable to parse INFO message from %s.\n", dialogue->callid);

		transmit_response(dialogue, "415 Unsupported media type", req, 0);
	}
}


static void handle_request_publish(struct sip_pvt *dialogue, struct sip_request *req, int ignore)
{
	CW_UNUSED(ignore);

	/* We don't currently parse publish messages but regardless
	 * of what they were trying to tell us - we heard them.
	 */
	transmit_response(dialogue, "200 OK", req, 0);
}


/*! \brief  sip_do_debug: Enable SIP Debugging in CLI */
static int sip_do_debug_ip(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int i;

	pthread_rwlock_wrlock(&debugacl.lock);

	for (i = 3; i < argc; i++) {
		int err;

		if ((err = cw_acl_add(&debugacl.acl, "p", argv[i])))
			cw_dynstr_printf(ds_p, "%s: %s\n", argv[i], gai_strerror(err));
	}

	pthread_rwlock_unlock(&debugacl.lock);

	return RESULT_SUCCESS;
}

/*! \brief  sip_do_debug_peer: Turn on SIP debugging with peer mask */
static int sip_do_debug_peer(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int i;

	pthread_rwlock_wrlock(&debugacl.lock);

	for (i = 3; i < argc; i++) {
		struct sip_peer *peer;

		if ((peer = find_peer(argv[i], NULL, 1))) {
			int err;

			if ((err = cw_acl_add_addr(&debugacl.acl, "p", &peer->addr.sa, sizeof(peer->addr), -1)))
				cw_dynstr_printf(ds_p, "%s: %s\n", argv[i], gai_strerror(err));

			cw_object_put(peer);
		}
	}

	pthread_rwlock_unlock(&debugacl.lock);

	return RESULT_SUCCESS;
}


/*! \brief  sip_do_debug_show: Show SIP debugging ACLs (CLI command) */
static void sip_do_debug_show(struct cw_dynstr *ds_p)
{
	cw_dynstr_printf(ds_p, "Global debug is %s\nMessage debug ACLs:\n", (sipdebug ? "ON" : "OFF"));

	while (pthread_rwlock_rdlock(&debugacl.lock) == EAGAIN)
		usleep(1000);

	cw_acl_print(ds_p, debugacl.acl);

	pthread_rwlock_unlock(&debugacl.lock);
}

/*! \brief  sip_do_debug: Turn on SIP debugging (CLI command) */
static int sip_do_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res = RESULT_SHOWUSAGE;

	if (argc == 2) {
		sipdebug = 1;
		cw_dynstr_printf(ds_p, "SIP Debugging enabled\n");
		res = RESULT_SUCCESS;
	} else if (argc == 3 && !strcmp(argv[2], "show")) {
		sip_do_debug_show(ds_p);
		res = RESULT_SUCCESS;
	} else if (argc > 3) {
		if (!strcmp(argv[2], "ip"))
			res = sip_do_debug_ip(ds_p, argc, argv);
		else if (!strcmp(argv[2], "peer"))
			res = sip_do_debug_peer(ds_p, argc, argv);
	}

	return res;
}

/*! \brief  sip_notify: Send SIP notify to peer */
static int sip_notify(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    struct cw_variable *varlist;
    int i;

    if (argc < 4)
        return RESULT_SHOWUSAGE;

    if (!notify_types)
    {
        cw_dynstr_printf(ds_p, "No %s file found, or no types listed there\n", notify_config);
        return RESULT_FAILURE;
    }

    varlist = cw_variable_browse(notify_types, argv[2]);

    if (!varlist)
    {
        cw_dynstr_printf(ds_p, "Unable to find notify type '%s'\n", argv[2]);
        return RESULT_FAILURE;
    }

    for (i = 3;  i < argc;  i++)
    {
        struct sip_pvt *p;
        struct sip_request req;
        struct cw_variable *var;

        p = sip_alloc();
        if (!p)
        {
            cw_log(CW_LOG_WARNING, "Unable to build sip pvt data for notify\n");
            return RESULT_FAILURE;
        }

        cw_copy_string(p->fromdomain, default_fromdomain, sizeof(p->fromdomain));

        if (create_addr(p, argv[i], NULL, 0))
        {
            /* Maybe they're not registered, etc. */
            sip_destroy(p);
            cw_object_put(p);
            cw_dynstr_printf(ds_p, "Could not create address for '%s'\n", argv[i]);
            continue;
        }

        initreqprep(&req, p, SIP_NOTIFY);

        for (var = varlist; var; var = var->next)
            cw_dynstr_printf(&req.pkt, "%s: %s\r\n", sip_hdr_generic(var->name), var->value);

	cw_dynstr_printf(&req.pkt, "\r\n");

        p->reg_entry = cw_registry_add(&dialogue_registry, dialogue_hash(p), &p->obj);

        cw_dynstr_printf(ds_p, "Sending NOTIFY of type '%s' to '%s'\n", argv[2], argv[i]);
        transmit_sip_request(p, &req);
        sip_scheddestroy(p, -1);
        cw_object_put(p);
    }

    return RESULT_SUCCESS;
}


/*! \brief  sip_no_debug: Disable SIP Debugging in CLI */
static int sip_no_debug(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	int res = RESULT_SHOWUSAGE;

	CW_UNUSED(argv);

	if (argc == 3) {
		pthread_rwlock_wrlock(&debugacl.lock);

		cw_acl_free(debugacl.acl);
		debugacl.acl = NULL;

		pthread_rwlock_unlock(&debugacl.lock);

		sipdebug = 0;

		cw_dynstr_printf(ds_p, "SIP Debugging disabled\n");
		res = RESULT_SUCCESS;
	}

	return res;
}

static int reply_digest(struct sip_pvt *p, struct sip_request *req, const char *header, enum sipmethod sipmethod, struct cw_dynstr  *digest);

/*! \brief  do_register_auth: Authenticate for outbound registration */
static int do_register_auth(struct sip_pvt *p, struct sip_request *req, const char *header, const char *respheader)
{
	struct cw_dynstr digest = CW_DYNSTR_INIT;
	int res = -1;
    
	p->authtries++;
	if (!reply_digest(p, req, header, SIP_REGISTER, &digest)) {
		if (sip_debug_test_pvt(p) && p->registry)
			cw_verbose("Responding to challenge, registration to domain/host name %s\n", p->registry->hostname);
		res = transmit_register(p->registry, SIP_REGISTER, &digest, respheader);
		cw_dynstr_free(&digest);
	} else {
		/* There's nothing to use for authentication */
		/* No digest challenge in request */
		if (sip_debug_test_pvt(p) && p->registry)
			cw_verbose("No authentication challenge, sending blank registration to domain/host name %s\n", p->registry->hostname);
	}
	return res;
}


/*! \brief  do_proxy_auth: Add authentication on outbound SIP packet */
static int do_proxy_auth(struct sip_pvt *p, struct sip_request *req, const char *header, const char *respheader, enum sipmethod sipmethod, int init)
{
	int res = -2;

	if (!p->options && (p->options = calloc(1, sizeof(struct sip_invite_param))))
		cw_dynstr_init(&p->options->auth, 0, CW_DYNSTR_DEFAULT_CHUNK);

	if (p->options) {
		p->authtries++;
		if (option_debug > 1)
			cw_log(CW_LOG_DEBUG, "Auth attempt %d on %s\n", p->authtries, sip_methods[sipmethod].text);

		cw_dynstr_reset(&p->options->auth);
		if (!reply_digest(p, req, header, sipmethod, &p->options->auth) && !p->options->auth.error) {
			p->options->authheader = respheader;
			res = transmit_invite(p, sipmethod, sipmethod == SIP_INVITE, init);
		}
	}

	return res;
}


/*! \brief  reply_digest: reply to authentication for outbound registrations */
/*      This is used for register= servers in sip.conf, SIP proxies we register
        with  for receiving calls from.  */
/*    Returns -1 if we have no auth */
static int reply_digest(struct sip_pvt *p, struct sip_request *req, const char *header, enum sipmethod sipmethod,  struct cw_dynstr *digest)
{
	char tmp[512];
	char oldnonce[256];
	char *c;

	/* table of recognised keywords, and places where they should be copied */
	const struct x {
		const char *key;
		char *dst;
		int dstlen;
	} *i, keys[] = {
		{ "realm=", p->realm, sizeof(p->realm) },
		{ "nonce=", p->nonce, sizeof(p->nonce) },
		{ "opaque=", p->opaque, sizeof(p->opaque) },
		{ "qop=", p->qop, sizeof(p->qop) },
		{ "domain=", p->domain, sizeof(p->domain) },
		{ NULL, NULL, 0 },
	};

	cw_copy_string(tmp, get_header(req, SIP_HDR_GENERIC(header)), sizeof(tmp));
	if (cw_strlen_zero(tmp))
		return -1;
	if (strncasecmp(tmp, "Digest ", strlen("Digest "))) {
		cw_log(CW_LOG_WARNING, "missing Digest.\n");
		return -1;
	}
	c = tmp + strlen("Digest ");
	for (i = keys; i->key != NULL; i++)
		i->dst[0] = '\0';    /* init all to empty strings */
	cw_copy_string(oldnonce, p->nonce, sizeof(oldnonce));
	while (c && *(c = cw_skip_blanks(c))) {
		/* lookup for keys */
		for (i = keys; i->key != NULL; i++) {
			const char *separator;
			char *src;

			if (strncasecmp(c, i->key, strlen(i->key)) != 0)
				continue;
			/* Found. Skip keyword, take text in quotes or up to the separator. */
			c += strlen(i->key);
			if (*c == '\"') {
				src = ++c;
				separator = "\"";
			} else {
				src = c;
				separator = ",";
			}
			strsep(&c, separator); /* clear separator and move ptr */
			cw_copy_string(i->dst, src, i->dstlen);
			break;
		}
		if (i->key == NULL) /* not found, try ',' */
			strsep(&c, ",");
	}
	/* Reset nonce count */
	if (strcmp(p->nonce, oldnonce))
		p->noncecount = 0;

	/* Save auth data for following registrations */
	if (p->registry) {
		struct sip_registry *r = p->registry;

		if (strcmp(r->nonce, p->nonce)) {
			cw_copy_string(r->realm, p->realm, sizeof(r->realm));
			cw_copy_string(r->nonce, p->nonce, sizeof(r->nonce));
			cw_copy_string(r->domain, p->domain, sizeof(r->domain));
			cw_copy_string(r->opaque, p->opaque, sizeof(r->opaque));
			cw_copy_string(r->qop, p->qop, sizeof(r->qop));
			r->noncecount = 0;
		}
	}
	return build_reply_digest(p, sipmethod, digest);
}

/*! \brief  build_reply_digest:  Build reply digest */
/*      Build digest challenge for authentication of peers (for registration)
    and users (for calls). Also used for authentication of CANCEL and BYE */
/*    Returns -1 if we have no auth */
static int build_reply_digest(struct sip_pvt *p, enum sipmethod method, struct cw_dynstr *digest)
{
	char uri[256];
	char a1_hash[CW_MAX_HEX_MD_SIZE];
	char a2_hash[CW_MAX_HEX_MD_SIZE];
	char resp_hash[CW_MAX_HEX_MD_SIZE];
	char cnonce[8 + 1];
	char *username;
	char *secret;
	char *md5secret;
	struct sip_auth *auth = (struct sip_auth *) NULL;    /* Realm authentication */
	size_t mark;
	int res = -1;

	if (!cw_strlen_zero(p->domain))
		cw_copy_string(uri, p->domain, sizeof(uri));
	else if (!cw_strlen_zero(p->uri))
		cw_copy_string(uri, p->uri, sizeof(uri));
	else
		cw_snprintf(uri, sizeof(uri), "sip:%s@%#@", p->username, &p->peeraddr.sa);

	snprintf(cnonce, sizeof(cnonce), "%08lx", cw_random());

	/* Check if we have separate auth credentials */
	if ((auth = find_realm_authentication(authl, p->realm))) {
		username = auth->username;
		secret = auth->secret;
		md5secret = auth->md5secret;
		if (sipdebug)
			cw_log(CW_LOG_DEBUG,"Using realm %s authentication for call %s\n", p->realm, p->callid);
	} else {
		/* No authentication, use peer or register= config */
		username = p->authname;
		secret =  p->peersecret;
		md5secret = p->peermd5secret;
	}

	if (!cw_strlen_zero(username)) {
		mark = digest->used;

		/* Calculate SIP digest response */
		if (!cw_strlen_zero(md5secret))
			cw_copy_string(a1_hash, md5secret, sizeof(a1_hash));
		else {
			cw_dynstr_printf(digest, "%s:%s:%s", username, p->realm, secret);
			cw_md5_hash(a1_hash, &digest->data[mark]);
			cw_dynstr_truncate(digest, mark);
		}
		cw_dynstr_printf(digest, "%s:%s", sip_methods[method].text, uri);
		cw_md5_hash(a2_hash, &digest->data[mark]);
		cw_dynstr_truncate(digest, mark);

		p->noncecount++;

		/* XXX We hard code our qop to "auth" for now.  XXX */
		if (!cw_strlen_zero(p->qop))
			cw_dynstr_printf(digest, "%s:%s:%08x:%s:auth:%s", a1_hash, p->nonce, p->noncecount, cnonce, a2_hash);
		else
			cw_dynstr_printf(digest, "%s:%s:%s", a1_hash, p->nonce, a2_hash);
		cw_md5_hash(resp_hash, &digest->data[mark]);
		cw_dynstr_truncate(digest, mark);

		if (!cw_strlen_zero(p->qop))
			cw_dynstr_printf(digest, "Digest username=\"%s\", realm=\"%s\", algorithm=MD5, uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\", qop=auth, cnonce=\"%s\", nc=%08x", username, p->realm, uri, p->nonce, resp_hash, p->opaque, cnonce, p->noncecount);
		else
			cw_dynstr_printf(digest, "Digest username=\"%s\", realm=\"%s\", algorithm=MD5, uri=\"%s\", nonce=\"%s\", response=\"%s\", opaque=\"%s\"", username, p->realm, uri, p->nonce, resp_hash, p->opaque);

		res = 0;
	}

	return res;
}
    
static const char show_domains_usage[] =
"Usage: sip show domains\n"
"       Lists all configured SIP local domains.\n"
"       CallWeaver only responds to SIP messages to local domains.\n";

static const char notify_usage[] =
"Usage: sip notify <type> <peer> [<peer>...]\n"
"       Send a NOTIFY message to a SIP peer or peers\n"
"       Message types are defined in sip_notify.conf\n";

static const char show_users_usage[] =
"Usage: sip show users [like <pattern>]\n"
"       Lists all known SIP users.\n"
"       Optional regular expression pattern is used to filter the user list.\n";

static const char show_user_usage[] =
"Usage: sip show user <name> [load]\n"
"       Lists all details on one SIP user and the current status.\n"
"       Option \"load\" forces lookup of peer in realtime storage.\n";

static const char show_inuse_usage[] =
"Usage: sip show inuse [all]\n"
"       List all SIP users and peers usage counters and limits.\n"
"       Add option \"all\" to show all devices, not only those with a limit.\n";

static const char show_channels_usage[] =
"Usage: sip show channels\n"
"       Lists all currently active SIP channels.\n";

static const char show_channel_usage[] =
"Usage: sip show channel <channel>\n"
"       Provides detailed status on a given SIP channel.\n";

static const char show_peers_usage[] =
"Usage: sip show peers [like <pattern>]\n"
"       Lists all known SIP peers.\n"
"       Optional regular expression pattern is used to filter the peer list.\n";

static const char show_peer_usage[] =
"Usage: sip show peer <name> [load]\n"
"       Lists all details on one SIP peer and the current status.\n"
"       Option \"load\" forces lookup of peer in realtime storage.\n";

static const char prune_realtime_usage[] =
"Usage: sip prune realtime [peer|user] [<name>|all|like <pattern>]\n"
"       Prunes object(s) from the cache.\n"
"       Optional regular expression pattern is used to filter the objects.\n";

static const char show_reg_usage[] =
"Usage: sip show registry\n"
"       Lists all registration requests and status.\n";

static const char debug_usage[] =
"Usage: sip debug\n"
"       sip debug show\n"
"       Shows the current SIP debugging state.\n\n"
"       sip debug ip <host[:PORT]>\n"
"       Enables dumping of SIP packets to and from host.\n\n"
"       sip debug peer <peername>\n"
"       Enables dumping of SIP packets to and from host.\n"
"       Require peer to be registered.\n";

static const char no_debug_usage[] =
"Usage: sip no debug\n"
"       Disables dumping of SIP packets for debugging purposes\n";

static const char sip_reload_usage[] =
"Usage: sip reload\n"
"       Reloads SIP configuration from sip.conf\n";

static const char show_subscriptions_usage[] =
"Usage: sip show subscriptions\n" 
"       Shows active SIP subscriptions for extension states\n";

static const char show_settings_usage[] =
"Usage: sip show settings\n"
"       Provides detailed list of the configuration of the SIP channel.\n";


static int func_sipblacklist(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(argc);
	CW_UNUSED(argv);
	CW_UNUSED(result);

	if (chan->type == channeltype) {
		struct sip_pvt *p = chan->tech_pvt;
		cw_blacklist_add(&p->peeraddr.sa);
	}

	return 0;
}


struct func_sipbuilddial_args {
	regex_t preg;
	struct cw_dynstr *result;
	int isfirst:1;
};

static int func_sipbuilddial_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);
	struct func_sipbuilddial_args *args = data;

	if (regexec(&args->preg, peer->name, 0, NULL, 0)) {
		cw_dynstr_printf(args->result, "%sSIP/%s", (args->isfirst ? "" : "&"), peer->name);
		args->isfirst= 0;
	}

	return 0;
}

/*! \brief  func_sipbuilddial_read: Read DIAL string based on regex (dialplan function) */
static int func_sipbuilddial(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct func_sipbuilddial_args args = {
		.result = result,
		.isfirst = 1,
	};
	char *buf;
	size_t l;
	int err;

	CW_UNUSED(chan);
	CW_UNUSED(argc);

	if (!(err = regcomp(&args.preg, argv[0], REG_EXTENDED | REG_NOSUB))) {
		/* People debugging might like the results ordered but that just
		 * adds extra work to the common case. So tough. Suck it up!
		 */
		cw_registry_iterate(&peerbyname_registry, func_sipbuilddial_one, NULL);
		regfree(&args.preg);
		return 0;
	}

	l = regerror(err, &args.preg, NULL, 0);
	buf = alloca(l);
	l = regerror(err, &args.preg, buf, l);
	cw_log(CW_LOG_ERROR, "Error in regex \"%s\": %s\n", argv[0], buf);
	return -1;
}


/*! \brief  func_header_read: Read SIP header (dialplan function) */
static int func_header_read(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct sip_pvt *p;
    char *content;
    
    if (argc != 1 || !argv[0][0])
	    return cw_function_syntax(sipheader_func_syntax);

    cw_channel_lock(chan);

    if (chan->type != channeltype)
    {
        cw_log(CW_LOG_WARNING, "This function can only be used on SIP channels.\n");
        cw_channel_unlock(chan);
        return -1;
    }

    p = chan->tech_pvt;

    /* If there is no private structure, this channel is no longer alive */
    if (!p)
    {
        cw_channel_unlock(chan);
        return -1;
    }

    if (result) {
        content = get_header(&p->initreq, SIP_HDR_GENERIC(argv[0]));

        if (!cw_strlen_zero(content))
            cw_dynstr_printf(result, "%s", content);

    }
    cw_channel_unlock(chan);
    return 0;
}


/*! \brief  function_check_sipdomain: Dial plan function to check if domain is local */
static int func_check_sipdomain(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	CW_UNUSED(chan);

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(checksipdomain_func_syntax);

	if (result && check_sip_domain(argv[0], NULL, 0))
		cw_dynstr_printf(result, "%s", argv[0]);

	return 0;
}


/*! \brief  function_sippeer: ${SIPPEER()} Dialplan function - reads peer data */
static int function_sippeer(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct sip_peer *peer;

    CW_UNUSED(chan);

    if (argc < 1 || argc > 2 || !argv[0][0])
	    return cw_function_syntax(sippeer_func_syntax);

    if (!result)
	    return 0;

    if (argc == 1)
    {
        /* As long as we have one argument argv[1] exists as well
         * and contains the terminating NULL.
         */
        if ((argv[1] = strchr(argv[0], ':')))
        {
            static int deprecated = 0;
            if (!deprecated)
            {
                cw_log(CW_LOG_WARNING, "Syntax SIPPEER(peername:item) is deprecated. Use SIPPEER(peername, item) instead\n");
                deprecated= 1;
            }
            *(argv[1]++) = '\0';
	}
        else
            argv[1] = (char *)"ip";
    }

    if (!(peer = find_peer(argv[0], NULL, 1)))
        return 0;

    if (!strcasecmp(argv[1], "ip"))
        cw_dynstr_printf(result, "%l@", &peer->addr.sa);
    else  if (!strcasecmp(argv[1], "status"))
    {
        static int deprecated = 0;
        if (!deprecated)
        {
            cw_log(CW_LOG_WARNING, "SIPPEER(peername, status) is deprecated. Use SIPPEER(peername, reachability) and/or SIPPEER(peername, rtt) instead\n");
            deprecated= 1;
        }
        if (peer->maxms && peer->timer_t1 >= 0)
            cw_dynstr_printf(result, "%s (%d ms)", peer_status(peer), peer->timer_t1);
        else
            cw_dynstr_printf(result, "%s", peer_status(peer));
    }
    else  if (!strcasecmp(argv[1], "reachability"))
    {
        cw_dynstr_printf(result, "%s", peer_status(peer));
    }
    else  if (!strcasecmp(argv[1], "rtt"))
    {
        cw_dynstr_printf(result, "%d", peer->timer_t1);
    }
    else  if (!strcasecmp(argv[1], "language"))
    {
        cw_dynstr_printf(result, "%s", peer->language);
    }
    else  if (!strcasecmp(argv[1], "regexten"))
    {
        cw_dynstr_printf(result, "%s", peer->regexten);
    }
    else  if (!strcasecmp(argv[1], "limit"))
    {
        cw_dynstr_printf(result, "%d", peer->call_limit);
    }
    else  if (!strcasecmp(argv[1], "curcalls"))
    {
        cw_dynstr_printf(result, "%d", peer->inUse);
    }
    else  if (!strcasecmp(argv[1], "useragent"))
    {
        cw_dynstr_printf(result, "%s", peer->useragent);
    }
    else  if (!strcasecmp(argv[1], "mailbox"))
    {
        cw_dynstr_printf(result, "%s", peer->mailbox);
    }
    else  if (!strcasecmp(argv[1], "context"))
    {
        cw_dynstr_printf(result, "%s", peer->context);
    }
    else  if (!strcasecmp(argv[1], "expire"))
    {
        cw_dynstr_printf(result, "%d", peer->expire);
    }
    else  if (!strcasecmp(argv[1], "dynamic"))
    {
        cw_dynstr_printf(result, "%s", (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) ? "yes" : "no"));
    }
    else  if (!strcasecmp(argv[1], "callerid_name"))
    {
        cw_dynstr_printf(result, "%s", peer->cid_name);
    }
    else  if (!strcasecmp(argv[1], "callerid_num"))
    {
        cw_dynstr_printf(result, "%s", peer->cid_num);
    }
    else  if (!strcasecmp(argv[1], "codecs"))
    {
        cw_getformatname_multiple(result, peer->capability);
    }
    else  if (!strncasecmp(argv[1], "codec[", 6))
    {
        char *codecnum, *ptr;
        int codec = 0;
        
        codecnum = strchr(argv[1], '[');
        *codecnum = '\0';
        codecnum++;
        if ((ptr = strchr(codecnum, ']')))
            *ptr = '\0';

        if ((codec = cw_codec_pref_index(&peer->prefs, atoi(codecnum))))
            cw_dynstr_printf(result, "%s", cw_getformatname(codec));
    }

    cw_object_put(peer);

    return 0;
}


static int function_sippeervar(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct sip_peer *peer;
    struct cw_variable *var;

    CW_UNUSED(chan);

    if (argc != 2 || !argv[0][0])
	    return cw_function_syntax(sippeervar_func_syntax);

    if (!result || !(peer = find_peer(argv[0], NULL, 1)))
        return 0;

    for (var = peer->chanvars; var; var = var->next)
    {
        if (!strcmp(var->name, argv[1]))
        {
            cw_dynstr_printf(result, "%s", var->value);
            break;
        }
    }

    cw_object_put(peer);
    return 0;
}


/*! \brief  function_sipchaninfo_read: ${SIPCHANINFO()} Dialplan function - reads sip channel data */
static int function_sipchaninfo_read(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct sip_pvt *p;
	int res = -1;

	if (argc != 1 || !argv[0][0])
		return cw_function_syntax(sipchaninfo_func_syntax);

	cw_channel_lock(chan);

	if (chan->type != channeltype) {
		cw_log(CW_LOG_ERROR, "%s: SIPCHANINFO can only be used on SIP channels.\n", chan->name);
		goto out;
	}


	/* If there is no private structure, this channel is no longer alive */
	if (!(p = chan->tech_pvt))
		goto out;

	res = 0;

	if (!result)
		goto out;

	if (!strcasecmp(argv[0], "peeraddr")) {
		cw_dynstr_printf(result, "%l@", &p->peeraddr.sa);
	} else if (!strcasecmp(argv[0], "peerip")) {
		cw_dynstr_printf(result, "%@", &p->peeraddr.sa);
	} else  if (!strcasecmp(argv[0], "recvip")) {
		cw_dynstr_printf(result, "%@", &p->peeraddr.sa);
	} else  if (!strcasecmp(argv[0], "from"))
		cw_dynstr_printf(result, "%s", p->from);
	else  if (!strcasecmp(argv[0], "uri"))
		cw_dynstr_printf(result, "%s", p->uri);
	else  if (!strcasecmp(argv[0], "useragent"))
		cw_dynstr_printf(result, "%s", p->useragent);
	else  if (!strcasecmp(argv[0], "peername"))
		cw_dynstr_printf(result, "%s", p->peername);
	else
		res = -1;

out:
	cw_channel_unlock(chan);
	return res;
}


/*! \brief  parse_moved_contact: Parse 302 Moved temporalily response */
static void parse_moved_contact(struct sip_pvt *p, struct sip_request *req)
{
    char tmp[256];
    char *s, *e;
    cw_copy_string(tmp, get_header(req, SIP_HDR_CONTACT), sizeof(tmp));
    s = get_in_brackets(tmp);
    e = strchr(s, ';');
    if (e)
        *e = '\0';
    if (cw_test_flag(p, SIP_PROMISCREDIR))
    {
        if (!strncasecmp(s, "sip:", 4))
            s += 4;
        e = strchr(s, '/');
        if (e)
            *e = '\0';
        cw_log(CW_LOG_DEBUG, "Found promiscuous redirection to 'SIP/%s'\n", s);
        if (p->owner)
            snprintf(p->owner->call_forward, sizeof(p->owner->call_forward), "SIP/%s", s);
    }
    else
    {
        e = strchr(tmp, '@');
        if (e)
            *e = '\0';
        e = strchr(tmp, '/');
        if (e)
            *e = '\0';
        if (!strncasecmp(s, "sip:", 4))
            s += 4;
        cw_log(CW_LOG_DEBUG, "Found 302 Redirect to extension '%s'\n", s);
        if (p->owner)
            cw_copy_string(p->owner->call_forward, s, sizeof(p->owner->call_forward));
    }
}

/*! \brief  check_pendings: Check pending actions on SIP call */
static void check_pendings(struct sip_pvt *p)
{
    /* Go ahead and send bye at this point */
    if (cw_test_flag(p, SIP_PENDINGBYE))
    {
	/* if we can't BYE, then this is really a pending CANCEL */
	if (!cw_test_flag(p, SIP_CAN_BYE))
	    transmit_request_with_auth(p, SIP_CANCEL, p->ocseq, 1, 0);
		/* Actually don't destroy us yet, wait for the 487 on our original 
		   INVITE, but do set an autodestruct just in case we never get it. */
	else 
	    transmit_request_with_auth(p, SIP_BYE, 0, 1, 1);
	cw_clear_flag(p, SIP_PENDINGBYE);	
	sip_scheddestroy(p, -1);
    }
    else if (cw_test_flag(p, SIP_NEEDREINVITE))
    {
        cw_log(CW_LOG_DEBUG, "Sending pending reinvite on '%s'\n", p->callid);
        /* Didn't get to reinvite yet, so do it now */
        transmit_reinvite_with_sdp(p);
        cw_clear_flag(p, SIP_NEEDREINVITE);    
    }
}

/*! \brief  handle_response_invite: Handle SIP response in dialogue */
static void handle_response_invite(struct sip_pvt *p, int resp, struct sip_request *req, int ignore, int seqno)
{
    int outgoing = cw_test_flag(p, SIP_OUTGOING);
    
    CW_UNUSED(seqno);

    if (option_debug > 3)
    {
        int reinvite = (p->owner && p->owner->_state == CW_STATE_UP);
        if (reinvite)
            cw_log(CW_LOG_DEBUG, "SIP response %d to RE-invite on %s call %s\n", resp, outgoing ? "outgoing" : "incoming", p->callid);
        else
            cw_log(CW_LOG_DEBUG, "SIP response %d to standard invite\n", resp);
    }

    if (cw_test_flag(p, SIP_ALREADYGONE))
    {
        /* This call is already gone */
        cw_log(CW_LOG_DEBUG, "Got response on call that is already terminated: %s (ignoring)\n", p->callid);
        return;
    }

    /* RFC3261 says we must treat every 1xx response (but not 100)
       that we don't recognize as if it was 183.
    */
    if ((resp > 100) &&
        (resp < 200) &&
        (resp != 180) &&
        (resp != 183))
    	resp = 183;

    switch (resp)
    {
    case 100:
        /* Trying */
        if (!ignore)
            sip_cancel_destroy(p);
        check_pendings(p);
        cw_set_flag(p, SIP_CAN_BYE);
        break;
    case 180:
        /* 180 Ringing */
        if (!ignore)
            sip_cancel_destroy(p);
        if (!ignore  &&  p->owner)
        {
            cw_queue_control(p->owner, CW_CONTROL_RINGING);
            if (p->owner->_state != CW_STATE_UP)
	            cw_setstate(p->owner, CW_STATE_RINGING);
        }
        if (mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions)))
        {
            if (!ignore && p->owner)
            {
                /* Queue a progress frame only if we have SDP in 180 */
                cw_queue_control(p->owner, CW_CONTROL_PROGRESS);
            }
        }
        cw_set_flag(p, SIP_CAN_BYE);
        check_pendings(p);
        break;
    case 183:
        /* Session progress */
        if (!ignore)
            sip_cancel_destroy(p);
        if (mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions)))
        {
            if (!ignore && p->owner)
            {
                /* Queue a progress frame */
                cw_queue_control(p->owner, CW_CONTROL_PROGRESS);
            }
        }
        cw_set_flag(p, SIP_CAN_BYE);
        check_pendings(p);
        break;
    case 200:    /* 200 OK on invite - someone's answering our call */
        if (!ignore)
            sip_cancel_destroy(p);
        p->authtries = 0;
        mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions));

        /* Parse contact header for continued conversation */
        /* When we get 200 OK, we know which device (and IP) to contact for this call */
        /* This is important when we have a SIP proxy between us and the phone */
        if (outgoing)
        {
            char *uri = parse_ok_contact(p, req);

            /* Save Record-Route for any later requests we make on this dialogue */
            build_route(p, req, 1);
            if (p->route)
                uri = p->route->hop;

            set_destination(p, uri);
        }
        if (p->owner && (p->owner->_state == CW_STATE_UP))
        {
            /* if this is a re-invite */ 
            struct cw_channel *bridgepeer = NULL;
            struct sip_pvt *bridgepvt = NULL;

            if ((bridgepeer = cw_bridged_channel(p->owner)))
            {
                if (!strcasecmp(bridgepeer->type, "SIP"))
                {
                    if ((bridgepvt = (struct sip_pvt *) bridgepeer->tech_pvt))
		            {
                        if (bridgepvt->udptl)
                        {
                            if (p->t38state == SIP_T38_OFFER_RECEIVED_REINVITE)
                            { 
                                /* This is 200 OK to re-invite where T38 was offered on channel so we need to send 200 OK with T38 the other side of the bridge */
                                /* Send response with T38 SDP to the other side of the bridge */
                                sip_handle_t38_reinvite(bridgepeer, p, 0);
                                cw_channel_set_t38_status(p->owner, T38_NEGOTIATED);
                            }
                            else if (p->t38state == SIP_T38_STATUS_UNKNOWN  &&  bridgepeer  &&  (bridgepvt->t38state == SIP_T38_NEGOTIATED))
                            {
                                /* This is case of RTP re-invite after T38 session */
                                cw_log(CW_LOG_WARNING, "RTP re-invite after T38 session not handled yet !\n");
                                /* Insted of this we should somehow re-invite the other side of the bridge to RTP */
                                sip_destroy(p);
                            }
                        }
                        else
                        {
                            cw_log(CW_LOG_WARNING, "Strange... The other side of the bridge don't have udptl struct\n");
                            cw_mutex_lock(&bridgepvt->lock);
                            bridgepvt->t38state = SIP_T38_STATUS_UNKNOWN;
                            cw_mutex_unlock(&bridgepvt->lock);
                            cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",bridgepvt->t38state, bridgepeer->name);
                            p->t38state = SIP_T38_STATUS_UNKNOWN;
                            cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
                        }
                    }
                    else
                    {
                        cw_log(CW_LOG_WARNING, "Strange... The other side of the bridge don't seem to exist\n");
                    }
                }
                else
                {
                        /* Other side is not a SIP channel */ 
                        cw_log(CW_LOG_WARNING, "Strange... The other side of the bridge is not a SIP channel\n");
                        p->t38state = SIP_T38_STATUS_UNKNOWN;
                        cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
                }
                cw_object_put(bridgepeer);
            }
            else
            {
                cw_log(CW_LOG_DEBUG, "Channel Bridge information is non-existant. T38 Termination requested.\n");
            }
        }
        if ((p->t38state == SIP_T38_OFFER_SENT_REINVITE)  ||  (p->t38state == SIP_T38_OFFER_SENT_DIRECT))
        {
            /* If there was T38 reinvite and we are supposed to answer with 200 OK than this should set us to T38 negotiated mode */
            p->t38state = SIP_T38_NEGOTIATED;
            cw_log(CW_LOG_DEBUG, "T38 changed state to %d on channel %s\n", p->t38state, p->owner ? p->owner->name : "<none>");
            if (p->owner)
            {
                cw_channel_set_t38_status(p->owner, T38_NEGOTIATED);
                cw_log(CW_LOG_DEBUG, "T38mode enabled for channel %s\n", p->owner->name);
            }
        }
        if (!ignore  &&  p->owner)
        {
            if (p->owner->_state != CW_STATE_UP)
            {
#ifdef OSP_SUPPORT    
                time(&p->ospstart);
#endif
                cw_queue_control(p->owner, CW_CONTROL_ANSWER);
            }
            else
            {
                /* RE-invite */
                cw_queue_frame(p->owner, &cw_null_frame);
            }
        }
        else
        {
             /* It's possible we're getting an ACK after we've tried to disconnect
                  by sending CANCEL */
            /* THIS NEEDS TO BE CHECKED: OEJ */
            if (!ignore)
                cw_set_flag(p, SIP_PENDINGBYE);    
        }
        /* If I understand this right, the branch is different for a non-200 ACK only */
        transmit_ack(p, req, 1);
        cw_set_flag(p, SIP_CAN_BYE);
        check_pendings(p);
        break;
    case 407: /* Proxy authentication */
    case 401: /* Www auth */
        /* First we ACK */
        transmit_ack(p, req, 0);
        if (p->options)
            p->options->auth_type = (resp == 401 ? WWW_AUTH : PROXY_AUTH);

        /* Then we AUTH */
#if 0
        /* FIXME: is this right? If so we need to unregister and reregister p */
        p->theirtag[0]='\0';    /* forget their old tag, so we don't match tags when getting response */
#endif
        if (!ignore)
        {
            const char *authenticate = (resp == 401 ? "WWW-Authenticate" : "Proxy-Authenticate");
            const char *authorization = (resp == 401 ? "Authorization" : "Proxy-Authorization");
        
            if ((p->authtries == MAX_AUTHTRIES) || do_proxy_auth(p, req, authenticate, authorization, SIP_INVITE, 1))
            {
                cw_log(CW_LOG_NOTICE, "Failed to authenticate on INVITE to '%s'\n", get_header(&p->initreq, SIP_HDR_FROM));
                if (p->owner)
                    cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
                cw_set_flag(p, SIP_ALREADYGONE);
                sip_destroy(p);
            }
        }
        break;
    case 403: /* Forbidden */
        /* First we ACK */
        transmit_ack(p, req, 0);
        cw_log(CW_LOG_WARNING, "Forbidden - wrong password on authentication for INVITE to '%s'\n", get_header(&p->initreq, SIP_HDR_FROM));
        if (!ignore && p->owner)
            cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
        cw_set_flag(p, SIP_ALREADYGONE);
        sip_destroy(p);
        break;
    case 404: /* Not found */
        transmit_ack(p, req, 0);
        if (p->owner && !ignore)
            cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
        cw_set_flag(p, SIP_ALREADYGONE);    
        break;
    case 481: /* Call leg does not exist */
        /* Could be REFER or INVITE */
        cw_log(CW_LOG_WARNING, "Re-invite to non-existing call leg on other UA. SIP dialog '%s'. Giving up.\n", p->callid);
        transmit_ack(p, req, 0);
        break;
    case 487: /* Cancelled transaction */
		/* We have sent CANCEL on an outbound INVITE 
           This transaction is already scheduled to be killed by sip_hangup().
		*/
        transmit_ack(p, req, 0);
        if (p->owner && !ignore)
        	cw_queue_hangup(p->owner);
        else if (!ignore)
        	update_call_counter(p, DEC_CALL_LIMIT);
        break;
    case 491: /* Pending */
        /* we have to wait a while, then retransmit */
        /* Transmission is rescheduled, so everything should be taken care of.
           We should support the retry-after at some point */
        break;
    case 501: /* Not implemented */
        transmit_ack(p, req, 0);
        if (p->owner)
            cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
        break;
    }
}

/*! \brief  handle_response_register: Handle responses on REGISTER to services */
static int handle_response_register(struct sip_pvt *p, int resp, struct sip_request *req, int ignore, int seqno)
{
    int expires, expires_ms;
    struct sip_registry *r;

    CW_UNUSED(ignore);
    CW_UNUSED(seqno);

    if (!(r = p->registry))
        return 1;

    /* There should be a timeout scheduled. If we can delete
     * it we own the call. If we can't we have to stop now.
     */

    if (r->timeout == -1 || cw_sched_del(sched, r->timeout))
        return 1;

    r->timeout = -1;

    switch (resp)
    {
    case 401:
        /* Unauthorized */
        if ((p->authtries == MAX_AUTHTRIES) || do_register_auth(p, req, "WWW-Authenticate", "Authorization"))
        {
            cw_log(CW_LOG_NOTICE, "Failed to authenticate on REGISTER to '%s@%s' (Tries %d)\n", p->registry->username, p->registry->hostname, p->authtries);
            goto destroy;
        }
        break;
    case 403:
        /* Forbidden */
        cw_log(CW_LOG_WARNING, "Forbidden - wrong password on authentication for REGISTER for '%s' to '%s'\n", p->registry->username, p->registry->hostname);
        if (global_regattempts_max)
            p->registry->regattempts = global_regattempts_max+1;
        goto destroy;
        break;
    case 404:
        /* Not found */
        cw_log(CW_LOG_WARNING, "Got 404 Not found on SIP register to service %s@%s, giving up\n", p->registry->username,p->registry->hostname);
        if (global_regattempts_max)
            p->registry->regattempts = global_regattempts_max+1;
        goto destroy;
        break;
    case 407:
        /* Proxy auth */
        if ((p->authtries == MAX_AUTHTRIES) || do_register_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization"))
        {
            cw_log(CW_LOG_NOTICE, "Failed to authenticate on REGISTER to '%s' (tries '%d')\n", get_header(&p->initreq, SIP_HDR_FROM), p->authtries);
            goto destroy;
        }
        break;
    case 479:
        /* SER: Not able to process the URI - address is wrong in register*/
        cw_log(CW_LOG_WARNING, "Got error 479 on register to %s@%s, giving up (check config)\n", p->registry->username,p->registry->hostname);
        if (global_regattempts_max)
            p->registry->regattempts = global_regattempts_max+1;
        goto destroy;
        break;
    case 200:
        /* 200 OK */
        if (!r)
        {
            cw_log(CW_LOG_WARNING, "Got 200 OK on REGISTER that isn't a register\n");
            sip_destroy(p);
            return 0;
        }

        r->regstate = REG_STATE_REGISTERED;
        cw_manager_event(EVENT_FLAG_SYSTEM, "Registry",
              4,
              cw_msg_tuple("Channel",  "%s", "SIP"),
              cw_msg_tuple("Username", "%s", r->username),
              cw_msg_tuple("Domain",   "%s", r->hostname),
              cw_msg_tuple("Status",   "%s", regstate2str(r->regstate))
        );
        r->regattempts = 0;
        cw_log(CW_LOG_DEBUG, "Registration successful\n");

        /* Let this one hang around until we have all the responses */
        sip_scheddestroy(p, -1);

        /* set us up for re-registering */
        /* figure out how long we got registered for */
        /* according to section 6.13 of RFC, contact headers override
           expires headers, so check those first */
        expires = 0;
        if (!cw_strlen_zero(get_header(req, SIP_HDR_CONTACT)))
        {
            char *contact = NULL;
            char *tmptmp = NULL;
            int start = req->hdr_start;
        
            for (;;)
            {
                contact = __get_header(req, SIP_HDR_CONTACT, &start);
                /* this loop ensures we get a contact header about our register request */
                if (!cw_strlen_zero(contact))
                {
                    if ((tmptmp = strstr(contact, p->our_contact)))
                    {
                        contact = tmptmp;
                        break;
                    }
                }
                else
                    break;
            }
	    if (contact[0] == '<' && (tmptmp = strchr(contact, '>')))
		    contact = tmptmp + 1;
            tmptmp = strcasestr(contact, ";expires=");
            if (tmptmp)
            {
                if (sscanf(tmptmp + sizeof(";expires=") - 1, "%d;", &expires) != 1)
                    expires = 0;
            }

        }
        if (!expires) 
            expires = atoi(get_header(req, SIP_HDR_NOSHORT("expires")));
        if (!expires)
            expires = default_expiry;

        expires_ms = expires * 1000;
        if (expires <= EXPIRY_GUARD_LIMIT)
            expires_ms -= MAX((int)(expires_ms * EXPIRY_GUARD_PCT), EXPIRY_GUARD_MIN);
        else
            expires_ms -= EXPIRY_GUARD_SECS * 1000;
        if (sipdebug)
            cw_log(CW_LOG_NOTICE, "Outbound Registration: Expiry for %s is %d sec (Scheduling reregistration in %d s)\n", r->hostname, expires, expires_ms/1000); 

        r->refresh = (int) expires_ms / 1000;

        /* Schedule re-registration before we expire */
        if (r->expire == -1 || cw_sched_del(sched, r->expire))
	    cw_object_dup(r);

        r->expire = cw_sched_add(sched, expires_ms, sip_reregister, r);

        cw_object_put(r->dialogue);
        r->dialogue = NULL;

        cw_object_put(r);
        p->registry = NULL;

        return 1;
    }

    r->timeout = cw_sched_add(sched, global_reg_timeout*1000, sip_reg_timeout, r);
    return 1;

destroy:
    sip_destroy(p);
    cw_object_put(r->dialogue);
    r->dialogue = NULL;
    return 1;
}

/*! \brief  handle_response_peerpoke: Handle qualification responses (OPTIONS) */
static int handle_response_peerpoke(struct sip_pvt *p, int resp, struct sip_request *req, int ignore, int seqno, enum sipmethod sipmethod)
{
	CW_UNUSED(req);
	CW_UNUSED(ignore);
	CW_UNUSED(seqno);
	CW_UNUSED(sipmethod);

	if (resp != 100) {
#ifdef VOCAL_DATA_HACK
		if (sipmethod == SIP_INVITE)
			transmit_ack(p, req, 0);
#endif
		sip_destroy(p);
	}
	return 1;
}

/*! \brief  handle_response: Handle SIP response in dialogue */
static void handle_response(struct sip_pvt *p, struct sip_request *req, int ignore)
{
    const char *msg;
    struct cw_channel *owner;
    enum sipmethod sipmethod;
    int resp;

    if (!p->initreq.pkt.used)
    {
        cw_log(CW_LOG_DEBUG, "That's odd...  Got a response on a call we dont know about. Cseq %s\n", req->pkt.data + req->cseq);
        sip_destroy(p);
        return;
    }

    if (p->ocseq && (p->ocseq < req->seqno))
    {
        cw_log(CW_LOG_DEBUG, "Ignoring out of order response %u (expecting %u)\n", req->seqno, p->ocseq);
        return;
    }

    if (sscanf(req->pkt.data + req->uriresp, " %d", &resp) != 1)
    {
        cw_log(CW_LOG_WARNING, "Invalid response: \"%s\"\n", req->pkt.data + req->uriresp);
        return;
    }

    msg = get_header(req, SIP_HDR_NOSHORT("User-Agent"));
    if (msg && msg[0])
    {
        cw_copy_string(p->useragent, msg, sizeof(p->useragent));
        if (p->rtp)
            p->rtp->bug_sonus = (strstr(msg, "Sonus_UAC") != NULL);
    }

    /* More SIP ridiculousness, we have to ignore bogus contacts in 100 etc responses */
    if (resp == 200 || (resp >= 300 && resp <= 399))
        extract_uri(p, req);

    msg = strchr(req->pkt.data + req->cseq, ' ');
    if (!msg)
        msg = "";
    else
        msg++;
    sipmethod = find_sip_method(msg);

    owner = p->owner;
    if (owner) 
        owner->hangupcause = hangup_sip2cause(resp);

    if (p->peerpoke)
    {
        /* We don't really care what the response is, just that it replied back. 
           Well, as long as it's not a 100 response...  since we might
           need to hang around for something more "definitive" */

        handle_response_peerpoke(p, resp, req, ignore, req->seqno, sipmethod);
    }
    else if (cw_test_flag(p, SIP_OUTGOING))
    {
        /* Acknowledge sequence number */
        /* Cancel the auto-congest if possible since we've got something useful back */
        if (p->initid > -1 && !cw_sched_del(sched, p->initid))
        {
            cw_object_put(p);
            p->initid = -1;
        }
        switch(resp)
        {
        case 100:    /* 100 Trying */
            if (sipmethod == SIP_INVITE) 
                handle_response_invite(p, resp, req, ignore, req->seqno);
            break;
        case 183:    /* 183 Session Progress */
            if (sipmethod == SIP_INVITE) 
                handle_response_invite(p, resp, req, ignore, req->seqno);
            break;
        case 180:    /* 180 Ringing */
            if (sipmethod == SIP_INVITE) 
                handle_response_invite(p, resp, req, ignore, req->seqno);
            break;
        case 200:    /* 200 OK */
            p->authtries = 0;    /* Reset authentication counter */
            if (sipmethod == SIP_MESSAGE)
            {
                /* We successfully transmitted a message */
                sip_destroy(p);
            }
            else if (sipmethod == SIP_NOTIFY)
            {
                /* They got the notify, this is the end */
                if (p->owner)
                {
                    cw_log(CW_LOG_WARNING, "Notify answer on an owned channel?\n");
                    cw_queue_hangup(p->owner);
                }
                else
                {
                    if (p->subscribed == NONE)
                    {
                        sip_destroy(p);
                    }
                }
            }
            else if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (p->registry && sipmethod == SIP_REGISTER)
            {
                handle_response_register(p, resp, req, ignore, req->seqno);
	    } else if (sipmethod == SIP_BYE) {
		/* Ok, we're ready to go */
		sip_destroy(p);
	    } 
            break;
        case 401: /* Not www-authorized on SIP method */
            if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (p->registry && sipmethod == SIP_REGISTER)
            {
                handle_response_register(p, resp, req, ignore, req->seqno);
            }
            else
            {
                cw_log(CW_LOG_WARNING, "Got authentication request (401) on unknown %s to '%s'\n", sip_methods[sipmethod].text, get_header(req, SIP_HDR_TO));
                sip_destroy(p);
            }
            break;
        case 403: /* Forbidden - we failed authentication */
            if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (p->registry && sipmethod == SIP_REGISTER)
            {
                handle_response_register(p, resp, req, ignore, req->seqno);
            }
            else
            {
                cw_log(CW_LOG_WARNING, "Forbidden - wrong password on authentication for %s\n", msg);
            }
            break;
        case 404: /* Not found */
            if (p->registry && sipmethod == SIP_REGISTER)
            {
                handle_response_register(p, resp, req, ignore, req->seqno);
            }
            else if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (owner)
                cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
            break;
        case 407: /* Proxy auth required */
            if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (sipmethod == SIP_BYE || sipmethod == SIP_REFER)
            {
                if (cw_strlen_zero(p->authname))
                {
                    cw_log(CW_LOG_WARNING, "Asked to authenticate %s, to %#l@ but we have no matching peer!\n", msg, &p->peeraddr.sa);
                    sip_destroy(p);
                }
		else if ((p->authtries == MAX_AUTHTRIES) || do_proxy_auth(p, req, "Proxy-Authenticate", "Proxy-Authorization", sipmethod, 0))
                {
                    cw_log(CW_LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, SIP_HDR_FROM));
                    sip_destroy(p);
                }
            }
            else if (p->registry && sipmethod == SIP_REGISTER)
            {
                handle_response_register(p, resp, req, ignore, req->seqno);
            }
            else
            {
                /* We can't handle this, giving up in a bad way */
                sip_destroy(p);
            }
            break;
	case 487:
	    if (sipmethod == SIP_INVITE)
		handle_response_invite(p, resp, req, ignore, req->seqno);
	    break;
        case 491: /* Pending */
            if (sipmethod == SIP_INVITE)
                handle_response_invite(p, resp, req, ignore, req->seqno);
            else
                cw_log(CW_LOG_WARNING, "Host %#l@ (received from %#l@) does not implement %s\n", &p->peeraddr.sa, &req->recvdaddr.sa, msg);
            break;
        case 501: /* Not Implemented */
            if (sipmethod == SIP_INVITE)
                handle_response_invite(p, resp, req, ignore, req->seqno);
            else
                cw_log(CW_LOG_WARNING, "Host %#l@ (received from %#l@) does not implement %s\n", &p->peeraddr.sa, &req->recvdaddr.sa, msg);
            break;
        default:
            if ((resp >= 300) && (resp < 700))
            {
                if ((option_verbose > 2) && (resp != 487))
                    cw_verbose(VERBOSE_PREFIX_3 "Got SIP response \"%s\" back from %#l@ (received from %#l@)\n", req->pkt.data + req->uriresp, &p->peeraddr.sa, &req->recvdaddr.sa);
                if (p->rtp)
                {
                    /* Immediately stop RTP */
                    cw_rtp_stop(p->rtp);
                }
                if (p->vrtp)
                {
                    /* Immediately stop VRTP */
                    cw_rtp_stop(p->vrtp);
                }
                if (p->udptl)
                {
                    /* Immediately stop T.38 UDPTL */
                    cw_udptl_stop(p->udptl);
                }
                /* XXX Locking issues?? XXX */
                switch (resp)
                {
                case 300: /* Multiple Choices */
                case 301: /* Moved permenantly */
                case 302: /* Moved temporarily */
                case 305: /* Use Proxy */
                    parse_moved_contact(p, req);
                    /* Fall through */
                case 486: /* Busy here */
                case 600: /* Busy everywhere */
                case 603: /* Decline */
                    if (p->owner)
                        cw_queue_control(p->owner, CW_CONTROL_BUSY);
                    break;
                case 482: /* SIP is incapable of performing a hairpin call, which
                             is yet another failure of not having a layer 2 (again, YAY
                             IETF for thinking ahead).  So we treat this as a call
                             forward and hope we end up at the right place... */
                    cw_log(CW_LOG_DEBUG, "Hairpin detected, setting up call forward for what it's worth\n");
                    if (p->owner)
                        snprintf(p->owner->call_forward, sizeof(p->owner->call_forward), "Local/%s@%s", p->username, p->context);
                    /* Fall through */
                case 488: /* Not acceptable here - codec error */
                case 480: /* Temporarily Unavailable */
                case 404: /* Not Found */
                case 410: /* Gone */
                case 400: /* Bad Request */
                case 500: /* Server error */
                case 503: /* Service Unavailable */
                    if (owner)
                        cw_queue_control(p->owner, CW_CONTROL_CONGESTION);
                    break;
                default:
                    /* Send hangup */    
                    if (owner)
                        cw_queue_hangup(p->owner);
                    break;
                }
                /* ACK on invite */
                if (sipmethod == SIP_INVITE) 
                    transmit_ack(p, req, 0);
                cw_set_flag(p, SIP_ALREADYGONE);    
                if (!p->owner)
                    sip_destroy(p);
            }
            else if ((resp >= 100) && (resp < 200))
            {
                if (sipmethod == SIP_INVITE)
                {
                    if (!ignore) sip_cancel_destroy(p);
                    mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions));
                    if (p->owner)
                    {
                        /* Queue a progress frame */
                        cw_queue_control(p->owner, CW_CONTROL_PROGRESS);
                    }
                }
            } else
                cw_log(CW_LOG_NOTICE, "Dont know how to handle \"%s\" response from %#l@ (received from %#l@)\n", req->pkt.data + req->uriresp, &p->peeraddr.sa, &req->recvdaddr.sa);
        }
    }
    else
    {
        /* Responses to OUTGOING SIP requests on INCOMING calls 
           get handled here. As well as out-of-call message responses */
        if (req->debug)
            cw_verbose("SIP Response message for INCOMING dialog %s arrived\n", msg);

        switch(resp)
        {
        case 200:
            if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            else if (sipmethod == SIP_CANCEL)
            {
                cw_log(CW_LOG_DEBUG, "Got 200 OK on CANCEL\n");
            }
            else if (sipmethod == SIP_MESSAGE)
                /* We successfully transmitted a message */
                sip_destroy(p);
            else if (sipmethod == SIP_BYE)
                /* ok done */
                sip_destroy(p);
            break;
        case 401:    /* www-auth */
        case 407:
            if (sipmethod == SIP_BYE || sipmethod == SIP_REFER)
            {
                const char *auth, *auth2;

                if (resp == 407)
                {
                    auth = "Proxy-Authenticate";
                    auth2 = "Proxy-Authorization";
                }
                else
                {
                    auth = "WWW-Authenticate";
                    auth2 = "Authorization";
                }
                if ((p->authtries == MAX_AUTHTRIES) || do_proxy_auth(p, req, auth, auth2, sipmethod, 0))
                {
                    cw_log(CW_LOG_NOTICE, "Failed to authenticate on %s to '%s'\n", msg, get_header(&p->initreq, SIP_HDR_FROM));
                    sip_destroy(p);
                }
            }
            else if (sipmethod == SIP_INVITE)
            {
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            break;
        case 481:    /* Call leg does not exist */
            if (sipmethod == SIP_INVITE)
            {
                /* Re-invite failed */
                handle_response_invite(p, resp, req, ignore, req->seqno);
            }
            break;
        default:    /* Errors without handlers */
            if ((resp >= 100) && (resp < 200))
            {
                if (sipmethod == SIP_INVITE && !ignore)
                {
                    /* re-invite */
                    sip_cancel_destroy(p);
                }
            }
            if ((resp >= 300) && (resp < 700))
            {
                if ((option_verbose > 2) && (resp != 487))
                    cw_verbose(VERBOSE_PREFIX_3 "Incoming call: Got SIP response \"%s\" back from %#l@ (received from %#l@)\n", req->pkt.data + req->uriresp, &p->peeraddr.sa, &req->recvdaddr.sa);
                switch (resp)
                {
                case 488: /* Not acceptable here - codec error */
                case 603: /* Decline */
                case 500: /* Server error */
                case 503: /* Service Unavailable */
                    if (sipmethod == SIP_INVITE && !ignore)
                    {
                        /* re-invite failed */
                        sip_cancel_destroy(p);
                    }
                    break;
                }
            }
            break;
        }
    }
}

struct sip_dual {
    struct cw_channel *chan1;
    struct cw_channel *chan2;
    struct sip_request req;
};

/*! \brief  sip_park_thread: Park SIP call support function */
static void *sip_park_thread(void *stuff)
{
    struct cw_channel *chan1, *chan2;
    struct sip_dual *d;
    int ext;

    d = stuff;
    chan1 = d->chan1;
    chan2 = d->chan2;
    free(d);
    cw_channel_lock(chan1);
    cw_do_masquerade(chan1);
    cw_channel_unlock(chan1);
    cw_park_call(chan1, chan2, 0, &ext);
    /* Then hangup */
    cw_hangup(chan2);
    cw_log(CW_LOG_DEBUG, "Parked on extension '%d'\n", ext);
    return NULL;
}

/*! \brief  sip_park: Park a call */
static int sip_park(struct cw_channel *chan1, struct cw_channel *chan2, struct sip_request *req)
{
    struct sip_dual *d;
    struct cw_channel *chan1m, *chan2m;
    pthread_t th;

    /* We make a clone of the peer channel too, so we can play
       back the announcement */
    chan1m = cw_channel_alloc(0, "Parking/%s", chan1->name);
    chan2m = cw_channel_alloc(0, "SIPPeer/%s", chan2->name);
    if ((!chan2m) || (!chan1m))
    {
        if (chan1m)
            cw_hangup(chan1m);
        if (chan2m)
            cw_hangup(chan2m);
        return -1;
    }
    /* Make formats okay */
    chan1m->readformat = chan1->readformat;
    chan1m->writeformat = chan1->writeformat;
    cw_channel_masquerade(chan1m, chan1);
    /* Setup the extensions and such */
    cw_copy_string(chan1m->context, chan1->context, sizeof(chan1m->context));
    cw_copy_string(chan1m->exten, chan1->exten, sizeof(chan1m->exten));
    chan1m->priority = chan1->priority;
        
    /* Make formats okay */
    chan2m->readformat = chan2->readformat;
    chan2m->writeformat = chan2->writeformat;
    cw_channel_masquerade(chan2m, chan2);
    /* Setup the extensions and such */
    cw_copy_string(chan2m->context, chan2->context, sizeof(chan2m->context));
    cw_copy_string(chan2m->exten, chan2->exten, sizeof(chan2m->exten));
    chan2m->priority = chan2->priority;
    cw_channel_lock(chan2m);
    if (cw_do_masquerade(chan2m))
    {
        cw_log(CW_LOG_WARNING, "Masquerade failed :(\n");
        cw_channel_unlock(chan2m);
        cw_hangup(chan2m);
        return -1;
    }
    cw_channel_unlock(chan2m);
    d = malloc(sizeof(struct sip_dual));
    if (d)
    {
        memset(d, 0, sizeof(struct sip_dual));
        /* Save original request for followup */
	init_msg(&d->req, 0);
        copy_request(&d->req, req);
        d->chan1 = chan1m;
        d->chan2 = chan2m;
        if (!cw_pthread_create(&th, &global_attr_detached, sip_park_thread, d))
            return 0;
        free(d);
    }
    return -1;
}

/*! \brief  cw_quiet_chan: Turn off generator data */
static void cw_quiet_chan(struct cw_channel *chan) 
{
    if (chan)
        cw_generator_deactivate(&chan->generator);
    else
        cw_log(CW_LOG_WARNING, "Aiiiee. Tried to quit_chan non-existing Channel!\n");
}

/*! \brief  attempt_transfer: Attempt transfer of SIP call */
static int attempt_transfer(struct sip_pvt *p1, struct sip_pvt *p2)
{
    int res = 0;
    struct cw_channel 
        *chana = NULL,
        *chanb = NULL,
        *bridgea = NULL,
        *bridgeb = NULL,
        *peera = NULL,
        *peerb = NULL,
        *peerc = NULL,
        *peerd = NULL;

    if (!p1->owner || !p2->owner)
    {
        cw_log(CW_LOG_WARNING, "Transfer attempted without dual ownership?\n");
        return -1;
    }
    chana = p1->owner;
    chanb = p2->owner;
    bridgea = cw_bridged_channel(chana);
    bridgeb = cw_bridged_channel(chanb);

    
    // Will the other bridge ever be active?
    // i.e.: is peerd ever != NULL? -mc
    if (bridgea)
    {
        peera = chana;
        peerb = chanb;
        peerc = bridgea;
        peerd = bridgeb;
    }
    else if (bridgeb)
    {
        peera = chanb;
        peerb = chana;
        peerc = bridgeb;
        peerd = bridgea;
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Neither bridgea nor bridgeb?\n");
    }

    if (peera && peerb && peerc && (peerb != peerc))
    {
        cw_quiet_chan(peera);
        cw_quiet_chan(peerb);
        cw_quiet_chan(peerc);
        if (peerd)
            cw_quiet_chan(peerd);

        if (peera->cdr && peerb->cdr)
        {
            peerb->cdr = cw_cdr_append(peerb->cdr, peera->cdr);
        }
        else if (peera->cdr)
        {
            peerb->cdr = peera->cdr;
        }
        peera->cdr = NULL;

        if (peerb->cdr && peerc->cdr)
        {
            peerb->cdr = cw_cdr_append(peerb->cdr, peerc->cdr);
        }
        else if (peerc->cdr)
        {
            peerb->cdr = peerc->cdr;
        }
        peerc->cdr = NULL;
        
        if (cw_channel_masquerade(peerb, peerc))
        {
            cw_log(CW_LOG_WARNING, "Failed to masquerade %s into %s\n", peerb->name, peerc->name);
            res = -1;
        }
    }
    else
    {
        cw_log(CW_LOG_NOTICE, "Transfer attempted with no appropriate bridged calls to transfer\n");
        if (chana)
            cw_softhangup_nolock(chana, CW_SOFTHANGUP_DEV);
        if (chanb)
            cw_softhangup_nolock(chanb, CW_SOFTHANGUP_DEV);
        res = -1;
    }

    if (bridgea)
        cw_object_put(bridgea);
    if (bridgeb)
        cw_object_put(bridgeb);

    return res;
}


/*! \brief  handle_request_options: Handle incoming OPTIONS request */
static int handle_request_options(struct sip_pvt *p, struct sip_request *req, int debug)
{
    int res;

    CW_UNUSED(debug);

    res = get_destination(p, req);
    build_contact(p);
    /* XXX Should we authenticate OPTIONS? XXX */
    if (cw_strlen_zero(p->context))
        strcpy(p->context, default_context);
    if (res < 0)
        transmit_response_with_allow(p, "404 Not Found", req, 0);
    else if (res > 0)
        transmit_response_with_allow(p, "484 Address Incomplete", req, 0);
    else 
        transmit_response_with_allow(p, "200 OK", req, 0);
    /* Destroy if this OPTIONS was the opening request, but not if
       it's in the middle of a normal call flow. */
    if (!p->lastinvite)
        sip_destroy(p);

    return res;
}

/*! \brief  handle_request_invite: Handle incoming INVITE request */
static int handle_request_invite(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
    int res = 1;
    struct cw_channel *c = NULL;
    int gotdest;
    char *supported;
    char *required;
    unsigned int required_profile = 0;

    /* Note the current time so we can add delay info to the timestamp header
     * in the response.
     */
    cw_clock_gettime(global_clock_monotonic, &req->txtime);

    /* Find out what they support */
    if (!p->sipoptions)
    {
        if ((supported = get_header(req, SIP_HDR_NOSHORT("Supported"))) && supported[0])
            parse_sip_options(p, supported);
    }
    if ((required = get_header(req, SIP_HDR_NOSHORT("Required"))) && required[0])
    {
        required_profile = parse_sip_options(NULL, required);
        if (required_profile)
        {
            /* They require something */
            /* At this point we support no extensions, so fail */
            transmit_response_with_unsupported(p, "420 Bad extension", req, required);
            if (!p->lastinvite)
                sip_destroy(p);
            return -1;
        }
    }
    /* save the Request line */
    cw_copy_string(p->ruri, req->pkt.data + req->uriresp, sizeof(p->ruri));

    /* Check if this is a loop */
    /* This happens since we do not properly support SIP domain
       handling yet... -oej */
    if (cw_test_flag(p, SIP_OUTGOING) && p->owner && (p->owner->_state != CW_STATE_UP))
    {
        /* This is a call to ourself.  Send ourselves an error code and stop
           processing immediately, as SIP really has no good mechanism for
           being able to call yourself */
        transmit_response(p, "482 Loop Detected", req, 0);
        /* We do NOT destroy p here, so that our response will be accepted */
        return 0;
    }
    if (!ignore)
    {
        /* Use this as the basis */
        if (debug)
            cw_verbose("Using INVITE request as basis request - %s\n", p->callid);
        sip_cancel_destroy(p);
        /* This call is no longer outgoing if it ever was */
        cw_clear_flag(p, SIP_OUTGOING);
        /* This also counts as a pending invite */
        p->pendinginvite = req->seqno;
        copy_request(&p->initreq, req);
        if (p->owner)
        {
            /* Handle SDP here if we already have an owner */
            if (mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions)) < 0)
            {
                transmit_response(p, "488 Not acceptable here", req, 0);
                if (!p->lastinvite)
                    sip_destroy(p);
                return -1;
            }
            else
            {
                p->jointcapability = p->capability;
                cw_log(CW_LOG_DEBUG, "Hm....  No sdp for the moment\n");
            }
        }
    }
    else if (debug)
    {
        cw_verbose("Ignoring this INVITE request\n");
    }

    if (!p->lastinvite && !ignore && !p->owner)
    {
        /* Handle authentication if this is our first invite */
        res = check_user_full(p, req, SIP_INVITE, req->pkt.data + req->uriresp, 1, ignore, NULL, 0);
	if (res > 0)
	    return 0;
        if (res < 0)
        {
	    if (res == -4) {
		cw_log(CW_LOG_NOTICE, "Sending fake auth rejection for user %s\n", get_header(req, SIP_HDR_FROM));
		transmit_fake_auth_response(p, req, p->randdata, sizeof(p->randdata), 1);
	    } else {
		cw_log(CW_LOG_NOTICE, "Failed to authenticate user %s\n", get_header(req, SIP_HDR_FROM));
		cw_set_flag(req, FLAG_FATAL);
		transmit_response(p, "403 Forbidden", req, 1);
	    }
	    sip_destroy(p);
	    return 0;
        }
        /* Process the SDP portion */
        if (mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions)) < 0)
        {
            transmit_response(p, "488 Not acceptable here", req, 0);
            sip_destroy(p);
            return -1;
        }
        else
        {
            p->jointcapability = p->capability;
            cw_log(CW_LOG_DEBUG, "Hm....  No sdp for the moment\n");
        }
        /* Queue NULL frame to prod cw_rtp_bridge if appropriate */
        if (p->owner)
            cw_queue_frame(p->owner, &cw_null_frame);
        /* Initialize the context if it hasn't been already */
        if (cw_strlen_zero(p->context))
            strcpy(p->context, default_context);
        /* Check number of concurrent calls -vs- incoming limit HERE */
        cw_log(CW_LOG_DEBUG, "Checking SIP call limits for device %s\n", p->username);
        res = update_call_counter(p, INC_CALL_LIMIT);
        if (res)
        {
            if (res < 0)
            {
                cw_log(CW_LOG_NOTICE, "Failed to place call for user %s, too many calls\n", p->username);
		cw_set_flag(req, FLAG_FATAL);
                transmit_response(p, "480 Temporarily Unavailable (Call limit) ", req, 1);
                sip_destroy(p);
            }
            return 0;
        }
        /* Get destination right away */
        gotdest = get_destination(p, NULL);

        get_rdnis(p, NULL);
        extract_uri(p, req);
        build_contact(p);

        if (gotdest)
        {
            cw_set_flag(req, FLAG_FATAL);
            if (gotdest < 0)
                transmit_response(p, "404 Not Found", req, 1);
            else
                transmit_response(p, "484 Address Incomplete", req, 1);
            update_call_counter(p, DEC_CALL_LIMIT);
	    sip_destroy(p);
	    return 0;
        }

        /* If no extension was specified, use the s one */
        if (cw_strlen_zero(p->exten))
            cw_copy_string(p->exten, "s", sizeof(p->exten));
        /* First invitation */
        c = sip_new(p, CW_STATE_DOWN, cw_strlen_zero(p->username)  ?  NULL  :  p->username);
        /* Save Record-Route for any later requests we make on this dialogue */
	/* FIXME: is this right? Shouldn't we only save it from a response to the INVITE? */
        build_route(p, req, 0);
        set_destination(p, (p->route ? p->route->hop : p->okcontacturi));
    }
    else
    {
        if (option_debug > 1  &&  sipdebug)
            cw_log(CW_LOG_DEBUG, "Got a SIP re-invite for call %s\n", p->callid);
        c = p->owner;
    }
    if (!ignore  &&  p)
        p->lastinvite = req->seqno;
    if (c)
    {
#ifdef OSP_SUPPORT
        cw_channel_setwhentohangup(c, p->osptimelimit);
#endif
        switch (c->_state)
        {
        case CW_STATE_DOWN:
            transmit_response(p, "100 Trying", req, 0);
            cw_setstate(c, CW_STATE_RING);
            if (strcmp(p->exten, cw_pickup_ext()))
            {
                enum cw_pbx_result pbxres = cw_pbx_start(c);

                if (pbxres != CW_PBX_SUCCESS)
                {
                    switch (pbxres)
                    {
                    case CW_PBX_FAILED:
                        cw_log(CW_LOG_WARNING, "Failed to start PBX :(\n");
                        if (ignore)
                            transmit_response(p, "503 Unavailable", req, 0);
                        else {
                            cw_set_flag(req, FLAG_FATAL);
                            transmit_response(p, "503 Unavailable", req, 1);
                        }
                        break;
                    case CW_PBX_CALL_LIMIT:
                        cw_log(CW_LOG_WARNING, "Failed to start PBX (call limit reached) \n");
                        if (ignore)
                            transmit_response(p, "480 Temporarily Unavailable", req, 0);
                        else {
                            cw_set_flag(req, FLAG_FATAL);
                            transmit_response(p, "480 Temporarily Unavailable", req, 1);
                        }
                        break;
                    case CW_PBX_SUCCESS:
                        /* nothing to do */
                        break;
                    }

                    cw_log(CW_LOG_WARNING, "Failed to start PBX :(\n");
                    /* Unlock locks so cw_hangup can do its magic */
                    cw_channel_unlock(c);
                    cw_mutex_unlock(&p->lock);
                    cw_hangup(c);
                    cw_mutex_lock(&p->lock);
                    c = NULL;
                }
            }
            else
            {
                cw_channel_unlock(c);
                if (cw_pickup_call(c))
                {
                    cw_log(CW_LOG_NOTICE, "Nothing to pick up\n");
                    if (ignore)
                        transmit_response(p, "503 Unavailable", req, 0);
                    else {
                        cw_set_flag(req, FLAG_FATAL);
                        transmit_response(p, "503 Unavailable", req, 1);
                    }
                    cw_set_flag(p, SIP_ALREADYGONE);    
                    /* Unlock locks so cw_hangup can do its magic */
                    cw_mutex_unlock(&p->lock);
                    cw_hangup(c);
                    cw_mutex_lock(&p->lock);
                    c = NULL;
                }
                else
                {
                    cw_mutex_unlock(&p->lock);
                    cw_setstate(c, CW_STATE_DOWN);
                    cw_hangup(c);
                    cw_mutex_lock(&p->lock);
                    c = NULL;
                }
            }
            break;
        case CW_STATE_RING:
            transmit_response(p, "100 Trying", req, 0);
            break;
        case CW_STATE_RINGING:
            transmit_response(p, "180 Ringing", req, 0);
            break;
        case CW_STATE_UP:
            if (p->t38state == SIP_T38_OFFER_RECEIVED_REINVITE)
            {
                struct cw_channel *bridgepeer = NULL;
                struct sip_pvt *bridgepvt = NULL;
                
                if ((bridgepeer = cw_bridged_channel(p->owner)))
                {
                    /* We have a bridge, and this is re-invite to switchover to T38 so we send re-invite with T38 SDP, to other side of bridge*/
                    /*! XXX: we should also check here does the other side supports t38 at all !!! XXX */  
                    if (!strcasecmp(bridgepeer->type,"SIP"))
                    {
                        /* If we are bridged to SIP channel */
                        if ((bridgepvt = (struct sip_pvt *) bridgepeer->tech_pvt))
                        {
                            if (bridgepvt->t38state >= SIP_T38_STATUS_UNKNOWN)
                            {
                                if (bridgepvt->udptl)
                                {
                                    /* If everything is OK with other side's udptl struct */
                                    /* Send re-invite to the bridged channel */ 
                                    sip_handle_t38_reinvite(bridgepeer, p, 1);
                                    cw_channel_set_t38_status(bridgepeer, T38_NEGOTIATING);
                                }
                                else
                                {
                                    /* Something is wrong with peers udptl struct */
                                    cw_log(CW_LOG_WARNING, "Strange... The other side of the bridge don't have udptl struct\n");
                                    cw_mutex_lock(&bridgepvt->lock);
                                    bridgepvt->t38state = SIP_T38_STATUS_UNKNOWN;
                                    cw_mutex_unlock(&bridgepvt->lock);
                                    cw_log(CW_LOG_DEBUG,"T38 state changed to %d on channel %s\n",bridgepvt->t38state, bridgepeer->name);
                                    p->t38state = SIP_T38_STATUS_UNKNOWN;
                                    cw_log(CW_LOG_DEBUG,"T38 state changed to %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
                                    if (ignore)
                                        transmit_response(p, "415 Unsupported Media Type", req, 0);
                                    else {
                                        cw_set_flag(req, FLAG_FATAL);
                                        transmit_response(p, "415 Unsupported Media Type", req, 1);
                                    }
                                    sip_destroy(p);
                                } 
                            }
                        }
                        else
                        {
                            cw_log(CW_LOG_WARNING, "Strange... The other side of the bridge don't seem to exist\n");
                        }
                    }
                    else
                    {
                        /* Other side is not a SIP channel */
                        if (ignore)
                            transmit_response(p, "415 Unsupported Media Type", req, 0);
                        else {
                                cw_set_flag(req, FLAG_FATAL);
                                transmit_response(p, "415 Unsupported Media Type", req, 1);
                        }
                        p->t38state = SIP_T38_STATUS_UNKNOWN;
                        cw_log(CW_LOG_DEBUG,"T38 state changed to %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
                        sip_destroy(p);
                    }    
                    cw_object_put(bridgepeer);
                }
                else
                {
                    /* we are not bridged in a call */ 
                    transmit_response_with_t38_sdp(p, "200 OK", req, 1);
                    p->t38state = SIP_T38_NEGOTIATED;
                    cw_log(CW_LOG_DEBUG,"T38 state changed to %d on channel %s\n",p->t38state, p->owner ? p->owner->name : "<none>");
                    if (p->owner)
                    {
                        cw_channel_set_t38_status(p->owner, T38_NEGOTIATED);
                        cw_log(CW_LOG_DEBUG,"T38mode enabled for channel %s\n", p->owner->name);
                    }
                }
            }
            else if (p->t38state == SIP_T38_STATUS_UNKNOWN)
            {
                /* Channel doesn't have T38 offered or enabled */
                /* If we are bridged to a channel that has T38 enabled than this is a case of RTP re-invite after T38 session */
                /* so handle it here (re-invite other party to RTP) */
                struct cw_channel *bridgepeer = NULL;
                struct sip_pvt *bridgepvt = NULL;
    
                if ( (bridgepeer = cw_bridged_channel(p->owner) ) && !strcasecmp(bridgepeer->type,"SIP") )
		{
                    {
                        bridgepvt = (struct sip_pvt*)bridgepeer->tech_pvt;
                        if (bridgepvt->t38state == SIP_T38_NEGOTIATED)
                        {
                            cw_log(CW_LOG_WARNING, "RTP re-invite after T38 session not handled yet !\n");
                            /* Insted of this we should somehow re-invite the other side of the bridge to RTP */
                            if (ignore)
                                transmit_response(p, "488 Not Acceptable Here (unsupported)", req, 0);
                            else {
                                cw_set_flag(req, FLAG_FATAL);
                                transmit_response(p, "488 Not Acceptable Here (unsupported)", req, 1);
                            }
                            sip_destroy(p);
                        }
                        else
                        {
                            /* No bridged peer with T38 enabled*/
                            transmit_response_with_sdp(p, "200 OK", req, 1);
                        }
                    }
                    cw_object_put(bridgepeer);
                }
        	else
		    transmit_response_with_sdp(p, "200 OK", req, 1);
            } 
            break;
        default:
            cw_log(CW_LOG_WARNING, "Don't know how to handle INVITE in state %d\n", c->_state);
            transmit_response(p, "100 Trying", req, 0);
        }
    }
    else
    {
        if (p && !ignore)
        {
            cw_set_flag(req, FLAG_FATAL);
            if (!p->jointcapability)
                transmit_response(p, "488 Not Acceptable Here (codec error)", req, 1);
            else
            {
                cw_log(CW_LOG_NOTICE, "Unable to create/find channel\n");
                transmit_response(p, "503 Unavailable", req, 1);
            }
            sip_destroy(p);
        }
    }
    return res;
}

/*! \brief  handle_request_refer: Handle incoming REFER request */
static int handle_request_refer(struct sip_pvt *p, struct sip_request *req, int debug, int ignore, int *nounlock)
{
    struct cw_channel *c=NULL;
    struct cw_channel *transfer_to;
    struct cw_var_t *transfercontext;
    int res;

    CW_UNUSED(debug);

    if (option_debug > 2)
        cw_log(CW_LOG_DEBUG, "SIP call transfer received for call %s (REFER)!\n", p->callid);

    transfercontext = pbx_builtin_getvar_helper(p->owner, CW_KEYWORD_TRANSFER_CONTEXT, "TRANSFER_CONTEXT");
    res = get_refer_info(p, req, (transfercontext ? transfercontext->value : p->context));

    if (cw_strlen_zero(p->context))
        strcpy(p->context, default_context);

    if (res < 0)
        transmit_response(p, "603 Declined", req, 0);
    else if (res > 0)
        transmit_response(p, "484 Address Incomplete", req, 0);
    else
    {
        int nobye = 0;
        
        if (!ignore)
        {
            if (p->refer_call)
            {
                cw_log(CW_LOG_DEBUG,"202 Accepted (supervised)\n");
                attempt_transfer(p, p->refer_call);
                if (p->refer_call->owner)
                    cw_channel_unlock(p->refer_call->owner);
                cw_mutex_unlock(&p->refer_call->lock);
                p->refer_call = NULL;
                cw_set_flag(p, SIP_GOTREFER);    
            }
            else
            {
                cw_log(CW_LOG_DEBUG,"202 Accepted (blind)\n");
                c = p->owner;
                if (c)
                {
                    if ((transfer_to = cw_bridged_channel(c)))
                    {
                        cw_log(CW_LOG_DEBUG, "Got SIP blind transfer, applying to '%s'\n", transfer_to->name);
                        cw_moh_stop(transfer_to);
                        if (!strcmp(p->refer_to, cw_parking_ext()))
                        {
                            /* Must release c's lock now, because it will not longer
                               be accessible after the transfer! */
                            *nounlock = 1;
                            cw_channel_unlock(c);
                            sip_park(transfer_to, c, req);
                            nobye = 1;
                        }
                        else
                        {
                            /* Must release c's lock now, because it will not longer
                                be accessible after the transfer! */
                            *nounlock = 1;
                            cw_channel_unlock(c);
                            cw_async_goto_n(transfer_to, (transfercontext ? transfercontext->value : p->context), p->refer_to, 1);
                        }
                        cw_object_put(transfer_to);
                    }
                    else
                    {
                        cw_log(CW_LOG_DEBUG, "Got SIP blind transfer but nothing to transfer to.\n");
                        cw_queue_hangup(p->owner);
                    }
                }
                cw_set_flag(p, SIP_GOTREFER);    
            }
            transmit_response(p, "202 Accepted", req, 0);
            transmit_notify_with_sipfrag(p, req->seqno);
            /* Always increment on a BYE */
            if (!nobye)
            {
                transmit_request_with_auth(p, SIP_BYE, 0, 1, 1);
                cw_set_flag(p, SIP_ALREADYGONE);    
            }
        }
    }

    if (transfercontext)
        cw_object_put(transfercontext);

    return res;
}
/*! \brief  handle_request_cancel: Handle incoming CANCEL request */
static int handle_request_cancel(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
    int ret;

    CW_UNUSED(debug);

    cw_set_flag(p, SIP_ALREADYGONE);    
    if (p->rtp)
    {
        /* Immediately stop RTP */
        cw_rtp_stop(p->rtp);
    }
    if (p->vrtp)
    {
        /* Immediately stop VRTP */
        cw_rtp_stop(p->vrtp);
    }
    if (p->udptl)
    {
        /* Immediately stop T.38 UDPTL */
        cw_udptl_stop(p->udptl);
    }
    if (p->initreq.pkt.used > 0)
    {
        if (!ignore) {
            cw_set_flag(&p->initreq, FLAG_FATAL);
            transmit_response(p, "487 Request Terminated", &p->initreq, 1);
        }
        transmit_response(p, "200 OK", req, 0);
        ret = 1;
    }
    else
    {
        transmit_response(p, "481 Call Leg Does Not Exist", req, 0);
        ret = 0;
    }

    if (p->owner)
        cw_queue_hangup(p->owner);
    else
        sip_destroy(p);

    return ret;
}

/*! \brief  handle_request_bye: Handle incoming BYE request */
static int handle_request_bye(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
    struct cw_channel *c = NULL;
    struct cw_channel *bridged_to;
    int res;

    CW_UNUSED(debug);

    if (p->pendinginvite && !cw_test_flag(p, SIP_OUTGOING) && !ignore && !p->owner) {
        cw_set_flag(&p->initreq, FLAG_FATAL);
        transmit_response(p, "487 Request Terminated", &p->initreq, 1);
    }

    copy_request(&p->initreq, req);
    cw_set_flag(p, SIP_ALREADYGONE);    
    if (p->rtp)
    {
        /* Immediately stop RTP */
        cw_rtp_stop(p->rtp);
    }
    if (p->vrtp)
    {
        /* Immediately stop VRTP */
        cw_rtp_stop(p->vrtp);
    }
    if (p->udptl)
    {
        /* Immediately stop T.38 UDPTL */
        cw_udptl_stop(p->udptl);
    }
    if (!cw_strlen_zero(get_header(req, SIP_HDR_NOSHORT("Also"))))
    {
        cw_log(CW_LOG_NOTICE, "Client %#l@ (peer %#l@) using deprecated BYE/Also transfer method.  Ask vendor to support REFER instead\n", &req->recvdaddr.sa, &p->peeraddr.sa);
        if (cw_strlen_zero(p->context))
            strcpy(p->context, default_context);
        res = get_also_info(p, req);
        if (!res)
        {
            c = p->owner;
            if (c)
            {
                if ((bridged_to = cw_bridged_channel(c)))
                {
                    /* Don't actually hangup here... */
                    cw_moh_stop(bridged_to);
                    cw_async_goto_n(bridged_to, p->context, p->refer_to, 1);
                    cw_object_put(bridged_to);
                }
                else
                    cw_queue_hangup(p->owner);
            }
        }
        else
        {
            cw_log(CW_LOG_WARNING, "Invalid transfer information from %#l@ (peer %#l@)\n", &p->peeraddr.sa, &req->recvdaddr.sa);
            cw_queue_hangup(p->owner);
        }
    }
    else if (p->owner)
        cw_queue_hangup(p->owner);
    else
        sip_destroy(p);
    transmit_response(p, "200 OK", req, 0);

    return 1;
}

/*! \brief  handle_request_message: Handle incoming MESSAGE request */
static int handle_request_message(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
    if (!ignore)
    {
        if (debug)
            cw_verbose("Receiving message!\n");
        receive_message(p, req);
    }
    else
    {
        transmit_response(p, "202 Accepted", req, 0);
    }
    return 1;
}


static int purge_old_subscription(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct sip_pvt *new_dialogue = data;

	if (dialogue != new_dialogue && dialogue->initreq.method == SIP_SUBSCRIBE && dialogue->subscribed != NONE) {
		cw_mutex_lock(&dialogue->lock);

		if (!strcmp(dialogue->username, new_dialogue->username)
		 && !strcmp(dialogue->exten, new_dialogue->exten) && !strcmp(dialogue->context, new_dialogue->context)) {
			sip_destroy(dialogue);
			cw_mutex_unlock(&dialogue->lock);
			return 1;
		}

		cw_mutex_unlock(&dialogue->lock);
	}

	return 0;
}

/*! \brief  handle_request_subscribe: Handle incoming SUBSCRIBE request */
static int handle_request_subscribe(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
    int gotdest;
    int res = 0;
    int firststate = CW_EXTENSION_REMOVED;

    if (p->initreq.pkt.used)
    {    
        /* We already have a dialog */
        if (p->initreq.method != SIP_SUBSCRIBE)
        {
            /* This is a SUBSCRIBE within another SIP dialog, which we do not support */
            /* For transfers, this could happen, but since we haven't seen it happening, let us just refuse this */
             transmit_response(p, "403 Forbidden (within dialog)", req, 0);
            /* Do not destroy session, since we will break the call if we do */
            cw_log(CW_LOG_DEBUG, "Got a subscription within the context of another call, can't handle that - %s (Method %s)\n", p->callid, sip_methods[p->initreq.method].text);
            return 0;
        }
        else
        {
            if (debug)
                cw_log(CW_LOG_DEBUG, "Got a re-subscribe on existing subscription %s\n", p->callid);
        }
    }
    if (!ignore && !p->initreq.pkt.used)
    {
        /* Use this as the basis */
        if (debug)
            cw_verbose("Using latest SUBSCRIBE request as basis request\n");
        /* This call is no longer outgoing if it ever was */
        cw_clear_flag(p, SIP_OUTGOING);
        copy_request(&p->initreq, req);
    }
    else if (debug && ignore)
        cw_verbose("Ignoring this SUBSCRIBE request\n");

    if (!p->lastinvite)
    {
        char mailboxbuf[256]="";
        int found = 0;
        char *mailbox = NULL;
        int mailboxsize = 0;
	char *eventparam;

        char *event = get_header(req, SIP_HDR_EVENT);    /* Get Event package name */
        const char *hdr_accept = get_header(req, SIP_HDR_NOSHORT("Accept"));

	/* Find parameters to Event: header value and remove them for now */
	eventparam = strchr(event, ';');
	if (eventparam) {
		*eventparam = '\0';
		eventparam++;
	}

        if (!strcmp(event, "message-summary") && !strcmp(hdr_accept, "application/simple-message-summary"))
        {
            mailbox = mailboxbuf;
            mailboxsize = sizeof(mailboxbuf);
        }
        /* Handle authentication if this is our first subscribe */
        res = check_user_full(p, req, SIP_SUBSCRIBE, req->pkt.data + req->uriresp, 0, ignore, mailbox, mailboxsize);
	/* if an authentication challenge was sent, we are done here */
	if (res > 0)
	    return 0;
        if (res<0)
        {
		if (res == -4) {
			cw_log(CW_LOG_NOTICE, "Sending fake auth rejection for user %s\n", get_header(req, SIP_HDR_FROM));
			transmit_fake_auth_response(p, req, p->randdata, sizeof(p->randdata), 1);
		} else {
			cw_log(CW_LOG_NOTICE, "Failed to authenticate user %s for SUBSCRIBE\n", get_header(req, SIP_HDR_FROM));
			if (ignore)
				transmit_response(p, "403 Forbidden", req, 0);
			else {
				cw_set_flag(req, FLAG_FATAL);
				transmit_response(p, "403 Forbidden", req, 1);
			}
		}
		sip_destroy(p);
		return 0;
        }
        gotdest = get_destination(p, NULL);

        /* Initialize the context if it hasn't been already 
	   note this is done _after_ handling any domain lookups,
	   because the context specified there is for calls, not
	   subscriptions
	*/
        if (!cw_strlen_zero(p->subscribecontext))
            cw_copy_string(p->context, p->subscribecontext, sizeof(p->context));
        else if (cw_strlen_zero(p->context))
            strcpy(p->context, default_context);
        /* Get destination right away */
        build_contact(p);
        if (gotdest)
        {
            if (gotdest < 0)
                transmit_response(p, "404 Not Found", req, 0);
            else
                transmit_response(p, "484 Address Incomplete", req, 0);    /* Overlap dialing on SUBSCRIBE?? */
            sip_destroy(p);
        }
        else
        {
            if (!strcmp(event, "presence") || !strcmp(event, "dialog"))
            {
                /* Presence, RFC 3842 */

                /* Header from Xten Eye-beam Accept: multipart/related, application/rlmi+xml, application/pidf+xml, application/xpidf+xml */
                /* Polycom phones only handle xpidf+xml, even if they say they can
                   handle pidf+xml as well
                */
                if (strstr(p->useragent, "Polycom"))
                {
                    p->subscribed = XPIDF_XML;
                }
                else if (strstr(hdr_accept, "application/pidf+xml"))
                {
                    p->subscribed = PIDF_XML;         /* RFC 3863 format */
                }
                else if (strstr(hdr_accept, "application/dialog-info+xml"))
                {
                    p->subscribed = DIALOG_INFO_XML;
                    /* IETF draft: draft-ietf-sipping-dialog-package-05.txt */
                }
                else if (strstr(hdr_accept, "application/cpim-pidf+xml"))
                {
                    p->subscribed = CPIM_PIDF_XML;    /* RFC 3863 format */
                }
                else if (strstr(hdr_accept, "application/xpidf+xml"))
                {
                    p->subscribed = XPIDF_XML;        /* Early pre-RFC 3863 format with MSN additions (Microsoft Messenger) */
		} else if (cw_strlen_zero(hdr_accept)) {
		    if (p->subscribed == NONE) { 
			/* if the subscribed field is not already set, and there is no accept header... */
			transmit_response(p, "489 Bad Event", req, 0);
			cw_log(CW_LOG_WARNING,"SUBSCRIBE failure: no Accept header: pvt: stateid: %d, laststate: %d, dialogver: %d, subscribecont: '%s'\n",
					p->stateid, p->laststate, p->dialogver, p->subscribecontext);
			sip_destroy(p);
			return 0;
		    }
		    /* if p->subscribed is non-zero, then accept is not obligatory; according to rfc 3265 section 3.1.3, at least.
			so, we'll just let it ride, keeping the value from a previous subscription, and not abort the subscription */
                }
		else
                {
                    /* Can't find a format for events that we know about */
		    char mybuf[200];
		    snprintf(mybuf,sizeof(mybuf),"489 Bad Event (format %s)", hdr_accept);
		    transmit_response(p, mybuf, req, 0);
		    sip_destroy(p);
                    return 0;
                }
		if (option_debug > 2) {
		    const struct cfsubscription_types *st = find_subscription_type(p->subscribed);
		    if ( st != NULL )
			cw_log(CW_LOG_DEBUG, "Subscription type: Event: %s Format: %s\n",  st->event, st->mediatype);
		}		    
		    
            }
            else if (!strcmp(event, "message-summary") && !strcmp(hdr_accept, "application/simple-message-summary"))
            {
                /* Looks like they actually want a mailbox status */

                /* At this point, we should check if they subscribe to a mailbox that
                  has the same extension as the peer or the mailbox id. If we configure
                  the context to be the same as a SIP domain, we could check mailbox
                  context as well. To be able to securely accept subscribes on mailbox
                  IDs, not extensions, we need to check the digest auth user to make
                  sure that the user has access to the mailbox.
                 
                  Since we do not act on this subscribe anyway, we might as well 
                  accept any authenticated peer with a mailbox definition in their 
                  config section.
                
                */
                if (!cw_strlen_zero(mailbox))
                {
                    found++;
                }

                if (found)
                    transmit_response(p, "200 OK", req, 0);
                else
                    transmit_response(p, "404 Not found", req, 0);
                sip_destroy(p);
                return 0;
            }
            else
            {
                /* At this point, CallWeaver does not understand the specified event */
                transmit_response(p, "489 Bad Event", req, 0);
                if (option_debug > 1)
                    cw_log(CW_LOG_DEBUG, "Received SIP subscribe for unknown event package: %s\n", event);
                sip_destroy(p);
                return 0;
            }
            if (p->subscribed != NONE)
                p->stateid = cw_extension_state_add(p->context, p->exten, cb_extensionstate, p);
        }
    }

    if (!ignore && p)
        p->lastinvite = req->seqno;
    if (p)
    {
        p->expiry = atoi(get_header(req, SIP_HDR_NOSHORT("Expires")));
        /* The next 4 lines can be removed if the SNOM Expires bug is fixed */
        if (p->subscribed == DIALOG_INFO_XML)
        {
            if (p->expiry > max_expiry)
                p->expiry = max_expiry;
        }
        if (sipdebug  ||  option_debug > 1)
            cw_log(CW_LOG_DEBUG, "Adding subscription for extension %s context %s for peer %s\n", p->exten, p->context, p->username);
        if (p->autokillid > -1)
            sip_cancel_destroy(p);    /* Remove subscription expiry for renewals */
        sip_scheddestroy(p, (p->expiry + 10) * 1000);    /* Set timer for destruction of call at expiration */

        if ((firststate = cw_extension_state(NULL, p->context, p->exten)) < 0)
        {
            cw_log(CW_LOG_ERROR, "Got SUBSCRIBE for extensions without hint. Please add hint to %s in context %s\n", p->exten, p->context);
            transmit_response(p, "404 Not found", req, 0);
            sip_destroy(p);
            return 0;
        }
        else
        {
            transmit_response(p, "200 OK", req, 0);
            transmit_state_notify(p, firststate, 1, 1, 0);    /* Send first notification */

	    /* remove any old subscription from this peer for the same exten/context,
	       as the peer has obviously forgotten about it and it's wasteful to wait
	       for it to expire and send NOTIFY messages to the peer only to have them
	       ignored (or generate errors)
	    */
	    /* FIXME: this is a bit expensive if we're dealing with many dialogues, no? */
	    cw_registry_iterate(&dialogue_registry, purge_old_subscription, p);
        }
        if (!p->expiry)
            sip_destroy(p);
    }
    return 1;
}

/*! \brief  handle_request_register: Handle incoming REGISTER request */
static int handle_request_register(struct sip_pvt *p, struct sip_request *req, int debug, int ignore)
{
	int res;

	/* Use this as the basis */
	if (debug)
		cw_verbose("Using latest REGISTER request as basis request\n");

	copy_request(&p->initreq, req);

	if ((res = register_verify(p, req, req->pkt.data + req->uriresp, ignore)) < 0) {
		cw_log(CW_LOG_NOTICE, "Registration of '%s' from %#l@ failed: %s\n", get_header(req, SIP_HDR_TO), &req->recvdaddr.sa, (res == -1 ? "Wrong password" : (res == -2 ? "Username/auth name mismatch" : "Not a local SIP domain")));
	}

	if (res < 1) {
		/* Destroy the session, but keep us around for just a bit in case they don't get our 200 OK */
		sip_scheddestroy(p, -1);
	}

	return res;
}

/*! \brief  handle_request: Handle SIP requests (methods) */
/*      this is where all incoming requests go first   */
static int handle_request(struct sip_pvt *p, struct sip_request *req, int *nounlock)
{
    /* Called with p->lock held, as well as p->owner->lock if appropriate, keeping things
       relatively static */
    char *s;
    int ignore = 0;
    int res = 0;
    int debug = sip_debug_test_pvt(p);

    /* New SIP request coming in
       (could be new request in existing SIP dialog as well...)
     */

    if (option_debug > 2)
        cw_log(CW_LOG_DEBUG, "**** Received %s (%d) - Command in SIP %s\n", sip_methods[req->method].text, (int)req->method, req->pkt.data);

    /* RFC3261: 12.2.2
     *     If the remote sequence number was not empty, and
     *     the sequence number of the request is greater than the remote
     *     sequence number, the request is in order.  It is possible for the
     *     CSeq sequence number to be higher than the remote sequence number by
     *     more than one.  This is not an error condition, and a UAS SHOULD be
     *     prepared to receive and process requests with CSeq values more than
     *     one higher than the previous received request.  The UAS MUST then set
     *     the remote sequence number to the value of the sequence number in the
     *     CSeq header field value in the request.
     */
    if (req->seqno > p->icseq)
        p->icseq = req->seqno;
    else if (p->icseq)
    {
        /* RFC3261: 12.2.2:
         *     If the remote sequence number was not empty, but the sequence number
         *     of the request is lower than the remote sequence number, the request
         *     is out of order and MUST be rejected with a 500 (Server Internal
         *     Error) response.
         */
        if (req->seqno < p->icseq)
        {
            if (req->method == SIP_ACK)
            {
                return 0;
            }
            else
            {
	        if (option_debug)
                    cw_log(CW_LOG_DEBUG, "Ignoring too old SIP packet packet %u (expecting >= %u)\n", req->seqno, p->icseq);
                transmit_response(p, "500 Server Internal Error", req, 0);
                return -1;
            }
        }
        else if (p->icseq == req->seqno && req->method != SIP_ACK && (req->method != SIP_CANCEL || cw_test_flag(p, SIP_ALREADYGONE)))
        {
            /* ignore means "don't do anything with it" but we still have to
             * respond appropriately.  We do this if we receive a repeat of
             * the last sequence number
	     */
            ignore = 2;
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Ignoring SIP message because of retransmit (%s Seqno %u, ours %u)\n", sip_methods[req->method].text, p->icseq, req->seqno);
        }
    }

#if 0
    /* FIXME: just because someone sends us a request we start talking to
     * them instead of the original peer? That doesn't seem right?
     */
    if (!ignore) {
        p->peeraddr = req->recvdaddr;
        if (p->conn != req->conn) {
            cw_object_put(p->conn);
            p->conn = cw_object_dup(req->conn);
        }
    }
#endif

    if (pedanticsipchecking)
    {
        /* If this is a request packet without a from tag, it's not
            correct according to RFC 3261  */
        /* Check if this a new request in a new dialog with a totag already attached to it,
            RFC 3261 - section 12.2 - and we don't want to mess with recovery  */
        if (!p->initreq.pkt.used && cw_test_flag(req, SIP_PKT_WITH_TOTAG))
        {
            /* If this is a first request and it got a to-tag, it is not for us */
            if (!ignore && req->method == SIP_INVITE)
            {
		cw_set_flag(req, FLAG_FATAL);
                transmit_response(p, "481 Call/Transaction Does Not Exist", req, 1);
                /* Will cease to exist after ACK */
            }
	    else if (req->method != SIP_ACK)
            {
                transmit_response(p, "481 Call/Transaction Does Not Exist", req, 0);
                sip_destroy(p);
            }
            return res;
        }
    }
    if (!req->pkt.data[req->uriresp] && (req->method == SIP_INVITE || req->method == SIP_SUBSCRIBE || req->method == SIP_REGISTER)) {
        transmit_response(p, "400 Bad request", req, 0);
        sip_destroy(p);
        return -1;
    }

    if (!ignore)
    {
        snprintf(p->lastmsg, sizeof(p->lastmsg), "Rx: %s", req->pkt.data);

        s = get_header(req, SIP_HDR_NOSHORT("User-Agent"));
        if (s && s[0])
        {
            cw_copy_string(p->useragent, s, sizeof(p->useragent));
            if (p->rtp)
                p->rtp->bug_sonus = (strstr(s, "Sonus_UAC") != NULL);
        }
    }

    /* Handle various incoming SIP methods in requests */
    switch (req->method)
    {
    case SIP_OPTIONS:
        res = handle_request_options(p, req, debug);
        break;
    case SIP_INVITE:
        res = handle_request_invite(p, req, debug, ignore);
        break;
    case SIP_REFER:
        res = handle_request_refer(p, req, debug, ignore, nounlock);
        break;
    case SIP_CANCEL:
        res = handle_request_cancel(p, req, debug, ignore);
        break;
    case SIP_BYE:
        res = handle_request_bye(p, req, debug, ignore);
        break;
    case SIP_MESSAGE:
        res = handle_request_message(p, req, debug, ignore);
        break;
    case SIP_SUBSCRIBE:
        res = handle_request_subscribe(p, req, debug, ignore);
        break;
    case SIP_REGISTER:
        res = handle_request_register(p, req, debug, ignore);
        break;
    case SIP_INFO:
        handle_request_info(p, req, ignore);
        break;
    case SIP_PUBLISH:
        handle_request_publish(p, req, ignore);
        break;
    case SIP_NOTIFY:
        /* XXX we get NOTIFY's from some servers. WHY?? Maybe we should
            look into this someday XXX */
        transmit_response(p, "200 OK", req, 0);
        if (!p->lastinvite) 
            sip_destroy(p);
        break;
    case SIP_ACK:
        /* Make sure we don't ignore this */
        if (req->seqno == p->pendinginvite)
        {
            p->pendinginvite = 0;
            if (mime_process(p, req, ignore, mime_sdp_actions, arraysize(mime_sdp_actions)) < 0)
                return -1;
            check_pendings(p);
        }
        if (!p->lastinvite && cw_strlen_zero(p->randdata))
            sip_destroy(p);
        break;
    default:
        transmit_response_with_allow(p, "501 Method Not Implemented", req, 0);
        cw_log(CW_LOG_NOTICE, "Unknown SIP command '%s' from %#l@ (received from %#l@)\n", req->pkt.data, &p->peeraddr.sa, &req->recvdaddr.sa);
        /* If this is some new method, and we don't have a call, destroy it now */
        if (!p->initreq.pkt.used)
            sip_destroy(p);
        break;
    }

    return (ignore ? 0 : res);
}


static int handle_message(void *data)
{
	struct dialogue_key key;
	struct cw_object *obj;
	struct sip_request *req = data;
	struct sip_pvt *dialogue;
	unsigned int hash;
	char *p;
	int found, i;
	int nounlock = 0;

	hash = 0;
	key.callid = p = req->pkt.data + req->callid;
	while (*p)
		hash = cw_hash_add(hash, *(p++));

	found = 0;
	dialogue = NULL;
	if ((obj = cw_registry_find(&dialogue_registry, 1, hash, &key))) {
		found = 1;
		dialogue = container_of(obj, struct sip_pvt, obj);

		i = cw_mutex_trylock(&dialogue->lock);
		if (!i && dialogue->owner && (i = cw_channel_trylock(dialogue->owner)))
			cw_mutex_unlock(&dialogue->lock);
		if (i) {
			cw_object_put(dialogue);
			return 1;
		}

		/* If the dialogue already knows their tag the tag in the
		 * message must match. If we don't have their tag yet any
		 * message from any potential fork is valid.
		 */
		if (dialogue->theirtag_len && (dialogue->theirtag_len != req->taglen || memcmp(dialogue->theirtag, req->pkt.data + req->tag, dialogue->theirtag_len))) {
			cw_mutex_unlock(&dialogue->lock);
			if (dialogue->owner)
				cw_channel_unlock(dialogue->owner);
			cw_object_put(dialogue);
			dialogue = NULL;
		}
	}

	if (req->method == SIP_RESPONSE) {
		if (dialogue) {
			/* If the message is out of sequence we have nothing more to do here */
			if (req->seqno == dialogue->ocseq) {
				/* If we have no tag and this is a non-provisional
				 * response we save the tag now.
				 * FIXME: If there is SDP maybe we should set the tag too so we claim
				 * the call for whatever early media is being sent? But that causes
				 * other legs to be abandonned. Maybe we don't want that. Imagine some
				 * automated queue system that wants to keep saying how important the
				 * call is while trying to find someone to take it while, at the same
				 * time, there is a phone registered and ringing in parallel with it.
				 * Currently there are problems if more than one leg wants to send
				 * early media but the above will probably work. If we claim the call
				 * as soon as we see SDP only the first leg that offers early media
				 * will ever have a chance of taking the call.
				 */
				if (!dialogue->theirtag_len && *(req->pkt.data + req->uriresp) != '1' && req->taglen) {
					if (req->taglen < sizeof(dialogue->theirtag) - 1) {
						memcpy(dialogue->theirtag, req->pkt.data + req->tag, (dialogue->theirtag_len = req->taglen));
						dialogue->theirtag[req->taglen] = '\0';
					} else {
						cw_log(CW_LOG_ERROR, "%#l@ sent a tag longer than we can handle (%d > %lu, tag = \"%.*s\"\n", &req->recvdaddr.sa, req->taglen, (unsigned long)sizeof(dialogue->theirtag), req->taglen, req->pkt.data + req->tag);

						transmit_error(req, "500 Server internal error");
						sip_scheddestroy(dialogue, -1);
						cw_mutex_unlock(&dialogue->lock);
						if (dialogue->owner)
							cw_channel_unlock(dialogue->owner);
						cw_object_put(dialogue);
						dialogue = NULL;
					}
				}
			}

			if (dialogue) {
				/* Cease retransmissions of whatever the response is to */
				retrans_stop(dialogue, req, req->cseq_method);
				handle_response(dialogue, req, (dialogue->ocseq && (dialogue->ocseq != req->seqno)));
			}
		} else if (found) {
			/* We found the call ID but the tags didn't match. Maybe there's a
			 * forking proxy in the way and the call has already been claimed
			 * by something else?
			 */
			/* FIXME: we SHOULD send a CANCEL or BYE here? */
			transmit_error(req, "481 Call leg/transaction does not exist");
		}
	} else {
		if (dialogue && req->method == SIP_ACK)
			retrans_stop(dialogue, req, SIP_RESPONSE);
		else if (!found) {
			/* Nothing is known about this call so we create it if it makes sense */

			if (!cw_blacklist_check(&req->recvdaddr.sa)) {
				/* FIXME: If the message contains something purporting to be our tag
				 * at this point we should ignore it rather than create the call.
				 * I think...
				 */
				if (sip_methods[req->method].can_create == 1) {
					if (req->taglen < sizeof(dialogue->theirtag) - 1) {
						if ((dialogue = sip_alloc())) {
							if (req->method != SIP_REGISTER)
								cw_copy_string(dialogue->fromdomain, default_fromdomain, sizeof(dialogue->fromdomain));
							if (req->ouraddr.sa.sa_family != AF_UNSPEC) {
								dialogue->conn = cw_object_dup(req->conn);
								dialogue->stunaddr = dialogue->ouraddr = req->ouraddr;
								dialogue->peeraddr = req->recvdaddr;
								/* FIXME: req->ouraddr is where the request arrived. i.e. our
								 * _local_ address. We either need to do a STUN query or look
								 * in the request to find out where the remote thought they
								 * were sending to.
								 */
							} else
								cw_sip_ouraddrfor(dialogue, &req->recvdaddr.sa, sizeof(req->recvdaddr));

							if (dialogue->conn) {
								cw_copy_string(dialogue->callid, req->pkt.data + req->callid, sizeof(dialogue->callid));
								memcpy(dialogue->theirtag, req->pkt.data + req->tag, (dialogue->theirtag_len = req->taglen));
								dialogue->theirtag[req->taglen] = '\0';

								if (req->method == SIP_INVITE)
									sip_alloc_media(dialogue);

								dialogue->reg_entry = cw_registry_add(&dialogue_registry, hash, &dialogue->obj);

								cw_mutex_lock(&dialogue->lock);
							} else {
								/* Huh? They can talk to us but we can't talk to them?!? */
								cw_object_put(dialogue);
								dialogue = NULL;
							}
						} else {
							if (option_debug > 3)
								cw_log(CW_LOG_DEBUG, "Failed allocating SIP dialog, sending 500 Server internal error and giving up\n");
							transmit_error(req, "500 Server internal error");
						}
					} else {
						cw_log(CW_LOG_ERROR, "%#l@ sent a tag longer than we can handle (%d > %lu, tag = \"%.*s\"\n", &req->recvdaddr.sa, req->taglen, (unsigned long)sizeof(dialogue->theirtag), req->taglen, req->pkt.data + req->tag);

						transmit_error(req, "500 Server internal error");
					}
				} else {
					const char *response = NULL;

					if (sip_methods[req->method].can_create == 2) {
						/* In theory, can create dialog. We don't support it */
						switch (req->method) {
							case SIP_UNKNOWN:
							case SIP_PRACK:
								response = "501 Method not implemented";
								break;
							case SIP_REFER:
								response = "603 Declined (no dialog)";
								break;
							case SIP_NOTIFY:
								response = "489 Bad event";
								break;
							default:
								response = "481 Call leg/transaction does not exist";
								break;
						}
					} else if (req->method != SIP_ACK)
						response = "481 Call leg/transaction does not exist";

					if (response)
						transmit_error(req, response);
				}
			}
		} else if (!dialogue && req->method != SIP_ACK) {
			/* We found the call ID but the tags didn't match. Maybe there's a
			 * forking proxy in the way and the call has already been claimed
			 * by something else?
			 */
			/* FIXME: we SHOULD send a CANCEL or BYE here? */
			transmit_error(req, "481 Call leg/transaction does not exist");
		}

		if (dialogue) {
			if (handle_request(dialogue, req, &nounlock) < 0)
				cw_blacklist_add(&req->recvdaddr.sa);
		}
	}

	if (dialogue) {
		if (dialogue->owner && !nounlock)
			cw_channel_unlock(dialogue->owner);

		cw_mutex_unlock(&dialogue->lock);
		cw_object_put(dialogue);
	}

	if (req->free) {
		cw_object_put(req->conn);
		cw_dynstr_free(&req->pkt);
		free(req);
	}

	return 0;
}


/*! \brief  sipsock_read: Read data from SIP socket */
/*    Successful messages is connected to SIP call and forwarded to handle_request() */
static int sipsock_read(struct cw_connection *conn)
{
	/* FIXME: the statics should be part of the pvt data associated with the connection */
	static struct sip_request *req = NULL;
	static int oom = 0;
	struct parse_request_state pstate;
	socklen_t sa_from_len, sa_to_len;
	int msgsize, msgread;

	if (!req) {
		if ((req = malloc(sizeof(*req)))) {
			oom = 0;
			cw_dynstr_init(&req->pkt, 0, 1);
		} else {
			if (!oom) {
				cw_log(CW_LOG_WARNING, "Out of memory!\n");
				oom = 1;
			}
			usleep(1000);
			return 1;
		}
	}
	memset(req, 0, offsetof(typeof(*req), pkt));
	cw_dynstr_reset(&req->pkt);

	sa_from_len = sizeof(req->recvdaddr);
	sa_to_len = sizeof(req->ouraddr);

	ioctl(conn->sock, FIONREAD, &msgsize);
	cw_dynstr_need(&req->pkt, msgsize + 1);

	if (req->pkt.error || (msgread = cw_recvfromto(conn->sock, req->pkt.data, req->pkt.size - 1, 0, &req->recvdaddr.sa, &sa_from_len, &req->ouraddr.sa, &sa_to_len)) < msgsize) {
		if (req->pkt.error)
			usleep(10000);
		else if (msgread == -1) {
#if !defined(__FreeBSD__)
			if (errno == EAGAIN)
				cw_log(CW_LOG_NOTICE, "SIP: Received packet with bad UDP checksum\n");
			else
#endif
			if (errno != ECONNREFUSED)
				cw_log(CW_LOG_WARNING, "Recv error: %s\n", strerror(errno));
		}

		return 1;
	}
	req->pkt.data[msgsize] = '\0';
	req->pkt.used = msgread;

	if (req->ouraddr.sa.sa_family != AF_UNSPEC)
		cw_sockaddr_set_port(&req->ouraddr.sa, cw_sockaddr_get_port(&conn->addr));

	/* If this is IPv4 and either of the first two bytes are less than 32 this must be a stun packet */
	if (req->recvdaddr.sa.sa_family == AF_INET && (req->pkt.data[0] < ' ' || req->pkt.data[1] < ' ')) {
		cw_stun_handle_packet(conn->sock, &req->recvdaddr.sin, (unsigned char *)req->pkt.data, req->pkt.used, NULL);
	} else {
		if (sip_debug_test_addr(&req->recvdaddr.sa)) {
			cw_set_flag(req, SIP_PKT_DEBUG);
			cw_verbose("\n<-- SIP read from %#l@: \n%s---\n", &req->recvdaddr.sa, req->pkt.data);
		}

		parse_request_init(&pstate, req);

		if (!parse_request(&pstate, req)) {
			req->conn = conn;
			if (handle_message(req)) {
				req->free = 1;
				req->conn = cw_object_dup(conn);
				cw_sched_add(sched, 1, handle_message, req);
				req = NULL;
			}
		}
	}

	return 1;
}


#if 0
/* Currently unused... */

/*! \brief  sip_send_mwi_to_peer: Send message waiting indication */
static int sip_send_mwi_to_peer(struct sip_peer *peer)
{
    struct sip_pvt *p;
    int newmsgs, oldmsgs;

    /* Check for messages */
    cw_app_messagecount(peer->mailbox, &newmsgs, &oldmsgs);

    time(&peer->lastmsgcheck);
    
    /* Return now if it's the same thing we told them last time */
    if (((newmsgs << 8) | (oldmsgs)) == peer->lastmsgssent)
    {
        return 0;
    }
    
    p = sip_alloc();
    if (!p)
    {
        cw_log(CW_LOG_WARNING, "Unable to build sip pvt data for MWI\n");
        return -1;
    }
    cw_copy_string(p->fromdomain, default_fromdomain, sizeof(p->fromdomain));
    peer->lastmsgssent = ((newmsgs > 0x7fff ? 0x7fff0000 : (newmsgs << 16)) | (oldmsgs > 0xffff ? 0xffff : oldmsgs));
    if (create_addr(p, NULL, peer, 0))
    {
        /* Maybe they're not registered, etc. */
        sip_destroy(p);
        cw_object_put(p);
        return 0;
    }

    p->reg_entry = cw_registry_add(&dialogue_registry, dialogue_hash(p), &p->obj);

    /* Send MWI */
    cw_set_flag(p, SIP_OUTGOING);
    transmit_notify_with_mwi(p, newmsgs, oldmsgs, peer->vmexten);
    sip_scheddestroy(p, -1);
    cw_object_put(p);
    return 0;
}
#endif


struct do_monitor_dialogue_args {
	time_t t;
	int fastrestart;
};

static int do_monitor_dialogue_one(struct cw_object *obj, void *data)
{
	struct sip_pvt *dialogue = container_of(obj, struct sip_pvt, obj);
	struct do_monitor_dialogue_args *args = data;

	/* If we can't get the lock it's not a problem. We'll just try again next time. */
	if (!cw_mutex_trylock(&dialogue->lock)) {
		if (dialogue->rtp && dialogue->owner && (dialogue->owner->_state == CW_STATE_UP) && !cw_sockaddr_is_specific(&dialogue->redirip.sa)) {
			if (dialogue->lastrtptx && dialogue->rtpkeepalive && args->t > dialogue->lastrtptx + dialogue->rtpkeepalive) {
				/* Need to send an empty RTP packet */
				dialogue->lastrtptx = args->t;
				cw_rtp_sendcng(dialogue->rtp, 0);
			}

			if (dialogue->owner && dialogue->lastrtprx && (dialogue->rtptimeout || dialogue->rtpholdtimeout) && args->t > dialogue->lastrtprx + dialogue->rtptimeout) {
				/* Might be a timeout now -- see if we're on hold */
				if (cw_rtp_get_peer(dialogue->rtp)->sa_family != AF_UNSPEC
				|| (dialogue->rtpholdtimeout && args->t > dialogue->lastrtprx + dialogue->rtpholdtimeout)) {
					/* Needs a hangup */
					if (dialogue->rtptimeout) {
						/* If we can't get the lock we'll just try again next time */
						if (!cw_channel_trylock(dialogue->owner)) {
							cw_log(CW_LOG_NOTICE, "Disconnecting call '%s' for lack of RTP activity in %ld seconds\n", dialogue->owner->name, (long)(args->t - dialogue->lastrtprx));

							/* Issue a softhangup */
							cw_softhangup(dialogue->owner, CW_SOFTHANGUP_DEV);
							cw_channel_unlock(dialogue->owner);

							/* forget the timeouts for this call, since a hangup
							 * has already been requested and we don't want to
							 * repeatedly request hangups
							 */
							dialogue->rtptimeout = 0;
							dialogue->rtpholdtimeout = 0;
						} else
							args->fastrestart = 1;
					}
				}
			}
		}

		cw_mutex_unlock(&dialogue->lock);
	} else
		args->fastrestart = 1;

	return 0;
}

/*! \brief  do_monitor: The SIP monitoring thread */
static __attribute__((__noreturn__)) void *do_monitor(void *data)
{
    struct do_monitor_dialogue_args args;
    int res;

    CW_UNUSED(data);

    for (;;)
    {
        /* Check for interfaces needing to be killed */
        time(&args.t);
        args.fastrestart = 0;
        cw_registry_iterate(&dialogue_registry, do_monitor_dialogue_one, &args);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        /* If we have work to do don't wait long at all.*/
        res = (args.fastrestart ? 1 : 1000);
        cw_io_run(io, res);

#if 0
        /* needs work to send mwi to realtime peers */
        time(&args.t);
        args.fastrestart = 0;
        curpeernum = 0;
        peer = NULL;
        ASTOBJ_CONTAINER_TRAVERSE(&peerl, !peer, do {
            if ((curpeernum > lastpeernum) && !cw_strlen_zero(iterator->mailbox) && ((args.t - iterator->lastmsgcheck) > global_mwitime))
            {
                args.fastrestart = 1;
                lastpeernum = curpeernum;
                peer = ASTOBJ_REF(iterator);
            };
            curpeernum++;
        } while (0)
        );
        if (peer)
        {
            ASTOBJ_WRLOCK(peer);
            sip_send_mwi_to_peer(peer);
            ASTOBJ_UNLOCK(peer);
            ASTOBJ_UNREF(peer,sip_destroy_peer);
        }
        else
        {
            /* Reset where we come from */
            lastpeernum = -1;
        }
#endif
    }
}


/*! \brief  sip_poke_peer: Check availability of peer, also keep NAT open */
/*    This is done with the interval in qualify= option in sip.conf */
/*    Default is 2 seconds */
static void *sip_poke_peer_thread(void *data)
{
	struct sip_peer *peer = data;
	struct sip_pvt *p;

	/* If we are no longer registered we have nothing to do but shut down */
	if (!cw_test_flag(peer, SIP_ALREADYGONE)) {
		if ((p = sip_alloc())) {
			create_addr(p, NULL, peer, 1);

			if (p->conn) {
				p->reg_entry = cw_registry_add(&dialogue_registry, dialogue_hash(p), &p->obj);

				p->peerpoke = peer;
				p->timer_t1 = (peer->maxms ? peer->maxms : rfc_timer_t1);

				cw_set_flag(p, SIP_OUTGOING);
#ifdef VOCAL_DATA_HACK
				if (cw_strlen_zero(p->username))
					cw_copy_string(p->username, "__VOCAL_DATA_SHOULD_READ_THE_SIP_SPEC__", sizeof(p->username));
				transmit_invite(p, SIP_INVITE, 0, 2);
#else
				transmit_invite(p, SIP_OPTIONS, 0, 2);
#endif
				peer->pokeexpire = cw_sched_add(sched, DEFAULT_FREQ_OK, sip_poke_peer, peer);
			}

			cw_object_put(p);
		} else {
			cw_log(CW_LOG_ERROR, "SYSTEM OVERLOAD: Failed to allocate dialog for poking peer '%s'\n", peer->name);
			peer->pokeexpire = cw_sched_add(sched, RFC_TIMER_F * peer->timer_t1, sip_poke_peer, peer);
		}
	} else
		cw_object_put(peer);

	return 0;
}

static int sip_poke_peer(void *data)
{
	struct sip_peer *peer = data;
	pthread_t tid;

	/* Since we are now running we can't be unscheduled therefore
	 * even if we get a response handle_response_peerpoke will do
	 * nothing.
	 */

	/* Dynamic peers don't need to do DNS look ups. Everything else goes async. */
	if (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC))
		sip_poke_peer_thread(peer);
	else
		cw_pthread_create(&tid, &global_attr_detached, sip_poke_peer_thread, peer);

	return 0;
}

/*! \brief  sip_devicestate: Part of PBX channel interface */

/* Return values:---
    If we have qualify on and the device is not reachable, regardless of registration
    state we return CW_DEVICE_UNAVAILABLE

    For peers with call limit:
        not registered            CW_DEVICE_UNAVAILABLE
        registered, no call        CW_DEVICE_NOT_INUSE
        registered, calls possible    CW_DEVICE_INUSE
        registered, call limit reached    CW_DEVICE_BUSY
    For peers without call limit:
        not registered            CW_DEVICE_UNAVAILABLE
        registered            CW_DEVICE_UNKNOWN
*/
static int sip_devicestate(void *data)
{
    char *host;
    char *tmp;

    struct cw_hostent ahp;
    struct sip_peer *p;

    int res = CW_DEVICE_INVALID;

    host = cw_strdupa(data);
    if ((tmp = strchr(host, '@')))
        host = tmp + 1;

    if (option_debug > 2) 
        cw_log(CW_LOG_DEBUG, "Checking device state for peer %s\n", host);

    if ((p = find_peer(host, NULL, 1)))
    {
        if (p->addr.sa.sa_family != AF_UNSPEC  ||  p->defaddr.sa.sa_family != AF_UNSPEC)
        {
            /* we have an address for the peer */
            /* if qualify is turned on, check the status */
            if (p->maxms && (p->timer_t1 < 0 || p->timer_t1 > p->maxms))
            {
                res = CW_DEVICE_UNAVAILABLE;
            }
            else
            {
                /* qualify is not on, or the peer is responding properly */
                /* check call limit */
                if (p->call_limit && (p->inUse == p->call_limit))
                    res = CW_DEVICE_BUSY;
                else if (p->call_limit && p->inUse)
                    res = CW_DEVICE_INUSE;
                else if (p->call_limit)
                    res = CW_DEVICE_NOT_INUSE;
                else
                    res = CW_DEVICE_UNKNOWN;
            }
        }
        else
        {
            /* there is no address, it's unavailable */
            res = CW_DEVICE_UNAVAILABLE;
        }
        cw_object_put(p);
    }
    else
    {
        if (cw_gethostbyname(host, &ahp))
            res = CW_DEVICE_UNKNOWN;
    }

    return res;
}

/*! \brief  sip_request: PBX interface function -build SIP pvt structure */
/* SIP calls initiated by the PBX arrive here */
static struct cw_channel *sip_request_call(const char *type, int format, void *data, int *cause)
{
    char tmp[256];
    struct sip_pvt *p;
    struct cw_channel *tmpc = NULL;
    char *ext, *host;
    char *dest = data;
    int oldformat;

    CW_UNUSED(type);

    oldformat = format;
    format &= ((CW_FORMAT_MAX_AUDIO << 1) - 1);
    if (!format)
    {
        cw_log(CW_LOG_NOTICE, "Asked to get a channel of unsupported format %s while capability is %s\n", cw_getformatname(oldformat), cw_getformatname(global_capability));
        return NULL;
    }

    p = sip_alloc();
    if (!p)
    {
        cw_log(CW_LOG_WARNING, "Unable to build sip pvt data for '%s'\n", (char *)data);
        return NULL;
    }

    cw_copy_string(p->fromdomain, default_fromdomain, sizeof(p->fromdomain));

    p->options = calloc(1, sizeof(struct sip_invite_param));
    if (!p->options)
    {
	sip_destroy(p);
        cw_object_put(p);
	cw_log(CW_LOG_ERROR, "Unable to build option SIP data structure - Out of memory\n");
	*cause = CW_CAUSE_SWITCH_CONGESTION;
        return NULL;
    }

    cw_copy_string(tmp, dest, sizeof(tmp));
    host = strchr(tmp, '@');
    if (host)
    {
        *host = '\0';
        host++;
        ext = tmp;
    }
    else
    {
        ext = strchr(tmp, '/');
        if (ext)
        {
            *ext++ = '\0';
            host = tmp;
        }
        else
        {
            host = tmp;
            ext = NULL;
        }
    }

    if (create_addr(p, host, NULL, 0))
    {
        *cause = CW_CAUSE_UNREGISTERED;
        sip_destroy(p);
        p->obj.release(&p->obj);
        return NULL;
    }

    sip_alloc_media(p);

    /* We want at least audio... */
    if (!p->rtp)
    {
        *cause = CW_CAUSE_NORMAL_TEMPORARY_FAILURE;
        sip_destroy(p);
        cw_object_put(p);
        return NULL;
    }

    if (cw_strlen_zero(p->peername) && ext)
        cw_copy_string(p->peername, ext, sizeof(p->peername));

    p->reg_entry = cw_registry_add(&dialogue_registry, dialogue_hash(p), &p->obj);

    /* We have an extension to call, don't use the full contact here */
    /* This to enable dialling registered peers with extension dialling,
       like SIP/peername/extension     
       SIP/peername will still use the full contact */
    if (ext)
    {
        cw_copy_string(p->username, ext, sizeof(p->username));
        p->fullcontact[0] = 0;
    }
    p->prefcodec = format;

    cw_mutex_lock(&p->lock);

    tmpc = sip_new(p, CW_STATE_DOWN, host);    /* Place the call */
    if (!tmpc)
        sip_destroy(p);
    else
        cw_channel_unlock(tmpc);

    cw_mutex_unlock(&p->lock);
    cw_object_put(p);

    pthread_kill(monitor_thread, SIGURG);

    return tmpc;
}

/*! \brief  handle_common_options: Handle flag-type options common to users and peers */
static int handle_common_options(struct cw_flags *flags, struct cw_flags *mask, struct cw_variable *v)
{
    int res = 0;

    if (!strcasecmp(v->name, "trustrpid"))
    {
        cw_set_flag(mask, SIP_TRUSTRPID);
        cw_set2_flag(flags, cw_true(v->value), SIP_TRUSTRPID);
        res = 1;
    }
    else if (!strcasecmp(v->name, "sendrpid"))
    {
        cw_set_flag(mask, SIP_SENDRPID);
        cw_set2_flag(flags, cw_true(v->value), SIP_SENDRPID);
        res = 1;
    }
    else if (!strcasecmp(v->name, "useclientcode"))
    {
        cw_set_flag(mask, SIP_USECLIENTCODE);
        cw_set2_flag(flags, cw_true(v->value), SIP_USECLIENTCODE);
        res = 1;
    }
    else if (!strcasecmp(v->name, "dtmfmode"))
    {
        cw_set_flag(mask, SIP_DTMF);
        cw_clear_flag(flags, SIP_DTMF);
        if (!strcasecmp(v->value, "inband"))
            cw_set_flag(flags, SIP_DTMF_INBAND);
        else if (!strcasecmp(v->value, "rfc2833"))
            cw_set_flag(flags, SIP_DTMF_RFC2833);
        else if (!strcasecmp(v->value, "info"))
            cw_set_flag(flags, SIP_DTMF_INFO);
        else if (!strcasecmp(v->value, "auto"))
            cw_set_flag(flags, SIP_DTMF_AUTO);
        else
        {
            cw_log(CW_LOG_WARNING, "Unknown dtmf mode '%s' on line %d, using rfc2833\n", v->value, v->lineno);
            cw_set_flag(flags, SIP_DTMF_RFC2833);
        }
    }
    else if (!strcasecmp(v->name, "nat"))
    {
        cw_set_flag(mask, SIP_NAT);
        cw_clear_flag(flags, SIP_NAT);
        if (!strcasecmp(v->value, "never") || cw_false(v->value))
            cw_set_flag(flags, SIP_NAT_NEVER);
        else if (!strcasecmp(v->value, "route"))
            cw_set_flag(flags, SIP_NAT_ROUTE);
        else if (!strcasecmp(v->value, "always") || cw_true(v->value))
            cw_set_flag(flags, SIP_NAT_ALWAYS);
        else
            cw_set_flag(flags, SIP_NAT_RFC3581);
    }
    else if (!strcasecmp(v->name, "canreinvite"))
    {
        cw_set_flag(mask, SIP_REINVITE);
        cw_clear_flag(flags, SIP_REINVITE);
        if (!strcasecmp(v->value, "update"))
            cw_set_flag(flags, SIP_REINVITE_UPDATE | SIP_CAN_REINVITE);
        else
            cw_set2_flag(flags, cw_true(v->value), SIP_CAN_REINVITE);
    }
    else if (!strcasecmp(v->name, "insecure"))
    {
        cw_set_flag(mask, SIP_INSECURE_PORT | SIP_INSECURE_INVITE);
        cw_clear_flag(flags, SIP_INSECURE_PORT | SIP_INSECURE_INVITE);
        if (!strcasecmp(v->value, "very"))
            cw_set_flag(flags, SIP_INSECURE_PORT | SIP_INSECURE_INVITE);
        else if (cw_true(v->value))
            cw_set_flag(flags, SIP_INSECURE_PORT);
        else if (!cw_false(v->value))
        {
            char buf[64];
            char *word, *next;

            cw_copy_string(buf, v->value, sizeof(buf));
            next = buf;
            while ((word = strsep(&next, ",")))
            {
                if (!strcasecmp(word, "port"))
                    cw_set_flag(flags, SIP_INSECURE_PORT);
                else if (!strcasecmp(word, "invite"))
                    cw_set_flag(flags, SIP_INSECURE_INVITE);
                else
                    cw_log(CW_LOG_WARNING, "Unknown insecure mode '%s' on line %d\n", v->value, v->lineno);
            }
        }
    }
    else if (!strcasecmp(v->name, "progressinband"))
    {
        cw_set_flag(mask, SIP_PROG_INBAND);
        cw_clear_flag(flags, SIP_PROG_INBAND);
        if (cw_true(v->value))
            cw_set_flag(flags, SIP_PROG_INBAND_YES);
        else if (strcasecmp(v->value, "never"))
            cw_set_flag(flags, SIP_PROG_INBAND_NO);
    }
    else if (!strcasecmp(v->name, "allowguest"))
    {
#ifdef OSP_SUPPORT
        if (!strcasecmp(v->value, "osp"))
            global_allowguest = 2;
        else 
#endif
            if (cw_true(v->value)) 
                global_allowguest = 1;
            else
                global_allowguest = 0;
#ifdef OSP_SUPPORT
    }
    else if (!strcasecmp(v->name, "ospauth"))
    {
        cw_set_flag(mask, SIP_OSPAUTH);
        cw_clear_flag(flags, SIP_OSPAUTH);
        if (!strcasecmp(v->value, "proxy"))
            cw_set_flag(flags, SIP_OSPAUTH_PROXY);
        else if (!strcasecmp(v->value, "gateway"))
            cw_set_flag(flags, SIP_OSPAUTH_GATEWAY);
        else if(!strcasecmp (v->value, "exclusive"))
             cw_set_flag(flags, SIP_OSPAUTH_EXCLUSIVE);
#endif
    }
    else if (!strcasecmp(v->name, "promiscredir"))
    {
        cw_set_flag(mask, SIP_PROMISCREDIR);
        cw_set2_flag(flags, cw_true(v->value), SIP_PROMISCREDIR);
        res = 1;
    }

    return res;
}

/*! \brief  add_sip_domain: Add SIP domain to list of domains we are responsible for */
static int add_sip_domain(const char *domain, const enum domain_mode mode, const char *context)
{
    struct domain *d;

    if (cw_strlen_zero(domain))
    {
        cw_log(CW_LOG_WARNING, "Zero length domain.\n");
        return 1;
    }

    d = calloc(1, sizeof(struct domain));
    if (!d)
    {
        cw_log(CW_LOG_ERROR, "Allocation of domain structure failed, Out of memory\n");
        return 0;
    }

    cw_copy_string(d->domain, domain, sizeof(d->domain));

    if (!cw_strlen_zero(context))
        cw_copy_string(d->context, context, sizeof(d->context));

    d->mode = mode;

    CW_LIST_LOCK(&domain_list);
    CW_LIST_INSERT_TAIL(&domain_list, d, list);
    CW_LIST_UNLOCK(&domain_list);

     if (sipdebug)    
        cw_log(CW_LOG_DEBUG, "Added local SIP domain '%s'\n", domain);

    return 1;
}

/*! \brief  check_sip_domain: Check if domain part of uri is local to our server */
static int check_sip_domain(const char *domain, char *context, size_t len)
{
    struct domain *d;
    int result = 0;

    CW_LIST_LOCK(&domain_list);
    CW_LIST_TRAVERSE(&domain_list, d, list)
    {
        if (strcasecmp(d->domain, domain))
            continue;

        if (len && !cw_strlen_zero(d->context))
            cw_copy_string(context, d->context, len);
        
        result = 1;
        break;
    }
    CW_LIST_UNLOCK(&domain_list);

    return result;
}

/*! \brief  clear_sip_domains: Clear our domain list (at reload) */
static void clear_sip_domains(void)
{
    struct domain *d;

    CW_LIST_LOCK(&domain_list);
    while ((d = CW_LIST_REMOVE_HEAD(&domain_list, list)))
        free(d);
    CW_LIST_UNLOCK(&domain_list);
}


/*! \brief  add_realm_authentication: Add realm authentication in list */
static struct sip_auth *add_realm_authentication(struct sip_auth *authlist, char *configuration, int lineno)
{
    char authcopy[256];
    char *username=NULL, *realm=NULL, *secret=NULL, *md5secret=NULL;
    char *stringp;
    struct sip_auth *auth;
    struct sip_auth *b = NULL, *a = authlist;

    if (cw_strlen_zero(configuration))
        return authlist;

    cw_log(CW_LOG_DEBUG, "Auth config ::  %s\n", configuration);

    cw_copy_string(authcopy, configuration, sizeof(authcopy));
    stringp = authcopy;

    username = stringp;
    realm = strrchr(stringp, '@');
    if (realm)
    {
        *realm = '\0';
        realm++;
    }
    if (cw_strlen_zero(username) || cw_strlen_zero(realm))
    {
        cw_log(CW_LOG_WARNING, "Format for authentication entry is user[:secret]@realm at line %d\n", lineno);
        return authlist;
    }
    stringp = username;
    username = strsep(&stringp, ":");
    if (username)
    {
        secret = strsep(&stringp, ":");
        if (!secret)
        {
            stringp = username;
            md5secret = strsep(&stringp,"#");
        }
    }
    auth = malloc(sizeof(struct sip_auth));
    if (auth)
    {
        memset(auth, 0, sizeof(struct sip_auth));
        cw_copy_string(auth->realm, realm, sizeof(auth->realm));
        cw_copy_string(auth->username, username, sizeof(auth->username));
        if (secret)
            cw_copy_string(auth->secret, secret, sizeof(auth->secret));
        if (md5secret)
            cw_copy_string(auth->md5secret, md5secret, sizeof(auth->md5secret));
    }
    else
    {
        cw_log(CW_LOG_ERROR, "Allocation of auth structure failed, Out of memory\n");
        return authlist;
    }

    /* Add authentication to authl */
    if (!authlist)  /* No existing list */
        return auth;
    while (a)
    {
        b = a;
        a = a->next;
    }
    b->next = auth;    /* Add structure add end of list */

    if (option_verbose > 2)
        cw_verbose("Added authentication for realm %s\n", realm);

    return authlist;
}

/*! \brief  clear_realm_authentication: Clear realm authentication list */
static void clear_realm_authentication(struct sip_auth **authlist)
{
    struct sip_auth *a;

    while ((a = *authlist)) {
        *authlist = (*authlist)->next;
	free(a);
    }
}

/*! \brief  find_realm_authentication: Find authentication for a specific realm */
static struct sip_auth *find_realm_authentication(struct sip_auth *authlist, char *realm)
{
    struct sip_auth *a;

    for (a = authlist; a && strcasecmp(a->realm, realm); a = a->next);
    return a;
}

/*! \brief  build_user: Initialize a SIP user structure from sip.conf */
static struct sip_user *build_user(const char *name, struct cw_variable *v, int realtime)
{
    struct sip_user *user;
    int format;
    char *varname = NULL, *varval = NULL;
    struct cw_variable *tmpvar = NULL;
    struct cw_flags userflags = {(0)};
    struct cw_flags mask = {(0)};

    CW_UNUSED(realtime);

    if (!(user = calloc(1, sizeof(struct sip_user))))
        return NULL;

    suserobjs++;

    cw_object_init(user, NULL, 1);
    user->obj.release = sip_user_release;

    cw_copy_string(user->name, name, sizeof(user->name));
    cw_copy_flags(user, &global_flags, SIP_FLAGS_TO_COPY);
    user->capability = global_capability;
    user->prefs = prefs;
    /* set default context */
    strcpy(user->context, default_context);
    strcpy(user->language, default_language);
    strcpy(user->musicclass, global_musicclass);
    while (v)
    {
        if (handle_common_options(&userflags, &mask, v))
        {
            v = v->next;
            continue;
        }

        if (!strcasecmp(v->name, "context"))
        {
            cw_copy_string(user->context, v->value, sizeof(user->context));
        }
        else if (!strcasecmp(v->name, "subscribecontext"))
        {
            cw_copy_string(user->subscribecontext, v->value, sizeof(user->subscribecontext));
        }
        else if (!strcasecmp(v->name, "setvar"))
        {
            varname = cw_strdupa(v->value);
            if ((varval = strchr(varname, '=')))
            {
                *varval = '\0';
                varval++;
                if ((tmpvar = cw_variable_new(varname, varval)))
                {
                    tmpvar->next = user->chanvars;
                    user->chanvars = tmpvar;
                }
            }
        }
        else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny"))
        {
            int err;

            if ((err = cw_acl_add(&user->acl, v->name, v->value)))
                cw_log(CW_LOG_ERROR, "%s = %s: %s\n", v->name, v->value, gai_strerror(err));
        }
        else if (!strcasecmp(v->name, "secret"))
        {
            cw_copy_string(user->secret, v->value, sizeof(user->secret)); 
        }
        else if (!strcasecmp(v->name, "md5secret"))
        {
            cw_copy_string(user->md5secret, v->value, sizeof(user->md5secret));
        }
        else if (!strcasecmp(v->name, "callerid"))
        {
            cw_callerid_split(v->value, user->cid_name, sizeof(user->cid_name), user->cid_num, sizeof(user->cid_num));
        }
        else if (!strcasecmp(v->name, "callgroup"))
        {
            user->callgroup = cw_get_group(v->value);
        }
        else if (!strcasecmp(v->name, "pickupgroup"))
        {
            user->pickupgroup = cw_get_group(v->value);
        }
        else if (!strcasecmp(v->name, "language"))
        {
            cw_copy_string(user->language, v->value, sizeof(user->language));
        }
        else if (!strcasecmp(v->name, "musicclass") || !strcasecmp(v->name, "musiconhold"))
        {
            cw_copy_string(user->musicclass, v->value, sizeof(user->musicclass));
        }
        else if (!strcasecmp(v->name, "accountcode"))
        {
            cw_copy_string(user->accountcode, v->value, sizeof(user->accountcode));
        }
        else if (!strcasecmp(v->name, "call-limit") || !strcasecmp(v->name, "incominglimit"))
        {
            user->call_limit = atoi(v->value);
            if (user->call_limit < 0)
                user->call_limit = 0;
        }
        else if (!strcasecmp(v->name, "amaflags"))
        {
            format = cw_cdr_amaflags2int(v->value);
            if (format < 0)
            {
                cw_log(CW_LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
            }
            else
            {
                user->amaflags = format;
            }
        }
        else if (!strcasecmp(v->name, "allow"))
        {
            cw_parse_allow_disallow(&user->prefs, &user->capability, v->value, 1);
        }
        else if (!strcasecmp(v->name, "disallow"))
        {
            cw_parse_allow_disallow(&user->prefs, &user->capability, v->value, 0);
        }
        else if (!strcasecmp(v->name, "callingpres"))
        {
            user->callingpres = cw_parse_caller_presentation(v->value);
            if (user->callingpres == -1)
                user->callingpres = atoi(v->value);
        }
        /*else if (strcasecmp(v->name,"type"))
         *    cw_log(CW_LOG_WARNING, "Ignoring %s\n", v->name);
         */
        v = v->next;
    }

    cw_copy_flags(user, &userflags, mask.flags);

    return user;
}

/*! \brief  temp_peer: Create temporary peer (used in autocreatepeer mode) */
static struct sip_peer *temp_peer(const char *name)
{
    struct sip_peer *peer;

    if (!(peer = calloc(1, sizeof(*peer))))
        return NULL;

    apeerobjs++;

    cw_object_init(peer, NULL, 1);
    peer->obj.release = sip_peer_release;

    peer->expire = -1;
    peer->pokeexpire = -1;
    cw_copy_string(peer->name, name, sizeof(peer->name));
    cw_copy_flags(peer, &global_flags, SIP_FLAGS_TO_COPY);
    strcpy(peer->context, default_context);
    strcpy(peer->subscribecontext, default_subscribecontext);
    strcpy(peer->language, default_language);
    strcpy(peer->musicclass, global_musicclass);
    peer->capability = global_capability;
    peer->rtptimeout = global_rtptimeout;
    peer->rtpholdtimeout = global_rtpholdtimeout;
    peer->rtpkeepalive = global_rtpkeepalive;
    cw_set_flag(peer, SIP_SELFDESTRUCT);
    cw_set_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC);
    peer->prefs = prefs;
    reg_source_db(peer, 1);

    return peer;
}


struct async_get_ip_args {
	struct sip_peer *peer;
	struct sockaddr *addr;
	const char *service;
	char value[0];
};

static void async_get_ip_free_args(struct async_get_ip_args *args)
{
	if (args->peer)
		cw_object_put(args->peer);
	free(args);
}

static void *async_get_ip_handler(void *data)
{
	struct async_get_ip_args *args = (struct async_get_ip_args *)data;

	cw_get_ip_or_srv(AF_UNSPEC, args->addr, args->value, args->service);
	async_get_ip_free_args(args);
	return NULL;
}

static int async_get_ip(struct sip_peer *peer, struct sockaddr *addr, const char *value, const char *service)
{
	pthread_t tid;
	struct async_get_ip_args *args;
	int l, ret = -1;

	l = strlen(value) + 1;
	if ((args = malloc(sizeof(*args) + l))) {
		args->peer = (peer ? cw_object_dup(peer) : NULL);
		args->addr = addr;
		args->service = service;
		memcpy(args->value, value, l);
		if ((ret = cw_pthread_create(&tid, &global_attr_detached, async_get_ip_handler, args)))
			async_get_ip_free_args(args);
	}
	return ret;
}

/*! \brief  build_peer: Build peer from config file */
static struct sip_peer *build_peer(const char *name, struct cw_variable *v, int realtime, int sip_running)
{
    struct sip_peer *peer = NULL;
    char *varname = NULL, *varval = NULL;
    struct cw_variable *tmpvar = NULL;
    struct cw_flags peerflags = { 0 };
    struct cw_flags mask = { 0 };
    time_t regseconds;
    int obproxyfound = 0;
    int format = 0;        /* Ama flags */
    unsigned char addr_defined = 0;

    if (!(peer = calloc(1, sizeof(struct sip_peer)))) {
        cw_log(CW_LOG_WARNING, "Can't allocate SIP peer memory\n");
        return NULL;
    }

    if (realtime)
        rpeerobjs++;
    else
        speerobjs++;

    cw_object_init(peer, NULL, 1);
    peer->obj.release = sip_peer_release;

    peer->expire = -1;
    peer->pokeexpire = -1;

    peer->lastmsgssent = -1;

    if (name)
        cw_copy_string(peer->name, name, sizeof(peer->name));

    peer->defaddr.sa.sa_family = AF_UNSPEC;
    peer->addr.sa.sa_family = AF_UNSPEC;

    /* If we have channel variables, remove them (reload) */
    if (peer->chanvars)
    {
        cw_variables_destroy(peer->chanvars);
        peer->chanvars = NULL;
    }

    strcpy(peer->context, default_context);
    strcpy(peer->subscribecontext, default_subscribecontext);
    strcpy(peer->vmexten, global_vmexten);
    strcpy(peer->language, default_language);
    strcpy(peer->musicclass, global_musicclass);
    cw_copy_flags(peer, &global_flags, SIP_USEREQPHONE);
    peer->rtpkeepalive = global_rtpkeepalive;
    peer->maxms = default_qualify;
    peer->prefs = prefs;
    cw_copy_flags(peer, &global_flags, SIP_FLAGS_TO_COPY);
    peer->capability = global_capability;
    peer->timer_t2 = rfc_timer_t2;
    peer->rtptimeout = global_rtptimeout;
    peer->rtpholdtimeout = global_rtpholdtimeout;

    while (v)
    {
        if (handle_common_options(&peerflags, &mask, v))
        {
            v = v->next;
            continue;
        }

        if (realtime && !strcasecmp(v->name, "regseconds"))
        {
            if (sscanf(v->value, "%li", &regseconds) != 1)
                regseconds = 0;
        }
        else if (realtime && !strcasecmp(v->name, "ipaddr") && !cw_strlen_zero(v->value))
        {
            static const struct addrinfo hints = {
                .ai_flags = AI_V4MAPPED | AI_IDN,
                .ai_family = AF_INET,
                .ai_socktype = SOCK_DGRAM,
            };
            struct addrinfo *addrs;
	    int err;

            if (!(err = cw_getaddrinfo(v->value, DEFAULT_SIP_PORT_STR, &hints, &addrs, NULL))) {
                memcpy(&peer->addr, addrs->ai_addr, addrs->ai_addrlen);
                addr_defined = 1;
            } else
                cw_log(CW_LOG_ERROR, "%s: %s\n", v->value, gai_strerror(err));
        }
        else if (realtime && !strcasecmp(v->name, "name"))
            cw_copy_string(peer->name, v->value, sizeof(peer->name));
        else if (realtime && !strcasecmp(v->name, "fullcontact"))
        {
            cw_copy_string(peer->fullcontact, v->value, sizeof(peer->fullcontact));
            cw_set_flag((&peer->flags_page2), SIP_PAGE2_RT_FROMCONTACT);
        }
        else if (!strcasecmp(v->name, "secret")) 
            cw_copy_string(peer->secret, v->value, sizeof(peer->secret));
        else if (!strcasecmp(v->name, "md5secret")) 
            cw_copy_string(peer->md5secret, v->value, sizeof(peer->md5secret));
        else if (!strcasecmp(v->name, "auth"))
            peer->auth = add_realm_authentication(peer->auth, v->value, v->lineno);
        else if (!strcasecmp(v->name, "callerid"))
        {
            cw_callerid_split(v->value, peer->cid_name, sizeof(peer->cid_name), peer->cid_num, sizeof(peer->cid_num));
        }
        else if (!strcasecmp(v->name, "context"))
        {
            cw_copy_string(peer->context, v->value, sizeof(peer->context));
        }
        else if (!strcasecmp(v->name, "subscribecontext"))
        {
            cw_copy_string(peer->subscribecontext, v->value, sizeof(peer->subscribecontext));
        }
        else if (!strcasecmp(v->name, "fromdomain"))
            cw_copy_string(peer->fromdomain, v->value, sizeof(peer->fromdomain));
        else if (!strcasecmp(v->name, "usereqphone"))
            cw_set2_flag(peer, cw_true(v->value), SIP_USEREQPHONE);
        else if (!strcasecmp(v->name, "fromuser"))
            cw_copy_string(peer->fromuser, v->value, sizeof(peer->fromuser));
        else if (!strcasecmp(v->name, "host") || !strcasecmp(v->name, "outboundproxy"))
        {
            if (!strcasecmp(v->value, "dynamic"))
            {
                if (!strcasecmp(v->name, "outboundproxy") || obproxyfound)
                {
                    cw_log(CW_LOG_WARNING, "You can't have a dynamic outbound proxy at line %d.\n", v->lineno);
                }
                else
                {
                    /* They'll register with us */
                    cw_set_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC);
                    peer->addr.sa.sa_family = AF_UNSPEC;
                    addr_defined = 0;
#if 0
		    /* MDJ: What? If a port was specified for "ipaddr" how does this apply to
		     * an "outboundproxy" as well as and instead of?
		     */
                    if (cw_sockaddr_get_port(&peer->addr))
                    {
                        /* If we've already got a port, make it the default rather than absolute */
                        cw_sockaddr_set_port(&peer->defaddr, cw_sockaddr_get_port(&peer->addr));
                        cw_sockaddr__set_port(&peer->addr, 0);
                    }
#endif
                }
            }
            else
            {
                /* Non-dynamic.  Make sure we become that way if we're not */
                cw_clear_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC);    
                if (peer->expire != -1 && !cw_sched_del(sched, peer->expire)) {
                    cw_object_put(peer);
                    peer->expire = -1;
                }
                if (!strcasecmp(v->name, "outboundproxy")) {
                    cw_copy_string(peer->proxyhost, v->value, sizeof(peer->proxyhost));
                    obproxyfound=1;
		} else
                    cw_copy_string(peer->tohost, v->value, sizeof(peer->tohost));
            }
        }
        else if (!strcasecmp(v->name, "defaultip"))
        {
            async_get_ip(peer, &peer->defaddr.sa, v->value, NULL);
        }
        else if (!strcasecmp(v->name, "permit") || !strcasecmp(v->name, "deny"))
        {
            int err;

            if ((err = cw_acl_add(&peer->acl, v->name, v->value)))
                cw_log(CW_LOG_ERROR, "%s = %s: %s\n", v->name, v->value, gai_strerror(err));
        }
        else if (!strcasecmp(v->name, "port"))
        {
            if (!realtime && cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC))
                cw_sockaddr_set_port(&peer->defaddr.sa, atoi(v->value));
            else
                cw_sockaddr_set_port(&peer->addr.sa, atoi(v->value));
        }
        else if (!strcasecmp(v->name, "callingpres"))
        {
            peer->callingpres = cw_parse_caller_presentation(v->value);
            if (peer->callingpres == -1)
                peer->callingpres = atoi(v->value);
        }
        else if (!strcasecmp(v->name, "username"))
        {
            cw_copy_string(peer->username, v->value, sizeof(peer->username));
        }
        else if (!strcasecmp(v->name, "language"))
        {
            cw_copy_string(peer->language, v->value, sizeof(peer->language));
        }
        else if (!strcasecmp(v->name, "regexten"))
        {
            cw_copy_string(peer->regexten, v->value, sizeof(peer->regexten));
        }
        else if (!strcasecmp(v->name, "call-limit") || !strcasecmp(v->name, "incominglimit"))
        {
            peer->call_limit = atoi(v->value);
            if (peer->call_limit < 0)
                peer->call_limit = 0;
        }
        else if (!strcasecmp(v->name, "amaflags"))
        {
            format = cw_cdr_amaflags2int(v->value);
            if (format < 0)
            {
                cw_log(CW_LOG_WARNING, "Invalid AMA Flags for peer: %s at line %d\n", v->value, v->lineno);
            }
            else
            {
                peer->amaflags = format;
            }
        }
        else if (!strcasecmp(v->name, "accountcode"))
        {
            cw_copy_string(peer->accountcode, v->value, sizeof(peer->accountcode));
        }
        else if (!strcasecmp(v->name, "musiconhold"))
        {
            cw_copy_string(peer->musicclass, v->value, sizeof(peer->musicclass));
        }
        else if (!strcasecmp(v->name, "mailbox"))
        {
            cw_copy_string(peer->mailbox, v->value, sizeof(peer->mailbox));
        }
        else if (!strcasecmp(v->name, "vmexten"))
        {
            cw_copy_string(peer->vmexten, v->value, sizeof(peer->vmexten));
        }
        else if (!strcasecmp(v->name, "callgroup"))
        {
            peer->callgroup = cw_get_group(v->value);
        }
        else if (!strcasecmp(v->name, "pickupgroup"))
        {
            peer->pickupgroup = cw_get_group(v->value);
        }
        else if (!strcasecmp(v->name, "allow"))
        {
            cw_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 1);
        }
        else if (!strcasecmp(v->name, "disallow"))
        {
            cw_parse_allow_disallow(&peer->prefs, &peer->capability, v->value, 0);
        }
        else if (!strcasecmp(v->name, "rtt") || !strcasecmp(v->name, "timer_t1"))
        {
            peer->timer_t1 = atoi(v->value);
        }
        else if (!strcasecmp(v->name, "timer_t2"))
        {
            peer->timer_t2 = atoi(v->value);
        }
        else if (!strcasecmp(v->name, "rtptimeout"))
        {
            if ((sscanf(v->value, "%d", &peer->rtptimeout) != 1) || (peer->rtptimeout < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
                peer->rtptimeout = global_rtptimeout;
            }
        }
        else if (!strcasecmp(v->name, "rtpholdtimeout"))
        {
            if ((sscanf(v->value, "%d", &peer->rtpholdtimeout) != 1) || (peer->rtpholdtimeout < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
                peer->rtpholdtimeout = global_rtpholdtimeout;
            }
        }
        else if (!strcasecmp(v->name, "rtpkeepalive"))
        {
            if ((sscanf(v->value, "%d", &peer->rtpkeepalive) != 1) || (peer->rtpkeepalive < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP keepalive time at line %d.  Using default.\n", v->value, v->lineno);
                peer->rtpkeepalive = global_rtpkeepalive;
            }
        }
        else if (!strcasecmp(v->name, "setvar"))
        {
            /* Set peer channel variable */
            varname = cw_strdupa(v->value);
            if ((varval = strchr(varname, '=')))
            {
                *varval = '\0';
                varval++;
                if ((tmpvar = cw_variable_new(varname, varval)))
                {
                    tmpvar->next = peer->chanvars;
                    peer->chanvars = tmpvar;
                }
            }
        }
        else if (!strcasecmp(v->name, "qualify"))
        {
            if (!strcasecmp(v->value, "no"))
            {
                peer->maxms = 0;
            }
            else if (!strcasecmp(v->value, "yes"))
            {
                peer->maxms = default_qualify;
            }
            else if (sscanf(v->value, "%d", &peer->maxms) != 1)
            {
                cw_log(CW_LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of sip.conf\n", peer->name, v->lineno);
                peer->maxms = 0;
            }
        }
        /* else if (strcasecmp(v->name,"type"))
         *    cw_log(CW_LOG_WARNING, "Ignoring %s\n", v->name);
         */
        v = v->next;
    }
    if (!cw_test_flag((&global_flags_page2), SIP_PAGE2_IGNOREREGEXPIRE) && cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC)  &&  realtime)
    {
        time_t nowtime;

        time(&nowtime);
        if ((nowtime - regseconds) > 0)
        {
            destroy_association(peer);
            memset(&peer->addr, 0, sizeof(peer->addr));
            if (option_debug)
                cw_log(CW_LOG_DEBUG, "Bah, we're expired (%ld/%ld/%ld)!\n", nowtime - regseconds, regseconds, nowtime);
        }
    }
    cw_copy_flags(peer, &peerflags, mask.flags);

    if (!peer->timer_t1)
        peer->timer_t1 = (peer->maxms ? peer->maxms : rfc_timer_t1);

    if (cw_test_flag(&peer->flags_page2, SIP_PAGE2_DYNAMIC) && !cw_test_flag(peer, SIP_REALTIME))
        reg_source_db(peer, sip_running);

    peer->reg_entry_byname = cw_registry_add(&peerbyname_registry, cw_hash_string(0, peer->name), &peer->obj);
    if (addr_defined)
        peer->reg_entry_byaddr = cw_registry_add(&peerbyaddr_registry, cw_sockaddr_hash(&peer->addr.sa, 0), &peer->obj);

    return peer;
}

/*! \brief  sip_get_rtp_peer: Returns null if we can't reinvite (part of RTP interface) */
static struct cw_rtp *sip_get_rtp_peer(struct cw_channel *chan)
{
    struct sip_pvt *p;
    struct cw_rtp *rtp = NULL;
    p = chan->tech_pvt;
    if (!p)
        return NULL;
    cw_mutex_lock(&p->lock);
    if (p->rtp && cw_test_flag(p, SIP_CAN_REINVITE))
        rtp =  p->rtp;
    cw_mutex_unlock(&p->lock);
    return rtp;
}

/*! \brief  sip_get_vrtp_peer: Returns null if we can't reinvite video (part of RTP interface) */
static struct cw_rtp *sip_get_vrtp_peer(struct cw_channel *chan)
{
    struct sip_pvt *p;
    struct cw_rtp *rtp = NULL;
    p = chan->tech_pvt;
    if (!p)
        return NULL;

    cw_mutex_lock(&p->lock);
    if (p->vrtp && cw_test_flag(p, SIP_CAN_REINVITE))
        rtp = p->vrtp;
    cw_mutex_unlock(&p->lock);
    return rtp;
}

/*! \brief  sip_set_rtp_peer: Set the RTP peer for this call */
static int sip_set_rtp_peer(struct cw_channel *chan, struct cw_rtp *rtp, struct cw_rtp *vrtp, int codecs, int nat_active)
{
    struct sip_pvt *p;

    CW_UNUSED(nat_active);

    p = chan->tech_pvt;
    if (!p) 
        return -1;
    cw_mutex_lock(&p->lock);
    if (rtp)
        cw_sockaddr_copy(&p->redirip.sa, cw_rtp_get_peer(rtp));
    else
        p->redirip.sa.sa_family = AF_UNSPEC;
    if (vrtp)
        cw_sockaddr_copy(&p->vredirip.sa, cw_rtp_get_peer(vrtp));
    else
        p->vredirip.sa.sa_family = AF_UNSPEC;
    p->redircodecs = codecs;
    if (!cw_test_flag(p, SIP_GOTREFER))
    {
        if (!p->pendinginvite)
        {
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Sending reinvite on SIP '%s' - It's audio soon redirected to IP %#l@\n", p->callid, (rtp ? &p->redirip.sa : &p->stunaddr.sa));

            transmit_reinvite_with_sdp(p);
        }
        else if (!cw_test_flag(p, SIP_PENDINGBYE))
        {
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Deferring reinvite on SIP '%s' - It's audio will be redirected to IP %#l@\n", p->callid, (rtp ? &p->redirip.sa : &p->stunaddr.sa));

            cw_set_flag(p, SIP_NEEDREINVITE);    
        }
    }
    /* Reset lastrtprx timer */
    time(&p->lastrtprx);
    time(&p->lastrtptx);
    cw_mutex_unlock(&p->lock);
    return 0;
}

static cw_udptl_t *sip_get_udptl_peer(struct cw_channel *chan)
{
    struct sip_pvt *p;
    cw_udptl_t *udptl = NULL;

    p = chan->tech_pvt;
    if (!p)
        return NULL;

    cw_mutex_lock(&p->lock);
    if (p->udptl && cw_test_flag(p, SIP_CAN_REINVITE))
        udptl = p->udptl;
    cw_mutex_unlock(&p->lock);
    return udptl;
}

static int sip_set_udptl_peer(struct cw_channel *chan, cw_udptl_t *udptl)
{
	struct sip_pvt *p;

	if (!(p = chan->tech_pvt))
		return -1;

	cw_mutex_lock(&p->lock);

	p->udptlredirip.sa.sa_family = AF_UNSPEC;
	if (udptl)
		cw_sockaddr_copy(&p->udptlredirip.sa, cw_udptl_get_peer(udptl));

	if (!cw_test_flag(p, SIP_GOTREFER)) {
		if (!p->pendinginvite) {
			if (option_debug > 2)
				cw_log(CW_LOG_DEBUG, "Sending reinvite on SIP '%s' - It's UDPTL soon redirected to IP %#l@\n", p->callid, (udptl ? &p->udptlredirip.sa : &p->stunaddr.sa));

			transmit_reinvite_with_t38_sdp(p);
		} else if (!cw_test_flag(p, SIP_PENDINGBYE)) {
			if (option_debug > 2)
				cw_log(CW_LOG_DEBUG, "Deferring reinvite on SIP '%s' - It's UDPTL will be redirected to IP %#l@\n", p->callid, (udptl ? &p->udptlredirip.sa : &p->stunaddr.sa));

			cw_set_flag(p, SIP_NEEDREINVITE);
		}
	}

	/* Reset lastrtprx timer */
	time(&p->lastrtprx);

	cw_mutex_unlock(&p->lock);
	return 0;
}

#if 0
static struct cw_tpkt *sip_get_tpkt_peer(struct cw_channel *chan)
{
    struct sip_pvt *p;
    struct cw_tpkt *tpkt = NULL;

    p = chan->tech_pvt;
    if (!p)
        return NULL;

    cw_mutex_lock(&p->lock);
    if (p->tpkt && cw_test_flag(p, SIP_CAN_REINVITE))
        tpkt = p->tpkt;
    cw_mutex_unlock(&p->lock);
    return tpkt;
}

static int sip_set_tpkt_peer(struct cw_channel *chan, struct cw_tpkt *tpkt)
{
    struct sip_pvt *p;

    p = chan->tech_pvt;
    if (!p) 
        return -1;
    cw_mutex_lock(&p->lock);
    if (tpkt)
        cw_tpkt_get_peer(tpkt, &p->redirip.sa);
    else
        p->redirip.sa.sa_family = AF_UNSPEC;
    if (!cw_test_flag(p, SIP_GOTREFER))
    {
        if (!p->pendinginvite)
        {
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Sending reinvite on SIP '%s' - It's TPKT soon redirected to IP %#l@\n", p->callid, (tpkt ? &p->redirip.sa : &p->stunaddr.sa));

            transmit_reinvite_with_t38_sdp(p);
        }
        else if (!cw_test_flag(p, SIP_PENDINGBYE))
        {
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Deferring reinvite on SIP '%s' - It's TPKT will be redirected to IP %#l@\n", p->callid, (tpkt ? &p->redirip.sa : &p->stunaddr.sa));

            cw_set_flag(p, SIP_NEEDREINVITE);    
        }
    }
    /* Reset lastrtprx timer */

    time(&p->lastrtprx);
    time(&p->lastrtptx);
    cw_mutex_unlock(&p->lock);
    return 0;
}
#endif

static int sip_handle_t38_reinvite(struct cw_channel *chan, struct sip_pvt *pvt, int reinvite)
{
	struct sip_pvt *p;
	int flag = 0;

	if (!(p = chan->tech_pvt) || !pvt->udptl)
		return -1;

	cw_mutex_lock(&p->lock);

	/* Setup everything on the other side like offered/responded from first side */
	p->t38jointcapability = p->t38peercapability = pvt->t38jointcapability;
	cw_udptl_set_far_max_datagram(p->udptl, cw_udptl_get_local_max_datagram(pvt->udptl));
	cw_udptl_set_local_max_datagram(p->udptl, cw_udptl_get_local_max_datagram(pvt->udptl));
	cw_udptl_set_error_correction_scheme(p->udptl, UDPTL_ERROR_CORRECTION_REDUNDANCY);

	if (reinvite) {
		/* If we are handling sending re-invite to the other side of the bridge */
		p->udptlredirip.sa.sa_family = AF_UNSPEC;
		if (cw_test_flag(p, SIP_CAN_REINVITE) && cw_test_flag(pvt, SIP_CAN_REINVITE)) {
			cw_sockaddr_copy(&p->udptlredirip.sa, cw_udptl_get_peer(pvt->udptl));
			flag =1;
		}

		if (!cw_test_flag(p, SIP_GOTREFER)) {
			if (!p->pendinginvite) {
				if (option_debug > 2)
					cw_log(CW_LOG_DEBUG, "Sending reinvite on SIP '%s' - It's UDPTL soon redirected to IP %#l@\n", p->callid, (flag ? &p->udptlredirip.sa : &p->stunaddr.sa));

				transmit_reinvite_with_t38_sdp(p);
			} else if (!cw_test_flag(p, SIP_PENDINGBYE)) {
				if (option_debug > 2)
					cw_log(CW_LOG_DEBUG, "Deferring reinvite on SIP '%s' - It's UDPTL will be redirected to IP %#l@\n", p->callid, (flag ? &p->udptlredirip.sa : &p->stunaddr.sa));

				cw_set_flag(p, SIP_NEEDREINVITE);
			}
		}

		time(&p->lastrtprx);
	} else {
		/* If we are handling sending 200 OK to the other side of the bridge */
		p->udptlredirip.sa.sa_family = AF_UNSPEC;
		if (cw_test_flag(p, SIP_CAN_REINVITE) && cw_test_flag(pvt, SIP_CAN_REINVITE)) {
			cw_sockaddr_copy(&p->udptlredirip.sa, cw_udptl_get_peer(pvt->udptl));
			flag = 1;
		}

		if (option_debug > 2)
			cw_log(CW_LOG_DEBUG, "Responding 200 OK on SIP '%s' - It's UDPTL soon redirected to IP %#l@\n", p->callid, (flag ? &p->udptlredirip.sa : &p->stunaddr.sa));

		pvt->t38state = SIP_T38_NEGOTIATED;
		cw_log(CW_LOG_DEBUG, "T38 changed state to %d on channel %s\n", pvt->t38state, pvt->owner ? pvt->owner->name : "<none>");
		sip_debug_ports(pvt);

		p->t38state = SIP_T38_NEGOTIATED;
		cw_log(CW_LOG_DEBUG, "T38 changed state to %d on channel %s\n", p->t38state, chan ? chan->name : "<none>");
		sip_debug_ports(p);

		cw_channel_set_t38_status(chan, T38_NEGOTIATED);
		cw_log(CW_LOG_DEBUG,"T38mode enabled for channel %s\n", chan->name);

		transmit_response_with_t38_sdp(p, "200 OK", &p->initreq, 1);
		time(&p->lastrtprx);
	}

	cw_mutex_unlock(&p->lock);
	return 0;
}

static void *dtmfmode_app;
static char dtmfmode_name[] = "SipDTMFMode";
static char dtmfmode_synopsis[] = "Change the DTMF mode for a SIP call";
static char dtmfmode_syntax[] = "SipDTMFMode(inband|info|rfc2833)";
static char dtmfmode_description[] = "Changes the DTMF mode for a SIP call\n";

static void *sipaddheader_app;
static char sipaddheader_name[] = "SipAddHeader";
static char sipaddheader_synopsis[] = "Add a SIP header to the outbound call";
static char sipaddheader_syntax[] = "SipAddHeader(Header: Content)";
static char sipaddheader_description[] =
"Adds a header to a SIP call placed with DIAL.\n"
"Remember to user the X-header if you are adding non-standard SIP\n"
"headers, like \"X-CallWeaver-Accountcode:\". Use this with care.\n"
"Adding the wrong headers may jeopardize the SIP dialog.\n"
"Always returns 0\n";

static void *sipt38switchover_app;
static char sipt38switchover_name[] = "SipT38SwitchOver";
static char sipt38switchover_synopsis[] = "Forces a T38 switchover on a non-bridged channel.";
static char sipt38switchover_syntax[] = "SipT38SwitchOver()";
static char sipt38switchover_description[] = ""
"Forces a T38 switchover on a non-bridged channel.\n";

/*! \brief  app_sipt38switchover: forces a T38 Switchover on a sip channel. */
static int sip_t38switchover(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct sip_pvt *p;
    struct cw_channel *bchan;
    struct cw_var_t *var;

    CW_UNUSED(argc);
    CW_UNUSED(argv);
    CW_UNUSED(result);

    if ((var = pbx_builtin_getvar_helper(chan, CW_KEYWORD_T38_DISABLE, "T38_DISABLE"))) {
        cw_object_put(var);
        cw_log(CW_LOG_DEBUG, "T38_DISABLE variable found. Cannot send T38 switchover.\n");
        return 0;
    }

    cw_channel_lock(chan);

    if (chan->type != channeltype)
    {
        cw_log(CW_LOG_WARNING, "This function can only be used on SIP channels.\n");
        cw_channel_unlock(chan);
        return 0;
    }

    p = chan->tech_pvt;

    /* If there is no private structure, this channel is no longer alive */
    if (!p)
    {
        cw_channel_unlock(chan);
        return 0;
    }

    if ( 
	    t38udptlsupport 
	    && (p->t38state == SIP_T38_STATUS_UNKNOWN) 
	    //&& !(cw_bridged_channel(chan))
       )
    {
        if (!cw_test_flag(p, SIP_GOTREFER))
        {
            if (!p->pendinginvite)
            {
                if (option_debug > 2)
                    cw_log(CW_LOG_DEBUG, "Forcing reinvite on SIP (%s) for T.38 negotiation.\n",chan->name);
                p->t38state = SIP_T38_OFFER_SENT_REINVITE;
		cw_channel_set_t38_status(p->owner, T38_NEGOTIATING);
                transmit_reinvite_with_t38_sdp(p);
                cw_log(CW_LOG_DEBUG, "T38 state changed to %d on channel %s\n",p->t38state,chan->name);
            }
        }
        else if (!cw_test_flag(p, SIP_PENDINGBYE))
        {
            if (option_debug > 2)
                cw_log(CW_LOG_DEBUG, "Deferring forced reinvite on SIP (%s) - it will be re-negotiated for T.38\n",chan->name);
            cw_set_flag(p, SIP_NEEDREINVITE);
        }
    }
    else {
        if ( ! t38udptlsupport ) /* Don't warn if it's disabled */
        {
            bchan = cw_bridged_channel(chan);
            cw_log(CW_LOG_WARNING,
                "Cannot execute T38 reinvite [ t38udptlsupport: %d, p->t38state %d, bridged %d ]\n",
                t38udptlsupport,
                p->t38state,
                (bchan ? 1 : 0)
            );
            if (bchan)
                cw_object_put(bchan);
        }
    }

    cw_channel_unlock(chan);

    return 0;
}

static int sip_do_t38switchover(const struct cw_channel *chan) {
    return sip_t38switchover( (struct cw_channel*) chan, 0, NULL, NULL);
}


static void *siposd_app;
static const char siposd_name[] = "SIPOSD";
static const char siposd_syntax[] = "SIPOSD(Text)";
static const char siposd_synopsis[] = "Add a SIP OSD";
static const char siposd_description[] = ""
"  SIPOSD(Text)\n"
"Send a SIP Message to be displayed onto the phone LCD. It works if\n"
"supported by the SIP phone and if the channel has  already been answered.\n"
"Omitting the text parameter will allow the previous message to be cleared.";


/*
 * Display message onto phone LCD, if supported. -- Antonio Gallo
 */
static int sip_osd(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
	struct sip_pvt *p;
	int res = 0;

	CW_UNUSED(argc);
	CW_UNUSED(result);

	if (chan->tech != &sip_tech && chan->type != channeltype) {
		cw_log(CW_LOG_WARNING, "sip_osd: Call this application only on SIP incoming calls\n");
		return 0;
	}
	if (chan->_state != CW_STATE_UP) {
		cw_log(CW_LOG_WARNING, "sip_osd: channel is NOT YET answered!\n");
		return 0;
	}

	p = chan->tech_pvt;
	if (!p) {
		cw_log(CW_LOG_WARNING, "sip_osd: P IS NULL\n");
		return 0;
	}

	res = -1;
	if (!cw_test_flag(chan, CW_FLAG_ZOMBIE) && !cw_check_hangup(chan)) {
		CHECK_BLOCKING(chan);
		res = transmit_message_with_text(p, "text/plain", "desktop", argv[0]);
		cw_clear_flag(chan, CW_FLAG_BLOCKING);
	}

	return res;
}



/*! \brief  sip_dtmfmode: change the DTMFmode for a SIP call (application) */
static int sip_dtmfmode(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct sip_pvt *p;

    CW_UNUSED(result);

    if (argc != 1 || !argv[0][0])
        return cw_function_syntax(dtmfmode_syntax);

    cw_channel_lock(chan);
    if (chan->type != channeltype)
    {
        cw_log(CW_LOG_WARNING, "Call this application only on SIP incoming calls\n");
        cw_channel_unlock(chan);
        return 0;
    }
    p = chan->tech_pvt;
    if (!p)
    {
        cw_channel_unlock(chan);
        return 0;
    }
    cw_mutex_lock(&p->lock);
    if (!strcasecmp(argv[0],"info"))
    {
        cw_clear_flag(p, SIP_DTMF);
        cw_set_flag(p, SIP_DTMF_INFO);
    }
    else if (!strcasecmp(argv[0],"rfc2833"))
    {
        cw_clear_flag(p, SIP_DTMF);
        cw_set_flag(p, SIP_DTMF_RFC2833);
    }
    else if (!strcasecmp(argv[0],"inband"))
    { 
        cw_clear_flag(p, SIP_DTMF);
        cw_set_flag(p, SIP_DTMF_INBAND);
    }
    else
        cw_log(CW_LOG_WARNING, "I don't know about this dtmf mode: %s\n", argv[0]);
    if (cw_test_flag(p, SIP_DTMF) == SIP_DTMF_INBAND)
    {
        if (!p->vad)
        {
            p->vad = cw_dsp_new();
            cw_dsp_set_features(p->vad, DSP_FEATURE_DTMF_DETECT);
        }
    }
    else
    {
        if (p->vad)
        {
            cw_dsp_free(p->vad);
            p->vad = NULL;
        }
    }
    cw_mutex_unlock(&p->lock);
    cw_channel_unlock(chan);
    return 0;
}

/*! \brief  sip_addheader: Add a SIP header */
static int sip_addheader(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    char varbuf[sizeof("_SIPADDHEADERnn")];
    struct cw_var_t *var;
    int no = 0;

    CW_UNUSED(result);

    if (argc < 1 || !argv[0][0])
        return cw_function_syntax(sipaddheader_syntax);

    cw_channel_lock(chan);

    /* Check for headers */
    for (no = 1; no <= 50; no++)
    {
        snprintf(varbuf, sizeof(varbuf), "_SIPADDHEADER%.2d", no);
        if (!(var = pbx_builtin_getvar_helper(chan, cw_hash_var_name(varbuf), varbuf)))
            break;
        cw_object_put(var);
    }
    if (no <= 50)
    {
        pbx_builtin_setvar_helper(chan, varbuf, argv[0]);
        if (sipdebug)
            cw_log(CW_LOG_DEBUG,"SIP Header added \"%s\" as %s\n", argv[0], varbuf);
    }
    else
    {
        cw_log(CW_LOG_WARNING, "Too many SIP headers added, max 50\n");
    }

    cw_channel_unlock(chan);
    return 0;
}

/*! \brief  sip_sipredirect: Transfer call before connect with a 302 redirect */
/* Called by the transfer() dialplan application through the sip_transfer() */
/* pbx interface function if the call is in ringing state */
/* coded by Martin Pycko (m78pl@yahoo.com) */
static int sip_sipredirect(struct sip_pvt *p, const char *dest)
{
    char tmp[80];
    struct cw_dynstr ds = CW_DYNSTR_INIT;
    char *cdest;
    char *extension, *host, *port;

    cdest = cw_strdupa(dest);
    extension = strsep(&cdest, "@");
    host = strsep(&cdest, ":");
    port = strsep(&cdest, ":");
    if (!extension)
    {
        cw_log(CW_LOG_ERROR, "Missing mandatory argument: extension\n");
        return -1;
    }

    /* we'll issue the redirect message here */
    if (!host)
    {
        char *localtmp;
	char *scan;

      if (!cw_db_get("SIP/Registry", extension, &ds)) {
	    host = scan = ds.data;
            if (scan[0] == '[')
                while (*scan && *(scan++) != ']');
	    while (*scan && *scan != ':') scan++;
            if (scan[0] == ':') {
                *(scan++) = '\0';
                port = scan;
                while (*scan && *scan != ':') scan++;
		*scan = '\0';
            }
      } else {
        cw_copy_string(tmp, get_header(&p->initreq, SIP_HDR_TO), sizeof(tmp));
        if (!strlen(tmp))
        {
            cw_log(CW_LOG_ERROR, "Cannot retrieve the 'To' header from the original SIP request!\n");
            return -1;
        }
        if (((localtmp = strstr(tmp, "sip:")) || (localtmp = strstr(tmp, "SIP:"))) && (localtmp = strchr(localtmp, '@')))
        {
            char lhost[80];
            char lport[80];
            
            memset(lhost, 0, sizeof(lhost));
            memset(lport, 0, sizeof(lport));
            localtmp++;
            /* This is okey because lhost and lport are as big as tmp */
            sscanf(localtmp, "%[^<>:; ]:%[^<>:; ]", lhost, lport);
            if (!strlen(lhost))
            {
                cw_log(CW_LOG_ERROR, "Can't find the host address\n");
                return -1;
            }
            host = cw_strdupa(lhost);
            if (!cw_strlen_zero(lport))
                port = cw_strdupa(lport);
        }
      } // else If the device is not registered
    }

    snprintf(p->our_contact, sizeof(p->our_contact), "Transfer <sip:%s@%s%s%s>", extension, host, (port ? ":" : ""), (port ? port : ""));

    cw_dynstr_free(&ds);

    cw_set_flag(&p->initreq, FLAG_FATAL);
    transmit_response(p, "302 Moved Temporarily", &p->initreq, 1);

    /* this is all that we want to send to that SIP device */
    cw_set_flag(p, SIP_ALREADYGONE);

    return 0;
}

/*! \brief  sip_get_codec: Return SIP UA's codec (part of the RTP interface) */
static int sip_get_codec(struct cw_channel *chan)
{
    struct sip_pvt *p = chan->tech_pvt;
    return p->peercapability;    
}

/*! \brief  sip_rtp: Interface structure with callbacks used to connect to rtp module */
static struct cw_rtp_protocol sip_rtp =
{
    type: channeltype,
    get_rtp_info: sip_get_rtp_peer,
    get_vrtp_info: sip_get_vrtp_peer,
    set_rtp_peer: sip_set_rtp_peer,
    get_codec: sip_get_codec,
};

/*! \brief  sip_udptl: Interface structure with callbacks used to connect to UDPTL module */
static struct cw_udptl_protocol sip_udptl =
{
    type: channeltype,
    get_udptl_info: sip_get_udptl_peer,
    set_udptl_peer: sip_set_udptl_peer,
};

#if 0
/*! \brief  sip_tpkt: Interface structure with callbacks used to connect to TPKT module */
static struct cw_tpkt_protocol sip_tpkt =
{
    get_tpkt_info: sip_get_tpkt_peer,
    set_tpkt_peer: sip_set_tpkt_peer,
};
#endif

static int sip_poke_peer_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);

	CW_UNUSED(data);

	/* FIXME: peer qualifies should be staggered in a similar manner to registrations */
	if (peer->maxms && peer->pokeexpire == -1)
		peer->pokeexpire = cw_sched_add(sched, 1, sip_poke_peer, cw_object_dup(peer));

	return 0;
}

/*! \brief  sip_poke_new_peers: Send a poke to all new peers */
static void sip_poke_new_peers(void)
{
	cw_registry_iterate(&peerbyname_registry, sip_poke_peer_one, NULL);
}

/*! \brief  sip_send_all_registers: Send all known registrations */
static void sip_send_all_registers(void)
{
    struct sip_registry *reg;
    int ms;
    int regspacing;
    int shift = 1 + (int) (100.0 * (cw_random() / (RAND_MAX + 1.0)));

    if (!regobjs)
        return;

    regspacing = default_expiry * 1000/regobjs;
    if (regspacing > 100)
        regspacing = 100 + shift;

    ms = 0;

    for (reg = regl; reg; reg = reg->next) {
        if (reg->expire == -1 || cw_sched_del(sched, reg->expire))
		cw_object_dup(reg);

        reg->expire = cw_sched_add(sched, ms, sip_reregister, reg);

        ms += regspacing;
    }
}


static int flush_peers_one(struct cw_object *obj, void *data)
{
	struct sip_peer *peer = container_of(obj, struct sip_peer, obj);

	CW_UNUSED(data);

	/* Tell qualification to stop */
	cw_set_flag(peer, SIP_ALREADYGONE);

	cw_registry_del(&peerbyname_registry, peer->reg_entry_byname);
	peer->reg_entry_byname = NULL;
	if (peer->reg_entry_byaddr)
		cw_registry_del(&peerbyaddr_registry, peer->reg_entry_byaddr);
	peer->reg_entry_byaddr = NULL;

	if (peer->pokeexpire != -1 && !cw_sched_del(sched, peer->pokeexpire)) {
		cw_object_put(peer);
		peer->pokeexpire = -1;
	}

	return 0;
}


static void sip_destroyall_registry(void)
{
	struct sip_registry *reg;

	for (; (reg = regl); regl = regl->next) {
		reg->regstate = REG_STATE_SHUTDOWN;

		if (reg->dialogue) {
			if (option_debug > 2)
				cw_log(CW_LOG_DEBUG, "Destroying active SIP dialog for registry %s@%s\n", reg->username, reg->hostname);
			sip_destroy(reg->dialogue);
		}

		if (reg->expire > -1 && !cw_sched_del(sched, reg->expire))
			cw_object_put(reg);

		if (reg->timeout > -1 && !cw_sched_del(sched, reg->timeout))
			cw_object_put(reg);

		cw_object_put(reg);
	}
}


static int listener_close(struct cw_object *obj, void *data)
{
	struct cw_connection *conn = container_of(obj, struct cw_connection, obj);

	CW_UNUSED(data);

	if (conn->tech == &tech_sip && (conn->state == INIT || conn->state == LISTENING))
		cw_connection_close(conn);

	return 0;
}


/*! \brief  sip_reload_config: Re-read SIP.conf config file */
/*    This function reloads all config data, except for
    active peers (with registrations). They will only
    change configuration data at restart, not at reload.
    SIP debug state will not change
 */
static int sip_reload_config(void)
{
    char addrbuf[CW_MAX_ADDRSTRLEN > MAXHOSTNAMELEN ? CW_MAX_ADDRSTRLEN : MAXHOSTNAMELEN];
    struct cw_dynstr addr_ds = CW_DYNSTR_INIT_STATIC(addrbuf);
    struct cw_config *cfg;
    struct cw_variable *v;
    struct sip_peer *peer;
    struct sip_user *user;
    struct cw_hostent ahp;
    char *cat;
    char *utype;
    struct hostent *hp;
    int format;
    struct cw_flags dummy;
    int auto_sip_domains = 0;

    /* We *must* have a config file otherwise stop immediately */
    if (!(cfg = cw_config_load(config)))
    {
        cw_log(CW_LOG_NOTICE, "Unable to load config %s\n", config);
        return -1;
    }

    /* Shut down any existing listeners */
    cw_registry_iterate(&cw_connection_registry, listener_close, NULL);

    cw_registry_iterate(&peerbyname_registry, flush_peers_one, NULL);
    cw_registry_flush(&userbyname_registry);

    sip_destroyall_registry();
    regl_last = &regl;

    clear_realm_authentication(&authl);
    clear_sip_domains();

    /* Reset IP addresses  */
    memset(&externip, 0, sizeof(externip));
    cw_acl_free(localaddr);
    localaddr = NULL;
    memset(&prefs, 0 , sizeof(prefs));

    stunserver_ip.sin_family = AF_UNSPEC;

    /* Initialize some reasonable defaults at SIP reload */
    cw_copy_string(default_context, DEFAULT_CONTEXT, sizeof(default_context));
    default_subscribecontext[0] = '\0';
    default_language[0] = '\0';
    default_fromdomain[0] = '\0';
    default_qualify = 0;
    allow_external_domains = 1;    /* Allow external invites */
    externhost[0] = '\0';
    externexpire = 0;
    externrefresh = 10;
    cw_copy_string(default_useragent, DEFAULT_USERAGENT, sizeof(default_useragent));
    cw_copy_string(default_notifymime, DEFAULT_NOTIFYMIME, sizeof(default_notifymime));
    global_notifyringing = 1;
    global_alwaysauthreject = 0;
    cw_copy_string(global_realm, DEFAULT_REALM, sizeof(global_realm));
    cw_copy_string(global_musicclass, "default", sizeof(global_musicclass));
    cw_copy_string(default_callerid, DEFAULT_CALLERID, sizeof(default_callerid));
    memset(&outboundproxyip, 0, sizeof(outboundproxyip));
    videosupport = 0;
    t38udptlsupport = 0;
    t38rtpsupport = 0;
    t38tcpsupport = 0;
    sip_hdr_name = sip_hdr_fullname;
    relaxdtmf = 0;
    callevents = 0;
    global_rtptimeout = 0;
    global_rtpholdtimeout = 0;
    global_rtpkeepalive = 0;
    global_rtautoclear = 120;
    pedanticsipchecking = 0;
    global_reg_timeout = DEFAULT_REGISTRATION_TIMEOUT;
    global_regattempts_max = 0;
    cw_clear_flag(&global_flags, CW_FLAGS_ALL);
    cw_clear_flag(&global_flags_page2, CW_FLAGS_ALL);
    cw_set_flag(&global_flags, SIP_DTMF_RFC2833);
    cw_set_flag(&global_flags, SIP_NAT_RFC3581);
    cw_set_flag(&global_flags, SIP_CAN_REINVITE);
    cw_set_flag(&global_flags_page2, SIP_PAGE2_RTUPDATE);
    global_mwitime = DEFAULT_MWITIME;
    strcpy(global_vmexten, DEFAULT_VMEXTEN);
    srvlookup = 0;
    autocreatepeer = 0;
    rfc_timer_t1 = DEFAULT_RFC_TIMER_T1;
    rfc_timer_t2 = DEFAULT_RFC_TIMER_T2;
    rfc_timer_b = DEFAULT_RFC_TIMER_B;
    regcontext[0] = '\0';
    tos = 0;
    global_allowguest = 1;

    /* Copy the default jb config over global_jbconf */
    cw_jb_default_config(&global_jbconf);

    /* Read the [general] config section of sip.conf (or from realtime config) */
    for (v = cw_variable_browse(cfg, "general"); v; v = v->next)
    {
        if (handle_common_options(&global_flags, &dummy, v))
            ; /* Done */
	else if(cw_jb_read_conf(&global_jbconf, v->name, v->value) == 0)
            ; /* Done */
	else if (!strcasecmp(v->name, "context"))
            cw_copy_string(default_context, v->value, sizeof(default_context));
        else if (!strcasecmp(v->name, "realm"))
            cw_copy_string(global_realm, v->value, sizeof(global_realm));
        else if (!strcasecmp(v->name, "useragent"))
        {
            cw_copy_string(default_useragent, v->value, sizeof(default_useragent));
            cw_log(CW_LOG_DEBUG, "Setting User Agent Name to %s\n", default_useragent);
        }
        else if (!strcasecmp(v->name, "rtcachefriends"))
            cw_set2_flag((&global_flags_page2), cw_true(v->value), SIP_PAGE2_RTCACHEFRIENDS);
        else if (!strcasecmp(v->name, "rtupdate"))
            cw_set2_flag((&global_flags_page2), cw_true(v->value), SIP_PAGE2_RTUPDATE);
        else if (!strcasecmp(v->name, "ignoreregexpire"))
            cw_set2_flag((&global_flags_page2), cw_true(v->value), SIP_PAGE2_IGNOREREGEXPIRE);
        else if (!strcasecmp(v->name, "rtautoclear"))
        {
            int i = atoi(v->value);
            if (i > 0)
                global_rtautoclear = i;
            else
                i = 0;
            cw_set2_flag((&global_flags_page2), i || cw_true(v->value), SIP_PAGE2_RTAUTOCLEAR);
        }
        else if (!strcasecmp(v->name, "usereqphone"))
            cw_set2_flag((&global_flags), cw_true(v->value), SIP_USEREQPHONE);
        else if (!strcasecmp(v->name, "relaxdtmf"))
            relaxdtmf = cw_true(v->value);
        else if (!strcasecmp(v->name, "checkmwi"))
        {
            if ((sscanf(v->value, "%d", &global_mwitime) != 1) || (global_mwitime < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid MWI time setting at line %d.  Using default (10).\n", v->value, v->lineno);
                global_mwitime = DEFAULT_MWITIME;
            }
        }
        else if (!strcasecmp(v->name, "vmexten"))
            cw_copy_string(global_vmexten, v->value, sizeof(global_vmexten));
        else if (!strcasecmp(v->name, "rtptimeout"))
        {
            if ((sscanf(v->value, "%d", &global_rtptimeout) != 1) || (global_rtptimeout < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
                global_rtptimeout = 0;
            }
        }
        else if (!strcasecmp(v->name, "rtpholdtimeout"))
        {
            if ((sscanf(v->value, "%d", &global_rtpholdtimeout) != 1) || (global_rtpholdtimeout < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP hold time at line %d.  Using default.\n", v->value, v->lineno);
                global_rtpholdtimeout = 0;
            }
        }
        else if (!strcasecmp(v->name, "rtpkeepalive"))
        {
            if ((sscanf(v->value, "%d", &global_rtpkeepalive) != 1) || (global_rtpkeepalive < 0))
            {
                cw_log(CW_LOG_WARNING, "'%s' is not a valid RTP keepalive time at line %d.  Using default.\n", v->value, v->lineno);
                global_rtpkeepalive = 0;
            }
        }
        else if (!strcasecmp(v->name, "videosupport"))
            videosupport = cw_true(v->value);
        else if (!strcasecmp(v->name, "t38udptlsupport"))
            t38udptlsupport = cw_true(v->value);
        else if (!strcasecmp(v->name, "t38rtpsupport"))
            t38rtpsupport = cw_true(v->value);
        else if (!strcasecmp(v->name, "t38tcpsupport"))
            t38tcpsupport = cw_true(v->value);
        else if (!strcasecmp(v->name, "compactheaders")) {
            if (cw_true(v->value))
		    sip_hdr_name = sip_hdr_shortname;
	} else if (!strcasecmp(v->name, "notifymimetype"))
            cw_copy_string(default_notifymime, v->value, sizeof(default_notifymime));
        else if (!strcasecmp(v->name, "notifyringing"))
            global_notifyringing = cw_true(v->value);
	else if (!strcasecmp(v->name, "alwaysauthreject"))
	    global_alwaysauthreject = cw_true(v->value);
        else if (!strcasecmp(v->name, "musicclass") || !strcasecmp(v->name, "musiconhold"))
            cw_copy_string(global_musicclass, v->value, sizeof(global_musicclass));
        else if (!strcasecmp(v->name, "language"))
            cw_copy_string(default_language, v->value, sizeof(default_language));
        else if (!strcasecmp(v->name, "regcontext"))
        {
            cw_copy_string(regcontext, v->value, sizeof(regcontext));
            /* Create context if it doesn't exist already */
            if (!cw_context_find(regcontext))
                cw_context_create(NULL, regcontext, channeltype);
        }
        else if (!strcasecmp(v->name, "callerid"))
            cw_copy_string(default_callerid, v->value, sizeof(default_callerid));
        else if (!strcasecmp(v->name, "fromdomain"))
            cw_copy_string(default_fromdomain, v->value, sizeof(default_fromdomain));
        else if (!strcasecmp(v->name, "outboundproxy"))
            async_get_ip(NULL, &outboundproxyip.sa, v->value, (srvlookup ? "_sip._udp" : NULL));
        else if (!strcasecmp(v->name, "outboundproxyport"))
        {
            sscanf(v->value, "%d", &format);
            cw_sockaddr_set_port(&outboundproxyip.sa, format);
        }
        else if (!strcasecmp(v->name, "autocreatepeer"))
            autocreatepeer = cw_true(v->value);
        else if (!strcasecmp(v->name, "rtt") || !strcasecmp(v->name, "timer_t1"))
            rfc_timer_t1 = atoi(v->value);
        else if (!strcasecmp(v->name, "timer_t2"))
            rfc_timer_t2 = atoi(v->value);
        else if (!strcasecmp(v->name, "maxinvitetries"))
            rfc_timer_b = 1 << (atoi(v->value) - 1);
        else if (!strcasecmp(v->name, "srvlookup"))
            srvlookup = cw_true(v->value);
        else if (!strcasecmp(v->name, "pedantic"))
            pedanticsipchecking = cw_true(v->value);
        else if (!strcasecmp(v->name, "maxexpiry") || !strcasecmp(v->name, "maxexpirey"))
        {
            if ((max_expiry = atoi(v->value)) < 1)
                max_expiry = DEFAULT_MAX_EXPIRY;
        }
        else if (!strcasecmp(v->name, "defaultexpiry") || !strcasecmp(v->name, "defaultexpirey"))
        {
            if ((default_expiry = atoi(v->value)) < 1)
                default_expiry = DEFAULT_DEFAULT_EXPIRY;
        }
        else if (!strcasecmp(v->name, "sipdebug"))
            sipdebug = cw_true(v->value);
        else if (!strcasecmp(v->name, "dumphistory") || !strcasecmp(v->name, "recordhistory"))
            cw_log(CW_LOG_WARNING, "%s is deprecated and should be removed from %s\n", v->name, config);
        else if (!strcasecmp(v->name, "registertimeout"))
        {
            if ((global_reg_timeout = atoi(v->value)) < 1)
                global_reg_timeout = DEFAULT_REGISTRATION_TIMEOUT;
        }
        else if (!strcasecmp(v->name, "registerattempts"))
            global_regattempts_max = atoi(v->value);
        else if (!strcasecmp(v->name, "localnet"))
        {
            int err;

            if ((err = cw_acl_add(&localaddr, "p", v->value)))
                cw_log(CW_LOG_ERROR, "%s = %s: %s\n", v->name, v->value, gai_strerror(err));
        }
        else if (!strcasecmp(v->name, "localmask"))
        {
            cw_log(CW_LOG_WARNING, "Use of localmask is no long supported -- use localnet with mask syntax\n");
        }
        else if (!strcasecmp(v->name, "externip"))
        {
            static const struct addrinfo hints = {
                    .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_PASSIVE | AI_IDN,
                    .ai_family = AF_UNSPEC,
                    .ai_socktype = SOCK_DGRAM,
            };
            struct addrinfo *addrs;
            int err;

            if (!(err = cw_getaddrinfo(v->value, "0", &hints, &addrs, NULL))) {
                    memcpy(&externip, addrs->ai_addr, addrs->ai_addrlen);
                    freeaddrinfo(addrs);
            } else
                    cw_log(CW_LOG_WARNING, "externip = %s: %s\n", v->value, gai_strerror(err));
        }
        else if (!strcasecmp(v->name, "externhost"))
        {
            static const struct addrinfo hints = {
                    .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_PASSIVE | AI_IDN,
                    .ai_family = AF_UNSPEC,
                    .ai_socktype = SOCK_DGRAM,
            };
            struct addrinfo *addrs;
            int err;

            cw_copy_string(externhost, v->value, sizeof(externhost));

            if (!(err = cw_getaddrinfo(v->value, "0", &hints, &addrs, NULL))) {
                    memcpy(&externip, addrs->ai_addr, addrs->ai_addrlen);
                    freeaddrinfo(addrs);
            } else
                    cw_log(CW_LOG_WARNING, "externhost = %s: %s\n", v->value, gai_strerror(err));

            time(&externexpire);
        }
        else if (!strcasecmp(v->name, "externrefresh"))
        {
            if (sscanf(v->value, "%d", &externrefresh) != 1)
            {
                cw_log(CW_LOG_WARNING, "Invalid externrefresh value '%s', must be an integer >0 at line %d\n", v->value, v->lineno);
                externrefresh = 10;
            }
        }
        else if (!strcasecmp(v->name, "stunserver_host"))
        {
            if ((hp = cw_gethostbyname(v->value, &ahp)))
            {
                stunserver_ip.sin_family = AF_INET;
                stunserver_ip.sin_port = htons(3478);
                memcpy(&stunserver_ip.sin_addr, hp->h_addr, sizeof(stunserver_ip.sin_addr));
                cw_log(CW_LOG_NOTICE, "STUN: stunserver_host is: %s\n", v->value);
            }
            else
                cw_log(CW_LOG_NOTICE, "Invalid address for stunserver_host keyword: %s\n", v->value);
        }
        else if (!strcasecmp(v->name, "stunserver_port"))
        {
            int n;

            if (sscanf(v->value, "%d", &n) == 1)
                stunserver_ip.sin_port = htons(n);
            else
                    cw_log(CW_LOG_WARNING, "Invalid stun port number '%s' at line %d of %s\n", v->value, v->lineno, config);
        }
        else if (!strcasecmp(v->name, "allow"))
            cw_parse_allow_disallow(&prefs, &global_capability, v->value, 1);
        else if (!strcasecmp(v->name, "disallow"))
            cw_parse_allow_disallow(&prefs, &global_capability, v->value, 0);
        else if (!strcasecmp(v->name, "allowexternaldomains"))
            allow_external_domains = cw_true(v->value);
        else if (!strcasecmp(v->name, "autodomain"))
            auto_sip_domains = cw_true(v->value);
        else if (!strcasecmp(v->name, "domain"))
        {
            char *domain = cw_strdupa(v->value);
            char *context = strchr(domain, ',');

            if (context)
                *context++ = '\0';

            if (cw_strlen_zero(domain))
                cw_log(CW_LOG_WARNING, "Empty domain specified at line %d\n", v->lineno);
            else if (cw_strlen_zero(context))
                cw_log(CW_LOG_WARNING, "Empty context specified at line %d for domain '%s'\n", v->lineno, domain);
            else
                add_sip_domain(cw_strip(domain), SIP_DOMAIN_CONFIG, context ? cw_strip(context) : "");
        }
        else if (!strcasecmp(v->name, "register"))
            sip_register(v->value, v->lineno);
        else if (!strcasecmp(v->name, "tos"))
        {
            if (cw_str2tos(v->value, &tos))
                cw_log(CW_LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
        }
        else if (!strcasecmp(v->name, "qualify"))
        {
            if (!strcasecmp(v->value, "no"))
                default_qualify = 0;
            else if (!strcasecmp(v->value, "yes"))
                default_qualify = DEFAULT_MAXMS;
            else if (sscanf(v->value, "%d", &default_qualify) != 1)
            {
                cw_log(CW_LOG_WARNING, "Qualification default should be 'yes', 'no', or a number of milliseconds at line %d of sip.conf\n", v->lineno);
                default_qualify = 0;
            }
        }
        else if (!strcasecmp(v->name, "callevents"))
            callevents = cw_true(v->value);
        else if (strcasecmp(v->name, "bindaddr") && strcasecmp(v->name, "bindport"))
            cw_log(CW_LOG_ERROR, "sip.conf line %d: %s is not valid here\n", v->lineno, v->name);
    }

    if (!allow_external_domains && CW_LIST_EMPTY(&domain_list))
    {
        cw_log(CW_LOG_WARNING, "To disallow external domains, you need to configure local SIP domains.\n");
        allow_external_domains = 1;
    }

    /* Build list of authentication to various SIP realms, i.e. service providers */
    for (v = cw_variable_browse(cfg, "authentication"); v; v = v->next)
    {
        /* Format for authentication is auth = username:password@realm */
        if (!strcasecmp(v->name, "auth"))
             authl = add_realm_authentication(authl, v->value, v->lineno);
        else
            cw_log(CW_LOG_ERROR, "sip.conf line %d: %s is not valid here\n", v->lineno, v->name);
    }

    for (cat = cw_category_browse(cfg, NULL); cat; cat = cw_category_browse(cfg, cat)) {
	int isconn;

        if ((isconn = !strcasecmp(cat, "connection")) || !strcasecmp(cat, "general")) {
            char *addr = NULL;
            char *port = NULL;
	    int conntos = tos;

            for (v = cw_variable_browse(cfg, cat); v; v = v->next) {
                if (!strcasecmp(v->name, "bindaddr"))
                    addr = v->value;
		else if (!strcasecmp(v->name, "bindport"))
                    port = v->value;
		else if (isconn) {
                    if (!strcasecmp(v->name, "tos")) {
                        if (cw_str2tos(v->value, &conntos))
                            cw_log(CW_LOG_WARNING, "Invalid \"tos\" value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
		    } else
                        cw_log(CW_LOG_ERROR, "sip.conf line %d: \"%s\" is not valid here\n", v->lineno, v->name);
                }
            }

            if (addr) {
                static const struct addrinfo hints = {
                    .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_PASSIVE | AI_IDN,
                    .ai_family = AF_UNSPEC,
                    .ai_socktype = SOCK_DGRAM,
                };
                struct addrinfo *addrs;
                int err;

                if (!isconn)
                    cw_log(CW_LOG_WARNING, "Use of \"bindaddr\" and \"bindport\" in the \"general\" section of sip.conf is deprecated - move them to a \"connection\" section\n");

                if (!(err = cw_getaddrinfo(addr, port, &hints, &addrs, NULL))) {
                    struct addrinfo *ai;

                    if (auto_sip_domains) {
                        if ((strchr(addr, '.') && addr[strspn(addr, ".0123456789")] == '\0')
                        || ((utype = strchr(addr, ':')) && strchr(utype + 1, ':') && addr[strspn(addr, ":.0123456789abcdefABCDEF")] == '\0'))
                            add_sip_domain(addr, SIP_DOMAIN_AUTO, NULL);
                    }

                    for (ai = addrs; ai; ai = ai->ai_next) {
                        struct cw_connection *conn;

                        if ((conn = cw_connection_listen(SOCK_DGRAM, ai->ai_addr, ai->ai_addrlen, &tech_sip, NULL))) {
                            cw_log(CW_LOG_NOTICE, "Listening on %#l@\n", ai->ai_addr);

                            cw_udpfromto_init(conn->sock, conn->addr.sa_family);
                            if (conntos)
                                setsockopt(conn->sock, IPPROTO_IP, IP_TOS, &conntos, sizeof(conntos));

#if 0
			    /* This would assume every destination on this connection
			     * goes through the same NAT gateways and thus has the
			     * same effective external address.
			     * Of course, we really want at least externip, and preferably
			     * stunserver_ip as well, to be per-listener.
			     */
                            if (conn->addr.sa_family == stunserver_ip.sin_family)
                                cw_stun_bindrequest(conn->sock, &conn->addr, conn->addrlen, &stunserver_ip.sa, sizeof(stunserver_ip), &externip.sin);
#endif

                            cw_object_put(conn);
                        } else
                            cw_log(CW_LOG_ERROR, "Unable to listen on %#l@: %s\n", ai->ai_addr, strerror(errno));

                        if (auto_sip_domains) {
                            if (cw_sockaddr_is_specific(ai->ai_addr)) {
                                cw_dynstr_printf(&addr_ds, "%#@", ai->ai_addr);
                                add_sip_domain(addrbuf, SIP_DOMAIN_AUTO, NULL);
                                cw_dynstr_reset(&addr_ds);
                            } else
                                cw_log(CW_LOG_NOTICE, "Can't add wildcard IP address to domain list, please add IP address to domain manually.\n");
                        }
                    }

                    freeaddrinfo(addrs);
                } else
                    cw_log(CW_LOG_WARNING, "unable to resolve %s: %s\n", v->value, gai_strerror(err));
            }
        }
    }

    /* Load peers, users and friends */
    for (cat = cw_category_browse(cfg, NULL); cat; cat = cw_category_browse(cfg, cat))
    {
        if (strcasecmp(cat, "general") && strcasecmp(cat, "authentication") && strcasecmp(cat, "connection"))
        {
            if ((utype = cw_variable_retrieve(cfg, cat, "type")))
            {
                if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend"))
                {
                    if ((user = build_user(cat, cw_variable_browse(cfg, cat), 0))) {
                        user->reg_entry_byname = cw_registry_add(&userbyname_registry, cw_hash_string(0, user->name), &user->obj);
                        cw_object_put(user);
                    }
                }
                if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend"))
                {
                    if ((peer = build_peer(cat, cw_variable_browse(cfg, cat), 0, 0)))
                        cw_object_put(peer);
                }
                else if (strcasecmp(utype, "user"))
                    cw_log(CW_LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, "sip.conf");
            }
            else
                cw_log(CW_LOG_WARNING, "Section '%s' lacks type\n", cat);
        }
    }

    /* Add default domains - host name, IP address and IP:port */
    /* Only do this if user added any sip domain with "localdomains" */
    /* In order to *not* break backwards compatibility */
    /*     Some phones address us at IP only, some with additional port number */
    if (auto_sip_domains)
    {
//TODO add stun ip to domains if needed
        /* Our extern IP address, if configured */
        if (externip.sa.sa_family != AF_UNSPEC)
        {
            cw_dynstr_printf(&addr_ds, "%#@", &externip.sa);
            add_sip_domain(addrbuf, SIP_DOMAIN_AUTO, NULL);
            cw_dynstr_reset(&addr_ds);
        }

        /* Extern host name (NAT traversal support) */
        if (!cw_strlen_zero(externhost))
            add_sip_domain(externhost, SIP_DOMAIN_AUTO, NULL);

        /* Our host name */
        if (!gethostname(addrbuf, sizeof(addrbuf)))
            add_sip_domain(addrbuf, SIP_DOMAIN_AUTO, NULL);
    }

    /* Release configuration from memory */
    cw_config_destroy(cfg);

    /* Load the list of manual NOTIFY types to support */
    if (notify_types)
        cw_config_destroy(notify_types);
    notify_types = cw_config_load(notify_config);

    sip_poke_new_peers();
    sip_send_all_registers();

    return 0;
}


/*! \brief  sip_reload: Force reload of module from cli */
static int sip_reload(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(ds_p);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	if (!pthread_rwlock_trywrlock(&sip_reload_lock))
		sip_reload_config();
	else
		cw_dynstr_printf(ds_p, "SIP config busy - try again\n");

	pthread_rwlock_unlock(&sip_reload_lock);

	return 0;
}

/*! \brief  reload: Part of CallWeaver module interface */
static int reload_module(void)
{
	if (option_verbose > 0)
		cw_verbose(VERBOSE_PREFIX_1 "Reloading SIP\n");

	pthread_rwlock_wrlock(&sip_reload_lock);
	sip_reload_config();
	pthread_rwlock_unlock(&sip_reload_lock);

	return 0;
}

static struct cw_clicmd  my_clis[] = {
    {
	    .cmda = { "sip", "notify", NULL },
	    .handler = sip_notify,
	    .summary = "Send a notify packet to a SIP peer",
	    .usage = notify_usage,
	    .generator = complete_sipnotify,
    },
    {
	    .cmda = { "sip", "show", "users", NULL },
	    .handler = sip_show_users,
	    .summary = "Show defined SIP users",
	    .usage = show_users_usage,
    },
    {
	    .cmda = { "sip", "show", "user", NULL },
	    .handler = sip_show_user,
	    .summary = "Show details on specific SIP user",
	    .usage = show_user_usage,
	    .generator = complete_sip_show_user,
    },
    {
	    .cmda = { "sip", "show", "subscriptions", NULL },
	    .handler = sip_show_subscriptions,
	    .summary = "Show active SIP subscriptions",
	    .usage = show_subscriptions_usage,
    },
    {
	    .cmda = { "sip", "show", "channels", NULL },
	    .handler = sip_show_channels,
	    .summary = "Show active SIP channels",
	    .usage = show_channels_usage,
    },
    {
	    .cmda = { "sip", "show", "channel", NULL },
	    .handler = sip_show_channel,
	    .summary = "Show detailed SIP channel info",
	    .usage = show_channel_usage,
	    .generator = complete_sipch,
    },
    {
	    .cmda = { "sip", "show", "domains", NULL },
	    .handler = sip_show_domains,
	    .summary = "List our local SIP domains.",
	    .usage = show_domains_usage,
    },
    {
	    .cmda = { "sip", "show", "settings", NULL },
	    .handler = sip_show_settings,
	    .summary = "Show SIP global settings",
	    .usage = show_settings_usage,
    },
    {
	    .cmda = { "sip", "debug", NULL },
	    .handler = sip_do_debug,
	    .summary = "Enable SIP debugging",
	    .usage = debug_usage,
    },
    {
	    .cmda = { "sip", "debug", "show", NULL },
	    .handler = sip_do_debug,
	    .summary = "Show SIP debugging state",
	    .usage = debug_usage,
    },
    {
	    .cmda = { "sip", "debug", "ip", NULL },
	    .handler = sip_do_debug,
	    .summary = "Enable SIP debugging on IP",
	    .usage = debug_usage,
    },
    {
	    .cmda = { "sip", "debug", "peer", NULL },
	    .handler = sip_do_debug,
	    .summary = "Enable SIP debugging on Peername",
	    .usage = debug_usage,
	    .generator = complete_sip_debug_peer,
    },
    {
	    .cmda = { "sip", "show", "peer", NULL },
	    .handler = sip_show_peer,
	    .summary = "Show details on specific SIP peer",
	    .usage = show_peer_usage,
	    .generator = complete_sip_show_peer,
    },
    {
	    .cmda = { "sip", "show", "peers", NULL },
	    .handler = sip_show_peers,
	    .summary = "Show defined SIP peers",
	    .usage = show_peers_usage,
    },
    {
	    .cmda = { "sip", "prune", "realtime", NULL },
	    .handler = sip_prune_realtime,
	    .summary = "Prune cached Realtime object(s)",
	    .usage = prune_realtime_usage,
    },
    {
	    .cmda = { "sip", "prune", "realtime", "peer", NULL },
	    .handler = sip_prune_realtime,
	    .summary = "Prune cached Realtime peer(s)",
	    .usage = prune_realtime_usage,
	    .generator = complete_sip_prune_realtime_peer,
    },
    {
	    .cmda = { "sip", "prune", "realtime", "user", NULL },
	    .handler = sip_prune_realtime,
	    .summary = "Prune cached Realtime user(s)",
	    .usage = prune_realtime_usage,
	    .generator = complete_sip_prune_realtime_user,
    },
    {
	    .cmda = { "sip", "show", "inuse", NULL },
	    .handler = sip_show_inuse,
	    .summary = "List all inuse/limits",
	    .usage = show_inuse_usage,
    },
    {
	    .cmda = { "sip", "show", "registry", NULL },
	    .handler = sip_show_registry,
	    .summary = "Show SIP registration status",
	    .usage = show_reg_usage,
    },
    {
	    .cmda = { "sip", "no", "debug", NULL },
	    .handler = sip_no_debug,
	    .summary = "Disable SIP debugging",
	    .usage = no_debug_usage,
    },
    {
	    .cmda = { "sip", "reload", NULL },
	    .handler = sip_reload,
	    .summary = "Reload SIP configuration",
	    .usage = sip_reload_usage,
    },
};


static struct manager_action manager_actions[] = {
	{
		.action = "SIPpeers",
		.authority = EVENT_FLAG_SYSTEM,
		.func = manager_sip_show_peers,
		.synopsis = "List SIP peers (text format)",
		.description = mandescr_show_peers,
	},
	{
		.action = "SIPshowpeer",
		.authority = EVENT_FLAG_SYSTEM,
		.func = manager_sip_show_peer,
		.synopsis = "Show SIP peer (text format)",
		.description = mandescr_show_peer,
	},
};


/*! \brief  load_module: PBX load module - initialization */
static int load_module(void)
{
    cw_registry_init(&dialogue_registry, 1024);
    cw_registry_init(&peerbyname_registry, 1024);
    cw_registry_init(&peerbyaddr_registry, 1024);
    cw_registry_init(&userbyname_registry, 1024);

    pthread_rwlock_init(&sip_reload_lock, NULL);
    pthread_rwlock_init(&debugacl.lock, NULL);

    if ((sched = sched_context_create(1)) == NULL)
        cw_log(CW_LOG_WARNING, "Unable to create schedule context\n");

    if ((io = cw_io_context_create(256)) == CW_IO_CONTEXT_NONE)
        cw_log(CW_LOG_WARNING, "Unable to create I/O context\n");

    /* Make sure we can register our sip channel type */
    if (cw_channel_register(&sip_tech))
    {
        cw_log(CW_LOG_ERROR, "Unable to register channel type %s\n", channeltype);
        return -1;
    }

    /* Tell the RTP subdriver that we're here */
    cw_rtp_proto_register(&sip_rtp);

    /* Tell the UDPTL subdriver that we're here */
    cw_udptl_proto_register(&sip_udptl);

    /* Tell the TPKT subdriver that we're here */
    //cw_tpkt_proto_register(&sip_tpkt);

    sip_reload_config();    /* Load the configuration from sip.conf */

    if (cw_pthread_create(&monitor_thread, &global_attr_default, do_monitor, NULL) < 0) {
        cw_log(CW_LOG_ERROR, "Unable to start monitor thread.\n");
        return -1;
    }

    /* Register dialplan functions */
    dtmfmode_app = cw_register_function(dtmfmode_name, sip_dtmfmode, dtmfmode_synopsis, dtmfmode_syntax, dtmfmode_description);
    sipt38switchover_app = cw_register_function(sipt38switchover_name, sip_t38switchover, sipt38switchover_synopsis, sipt38switchover_syntax, sipt38switchover_description);
    cw_install_t38_functions(sip_do_t38switchover);
    sipheader_function = cw_register_function(sipheader_func_name, func_header_read, sipheader_func_synopsis, sipheader_func_syntax, sipheader_func_desc);
    sippeer_function = cw_register_function(sippeer_func_name, function_sippeer, sippeer_func_synopsis, sippeer_func_syntax, sippeer_func_desc);
    sippeervar_function = cw_register_function(sippeervar_func_name, function_sippeervar, sippeervar_func_synopsis, sippeervar_func_syntax, sippeervar_func_desc);
    sipchaninfo_function = cw_register_function(sipchaninfo_func_name, function_sipchaninfo_read, sipchaninfo_func_synopsis, sipchaninfo_func_syntax, sipchaninfo_func_desc);
    checksipdomain_function = cw_register_function(checksipdomain_func_name, func_check_sipdomain, checksipdomain_func_synopsis, checksipdomain_func_syntax, checksipdomain_func_desc);
    sipbuilddial_function = cw_register_function(sipbuilddial_func_name, func_sipbuilddial, sipbuilddial_func_synopsis, sipbuilddial_func_syntax, sipbuilddial_func_desc);
    sipblacklist_function = cw_register_function(sipblacklist_func_name, func_sipblacklist, sipblacklist_func_synopsis, sipblacklist_func_syntax, sipblacklist_func_desc);

    /* These will be removed soon */
    sipaddheader_app = cw_register_function(sipaddheader_name, sip_addheader, sipaddheader_synopsis, sipaddheader_syntax, sipaddheader_description);
    siposd_app = cw_register_function(siposd_name, sip_osd, siposd_synopsis, siposd_syntax, siposd_description);

    /* Register manager commands */
    cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));

    /* Register all CLI functions for SIP */
    cw_cli_register_multiple(my_clis, arraysize(my_clis));

    return 0;
}

static int unload_module(void)
{
	int res = 0;

	/* First, take us out of the channel type list */
	cw_channel_unregister(&sip_tech);

	res |= cw_unregister_function(sipblacklist_function);
	res |= cw_unregister_function(sipbuilddial_function);
	res |= cw_unregister_function(checksipdomain_function);
	res |= cw_unregister_function(sipchaninfo_function);
	res |= cw_unregister_function(sippeer_function);
	res |= cw_unregister_function(sippeervar_function);
	res |= cw_unregister_function(sipheader_function);

	res |= cw_unregister_function(sipt38switchover_app);
	cw_uninstall_t38_functions();
	res |= cw_unregister_function(dtmfmode_app);
	res |= cw_unregister_function(sipaddheader_app);
	res |= cw_unregister_function(siposd_app);

	cw_cli_unregister_multiple(my_clis, sizeof(my_clis) / sizeof(my_clis[0]));

	cw_udptl_proto_unregister(&sip_udptl);

	cw_rtp_proto_unregister(&sip_rtp);

	cw_manager_action_unregister_multiple(manager_actions, arraysize(manager_actions));

	return res;
}


static void release_module(void)
{
	/* Shut down any existing listeners */
	cw_registry_iterate(&cw_connection_registry, listener_close, NULL);

	if (!pthread_equal(monitor_thread, CW_PTHREADT_NULL)) {
		pthread_cancel(monitor_thread);
		pthread_kill(monitor_thread, SIGURG);
		pthread_join(monitor_thread, NULL);
	}

	/* Free memory for local network address mask */
	cw_acl_free(localaddr);

	sip_destroyall_registry();

	clear_realm_authentication(&authl);
	clear_sip_domains();
	cw_io_context_destroy(io);
	sched_context_destroy(sched);

	pthread_rwlock_destroy(&debugacl.lock);

	cw_registry_destroy(&dialogue_registry);
	cw_registry_destroy(&peerbyname_registry);
	cw_registry_destroy(&peerbyaddr_registry);
	cw_registry_destroy(&userbyname_registry);
}


MODULE_INFO(load_module, reload_module, unload_module, release_module, desc)
