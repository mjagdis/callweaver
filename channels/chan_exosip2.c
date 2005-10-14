/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Joshua Colp.
 *
 * Joshua Colp <jcolp@asterlink.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*
 *
 * eXosip2 Alternate SIP Channel Driver
 * 
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <pthread.h>
#include <malloc.h>
#include <errno.h>
#include <eXosip2/eXosip.h>
#include <ortp/ortp.h>
#include <ortp/telephonyevents.h>
#define OSIP_MT
#include <osip2/osip_mt.h>

/* ortp.h defines stuff... undef 'em */
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "openpbx.h"

#include "openpbx/lock.h"
#include "openpbx/channel.h"
#include "openpbx/config.h"
#include "openpbx/logger.h"
#include "openpbx/module.h"
#include "openpbx/pbx.h"
#include "openpbx/options.h"
#include "openpbx/lock.h"
#include "openpbx/sched.h"
#include "openpbx/io.h"
#include "openpbx/rtp.h"
#include "openpbx/acl.h"
#include "openpbx/callerid.h"
#include "openpbx/file.h"
#include "openpbx/cli.h"
#include "openpbx/app.h"
#include "openpbx/musiconhold.h"
#include "openpbx/manager.h"
#include "openpbx/frame.h"

static const char desc[] = "eXosip2 Channel";
static const char type[] = "SIP2";
static const char tdesc[] = "eXosip2 Channel Driver";

static int usecnt =0;
OPBX_MUTEX_DEFINE_STATIC(usecnt_lock);
OPBX_MUTEX_DEFINE_STATIC(calllock);
OPBX_MUTEX_DEFINE_STATIC(portlock);

#define UA_STRING "OpenPBX/eXosip2_OpenSIP"
#define DEFAULT_CID_NAME "Unknown Name"
#define DEFAULT_CID_NUM "unknown"

/* Various tweaks */
//#define CALL_SETUP_TRICK
//#define EVENT_DEBUG
//#define RTP_NO_RECYCLE

#ifdef CALL_SETUP_TRICK
static int current_calls = 0;
#endif

/* Various threads for events, registrations, subscriptions */
pthread_t event_thread;
pthread_t registrations_thread;
pthread_t subscriptions_tread;

/* Default settings */
static int global_capability = OPBX_FORMAT_ULAW | OPBX_FORMAT_ALAW | OPBX_FORMAT_GSM;

/* RTP stuff (like local port) */
#define MAX_RTP_PORTS 1000
static int rtp_start_port = 10000;
static int rtp_allocations[MAX_RTP_PORTS];

static struct opbx_channel *sip_request(const char *type, int format, void *data, int *cause);
static int sip_digit(struct opbx_channel *ast, char digit);
static int sip_call(struct opbx_channel *ast, char *dest, int timeout);
static int sip_hangup(struct opbx_channel *ast);
static int sip_answer(struct opbx_channel *ast);
static struct opbx_frame *sip_read(struct opbx_channel *ast);
static int sip_write(struct opbx_channel *ast, struct opbx_frame *f);
static int sip_indicate(struct opbx_channel *ast, int condition);
static int sip_fixup(struct opbx_channel *oldchan, struct opbx_channel *newchan);
static int sip_sendhtml(struct opbx_channel *ast, int subclass, const char *data, int datalen);

/* PBX interface structure for channel registration */
static const struct opbx_channel_tech sip_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,
	.requester = sip_request,
	.send_digit = sip_digit,
	.call = sip_call,
	.hangup = sip_hangup,
	.answer = sip_answer,
	.read = sip_read,
	.write = sip_write,
	.exception = sip_read,
	.indicate = sip_indicate,
	.fixup = sip_fixup,
	.send_html = sip_sendhtml,
};

#define MAX_NUMBER_OF_CALLS 1000

struct call {
  /* Unique properties of the call */
  int cid;
  int did;
  int tid;
  char callid[128];
  /* State of the call */
#define NOT_USED 0
#define INUSE 1
  int state;
  /* Type of call */
#define B2BUA 0
  int type;
  /* Direction of the call */
#define DIRECTION_IN 0
#define DIRECTION_OUT 1
  int direction;
  /* RTP stream (both directions) */
  RtpSession *rtp_session;
  char remote_sdp_audio_ip[50];
  int remote_sdp_audio_port;
  char local_sdp_audio_ip[50];
  int local_sdp_audio_port;
  /* OpenPBX related stuff... like master channel */
  struct opbx_channel *owner;
  /* Extension and context */
  char context[OPBX_MAX_CONTEXT];
  char exten[OPBX_MAX_EXTENSION];
  /* Capabilities (codecs/etc) */
  int capability;                        
  int jointcapability;                   
  int peercapability;                    
  int prefcodec;                         
  int noncodeccapability;
  int payload; /* End result negotiated payload */
  int subclass; /* The subclass of above */
  int size;
  int duration;
  struct opbx_codec_pref prefs;
  /* Audio threads and information */
  struct osip_thread *recv_thread;
  int send_timestamp;
  int recv_timestamp;
  int death; /* Killing off the thread? */
  /* DTMF method */
#define DTMF_RFC2833 1
#define DTMF_INFO 2
#define DTMF_INBAND 3
  int dtmf;
  /* Caller ID */
  char cid_num[256];
  char cid_name[256];
  /* State of RTP stream */
#define SENDRECV 1
#define SENDONLY 2
#define RECVONLY 3
  int rtp_state;
};

struct payload {
  int id; /* RTP payload */
  int size; /* Payload size (ulaw=160 gsm=33) */
  int duration; /* How long? Usually 20ms... */
  char name[32]; /* RTP payload name */
  char extra[32]; /* Extra stuff to put in the SDP */
};

typedef struct call call_t;
typedef struct payload payload_t;

static call_t calls[MAX_NUMBER_OF_CALLS];

static void *rtp_int2payload(int payload_internal, payload_t *payload);
void rcv_telephone_event (RtpSession * rtp_session, call_t * ca);
static int rtp_payload2int(int payload, char *payload_name);

#ifdef EVENT_DEBUG
static void log_event(eXosip_event_t *je)
{
  char buf[100];

  buf[0] = '\0';
  if (je->type == EXOSIP_CALL_NOANSWER) {
    snprintf (buf, 99, "<- (%i %i) No answer", je->cid, je->did);
  } else if (je->type == EXOSIP_CALL_CLOSED) {
    snprintf (buf, 99, "<- (%i %i) Call Closed", je->cid, je->did);
  } else if (je->type == EXOSIP_CALL_RELEASED) {
    snprintf (buf, 99, "<- (%i %i) Call released", je->cid, je->did);
  } else if (je->type == EXOSIP_MESSAGE_NEW
	     && je->request!=NULL && MSG_IS_MESSAGE(je->request)) {
    char *tmp = NULL;
    
    if (je->request != NULL) {
      osip_body_t *body;
      osip_from_to_str (je->request->from, &tmp);
      
      osip_message_get_body (je->request, 0, &body);
      if (body != NULL && body->body != NULL) {
	snprintf (buf, 99, "<- (%i) from: %s TEXT: %s",
		  je->tid, tmp, body->body);
      }
      osip_free (tmp);
    } else {
      snprintf (buf, 99, "<- (%i) New event for unknown request?", je->tid);
    }
  } else if (je->type == EXOSIP_MESSAGE_NEW) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (%i) %s from: %s",
	      je->tid, je->request->sip_method, tmp);
    osip_free (tmp);
  } else if (je->type == EXOSIP_MESSAGE_PROCEEDING
             || je->type == EXOSIP_MESSAGE_ANSWERED
             || je->type == EXOSIP_MESSAGE_REDIRECTED
             || je->type == EXOSIP_MESSAGE_REQUESTFAILURE
             || je->type == EXOSIP_MESSAGE_SERVERFAILURE
	     || je->type == EXOSIP_MESSAGE_GLOBALFAILURE) {
    if (je->response != NULL && je->request != NULL) {
      char *tmp = NULL;
      
      osip_to_to_str (je->request->to, &tmp);
      snprintf (buf, 99, "<- (%i) [%i %s for %s] to: %s",
		je->tid, je->response->status_code,
		je->response->reason_phrase, je->request->sip_method, tmp);
      osip_free (tmp);
    } else if (je->request != NULL) {
      snprintf (buf, 99, "<- (%i) Error for %s request",
		je->tid, je->request->sip_method);
    } else {
      snprintf (buf, 99, "<- (%i) Error for unknown request", je->tid);
    }
  } else if (je->response == NULL && je->request != NULL && je->cid > 0) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (%i %i) %s from: %s",
	      je->cid, je->did, je->request->cseq->method, tmp);
    osip_free (tmp);
  } else if (je->response != NULL && je->cid > 0) {
    char *tmp = NULL;
    
    osip_to_to_str (je->request->to, &tmp);
    snprintf (buf, 99, "<- (%i %i) [%i %s] for %s to: %s",
	      je->cid, je->did, je->response->status_code,
	      je->response->reason_phrase, je->request->sip_method, tmp);
    osip_free (tmp);
  } else if (je->response == NULL && je->request != NULL && je->rid > 0) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (%i) %s from: %s",
	      je->rid, je->request->cseq->method, tmp);
    osip_free (tmp);
  } else if (je->response != NULL && je->rid > 0) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (%i) [%i %s] from: %s",
	      je->rid, je->response->status_code,
	      je->response->reason_phrase, tmp);
    osip_free (tmp);
  } else if (je->response == NULL && je->request != NULL && je->sid > 0) {
    char *tmp = NULL;
    char *stat = NULL;
    osip_header_t *sub_state;
    
    osip_message_header_get_byname (je->request, "subscription-state",
				    0, &sub_state);
    if (sub_state != NULL && sub_state->hvalue != NULL)
      stat = sub_state->hvalue;
    
    osip_uri_to_str (je->request->from->url, &tmp);
    snprintf (buf, 99, "<- (%i) [%s] %s from: %s",
	      je->sid, stat, je->request->cseq->method, tmp);
    osip_free (tmp);
  } else if (je->response != NULL && je->sid > 0) {
    char *tmp = NULL;
    
    osip_uri_to_str (je->request->to->url, &tmp);
    snprintf (buf, 99, "<- (%i) [%i %s] from: %s",
	      je->sid, je->response->status_code,
	      je->response->reason_phrase, tmp);
    osip_free (tmp);
  } else if (je->response == NULL && je->request != NULL) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (c=%i|d=%i|s=%i|n=%i) %s from: %s",
	      je->cid, je->did, je->sid, je->nid,
	      je->request->sip_method, tmp);
    osip_free (tmp);
  } else if (je->response != NULL) {
    char *tmp = NULL;
    
    osip_from_to_str (je->request->from, &tmp);
    snprintf (buf, 99, "<- (c=%i|d=%i|s=%i|n=%i) [%i %s] for %s from: %s",
	      je->cid, je->did, je->sid, je->nid,
	      je->response->status_code, je->response->reason_phrase,
	      je->request->sip_method, tmp);
    osip_free (tmp);
  } else {
    snprintf (buf, 99, "<- (c=%i|d=%i|s=%i|n=%i|t=%i) %s",
	      je->cid, je->did, je->sid, je->nid, je->tid, je->textinfo);
  }
  opbx_log(LOG_NOTICE,"%s\n", buf);
  /* Print it out */
}
#endif

/* RTP audio receive stream thread */
static void *rtp_stream_thread(void *_ca)
{
  call_t *call = (call_t *) _ca;
  char data_in[call->size];
  int read = 0, lastread = call->size, have_more = 0;
  struct timeval reference;
  struct opbx_frame f;
  int ms = 0;

  /* For timing purposes */
  gettimeofday(&reference, NULL);

  /* Death of this thread can be triggered by outside, or inside */
  while (call->death != 1) {
    do {
      memset(data_in, 0, sizeof(data_in));
      read = rtp_session_recv_with_ts(call->rtp_session, data_in, call->size, call->recv_timestamp, &have_more);
      /* Make sure we have data read - if not, instant death! */
      if (read < 0) {
	/* An error has occured while attempting to read */
	call->death = 1;
	break;
      } else if (read == 0) {
	/* We need to imitate a packet to keep everything going */
	read = lastread; /* Read in as much as we did last time... of nothing */
      } else {
	lastread = read;
      }
      /* Create a frame and send it off */
      f.subclass = call->subclass;
      f.frametype = OPBX_FRAME_VOICE;
      f.mallocd = 0;
      f.datalen = read;
      f.data = &data_in;
      f.offset = 0;
      f.src = "RTP";
      /* Theoretically the owner could disappear right here too... */
      if (call->rtp_state != SENDONLY)
	opbx_queue_frame(call->owner, &f);
      call->recv_timestamp += 160;
    } while (have_more);
    reference = opbx_tvadd(reference, opbx_tv(0, call->duration * 1000));
    while ((ms = opbx_tvdiff_ms(reference, opbx_tvnow())) > 0) {
      usleep(1);
    }
  }

  /* Announce our destruction */
  opbx_log(LOG_NOTICE, "This thread has died. Goodbye cruel cruel world!\n");
  return NULL;
}

/* Setup the RTP stream for a call */
static void *rtp_setup_stream(call_t *call)
{
  if (call->rtp_session != NULL)
    return NULL;
  call->rtp_session = rtp_session_new(RTP_SESSION_SENDRECV);
  rtp_session_set_scheduling_mode(call->rtp_session, 1); /* yes */
  rtp_session_set_blocking_mode(call->rtp_session, 0);
  rtp_session_set_profile(call->rtp_session, &av_profile);
  rtp_session_set_jitter_compensation(call->rtp_session, 60);
  rtp_session_set_local_addr(call->rtp_session, "0.0.0.0", call->local_sdp_audio_port);
  rtp_session_set_remote_addr(call->rtp_session,
			       call->remote_sdp_audio_ip, call->remote_sdp_audio_port);
  rtp_session_set_payload_type(call->rtp_session, call->payload);
  rtp_session_signal_connect(call->rtp_session, "telephone-event",
			      (RtpCallback) rcv_telephone_event, call);
  /* Just for kicks - setup like uh yeah... adaptive jitterbuffer! */
  rtp_session_enable_adaptive_jitter_compensation(call->rtp_session, 1);
  rtp_session_max_buf_size_set(call->rtp_session, 256);
  /* Now setup a thread to receive RTP media */
  call->recv_thread = osip_thread_create(20000, rtp_stream_thread, call);
  /* See if RFC2833 is supported */
  if (call->dtmf == DTMF_RFC2833 && rtp_session_telephone_events_supported(call->rtp_session) > 0) {
    opbx_log(LOG_NOTICE, "RFC2833 is enabled on this call.\n");
  }
  opbx_log(LOG_NOTICE,"End result on call: %i Port: %d\n", call->payload, call->local_sdp_audio_port);
  return NULL;
}

/* Handle codec elements */
static void *handle_codec_elements(call_t *call, sdp_media_t *remote_med)
{
  int pos = 0, payload = -1, payload_int = -1;
  char *tmp = NULL, *dpayload = NULL, *dname = NULL, *drate = NULL;

  /* Look for static payloads */
  while (!osip_list_eol (remote_med->m_payloads, pos)) {
    tmp = (char *) osip_list_get (remote_med->m_payloads, pos);
    if (tmp != NULL) {
      payload = atoi(tmp);
      if (payload >= 0 && 96 > payload) {
	/* Add to codec capabilities */
	payload_int = rtp_payload2int(payload, NULL);
	if (payload_int > 0)
	  call->peercapability |= payload_int;
      }
      payload = -1;
    }
    tmp = NULL;
    pos++;
  }
  pos = 0;
  /* Look for dynamic payloads */
  while (!osip_list_eol (remote_med->a_attributes, pos)) {
    sdp_attribute_t *at;
    at = (sdp_attribute_t *) osip_list_get (remote_med->a_attributes, pos);
    if (at->a_att_field != NULL && 0 == strcasecmp (at->a_att_field, "rtpmap") && at->a_att_value != NULL) {
      dpayload = strchr(at->a_att_value, ' ');
      *dpayload = '\0';
      dpayload++;
      payload = atoi(at->a_att_value);
      drate = strchr(dpayload, '/');
      *drate = '\0';
      drate++;
      dname = at->a_att_value;
      /* Only support 8000Hz */
      if (strcasecmp(drate,"8000") == 0) {
	/* Convert to internal numerical value and then add to capabilities */
	/* See if this is an RFC2833 element... */
	if (payload == 101 && strcasecmp(dpayload,"telephone-event") == 0) {
	  /* Add to non-codec capabilities... */
	} else {
	  /* Add to codec capabilities... */
	  payload_int = rtp_payload2int(-1, dpayload);
	  if (payload_int > 0)
	    call->peercapability |= payload_int;
	}
      }
    }
    pos++;
  }
  /* Now do the actual negotiation between the two and stuff */
  call->jointcapability = call->capability & call->peercapability;
  /* Finally print out the capabilities */
  const unsigned slen=512;
  char s1[slen], s2[slen], s3[slen];
  opbx_verbose("Capabilities: us - %s, peer - %s, combined - %s\n",
	       opbx_getformatname_multiple(s1, slen, call->capability),
	       opbx_getformatname_multiple(s2, slen, call->peercapability),
	       opbx_getformatname_multiple(s3, slen, call->jointcapability));
  /* Check to make sure we have compatible codecs */
  if (!call->jointcapability) {
    opbx_log(LOG_WARNING, "No compatible codecs!\n");
  }

  return NULL;
}

/* Generate SDP for a message */
static void *message_add_sdp(call_t *call, osip_message_t *message, int maxpayloads)
{
  struct opbx_channel *chan = call->owner;
  char buf[4096] = "", pay[128] = "", rtpmap[1024] = "";
  payload_t payload;
  char tmp[128] = "";
  int i = 0, alreadysent = 0, pref_codec = 0;

  /* Start off with basic SDP */
  snprintf (buf, 4096,
            "v=0\r\n"
            "o=OpenPBX 0 0 IN IP4 %s\r\n"
            "s=conversation\r\n" "c=IN IP4 %s\r\n" "t=0 0\r\n" "m=audio %i RTP/AVP", call->local_sdp_audio_ip, call->local_sdp_audio_ip,
	    call->local_sdp_audio_port);

  /* Move on to payloads */
  if (maxpayloads == -1) {
    /* Okay we've negotiated the codec, we'll use the native format of the owner */
    memset(&payload, 0, sizeof(payload_t));
    rtp_int2payload(chan->nativeformats, &payload);
    if (payload.id != -1) {
      /* We have a good payload value */
      snprintf(pay, sizeof(pay), " %i 101\r\na=rtpmap:%i %s/8000\r\na=rtpmap:101 telephone-event/8000\r\na=fmtp:101 0-15\r\n", payload.id, payload.id, payload.name);
      strcat(buf, pay);
      call->payload = payload.id;
      call->size = payload.size;
      call->duration = payload.duration;
      call->subclass = chan->nativeformats;
    }
  } else {
    memset(&rtpmap, 0, sizeof(rtpmap));
    for (i=0; i<OPBX_FORMAT_MAX_AUDIO; i++) {
      pref_codec = i;
      if (!(call->capability & pref_codec))
	continue;
      if (alreadysent & pref_codec)
	continue;
      memset(&payload, 0, sizeof(payload_t));
      rtp_int2payload(pref_codec, &payload);
      if (payload.id != -1) {
	/* Add it in */
	strcat(pay, " ");
	snprintf(tmp, sizeof(tmp), "%i", payload.id);
	strcat(pay, tmp);
	/* Now add to the rtpmap! */
	strcat(rtpmap, "a=rtpmap:");
	strcat(rtpmap, tmp);
	strcat(rtpmap, " ");
	strcat(rtpmap, payload.name);
	strcat(rtpmap, "/8000\r\n");
      }
      alreadysent |= pref_codec;
    }
    strcat(pay, " 101\r\n");
    /* Add telephone-event */
    strcat(rtpmap, "a=rtpmap:101 telephone-event/8000\r\na=fmtp:101 0-15\r\n");
    /* Now append the rtpmap */
    strcat(pay, rtpmap);
    /* Now append this all... to buf */
    strcat(buf, pay);
    /* No payload/subclass decision yet since this is a full SDP declaration */
    call->payload = -1;
    call->subclass = -1;
    call->size = -1;
    call->duration = -1;
  }

  /* Now that we have the SDP done, add it to the message */
  osip_message_set_body(message, buf, strlen (buf));
  osip_message_set_content_type(message, "application/sdp");

  return NULL;
}

static void *rtp_int2payload(int payload_internal, payload_t *payload)
{
  switch (payload_internal) {
  case OPBX_FORMAT_ULAW:
    payload->id = 0;
    payload->size = 160;
    payload->duration = 20;
    strncpy(payload->name, "PCMU", sizeof(payload->name));
    break;
  case OPBX_FORMAT_GSM:
    payload->id = 3;
    payload->size = 33;
    payload->duration = 20;
    strncpy(payload->name, "GSM", sizeof(payload->name));
    break;
  case OPBX_FORMAT_ALAW:
    payload->id = 8;
    payload->size = 160;
    payload->duration = 20;
    strncpy(payload->name, "PCMA", sizeof(payload->name));
    break;
  case 101:
    payload->id = 101;
    strncpy(payload->name, "telephone-event", sizeof(payload->name));
  default:
    payload->id = -1;
    payload->size = -1;
    payload->duration = -1;
    strncpy(payload->name, "", sizeof(payload->name));
    break;
  }
  return NULL;
}

/* Convert RTP payload ID or name to the internal value */
static int rtp_payload2int(int payload, char *payload_name)
{
  int payload_internal = -1;
  if (payload >= 0) {
    switch (payload) {
    case 0:
      payload_internal = OPBX_FORMAT_ULAW;
      break;
    case 2:
      payload_internal = OPBX_FORMAT_G726;
      break;
    case 3:
      payload_internal = OPBX_FORMAT_GSM;
      break;
    case 4:
      payload_internal = OPBX_FORMAT_G723_1;
      break;
    case 5:
      payload_internal = OPBX_FORMAT_ADPCM;
      break;
    case 6:
      payload_internal = OPBX_FORMAT_ADPCM;
      break;
    case 7:
      payload_internal = OPBX_FORMAT_LPC10;
      break;
    case 8:
      payload_internal = OPBX_FORMAT_ALAW;
      break;
    case 10:
      payload_internal = OPBX_FORMAT_SLINEAR;
      break;
    case 11:
      payload_internal = OPBX_FORMAT_SLINEAR;
      break;
    case 16:
      payload_internal = OPBX_FORMAT_ADPCM;
      break;
    case 17:
      payload_internal = OPBX_FORMAT_ADPCM;
      break;
    case 18:
      payload_internal = OPBX_FORMAT_G729A;
      break;
    default:
      break;
    }
  } else if (payload_name) {
    if (strcasecmp(payload_name, "G723") == 0) {
      payload_internal = OPBX_FORMAT_G723_1;
    } else if (strcasecmp(payload_name, "GSM") == 0) {
      payload_internal = OPBX_FORMAT_GSM;
    } else if (strcasecmp(payload_name, "ULAW") == 0) {
      payload_internal = OPBX_FORMAT_ULAW;
    } else if (strcasecmp(payload_name, "ALAW") == 0) {
      payload_internal = OPBX_FORMAT_ALAW;
    } else if (strcasecmp(payload_name, "G726-32") == 0) {
      payload_internal = OPBX_FORMAT_G726;
    } else if (strcasecmp(payload_name, "DVI4") == 0) {
      payload_internal = OPBX_FORMAT_ADPCM;
    } else if (strcasecmp(payload_name, "L16") == 0) {
      payload_internal = OPBX_FORMAT_SLINEAR;
    } else if (strcasecmp(payload_name, "LPC") == 0) {
      payload_internal = OPBX_FORMAT_LPC10;
    } else if (strcasecmp(payload_name, "G729") == 0) {
      payload_internal = OPBX_FORMAT_G729A;
    } else if (strcasecmp(payload_name, "speex") == 0) {
      payload_internal = OPBX_FORMAT_SPEEX;
    } else if (strcasecmp(payload_name, "iLBC") == 0) {
      payload_internal = OPBX_FORMAT_ILBC;
    }
  }
  return payload_internal;
}

int rtp_allocate_port(void)
{
  int port = 0, end_port = -1;

#ifdef RTP_NO_RECYCLE
  end_port = rtp_start_port;
  rtp_start_port++;
#else
  /* I'm crazy - let's allocate backwards! */

  opbx_mutex_lock(&portlock);
  for (port=MAX_RTP_PORTS; 0<=port; port--) {
    if (rtp_allocations[port] == NOT_USED) {
      end_port = rtp_start_port + port;
      rtp_allocations[port] = INUSE;
      break;
    }
  }
  opbx_mutex_unlock(&portlock);
#endif

  return end_port;
}

/* Find an available callslot and return a pointer to it */
static call_t *find_new_callslot(void)
{
#ifdef CALL_SETUP_TRICK
  int direction = 1;
#endif
  int i = 0;
  call_t *call_slot = NULL;

  opbx_mutex_lock(&calllock);

#ifndef CALL_SETUP_TRICK
  for (i=0; i<MAX_NUMBER_OF_CALLS; i++) {
    if (calls[i].state == NOT_USED) {
      call_slot = &(calls[i]);
      memset(call_slot, 0, sizeof(call_t));
      call_slot->state = INUSE;
      break;
    }
  }
#else
  if (current_calls >= (MAX_NUMBER_OF_CALLS/2)) {
    i = MAX_NUMBER_OF_CALLS-1;
    direction = -1;
  } else {
    i = 0;
    direction = 1;
  }
  while (i >= 0 && MAX_NUMBER_OF_CALLS > i) {
    if (calls[i].state == NOT_USED) {
      call_slot = &(calls[i]);
      memset(call_slot, 0, sizeof(call_t));
      call_slot->state = INUSE;
      current_calls++;
      break;
    }
    i += direction;
  }
#endif

  opbx_mutex_unlock(&calllock);

  return call_slot;
}

/* Destroy everything in a call */
static void destroy_call(call_t *call)
{
  /* Tell the rtp receive thread to die */
  call->death = 1;
  /* Queue a hangup baby */
  if (call->owner)
    opbx_queue_hangup(call->owner);

  if (call->recv_thread) {
    osip_thread_join(call->recv_thread); /* Wait for it to die - just in case */
    osip_free(call->recv_thread); /* And finally free it */
    call->recv_thread = NULL;
  }
  /* Free the rtp port allocation */
  if (call->local_sdp_audio_port != -1) {
    opbx_mutex_lock(&portlock);
    rtp_allocations[(call->local_sdp_audio_port)-(rtp_start_port)] = NOT_USED;
    opbx_mutex_unlock(&portlock);
  }
  /* Destroy the RTP session stuff if present */
  if (call->rtp_session) {
    rtp_session_signal_disconnect_by_callback (call->rtp_session,
					       "telephone-event",
					       (RtpCallback)
					       rcv_telephone_event);
    rtp_session_destroy(call->rtp_session);
    call->rtp_session = NULL;
  }
  opbx_log(LOG_NOTICE,"Destroyed call [cid=%d did=%d]\n", call->cid, call->did);
  /* Finally set state to not used */
  call->state = NOT_USED;
#ifdef CALL_SETUP_TRICK
  current_calls++;
#endif
}

/* Finds a call based on the cid and did */
static call_t *find_call(int cid, int did)
{
  int i = 0;
  call_t *call = NULL;
  opbx_mutex_lock(&calllock);
  for (i=0; i<MAX_NUMBER_OF_CALLS;i++) {
    /* CID = Call ID DID = Dialog ID */
    if (calls[i].state != NOT_USED && calls[i].cid == cid) {
      call = &(calls[i]);
      if (call->did == -1)
	call->did = did; /* Inherit the dialog ID from this request since not already present */
      break;
    }
  }
  opbx_mutex_unlock(&calllock);
  return call;
}

/* Destroy a call by the event */
static int destroy_call_by_event(eXosip_event_t *event)
{
  call_t *call = NULL;

  call = find_call(event->cid, event->did);

  /* We have the call.. get rid of everything */
  if (call)
    destroy_call(call);

  return 0;
}

/* Create a new OpenPBX channel structure */
static struct opbx_channel *sip_new(call_t *call, int state)
{
  int randnum = rand() & 0xffff;
  int fmt = 0;
  struct opbx_channel *chan = NULL;

  chan = opbx_channel_alloc(1);
  if (!chan) {
    opbx_log(LOG_ERROR, "Unable to allocate OpenPBX channel structure\n");
    return NULL;
  }
  /* Create a channel name */
  snprintf(chan->name, sizeof(chan->name), "SIP2/%s@%s-%04x,1", call->exten, call->context, randnum);
  chan->tech = &sip_tech;
  chan->type = type;
  /* Setup the codecs */
  if (call->jointcapability)
    chan->nativeformats = opbx_codec_choose(&call->prefs, call->jointcapability, 1);
  else
    chan->nativeformats = opbx_codec_choose(&call->prefs, call->capability, 1);
  /* Set read and write formats */
  fmt = opbx_best_codec(chan->nativeformats);
  chan->writeformat = fmt;
  chan->rawwriteformat = fmt;
  chan->readformat = fmt;
  chan->rawreadformat = fmt;
  /* Set initial state to ringing */
  opbx_setstate(chan, state);
  chan->tech_pvt = call;
  /* Copy over callerid from the call */
  chan->cid.cid_num = strdup(call->cid_num);
  chan->cid.cid_name = strdup(call->cid_name);
  call->owner = chan;
  opbx_mutex_lock(&usecnt_lock);
  usecnt++;
  usecnt++;
  opbx_mutex_unlock(&usecnt_lock);
  opbx_update_use_count();
  opbx_copy_string(chan->context, call->context, sizeof(chan->context));
  opbx_copy_string(chan->exten, call->exten, sizeof(chan->exten));
  chan->priority = 1;
  return chan;
}

/* Setup a new call */
static int new_call_by_event(eXosip_event_t *event)
{
  char localip[128] = "";
  int i = 0;
  call_t *call = NULL;
  sdp_message_t *remote_sdp = NULL;
  sdp_connection_t *conn = NULL;
  sdp_media_t *remote_med = NULL;
  osip_message_t *rejection = NULL;
  struct opbx_channel *chan = NULL;

  /* Make sure we have enough information */
  if (event->did < 1 && event->cid < 1) {
    opbx_log(LOG_ERROR, "Not enough information to setup the call.\n");
    return -1;
  }

  call = find_new_callslot();

  if (call) {
    /* We have a call structure of our very own!!! */
    call->cid = event->cid;
    call->did = event->did;
    call->tid = event->tid;
    if (event->request != NULL && event->request->call_id != NULL && event->request->call_id->number != NULL && event->request->call_id->host != NULL)
      snprintf(call->callid, sizeof(call->callid), "%s@%s", event->request->call_id->number, event->request->call_id->host);
    call->direction = DIRECTION_IN;
    call->send_timestamp = 0; /* Start sending timestamp at 0! */
    call->recv_timestamp = 0; /* Ditto */
    call->dtmf = DTMF_RFC2833;
    call->rtp_state = SENDRECV;
    strncpy(call->context, "default", sizeof(call->context));
    /* Setup the capabilities */
    call->capability = global_capability;
    /* Get the number dialed */
    if (event->request != NULL) {
      /* Parse out callerid elements */
      if (event->request->from) {
	if (event->request->from->displayname) /* Get the display name */
	  strncpy(call->cid_name, event->request->from->displayname, sizeof(call->cid_name));
	if (event->request->from->url && event->request->from->url->username) /* Get the number */
	  strncpy(call->cid_num, event->request->from->url->username, sizeof(call->cid_num));
      }
      /* Get the Request URI... it'll contain the extension */
      if (event->request->req_uri->username) {
	strncpy(call->exten, event->request->req_uri->username, sizeof(call->exten));
      } else {
	strncpy(call->exten, "s", sizeof(call->exten));
      }
      opbx_log(LOG_NOTICE, "Request is for %s@%s\n", call->exten, call->context);
      /* Make sure the above exists... if not - send back a 404 */
      if (!opbx_exists_extension(NULL, call->context, call->exten, 1, NULL)) {
	opbx_log(LOG_NOTICE, "No such extension/context %s@%s\n", call->exten, call->context);
	/* Send back a 404 and finally die a horrible death */
	eXosip_lock();
	eXosip_call_build_answer(call->tid, 404, &rejection);
	eXosip_call_send_answer(call->tid, 404, rejection);
	eXosip_unlock();
	call->state = NOT_USED;
	return -1;
      }
      /* Get the SDP from the Invite */
      remote_sdp = eXosip_get_sdp_info(event->request);
    }
    /* Setup the RTP stream if applicable */
    if (remote_sdp) {
      /* Now head on to media IP/port stuff */
      conn = eXosip_get_audio_connection(remote_sdp);
      remote_med = eXosip_get_audio_media(remote_sdp);
      if (conn != NULL && conn->c_addr != NULL) {
	snprintf (call->remote_sdp_audio_ip, 50, conn->c_addr);
      }
      if (remote_med == NULL || remote_med->m_port == NULL) {
	/* no audio media proposed */
	eXosip_call_send_answer (call->tid, 415, NULL);
	sdp_message_free(remote_sdp);
	return 0;
      }
      call->remote_sdp_audio_port = atoi(remote_med->m_port);
      opbx_log(LOG_NOTICE, "Remote media is at %s:%i\n", call->remote_sdp_audio_ip, call->remote_sdp_audio_port);
      /* Handle the codec stuff */
      handle_codec_elements(call, remote_med);
      /* If NAT helping is enabled - see if they match the originating IP address... if not - use the originating */
      eXosip_guess_localip (AF_INET, localip, 128);
      strncpy(call->local_sdp_audio_ip, localip, sizeof(call->local_sdp_audio_ip));
      call->local_sdp_audio_port = rtp_allocate_port();
      opbx_log(LOG_NOTICE, "Local media is at %s:%i\n", call->local_sdp_audio_ip, call->local_sdp_audio_port);
      sdp_message_free(remote_sdp);
    } else {
      opbx_log(LOG_NOTICE, "No SDP in initial invite!\n");
    }
    opbx_log(LOG_NOTICE, "Setup call %d\n", i);
    /* Create an actual channel and fire up the PBX core */
    chan = sip_new(call, OPBX_STATE_RING);
    /* Launch the PBX */
    opbx_pbx_start(chan);
  } else {
    opbx_log(LOG_ERROR, "No more call structures available!\n");
    return -1;
  }

  return 0;
}

static void *handle_ringing(eXosip_event_t *event)
{
  call_t *call = NULL;

  call = find_call(event->cid, event->did);
  if (call == NULL || call->owner == NULL)
    return NULL;

  opbx_queue_control(call->owner, OPBX_CONTROL_RINGING);
  if (call->owner->_state != OPBX_STATE_UP)
    opbx_setstate(call->owner, OPBX_STATE_RINGING);

  return NULL;
}

static void *handle_call_transfer(eXosip_event_t *event)
{
  struct opbx_channel *bridged = NULL;
  call_t *call = NULL, *consult_call = NULL;
  osip_header_t *header = NULL;
  osip_from_t *refer_to = NULL, *referred_by = NULL;
  osip_uri_header_t *uri_header = NULL;
  osip_message_t *answer = NULL, *notify = NULL;
  int pos = 0, i = 0, accept = 0;
  char *bleh = NULL, replace_callid[128] = "";
  char exten[OPBX_MAX_EXTENSION], host[256] = "";

  call = find_call(event->cid, event->did);
  if (call == NULL || call->owner == NULL)
    return NULL;

  bridged = opbx_bridged_channel(call->owner);
  if (bridged == NULL) {
    /* Can't transfer this! */
    eXosip_lock();
    eXosip_call_build_answer(call->tid, 400, &answer);
    eXosip_call_send_answer(call->tid, 400, answer);
    eXosip_unlock();
    call->tid++;
    opbx_queue_hangup(call->owner);
    return NULL;
  }

  /* Parse out required information */
  if (event->request != NULL) {
    osip_message_header_get_byname(event->request, "Refer-To", 0, &header);
    if (header) {
      /* We've got a Refer-To... */
      osip_from_init(&refer_to);
      osip_from_parse(refer_to, header->hvalue);
      header = NULL;
      /* Okay now parse it out to see the extension they want to transfer to, or the call to transfer to (for supervised transfers) */
      strncpy(exten, refer_to->url->username, sizeof(exten));
      strncpy(host, refer_to->url->host, sizeof(host));
      /* See if there's any extra arguments (like a Replaces) */
      if (refer_to->url->url_headers != NULL) {
	while (!osip_list_eol(refer_to->url->url_headers, pos)) {
	  uri_header = (osip_uri_header_t *)osip_list_get(refer_to->url->url_headers, pos);
	  if (uri_header != NULL && uri_header->gname != NULL && uri_header->gvalue != NULL) {
	    if (!strcasecmp(uri_header->gname, "Replaces")) {
	      /* Hey look it's a Replaces header! */
	      bleh = strchr(uri_header->gvalue, ';');
	      if (bleh) {
		*bleh = '\0';
		bleh++;
	      }
	      strncpy(replace_callid, uri_header->gvalue, sizeof(replace_callid));
	    }
	  }
	  pos++;
	}
      }
    }
    osip_message_header_get_byname(event->request, "Referred-By", 0, &header);
    if (header) {
      /* We've got a Referred-By... */
      osip_from_init(&referred_by);
      osip_from_parse(referred_by, header->hvalue);
      header = NULL;
      opbx_log(LOG_NOTICE, "Got a Referred-By header!!!\n");
    }
  }

  /* OKAY - if we have a Replaces callid... find it's channel - if it's not found and we still have it... initiate a new INVITE with the right info */
  if (strlen(replace_callid) > 0) {
    for (i=0; i<MAX_NUMBER_OF_CALLS; i++) {
      if (calls[i].state != NOT_USED && strcasecmp(calls[i].callid, replace_callid) == 0) {
	consult_call = &(calls[i]);
	break;
      }
    }
    if (consult_call) {
      /* This is a supervised call to one that's on this box... */
      opbx_log(LOG_NOTICE, "Okay we want to bridge %s to %s\n", bridged->name, consult_call->owner->name);
      /* First let's get rid of the musiconhold on this channel */
      opbx_moh_stop(call->owner);
      opbx_moh_stop(consult_call->owner);
      accept = 1;
    } else {
      /* Okay here's the situation - we have to transfer this call... elsewhere */
    }
  } else {
    /* Do a blind transfer if the extension exists */
    if (opbx_exists_extension(NULL, call->context, exten, 1, NULL)) {
      opbx_async_goto(bridged, call->context, exten, 1);
      accept = 1; /* Call transfer is doing just fine... */
    }
  }

  /* Clean Up */
  if (refer_to)
    osip_free(refer_to);
  if (referred_by)
    osip_free(referred_by);

  /* Return the status of the transfer - accepted or not. */
  eXosip_lock();
  if (accept == 1) {
    /* Send 202 Accepted */
    eXosip_call_build_answer(call->tid, 202, &answer);
    eXosip_call_send_answer(call->tid, 202, answer);
    /* Send a fragment as well */
    eXosip_call_build_notify(call->did, EXOSIP_SUBCRSTATE_ACTIVE, &notify);
    osip_message_set_header(notify, "Event", "refer");
    osip_message_set_content_type(notify, "message/sipfrag");
    osip_message_set_body(notify, "SIP/2.0 100 Trying", strlen("SIP/2.0 100 Trying"));
    eXosip_call_send_request(call->did, notify);
  } else {
    /* Transfer failed for whatever reason */
    eXosip_call_build_answer(call->tid, 400, &answer);
    eXosip_call_send_answer(call->tid, 400, answer);
  }
  eXosip_unlock();
  call->tid++;

  return NULL;
}

static void *handle_reinvite(eXosip_event_t *event)
{
  osip_message_t *answer = NULL;
  call_t *call = NULL;
  sdp_message_t *remote_sdp = NULL;
  sdp_connection_t *conn = NULL;
  sdp_media_t *remote_med = NULL;
  sdp_attribute_t *at = NULL;
  int i = 0, remote_port = 0, state = -1, pos = 0;
  char remote_ip[50] = "";

  call = find_call(event->cid, event->did);
  if (call == NULL || call->owner == NULL)
    return NULL;

  /* Handle the reinvite - see if it's for putting someone on hold, if not - modify the RTP session */
  remote_sdp = eXosip_get_sdp_info(event->request);
  if (remote_sdp == NULL) {
    /* Casually send back an error */
    return NULL;
  }
  /* Connection and remote media information */
  conn = eXosip_get_audio_connection(remote_sdp);
  remote_med = eXosip_get_audio_media(remote_sdp);

  if (conn != NULL && conn->c_addr != NULL) {
    snprintf (remote_ip, 50, conn->c_addr);
  }
  remote_port = atoi(remote_med->m_port);

  /* Grab the attributes of the SDP if applicable */
  while (!osip_list_eol (remote_med->a_attributes, pos)) {
    at = (sdp_attribute_t *) osip_list_get (remote_med->a_attributes, pos);
    if (at->a_att_field != NULL) {
      if (strcasecmp(at->a_att_field, "sendonly") == 0)
	state = SENDONLY;
      else if (strcasecmp(at->a_att_field, "recvonly") == 0)
	state = RECVONLY;
      else if (strcasecmp(at->a_att_field, "sendrecv") == 0)
	state = SENDRECV;
    }
    pos++;
  }

  /* Check to see if this is an on-hold notification... or off-hold notification */
  if (strcasecmp(remote_ip,"0.0.0.0") == 0 || state == SENDONLY) {
    /* Call is on hold! */
    opbx_moh_start(call->owner, NULL);
    call->rtp_state = SENDONLY;
  } else if (strcasecmp(remote_ip,call->remote_sdp_audio_ip) == 0 && call->rtp_state == SENDONLY) {
    opbx_moh_stop(call->owner);
    call->rtp_state = SENDRECV;
  } else {
    /* Suspend the RTP session, change the remote address, and then switch it back to active */
  }

  eXosip_lock();
  i = eXosip_call_build_answer(call->tid, 200, &answer);
  if (answer) {
    /* Add SDP data */
    message_add_sdp(call, answer, -1);
    /* Finally send it out */
    eXosip_call_send_answer(call->tid, 200, answer);
  }
  eXosip_unlock();

  /* Increment the transaction ID */
  call->tid++;

  return NULL;
}

static void *handle_answer(eXosip_event_t *event)
{
  call_t *call = NULL;
  osip_message_t *ack = NULL;
  sdp_message_t *remote_sdp = NULL;
  sdp_connection_t *conn = NULL;
  sdp_media_t *remote_med = NULL;
  payload_t payload;

  call = find_call(event->cid, event->did);
  if (call == NULL || call->owner == NULL)
    return NULL;

  /* Lock first */
  eXosip_lock();

  eXosip_call_build_ack(call->did, &ack);

  /* We need to generate an ACK... and if no RTP stuff is setup yet, do that too */
  if (event->response != NULL && (call->rtp_session == NULL || call->local_sdp_audio_port == 0 || call->remote_sdp_audio_port == 0)) {
    remote_sdp = eXosip_get_sdp_info(event->response);
    if (remote_sdp) {
      /* Set up our own RTP stuff */
      conn = eXosip_get_audio_connection(remote_sdp);
      remote_med = eXosip_get_audio_media(remote_sdp);
      if (conn != NULL && conn->c_addr != NULL) {
        snprintf (call->remote_sdp_audio_ip, 50, conn->c_addr);
      }
      call->remote_sdp_audio_port = atoi(remote_med->m_port);
      /* Now figure out the codec to use and such */
      handle_codec_elements(call, remote_med);
      /* See if we need to switch our channel around at all */
      if (!(call->owner->nativeformats & call->jointcapability)) {
	const unsigned slen=512;
	char s1[slen], s2[slen];
	opbx_log(LOG_DEBUG, "Oooh, we need to change our formats since our peer supports only %s and not %s\n",
		 opbx_getformatname_multiple(s1, slen, call->jointcapability),
		 opbx_getformatname_multiple(s2, slen, call->owner->nativeformats));
	call->owner->nativeformats = opbx_codec_choose(&call->prefs, call->jointcapability, 1);
	opbx_set_read_format(call->owner, call->owner->readformat);
	opbx_set_write_format(call->owner, call->owner->writeformat);
      }
      /* Change the payload and subclass */
      call->subclass = call->owner->nativeformats;
      rtp_int2payload(call->subclass, &payload);
      call->payload = payload.id;
      call->size = payload.size;
      call->duration = payload.duration;
      opbx_log(LOG_NOTICE, "Remote media is at %s:%i\n", call->remote_sdp_audio_ip, call->remote_sdp_audio_port);
      opbx_log(LOG_NOTICE, "Local media is at %s:%i\n", call->local_sdp_audio_ip, call->local_sdp_audio_port);
      /* Start up the RTP stream */
      rtp_setup_stream(call);
      sdp_message_free(remote_sdp);
    }
  }

  eXosip_call_send_ack(call->did, ack);

  /* Voila done - unlock */
  eXosip_unlock();

  /* Now make our presence known on the channel */
  opbx_queue_control(call->owner, OPBX_CONTROL_ANSWER);

  return NULL;
}

/* Event Thread */
static void *event_proc(void *arg)
{
  eXosip_event_t *event = NULL;

  for (;;) {
    if (!(event = eXosip_event_wait (0, 0))) {
      usleep (10000);
      continue;
    }
    eXosip_lock();
    eXosip_automatic_action ();
    eXosip_unlock();
#ifdef EVENT_DEBUG
    log_event(event);
#endif
    switch(event->type) {
      /* Call related stuff */
    case EXOSIP_CALL_INVITE:
      if (new_call_by_event(event))
	opbx_log(LOG_ERROR, "Unable to set us up the call ;(\n");
      break;
    case EXOSIP_CALL_REINVITE:
      /* See what the reinvite is about - on hold or whatever */
      handle_reinvite(event);
      opbx_log(LOG_NOTICE, "Got a reinvite.\n");
      break;
    case EXOSIP_CALL_MESSAGE_NEW:
      if (event->request != NULL && MSG_IS_REFER(event->request)) {
	handle_call_transfer(event);
      }
      break;
    case EXOSIP_CALL_ACK:
      /* If audio is not flowing and this has SDP - fire it up! */
      break;
    case EXOSIP_CALL_ANSWERED:
      handle_answer(event);
      break;
    case EXOSIP_CALL_PROCEEDING:
      /* This is like a 100 Trying... yeah */
      break;
    case EXOSIP_CALL_RINGING:
      handle_ringing(event);
      break;
    case EXOSIP_CALL_REDIRECTED:
      opbx_log(LOG_NOTICE, "Call was redirect\n");
      break;
    case EXOSIP_CALL_CLOSED:
      destroy_call_by_event(event);
      break;
    case EXOSIP_CALL_RELEASED:
      destroy_call_by_event(event);
      break;
    case EXOSIP_CALL_NOANSWER:
      opbx_log(LOG_NOTICE, "The call was not answered.\n");
      destroy_call_by_event(event);
      break;
    case EXOSIP_CALL_REQUESTFAILURE:
      opbx_log(LOG_NOTICE, "Request failure\n");
      destroy_call_by_event(event);
      break;
    case EXOSIP_CALL_SERVERFAILURE:
      opbx_log(LOG_NOTICE, "Server failure\n");
      destroy_call_by_event(event);
      break;
    case EXOSIP_CALL_GLOBALFAILURE:
      opbx_log(LOG_NOTICE, "Global failure\n");
      break;
      /* Registration related stuff */
    case EXOSIP_REGISTRATION_NEW:
      opbx_log(LOG_NOTICE, "Received registration attempt\n");
      break;
    default:
      /* Unknown event... casually absorb it for now */
      break;
    }
    eXosip_event_free (event);
  }
  return NULL;
}

void rcv_telephone_event (RtpSession * rtp_session, call_t * ca)
{
  struct opbx_frame f;
  telephone_event_t *tev = NULL;
  call_t *call = NULL;
  char digit = ' ';
  int i = 0;

  /* Find the real call... */
  for (i=0; i<MAX_NUMBER_OF_CALLS; i++) {
    if (calls[i].state != NOT_USED && calls[i].rtp_session == rtp_session)
      call = &(calls[i]);
    break;
  }

  /* Queue up the digit on the channel */
  if (call != NULL && call->owner != NULL && rtp_session->current_tev != NULL) {
    /* There are telephone events waiting */
    rtp_session_read_telephone_event(rtp_session, rtp_session->current_tev, &tev);
    if (tev != NULL) {
      switch (tev->event) {
      case 0:
	digit = '0';
	break;
      case 1:
	digit = '1';
	break;
      case 2:
	digit = '2';
        break;
      case 3:
	digit = '3';
        break;
      case 4:
	digit = '4';
        break;
      case 5:
	digit = '5';
	break;
      case 6:
	digit = '6';
	break;
      case 7:
	digit = '7';
	break;
      case 8:
	digit = '8';
	break;
      case 9:
	digit = '9';
	break;
      case 10:
	digit = '*';
	break;
      case 11:
	digit = '#';
	break;
      }
      memset(&f, 0, sizeof(f));
      /* Create the frame and then queue it up */
      f.frametype = OPBX_FRAME_DTMF;
      f.subclass = digit; /* Actual digit */
      f.src = "RTP";
      f.mallocd = 0;
      /* Now send it to the owner */
      opbx_queue_frame(call->owner, &f);
      opbx_log(LOG_NOTICE, "Received digit %d and queued on %s\n", tev->event, call->owner->name);
    }
  }
}

static int sip_answer(struct opbx_channel *ast)
{
  osip_message_t *answer = NULL;
  call_t *call = ast->tech_pvt;
  int res = -1;
  
  if (ast->_state != OPBX_STATE_UP) {
    /* We have everything we need so answer it! */
    if (call->rtp_session == NULL && call->direction == DIRECTION_IN) {
      /* Finally send the 200 OK acknowledging the call was answered */
      eXosip_lock();
      eXosip_call_build_answer(call->tid, 200, &answer);
      /* Send a 200 OK with SDP based on negotiated codec ahead of time */
      message_add_sdp(call, answer, -1);
      /* Queue the message up for sending out */
      eXosip_call_send_answer (call->tid, 200, answer);
      opbx_log(LOG_NOTICE, "Answered the call!\n");
      eXosip_unlock();
      call->tid++;
      /* No RTP stream is setup - we need to establish it - otherwise do nothing */
      rtp_setup_stream(call);
    } else if (call->rtp_session == NULL && call->direction == DIRECTION_OUT) {
      opbx_log(LOG_NOTICE,"Going... out\n");
    }
    /* Set state to up now */
    opbx_setstate(ast, OPBX_STATE_UP);
    res = 0;
  } 
  
  return res;
}

static struct opbx_frame  *sip_read(struct opbx_channel *ast)
{
  static struct opbx_frame null = { OPBX_FRAME_NULL, };
  return &null;
}

static int sip_write(struct opbx_channel *ast, struct opbx_frame *f)
{
  osip_message_t *answer = NULL;
  call_t *call = ast->tech_pvt;
  int res = 0, sent = 0;
  
  /* Write to the RTP session if allocated - if not... yeah... 183 Session Progress! */
  if (call->rtp_session == NULL && call->direction == DIRECTION_IN) {
    /* Setup the RTP stream */
    rtp_setup_stream(call);
    eXosip_lock();
    eXosip_call_build_answer(call->tid, 183, &answer);
    osip_message_set_supported(answer, "100rel");
    osip_message_set_supported(answer, "replaces");
    osip_message_set_header(answer, "RSeq", "1");
    message_add_sdp(call, answer, -1);
    eXosip_call_send_answer (call->tid, 183, answer);
    eXosip_unlock();
    call->tid++;
  } else if (call->rtp_session == NULL && call->direction == DIRECTION_OUT) {
    /* Yeah... - trying to write a frame ahead of time? */
    return res;
  }

  sent = rtp_session_send_with_ts(call->rtp_session, f->data, f->datalen, call->send_timestamp);
  call->send_timestamp += 160;
  
  return res;
}

static int sip_fixup(struct opbx_channel *oldchan, struct opbx_channel *newchan)
{
  //  call_t *call = newchan->tech_pvt;

  return 0;
}

static int sip_indicate(struct opbx_channel *ast, int condition)
{
  osip_message_t *answer = NULL;
  call_t *call = ast->tech_pvt;
  int res = -1;

  switch (condition) {
  case OPBX_CONTROL_RINGING:
    if (ast->_state != OPBX_STATE_UP && call->rtp_session == NULL) {
      /* Send it out of band */
      eXosip_call_send_answer(call->tid, 180, NULL);
      res = 0;
    }
    break;
  case OPBX_CONTROL_BUSY:
    if (ast->_state != OPBX_STATE_UP && call->rtp_session == NULL) {
      eXosip_call_send_answer(call->tid, 486, NULL);
      res = 0;
    }
    break;
  case OPBX_CONTROL_CONGESTION:
    if (ast->_state != OPBX_STATE_UP && call->rtp_session == NULL) {
      eXosip_call_send_answer(call->tid, 503, NULL);
      res = 0;
    }
    break;
  case OPBX_CONTROL_PROGRESS:
  case OPBX_CONTROL_PROCEEDING:
    if (ast->_state != OPBX_STATE_UP && call->rtp_session == NULL) {
      /* This is special - we need to send a 183 Session Progress with SDP */
      opbx_log(LOG_NOTICE, "Need to send 183 with SDP\n");
      eXosip_lock();
      eXosip_call_build_answer(call->tid, 183, &answer);
      message_add_sdp(call, answer, -1);
      eXosip_call_send_answer (call->tid, 183, answer);
      eXosip_unlock();
      res = 0;
    }
    break;
  case -1:
    /* This is essentially a prod to the channel */
    break;
  default:
    opbx_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
    break;
  }
  
  /* If we are doing inband... make sure RTP is up */
  if (res && call->rtp_session == NULL) {
    rtp_setup_stream(call);
    eXosip_lock();
    eXosip_call_build_answer(call->tid, 183, &answer);
    osip_message_set_supported(answer, "100rel");
    osip_message_set_supported(answer, "replaces");
    osip_message_set_header(answer, "RSeq", "1");
    message_add_sdp(call, answer, -1);
    eXosip_call_send_answer (call->tid, 183, answer);
    eXosip_unlock();
  } else if (res == 0) {
    /* Increment the transaction ID */
    call->tid++;
  }

  return res;
}

static int sip_digit(struct opbx_channel *ast, char digit)
{
  call_t *call = ast->tech_pvt;
  int res = -1;
  
  /* Check DTMF type for methods of sending */
  if (call->dtmf == DTMF_RFC2833 && call->rtp_session != NULL) {
    /* If the RTP session is setup, send it out */
    rtp_session_send_dtmf(call->rtp_session, digit, call->send_timestamp);
    call->send_timestamp += 160;
  } else if (call->dtmf == DTMF_INFO) {
    /* Create an INFO message and send it out */
  } else if (call->dtmf == DTMF_INBAND) {
    /* Send the digit as audio on the owner channel */
  }

  return res;
}

static int sip_sendhtml(struct opbx_channel *ast, int subclass, const char *data, int datalen)
{
  //call_t *call = ast->tech_pvt;
  int res = -1;
  
  return res;
}

/*--- sip_call: Initiate new call, part of PBX interface */
/* 	dest is the dial string */
static int sip_call(struct opbx_channel *ast, char *dest, int timeout)
{
  int i = 0;
  call_t *call = ast->tech_pvt;
  char *exten = NULL, *address = NULL;
  char sip_uri[512] = "", localip[128] = "", from_uri[512] = "";
  osip_message_t *invite;

  if (strncasecmp(dest,"sip:",4) == 0)
    strncpy(sip_uri, dest, sizeof(sip_uri));
  else {
    /* Parse it out and generate the URI */
    exten = dest;
    address = strchr(exten,'@');
    if (address) {
      *address = '\0';
      address++;
    }
    snprintf(sip_uri, sizeof(sip_uri), "sip:%s@%s", exten, address);
  }

  opbx_log(LOG_NOTICE,"SIP URI is %s\n", sip_uri);

  /* Setup the local RTP IP and port */
  eXosip_guess_localip (AF_INET, localip, 128);
  strncpy(call->local_sdp_audio_ip, localip, sizeof(call->local_sdp_audio_ip));
  call->local_sdp_audio_port = rtp_allocate_port();

  /* Use the callerid on the channel to build the From header - if none is present... use the default */
  if (ast->cid.cid_name == NULL)
    ast->cid.cid_name = strdup(DEFAULT_CID_NAME);
  if (ast->cid.cid_num == NULL)
    ast->cid.cid_num = strdup(DEFAULT_CID_NUM);
  /* Right now we only support setting it in the From URI - future is RPID */
  snprintf(from_uri, sizeof(from_uri), "\"%s\" <sip:%s@%s>", ast->cid.cid_name, ast->cid.cid_num, localip);
  
  /* Fire up the initial invite */
  eXosip_call_build_initial_invite(&invite, sip_uri, from_uri, NULL, NULL);
  /* Say we support 100rel */
  osip_message_set_supported(invite, "100rel");
  /* Add in all the SDP */
  message_add_sdp(call, invite, 1);
  /* Send it! */
  eXosip_lock();
  i = eXosip_call_send_initial_invite(invite);
  if (i > 0)
    call->cid = i; /* Call dialog identity */
  call->did = -1;
  call->dtmf = DTMF_RFC2833;
  call->rtp_state = SENDRECV;
  eXosip_unlock();

  return 0;
}

/*--- sip_hangup: Hangup a call through the sip proxy channel */
static int sip_hangup(struct opbx_channel *ast)
{
  call_t *call = ast->tech_pvt;
  
  if (call && call->state != NOT_USED && call->death != 1) {
    eXosip_call_terminate(call->cid, call->did);
    destroy_call(call);
  }

  ast->tech_pvt = NULL;
  opbx_mutex_lock(&usecnt_lock);
  usecnt--;
  opbx_mutex_unlock(&usecnt_lock);
  
  return 0;
}

/*--- sip_request: Part of PBX interface */
static struct opbx_channel *sip_request(const char *type, int format, void *data, int *cause)
{
  call_t *call = NULL;
  struct opbx_channel *chan = NULL;

  /* Find a free callslot... and do our thang */
  call = find_new_callslot();

  if (call == NULL)
    return NULL;

  /* Inherit call properties */
  call->capability = global_capability;
  call->direction = DIRECTION_OUT;
  call->send_timestamp = 0; /* Start sending timestamp at 0! */
  call->recv_timestamp = 0; /* Ditto */

  chan = sip_new(call, OPBX_STATE_DOWN);

  return chan;
}

/*--- load_module: Load module into PBX, register channel */
int load_module()
{
  int port = 5060;
  int i = 0;
  /* Setup the RTP stream stuff */
  ortp_init();
  ortp_scheduler_init();
  /* Reset RTP port allocations */
  for (i=0; i<MAX_RTP_PORTS; i++)
    rtp_allocations[i] = NOT_USED;
  /* Adjust the av_profile with extra codecs */
  rtp_profile_set_payload(&av_profile,0,&pcmu8000);
  rtp_profile_set_payload(&av_profile,3,&gsm);
  rtp_profile_set_payload(&av_profile,8,&pcma8000);
  rtp_profile_set_payload(&av_profile,101,&telephone_event);
  /* Setup our exosip stuff */
  if (eXosip_init ()) {
    opbx_log(LOG_ERROR, "eXosip_init initialization failed!\n");
    return -1;
  }
  if (eXosip_listen_addr (IPPROTO_UDP, NULL, port, AF_INET, 0)) {
    opbx_log(LOG_ERROR, "eXosip_listen_addr failed!\n");
    return -1;
  }
  /* Setup the user agent */
  eXosip_set_user_agent(UA_STRING);
  /* Spawn our thread to handle events */
  if (pthread_create(&event_thread, NULL, event_proc, NULL)) {
    opbx_log(LOG_ERROR, "Failed to launch event thread!\n");
    return -1;
  }
  /* Make sure we can register our channel type */
  if (opbx_channel_register(&sip_tech)) {
    opbx_log(LOG_ERROR, "Unable to register channel class %s\n", type);
    return -1;
  }
  return 0;
}

/*--- reload: Reload module */
int reload()
{
  return 0;
}

/*--- unload_module: Unload the sip proxy channel from OpenPBX */
int unload_module()
{
  int i = 0;

  /* First, take us out of the channel loop */
  opbx_channel_unregister(&sip_tech);
  if (!opbx_mutex_lock(&calllock)) {
    for (i=0; i<MAX_NUMBER_OF_CALLS; i++) {
      /* If in use and has an owner - hang them up */
      if (calls[i].state != NOT_USED)
	destroy_call(&(calls[i]));
    }
    opbx_mutex_unlock(&calllock);
  } else {
    opbx_log(LOG_WARNING, "Unable to lock the calls\n");
    return -1;
  }		
  return 0;
}

int usecount()
{
  return usecnt;
}

char *description()
{
	return (char *) desc;
}

