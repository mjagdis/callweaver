/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, 2007, Steve Underwood based on code which is
 * Copyright (C) 2004 - 2005, Adrian Kennard, rights assigned to Digium
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
 * \brief SMS application - ETSI ES 201 912 protocol 1 and 2 implementation
 */

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <spandsp.h>

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callerid.h"

#include "dll_sms.h"

/* ToDo */
/* Add full VP support */
/* Handle status report messages (generation and reception) */
/* Time zones on time stamps */
/* User ref field */

static volatile uint8_t message_ref;    /* arbitary message ref */
static volatile unsigned int seq;       /* arbitrary message sequence number for unqiue files */

static char log_file[255];
static char spool_dir[255];

static void *sms_app;
static const char sms_name[] = "SMS";
static const char sms_synopsis[] = "Communicates with SMS service centres and SMS capable analogue phones";
static const char sms_syntax[] = "SMS(name, [a][d<n>][p<n>][s])";
static const char sms_descrip[] =
    "SMS handles exchange of SMS data with a call to/from SMS capabale\n"
    "phone or SMS PSTN service center. Can send and/or receive SMS messages.\n"
    "Works to ETSI ES 201 912 compatible with BT SMS PSTN service in UK\n"
    "Typical usage is to use to handle called from the SMS service centre CLI,\n"
    "or to set up a call using 'outgoing' or manager interface to connect\n"
    "service centre to SMS()\n"
    "name is the name of the queue used in /var/spool/callweaver.org/sms\n"
    "Arguments:\n"
    " a: answer, i.e. send initial FSK packet.\n"
    " d<n>: delay n*100ms before the first send (protocol 1 only).\n"
    " p<n>: delect ETSI protocol n, where n is 1 or 2.\n"
    " s: act as service centre talking to a phone.\n"
    "Messages are processed as per text file message queues.\n"
    "smsq (a separate software) is a command to generate message\n"
    "queues and send messages.\n";

/* SMS 7 bit character mapping to UCS-2 */
static const uint16_t defaultalphabet[] =
{
    0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC,
    0x00F2, 0x00E7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5,
    0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8,
    0x03A3, 0x0398, 0x039E, 0x00A0, 0x00C6, 0x00E6, 0x00DF, 0x00C9,
    ' ', '!', '"', '#', 164, '%', '&', 39, '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?',
    161, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 196, 214, 209, 220, 167,
    191, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 228, 246, 241, 252, 224,
};

static const uint16_t escapes[] =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x000C, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0x005E, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0x007B, 0x007D, 0, 0, 0, 0, 0, 0x005C,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x005B, 0x007E, 0x005D, 0,
    0x007C, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0x20AC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define SMSLEN 160              /* max SMS length */

typedef struct sms_s
{
    int protocol;          /* The protocol being used */
    uint8_t initial_delay; /* Initial delay, in units of 100ms, as defined for ETSI protocol 1 */
    uint8_t hangup;        /* we are done... */
    uint8_t err;           /* set for any errors */
    uint8_t smsc : 1;      /* we are SMSC */
    uint8_t rx : 1;        /* this is a received message */
    char queue[30];        /* queue name */
    char oa[20];           /* originating address */
    char da[20];           /* destination address */
    time_t scts;           /* time stamp, UTC */
    uint8_t pid;           /* protocol ID */
    uint8_t dcs;           /* data coding scheme */
    int16_t mr;            /* message reference - actually a byte, but usde -1 for not set */
    int udl;               /* user data length */
    int udhl;              /* user data header length */
    uint8_t srr : 1;       /* Status Report request */
    uint8_t udhi : 1;      /* User Data Header required, even if length 0 */
    uint8_t rp : 1;        /* Reply Path */
    unsigned int vp;       /* validity period in minutes, 0 for not set */
    uint16_t ud[SMSLEN];   /* user data (message), UCS-2 coded */
    uint8_t udh[SMSLEN];   /* user data header */
    char cli[20];          /* caller ID */
    uint8_t omsg[256];     /* data buffer (out) */
    int omsg_len;          /* The length of the message in omsg */
    adsi_tx_state_t tx_adsi;
    adsi_rx_state_t rx_adsi;
    unsigned int opause;   /* silent pause before sending (in sample periods) */
    int tx_active;         /* TRUE if transmission in progress */

    /* Stuff to get rid of */
    uint8_t imsg[255];     /* data buffer (in) */
    signed long long ims0,
    imc0,
    ims1,
    imc1;                  /* magnitude averages sin/cos 0/1 */
    unsigned int idle;
    uint16_t imag;         /* signal level */
    uint8_t ips0,
    ips1,
    ipc0,
    ipc1;                  /* phase sin/cos 0/1 */
    uint8_t ibitl;         /* last bit */
    uint8_t ibitc;         /* bit run length count */
    uint8_t iphasep;       /* bit phase (0-79) for 1200 bps */
    uint8_t ibitn;         /* bit number in byte being received */
    uint8_t ibytev;        /* byte value being received */
    uint8_t ibytep;        /* byte pointer in message */
    uint8_t ibytec;        /* byte checksum for message */
    uint8_t ierr;          /* error flag */
    uint8_t ibith;         /* history of last bits */
    uint8_t ibitt;         /* total of 1's in last 3 bites */

    struct opbx_frame f;
    int16_t buf[OPBX_FRIENDLY_OFFSET / sizeof(int16_t) + 800];
} sms_t;

/* different types of encoding */
#define is7bit(dcs) (((dcs) & 0xC0)  ?  (!((dcs) & 4))  :  (!((dcs) & 12)))
#define is8bit(dcs) (((dcs) & 0xC0)  ?  (((dcs) & 4))  :  (((dcs) & 12) == 4))
#define is16bit(dcs) (((dcs) & 0xC0)  ?  0  :  (((dcs) & 12) == 8))

/* Code for protocol 2, which needs properly integrating */
#define BLOCK_SIZE 160

static void put_message(sms_t *h, const uint8_t *msg, int len);

static void *sms_alloc(struct opbx_channel *chan, void *params)
{
    return params;
}

static void sms_release(struct opbx_channel *chan, void *data)
{
    return;
}

int add_tm(sms_t *h)
{
    uint8_t tx_msg[256];
    int tx_len;
    int l;

    l = -1;
    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_INFO_MT, NULL, 0);
    tx_len += 2;
    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, tx_len, DLL_PARM_PROVIDER_ID, "CW", 2);
    if (h->da[0])
        tx_len = adsi_add_field(&h->tx_adsi, tx_msg, tx_len, DLL_PARM_DESTINATION, h->da, strlen(h->da));
    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, tx_len, DLL_PARM_DISPLAY_INFO, (const uint8_t *) h->ud, h->udl);
    tx_msg[2] = tx_len - 4;
    tx_msg[3] = 0;
    put_message(h, tx_msg, tx_len);
}

static void put_adsi_msg_prot2(void *user_data, const uint8_t *msg, int len)
{
    int i;
    int l;
    uint8_t field_type;
    const uint8_t *field_body;
    int field_len;
    uint8_t body[256];
    uint8_t tx_msg[256];
    int tx_len;
    int file;
    sms_t *h;

    opbx_log(OPBX_LOG_EVENT, "Good message received (%d bytes)\n", len);

    h = (sms_t *) user_data;
#if 0
    for (i = 0;  i < len;  i++)
    {
        printf("%2x ", msg[i]);
        if ((i & 0xf) == 0xf)
            printf("\n");
    }
    printf("\n");
#endif
    l = -1;
    do
    {
        if ((l = adsi_next_field(&h->rx_adsi, msg, len, l, &field_type, &field_body, &field_len)) > 0)
        {
            if (field_body)
            {
                memcpy(body, field_body, field_len);
                body[field_len] = '\0';
                opbx_log(OPBX_LOG_EVENT, "Type %x, len %d, '%s'\n", field_type, field_len, body);
            }
            else
            {
                opbx_log(OPBX_LOG_EVENT, "Message type %x\n", field_type);
                switch (field_type)
                {
                case DLL_SMS_P2_INFO_MO:
                    file = open("/tmp/pdus", O_WRONLY | O_CREAT, 0666);
                    if (file >= 0)
                    {
                        write(file, msg + 2, msg[1]);
                        close(file);
                    }
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    /* Skip the TL length */
                    l += 2;
                    break;
                case DLL_SMS_P2_INFO_MT:
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    /* Skip the TL length */
                    l += 2;
                    break;
                case DLL_SMS_P2_INFO_STA:
                    l += 2;
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    break;
                case DLL_SMS_P2_NACK:
                    add_tm(h);
                    l += 2;
                    break;
                case DLL_SMS_P2_ACK0:
                    l += 2;
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    break;
                case DLL_SMS_P2_ACK1:
                    l += 2;
                    add_tm(h);
                    break;
                case DLL_SMS_P2_ENQ:
                    l += 2;
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    break;
                case DLL_SMS_P2_REL:
                    l += 2;
                    tx_len = adsi_add_field(&h->tx_adsi, tx_msg, -1, DLL_SMS_P2_ACK0, NULL, 0);
                    put_message(h, tx_msg, tx_len);
                    break;
                }
            }
        }
    }
    while (l > 0);
}

/*! \brief copy number, skipping non digits apart from leading + */
static void numcpy(char *d, const char *s)
{
    if (*s == '+')
        *d++ = *s++;
    while (*s)
    {
        if (isdigit(*s))
            *d++ = *s;
        s++;
    }
    *d = 0;
}

/*! \brief static, return a date/time in ISO format */
static char *isodate(time_t t)
{
    static char date[20];

    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", localtime(&t));
    return date;
}

/*! \brief reads next UCS character from null terminated UTF-8 string and advanced pointer */
/* for non valid UTF-8 sequences, returns character as is */
/* Does not advance pointer for null termination */
static long utf8decode(uint8_t **pp)
{
    uint8_t *p = *pp;

    if (p[0] == '\0')
        return 0;                 /* Null termination of string */
    (*pp)++;
    if (*p < 0xC0)
        return *p;                /* ASCII or continuation character */
    if (*p < 0xE0)
    {
        if (*p < 0xC2  ||  (p[1] & 0xC0) != 0x80)
            return *p;             /* not valid UTF-8 */
        (*pp)++;
        return ((*p & 0x1F) << 6) + (p[1] & 0x3F);
    }
    if (*p < 0xF0)
    {
        if ((*p == 0xE0  &&  p[1] < 0xA0)  ||  (p[1] & 0xC0) != 0x80  ||  (p[2] & 0xC0) != 0x80)
            return *p;             /* not valid UTF-8 */
        (*pp) += 2;
        return ((*p & 0x0F) << 12) + ((p[1] & 0x3F) << 6) + (p[2] & 0x3F);
    }
    if (*p < 0xF8)
    {
        if ((*p == 0xF0  &&  p[1] < 0x90)  ||  (p[1] & 0xC0) != 0x80  ||  (p[2] & 0xC0) != 0x80  ||  (p[3] & 0xC0) != 0x80)
            return *p;             /* not valid UTF-8 */
        (*pp) += 3;
        return ((*p & 0x07) << 18) + ((p[1] & 0x3F) << 12) + ((p[2] & 0x3F) << 6) + (p[3] & 0x3F);
    }
    if (*p < 0xFC)
    {
        if ((p[0] == 0xF8  &&  p[1] < 0x88)
            ||
            (p[1] & 0xC0) != 0x80
            ||
            (p[2] & 0xC0) != 0x80
            ||
            (p[3] & 0xC0) != 0x80
            ||
            (p[4] & 0xC0) != 0x80)
        {
            return *p;             /* not valid UTF-8 */
        }
        (*pp) += 4;
        return ((*p & 0x03) << 24) + ((p[1] & 0x3F) << 18) + ((p[2] & 0x3F) << 12) + ((p[3] & 0x3F) << 6) + (p[4] & 0x3F);
    }
    if (*p < 0xFE)
    {
        if ((*p == 0xFC  &&  p[1] < 0x84)
            ||
            (p[1] & 0xC0) != 0x80
            ||
            (p[2] & 0xC0) != 0x80
            ||
            (p[3] & 0xC0) != 0x80
            ||
            (p[4] & 0xC0) != 0x80
            ||
            (p[5] & 0xC0) != 0x80)
        {
            return *p;             /* not valid UTF-8 */
        }
        (*pp) += 5;
        return ((*p & 0x01) << 30) + ((p[1] & 0x3F) << 24) + ((p[2] & 0x3F) << 18) + ((p[3] & 0x3F) << 12) + ((p[4] & 0x3F) << 6) + (p[5] & 0x3F);
    }
    return *p;                   /* not sensible */
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud) and packs in to o using SMS 7 bit character codes */
/* The return value is the number of septets packed into o, which is internally limited to SMSLEN */
/* o can be null, in which case this is used to validate or count only */
/* if the input contains invalid characters then the return value is -1 */
static int packsms7(uint8_t *o, int udhl, uint8_t *udh, int udl, uint16_t *ud)
{
    uint8_t p = 0;
    uint8_t b = 0;
    uint8_t n = 0;

    if (udhl)                              /* header */
    {
        if (o)
            o[p++] = udhl;
        b = 1;
        n = 1;
        while (udhl--)
        {
            if (o)
                o[p++] = *udh++;
            b += 8;
            while (b >= 7)
            {
                b -= 7;
                n++;
            }
            if (n >= SMSLEN)
                return n;
        }
        if (b)
        {
            b = 7 - b;
            if (++n >= SMSLEN)
                return n;
        }
        /* Filling to septet boundary */
    }
    if (o)
        o[p] = 0;
    /* Message */
    while (udl--)
    {
        long u;

        uint8_t v;
        u = *ud++;
        for (v = 0;  v < 128  &&  defaultalphabet[v] != u;  v++)
            ;
        if (v == 128  &&  u  &&  n + 1 < SMSLEN)
        {
            for (v = 0;  v < 128  &&  escapes[v] != u;  v++)
                ;
            if (v < 128)      /* escaped sequence */
            {
                if (o)
                    o[p] |= (27 << b);
                b += 7;
                if (b >= 8)
                {
                    b -= 8;
                    p++;
                    if (o)
                        o[p] = (27 >> (7 - b));
                }
                n++;
            }
        }
        if (v == 128)
            return -1;             /* invalid character */
        if (o)
            o[p] |= (v << b);
        b += 7;
        if (b >= 8)
        {
            b -= 8;
            p++;
            if (o)
                o[p] = (v >> (7 - b));
        }
        if (++n >= SMSLEN)
            break;
    }
    return n;
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2 message (udl characters at ud) and packs in to o using 8 bit character codes */
/* The return value is the number of bytes packed in to o, which is internally limited to 140 */
/* o can be null, in which case this is used to validate or count only */
/* if the input contains invalid characters then the return value is -1 */
static int packsms8(uint8_t *o, int udhl, uint8_t *udh, int udl, uint16_t *ud)
{
    uint8_t p = 0;

    /* header - no encoding */
    if (udhl)
    {
        if (o)
            o[p++] = udhl;
        while (udhl--)
        {
            if (o)
                o[p++] = *udh++;
            if (p >= 140)
                return p;
        }
    }
    while (udl--)
    {
        long u;

        u = *ud++;
        if (u < 0  ||  u > 0xFF)
            return -1;             /* not valid */
        if (o)
            o[p++] = u;
        if (p >= 140)
            return p;
    }
    return p;
}

/*! \brief takes a binary header (udhl bytes at udh) and UCS-2
    message (udl characters at ud) and packs in to o using 16 bit
    UCS-2 character codes
    The return value is the number of bytes packed in to o, which is
    internally limited to 140
    o can be null, in which case this is used to validate or count
    only if the input contains invalid characters then
    the return value is -1 */
static int packsms16(uint8_t *o, int udhl, uint8_t *udh, int udl, uint16_t *ud)
{
    uint8_t p = 0;

    /* header - no encoding */
    if (udhl)
    {
        if (o)
            o[p++] = udhl;
        while (udhl--)
        {
            if (o)
                o[p++] = *udh++;
            if (p >= 140)
                return p;
        }
    }
    while (udl--)
    {
        long u;

        u = *ud++;
        if (o)
            o[p++] = (u >> 8);
        if (p >= 140)
            return p - 1;           /* Could not fit last character */
        if (o)
            o[p++] = u;
        if (p >= 140)
            return p;
    }
    return p;
}

/*! \brief general pack, with length and data,
    returns number of bytes of target used */
static int packsms(uint8_t dcs, uint8_t *base, unsigned int udhl, uint8_t *udh, int udl, uint16_t *ud)
{
    uint8_t *p = base;
    int l = 0;

    if (udl)
    {
        if (is7bit(dcs))
        {
            if ((l = packsms7(p + 1, udhl, udh, udl, ud)) < 0)
                l = 0;
            *p++ = l;
            p += (l*7 + 7)/8;
        }
        else if (is8bit(dcs))
        {
            if ((l = packsms8(p + 1, udhl, udh, udl, ud)) < 0)
                l = 0;
            *p++ = l;
            p += l;
        }
        else
        {
            /* UCS-2 */
            if ((l = packsms16(p + 1, udhl, udh, udl, ud)) < 0)
                l = 0;
            *p++ = l;
            p += l;
        }
    }
    else
    {
        /* No user data */
        *p++ = 0;
    }
    return p - base;
}

/*! \brief pack a date and return */
static void packdate(uint8_t *o, time_t w)
{
    struct tm *t = localtime(&w);
#if defined(__FreeBSD__)  ||  defined(__OpenBSD__)  ||  defined( __NetBSD__ )  ||  defined(__APPLE__)
    int z = -t->tm_gmtoff/(60*15);
#else
    int z = timezone/(60*15);
#endif

    *o++ = ((t->tm_year % 10) << 4) + (t->tm_year % 100) / 10;
    *o++ = (((t->tm_mon + 1) % 10) << 4) + (t->tm_mon + 1) / 10;
    *o++ = ((t->tm_mday % 10) << 4) + t->tm_mday / 10;
    *o++ = ((t->tm_hour % 10) << 4) + t->tm_hour / 10;
    *o++ = ((t->tm_min % 10) << 4) + t->tm_min / 10;
    *o++ = ((t->tm_sec % 10) << 4) + t->tm_sec / 10;
    if (z < 0)
        *o++ = (((-z) % 10) << 4) + (-z) / 10 + 0x08;
    else
        *o++ = ((z % 10) << 4) + z / 10;
}

/*! \brief unpack a date and return */
static time_t unpackdate(const uint8_t *i)
{
    struct tm t;

    t.tm_year = 100 + (i[0] & 0xF) * 10 + (i[0] >> 4);
    t.tm_mon = (i[1] & 0xF) * 10 + (i[1] >> 4) - 1;
    t.tm_mday = (i[2] & 0xF) * 10 + (i[2] >> 4);
    t.tm_hour = (i[3] & 0xF) * 10 + (i[3] >> 4);
    t.tm_min = (i[4] & 0xF) * 10 + (i[4] >> 4);
    t.tm_sec = (i[5] & 0xF) * 10 + (i[5] >> 4);
    t.tm_isdst = 0;
    if (i[6] & 0x08)
        t.tm_min += 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
    else
        t.tm_min -= 15 * ((i[6] & 0x7) * 10 + (i[6] >> 4));
    return mktime(&t);
}

/*! \brief unpacks bytes (7 bit encoding) at i, len l septets,
    and places in udh and ud setting udhl and udl. udh not used
    if udhi not set */
static void unpacksms7(const uint8_t *i, uint8_t l, uint8_t *udh, int *udhl, uint16_t *ud, int *udl, char udhi)
{
    uint8_t b = 0;
    uint8_t p = 0;
    uint16_t *o = ud;

    *udhl = 0;
    if (udhi  &&  l)           /* header */
    {
        int h = i[p];

        *udhl = h;
        if (h)
        {
            b = 1;
            p++;
            l--;
            while (h--  &&  l)
            {
                *udh++ = i[p++];
                b += 8;
                while (b >= 7)
                {
                    b -= 7;
                    l--;
                    if (!l)
                        break;
                }
            }
            /* adjust for fill, septets */
            if (b)
            {
                b = 7 - b;
                l--;
            }
        }
    }
    while (l--)
    {
        uint8_t v;

        if (b < 2)
            v = ((i[p] >> b) & 0x7F);
        else
            v = ((((i[p] >> b) + (i[p + 1] << (8 - b)))) & 0x7F);
        b += 7;
        if (b >= 8)
        {
            b -= 8;
            p++;
        }
        if (o > ud  &&  o[-1] == 0x00A0  &&  escapes[v])
            o[-1] = escapes[v];
        else
            *o++ = defaultalphabet[v];
    }
    *udl = (o - ud);
}

/*! \brief unpacks bytes (8 bit encoding) at i, len l septets,
      and places in udh and ud setting udhl and udl. udh not used
      if udhi not set */
static void unpacksms8(const uint8_t *i, uint8_t l, uint8_t *udh, int *udhl, uint16_t *ud, int *udl, char udhi)
{
    uint16_t *o = ud;

    *udhl = 0;
    if (udhi)
    {
        int n = *i;

        *udhl = n;
        if (n)
        {
            i++;
            l--;
            while (l  &&  n)
            {
                l--;
                n--;
                *udh++ = *i++;
            }
        }
    }
    while (l--)
        *o++ = *i++;      /* not to UTF-8 as explicitely 8 bit coding in DCS */
    *udl = (o - ud);
}

/*! \brief unpacks bytes (16 bit encoding) at i, len l septets,
     and places in udh and ud setting udhl and udl.
    udh not used if udhi not set */
static void unpacksms16(const uint8_t *i, uint8_t l, uint8_t *udh, int *udhl, uint16_t *ud, int *udl, char udhi)
{
    uint16_t *o = ud;

    *udhl = 0;
    if (udhi)
    {
        int n = *i;

        *udhl = n;
        if (n)
        {
            i++;
            l--;
            while (l  &&  n)
            {
                l--;
                n--;
                *udh++ = *i++;
            }
        }
    }
    while (l--)
    {
        int v = *i++;
        if (l--)
            v = (v << 8) + *i++;
        *o++ = v;
    }
    *udl = (o - ud);
}

/*! \brief general unpack - starts with length byte (octet or septet) and returns number of bytes used, inc length */
static int unpacksms(uint8_t dcs, const uint8_t *i, uint8_t *udh, int *udhl, uint16_t *ud, int *udl, char udhi)
{
    int l = *i++;

    if (is7bit(dcs))
    {
        unpacksms7(i, l, udh, udhl, ud, udl, udhi);
        l = (l * 7 + 7) / 8;        /* adjust length to return */
    }
    else if (is8bit(dcs))
        unpacksms8(i, l, udh, udhl, ud, udl, udhi);
    else
        unpacksms16(i, l, udh, udhl, ud, udl, udhi);
    return l + 1;
}

/*! \brief unpack an address from i, return byte length, unpack to o */
static uint8_t unpackaddress(char *o, const uint8_t *i)
{
    uint8_t l = i[0];
    uint8_t p;

    if (i[1] == (0x80 | DLL_SMS_P1_DATA))
        *o++ = '+';
    for (p = 0;  p < l;  p++)
    {
        if (p & 1)
            *o++ = (i[2 + p / 2] >> 4) + '0';
        else
            *o++ = (i[2 + p / 2] & 0xF) + '0';
    }
    *o = 0;
    return (l + 5) / 2;
}

/*! \brief store an address at o, and return number of bytes used */
static uint8_t packaddress(uint8_t *o, const char *i)
{
    uint8_t p = 2;

    o[0] = 0;
    if (*i == '+')
    {
        i++;
        o[1] = 0x91;
    }
    else
    {
        o[1] = 0x81;
    }
    while (*i)
    {
        if (isdigit(*i))
        {
            if (o[0] & 1)
                o[p++] |= ((*i & 0xF) << 4);
            else
                o[p] = (*i & 0xF);
            o[0]++;
        }
        i++;
    }
    if (o[0] & 1)
        o[p++] |= 0xF0;              /* pad */
    return p;
}

/*! \brief Log the output, and remove file */
static void sms_log(sms_t *h, char status)
{
    int o;

    if (h->oa[0]  ||  h->da[0])
    {
        if ((o = open(log_file, O_CREAT | O_APPEND | O_WRONLY, 0666)) >= 0)
        {
            char line[1000];
            char mrs[3];
            char *p;
            uint8_t n;

            if (h->mr >= 0)
                snprintf(mrs, sizeof(mrs), "%02X", h->mr);
            else
                mrs[0] = '\0';
            snprintf(line,
                     sizeof(line),
                     "%s %c%c%c%s %s %s %s ",
                     isodate(time(NULL)),
                     status,
                     h->rx  ?  'I'  :  'O',
                     h->smsc  ?  'S'  :  'M',
                     mrs,
                     h->queue,
                     h->oa[0]  ?  h->oa  :  "-",
                     h->da[0]  ?  h->da  :  "-");
            p = line + strlen(line);
            for (n = 0;  n < h->udl;  n++)
            {
                if (h->ud[n] == '\\')
                {
                    *p++ = '\\';
                    *p++ = '\\';
                }
                else if (h->ud[n] == '\n')
                {
                    *p++ = '\\';
                    *p++ = 'n';
                }
                else if (h->ud[n] == '\r')
                {
                    *p++ = '\\';
                    *p++ = 'r';
                }
                else if (h->ud[n] < 32  ||  h->ud[n] == 127)
                    *p++ = 191;
                else
                    *p++ = h->ud[n];
            }
            *p++ = '\n';
            *p = 0;
            write(o, line, strlen(line));
            close(o);
        }
        h->oa[0] =
        h->da[0] =
        h->udl = 0;
    }
}

/*! \brief parse and delete a file */
static void sms_readfile(sms_t *h, char *fn)
{
    char line[1000];
    FILE *s;
    char dcsset = 0;                 /* if DSC set */

    opbx_log(OPBX_LOG_EVENT, "Sending %s\n", fn);
    h->rx =
    h->udl =
    h->oa[0] =
    h->da[0] =
    h->pid =
    h->srr =
    h->udhi =
    h->rp =
    h->vp =
    h->udhl = 0;
    h->mr = -1;
    h->dcs = 0xF1;                    /* normal messages class 1 */
    h->scts = time(NULL);
    s = fopen(fn, "r");
    if (s)
    {
        if (unlink(fn))
        {
            /* concurrent access, we lost */
            fclose(s);
            return;
        }
        while (fgets(line, sizeof(line), s))
        {
            /* process line in file */
            char *p;
            void *pp = &p;

            for (p = line;  *p  &&  *p != '\n'  &&  *p != '\r';  p++)
                ;
            *p = 0;                     /* strip eoln */
            p = line;
            if (!*p  ||  *p == ';')
                continue;              /* blank line or comment, ignore */
            while (isalnum(*p))
            {
                *p = tolower(*p);
                p++;
            }
            while (isspace(*p))
                *p++ = 0;
            if (*p == '=')
            {
                *p++ = 0;
                if (!strcmp(line, "ud"))
                {
                    /* parse message (UTF-8) */
                    uint8_t o = 0;

                    while (*p  &&  o < SMSLEN)
                        h->ud[o++] = utf8decode(pp);
                    h->udl = o;
                    if (*p)
                        opbx_log(OPBX_LOG_WARNING, "UD too long in %s\n", fn);
                }
                else
                {
                    while (isspace(*p))
                        p++;
                    if (strcmp(line, "oa") == 0  &&  strlen(p) < sizeof(h->oa))
                        numcpy(h->oa, p);
                    else if (strcmp(line, "da") == 0  &&  strlen(p) < sizeof(h->oa))
                        numcpy(h->da, p);
                    else if (strcmp(line, "pid") == 0)
                        h->pid = atoi(p);
                    else if (strcmp(line, "dcs") == 0)
                    {
                        h->dcs = atoi(p);
                        dcsset = 1;
                    }
                    else if (strcmp(line, "mr") == 0)
                        h->mr = atoi(p);
                    else if (strcmp(line, "srr") == 0)
                        h->srr = atoi(p)  ?   1  :  0;
                    else if (strcmp(line, "vp") == 0)
                        h->vp = atoi(p);
                    else if (strcmp(line, "rp") == 0)
                        h->rp = atoi(p)  ?  1  :  0;
                    else if (strcmp(line, "scts") == 0)
                    {
                        /* Get date/time */
                        int Y;
                        int m;
                        int d;
                        int H;
                        int M;
                        int S;
                        if (sscanf(p, "%d-%d-%dT%d:%d:%d", &Y, &m, &d, &H, &M, &S) == 6)
                        {
                            struct tm t;

                            t.tm_year = Y - 1900;
                            t.tm_mon = m - 1;
                            t.tm_mday = d;
                            t.tm_hour = H;
                            t.tm_min = M;
                            t.tm_sec = S;
                            t.tm_isdst = -1;
                            h->scts = mktime(&t);
                            if (h->scts == (time_t) - 1)
                                opbx_log(OPBX_LOG_WARNING, "Bad date/timein %s: %s", fn, p);
                        }
                    }
                    else
                    {
                        opbx_log(OPBX_LOG_WARNING, "Cannot parse in %s: %s=%si\n", fn, line, p);
                    }
                }
            }
            else if (*p == '#')
            {
                /* raw hex format */
                *p++ = 0;
                if (*p == '#')
                {
                    p++;
                    if (!strcmp(line, "ud"))
                    {
                        /* user data */
                        int o = 0;

                        while (*p  &&  o < SMSLEN)
                        {
                            if (!isxdigit(p[0])  ||  !isxdigit(p[1])  ||  isxdigit(p[2])  ||  !isxdigit(p[3]))
                                break;
                            h->ud[o++] =
                                (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 12) +
                                (((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF)) << 8) +
                                (((isalpha (p[2]) ? 9 : 0) + (p[2] & 0xF)) << 4) + ((isalpha (p[3]) ? 9 : 0) + (p[3] & 0xF));
                            p += 4;
                        }
                        h->udl = o;
                        if (*p)
                            opbx_log(OPBX_LOG_WARNING, "UD too long / invalid UCS-2 hex in %s\n", fn);
                    }
                    else
                    {
                        opbx_log(OPBX_LOG_WARNING, "Only ud can use ## format, %s\n", fn);
                    }
                }
                else if (!strcmp(line, "ud"))
                {
                    /* user data */
                    int o = 0;

                    while (*p  &&  o < SMSLEN)
                    {
                        if (!isxdigit(p[0])  ||  !isxdigit(p[1]))
                            break;
                        h->ud[o++] = (((isalpha (*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha (p[1]) ? 9 : 0) + (p[1] & 0xF));
                        p += 2;
                    }
                    h->udl = o;
                    if (*p)
                        opbx_log(OPBX_LOG_WARNING, "UD too long / invalid UCS-1 hex in %s\n", fn);
                }
                else if (!strcmp(line, "udh"))
                {
                    /* user data header */
                    uint8_t o = 0;
                    h->udhi = 1;
                    while (*p  &&  o < SMSLEN)
                    {
                        if (!isxdigit(p[0])  ||  !isxdigit(p[1]))
                            break;
                        h->udh[o] = (((isalpha(*p) ? 9 : 0) + (*p & 0xF)) << 4) + ((isalpha(p[1]) ? 9 : 0) + (p[1] & 0xF));
                        o++;
                        p += 2;
                    }
                    h->udhl = o;
                    if (*p)
                        opbx_log(OPBX_LOG_WARNING, "UDH too long / invalid hex in %s\n", fn);
                }
                else
                {
                    opbx_log(OPBX_LOG_WARNING, "Only ud and udh can use # format, %s\n", fn);
                }
            }
            else
            {
                opbx_log(OPBX_LOG_WARNING, "Cannot parse in %s: %s\n", fn, line);
            }
        }
        fclose(s);
        if (!dcsset  &&  packsms7(0, h->udhl, h->udh, h->udl, h->ud) < 0)
        {
            if (packsms8(0, h->udhl, h->udh, h->udl, h->ud) < 0)
            {
                if (packsms16(0, h->udhl, h->udh, h->udl, h->ud) < 0)
                {
                    opbx_log(OPBX_LOG_WARNING, "Invalid UTF-8 message even for UCS-2 (%s)\n", fn);
                }
                else
                {
                    h->dcs = 0x08;    /* default to 16 bit */
                    opbx_log(OPBX_LOG_WARNING, "Sending in 16 bit format (%s)\n", fn);
                }
            }
            else
            {
                h->dcs = 0xF5;        /* default to 8 bit */
                opbx_log(OPBX_LOG_WARNING, "Sending in 8 bit format (%s)\n", fn);
            }
        }
        if (is7bit(h->dcs)  &&  packsms7(0, h->udhl, h->udh, h->udl, h->ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 7 bit GSM data %s\n", fn);
        if (is8bit(h->dcs)  &&  packsms8(0, h->udhl, h->udh, h->udl, h->ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 8 bit data %s\n", fn);
        if (is16bit(h->dcs)  &&  packsms16(0, h->udhl, h->udh, h->udl, h->ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 16 bit data %s\n", fn);
    }
}

/*! \brief white a received text message to a file */
static void sms_writefile(sms_t *h)
{
    char fn[200] = "";
    char fn2[200] = "";
    FILE *o;
    unsigned int p;
    uint16_t v;

    opbx_copy_string(fn, spool_dir, sizeof(fn));
    mkdir(fn, 0777);            /* ensure it exists */
    snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/%s", h->smsc ? h->rx ? "morx" : "mttx" : h->rx ? "mtrx" : "motx");
    mkdir(fn, 0777);            /* ensure it exists */
    opbx_copy_string(fn2, fn, sizeof(fn2));
    snprintf(fn2 + strlen(fn2), sizeof(fn2) - strlen(fn2), "/%s.%s-%d", h->queue, isodate (h->scts), seq++);
    snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/.%s", fn2 + strlen(fn) + 1);
    o = fopen(fn, "w");
    if (o)
    {
        if (h->oa[0])
            fprintf(o, "oa=%s\n", h->oa);
        if (h->da[0])
            fprintf(o, "da=%s\n", h->da);
        if (h->udhi)
        {
            fprintf(o, "udh#");
            for (p = 0;  p < h->udhl;  p++)
                fprintf(o, "%02X", h->udh[p]);
            fprintf(o, "\n");
        }
        if (h->udl)
        {
            for (p = 0;  p < h->udl  &&  h->ud[p] >= ' ';  p++)
                ;
            if (p < h->udl)
                fputc(';', o);      /* cannot use ud=, but include as a comment for human readable */
            fprintf(o, "ud=");
            for (p = 0;  p < h->udl;  p++)
            {
                v = h->ud[p];
                if (v < 32)
                    fputc(191, o);
                else if (v < 0x80)
                    fputc(v, o);
                else if (v < 0x800)
                {
                    fputc(0xC0 + (v >> 6), o);
                    fputc(0x80 + (v & 0x3F), o);
                }
                else
                {
                    fputc(0xE0 + (v >> 12), o);
                    fputc(0x80 + ((v >> 6) & 0x3F), o);
                    fputc(0x80 + (v & 0x3F), o);
                }
            }
            fprintf(o, "\n");
            for (p = 0;  p < h->udl  &&  h->ud[p] >= ' ';  p++)
                ;
            if (p < h->udl)
            {
                for (p = 0;  p < h->udl  &&  h->ud[p] < 0x100;  p++)
                    ;
                if (p == h->udl)                           /* can write in ucs-1 hex */
                {
                    fprintf(o, "ud#");
                    for (p = 0;  p < h->udl;  p++)
                        fprintf(o, "%02X", h->ud[p]);
                    fprintf(o, "\n");
                }
                else                           /* write in UCS-2 */
                {
                    fprintf(o, "ud##");
                    for (p = 0;  p < h->udl;  p++)
                        fprintf(o, "%04X", h->ud[p]);
                    fprintf(o, "\n");
                }
            }
        }
        if (h->scts)
            fprintf(o, "scts=%s\n", isodate(h->scts));
        if (h->pid)
            fprintf(o, "pid=%d\n", h->pid);
        if (h->dcs != 0xF1)
            fprintf(o, "dcs=%d\n", h->dcs);
        if (h->vp)
            fprintf(o, "vp=%d\n", h->vp);
        if (h->srr)
            fprintf(o, "srr=1\n");
        if (h->mr >= 0)
            fprintf(o, "mr=%d\n", h->mr);
        if (h->rp)
            fprintf(o, "rp=1\n");
        fclose(o);
        if (rename(fn, fn2))
            unlink(fn);
        else
            opbx_log(OPBX_LOG_EVENT, "Received to %s\n", fn2);
    }
}

/*! \brief read dir skipping dot files... */
static struct dirent *readdirqueue(DIR * d, char *queue)
{
    struct dirent *f;
    do
    {
        f = readdir(d);
    }
    while (f  &&  (*f->d_name == '.' || strncmp(f->d_name, queue, strlen(queue)) || f->d_name[strlen(queue)] != '.'));
    return f;
}

/*! \brief handle the incoming message */
static uint8_t sms_handleincoming(sms_t *h, const uint8_t *msg, int len)
{
    uint8_t p = 3;

    if (h->smsc)
    {
        /* SMSC */
        if ((msg[2] & 3) == 1)
        {
            /* SMS-SUBMIT */
            h->udhl =
            h->udl = 0;
            h->vp = 0;
            h->srr = ((msg[2] & 0x20)  ?  1  :  0);
            h->udhi = ((msg[2] & 0x40)  ?  1  :  0);
            h->rp = ((msg[2] & 0x80)  ?  1  :  0);
            opbx_copy_string(h->oa, h->cli, sizeof(h->oa));
            h->scts = time(NULL);
            h->mr = msg[p++];
            p += unpackaddress(h->da, msg + p);
            h->pid = msg[p++];
            h->dcs = msg[p++];
            if ((msg[2] & 0x18) == 0x10)                               /* relative VP */
            {
                if (msg[p] < 144)
                    h->vp = (msg[p] + 1) * 5;
                else if (msg[p] < 168)
                    h->vp = 720 + (msg[p] - 143) * 30;
                else if (msg[p] < 197)
                    h->vp = (msg[p] - 166) * 1440;
                else
                    h->vp = (msg[p] - 192) * 10080;
                p++;
            }
            else if (msg[2] & 0x18)
            {
                p += 7;                 /* ignore enhanced / absolute VP */
            }
            p += unpacksms(h->dcs, h->imsg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
            h->rx = 1;                 /* received message */
            sms_writefile(h);      /* write the file */
            if (p != msg[1] + 2)
            {
                opbx_log(OPBX_LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, msg[1] + 2);
                return 0xFF;          /* duh! */
            }
        }
        else
        {
            opbx_log(OPBX_LOG_WARNING, "Unknown message type %02X\n", msg[2]);
            return 0xFF;
        }
    }
    else
    {
        /* client */
        if (!(msg[2] & 3))
        {
            /* SMS-DELIVER */
            *h->da = h->srr = h->rp = h->vp = h->udhi = h->udhl = h->udl = 0;
            h->srr = ((msg[2] & 0x20) ? 1 : 0);
            h->udhi = ((msg[2] & 0x40) ? 1 : 0);
            h->rp = ((msg[2] & 0x80) ? 1 : 0);
            h->mr = -1;
            p += unpackaddress(h->oa, msg + p);
            h->pid = msg[p++];
            h->dcs = msg[p++];
            h->scts = unpackdate(msg + p);
            p += 7;
            p += unpacksms(h->dcs, msg + p, h->udh, &h->udhl, h->ud, &h->udl, h->udhi);
            h->rx = 1;                 /* received message */
            sms_writefile(h);      /* write the file */
            if (p != msg[1] + 2)
            {
                opbx_log(OPBX_LOG_WARNING, "Mismatch receive unpacking %d/%d\n", p, msg[1] + 2);
                return 0xFF;          /* duh! */
            }
        }
        else
        {
            opbx_log(OPBX_LOG_WARNING, "Unknown message type %02X\n", msg[2]);
            return 0xFF;
        }
    }
    return 0;                          /* no error */
}

#ifdef SOLARIS
#define NAME_MAX 1024
#endif

/*! \brief find and fill in next message, or send a REL if none waiting */
static void sms_nextoutgoing(sms_t *h)
{
    char fn[100 + NAME_MAX] = "";
    DIR *d;
    char more = 0;
    uint8_t tx_msg[256];
    struct dirent *f;

    opbx_copy_string(fn, spool_dir, sizeof(fn));
    mkdir(fn, 0777);                /* ensure it exists */
    h->rx = 0;                      /* outgoing message */
    snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/%s", h->smsc  ?  "mttx"  :  "motx");
    mkdir(fn, 0777);                /* ensure it exists */
    d = opendir(fn);
    if (d)
    {
        if ((f = readdirqueue(d, h->queue)))
        {
            snprintf(fn + strlen(fn), sizeof(fn) - strlen(fn), "/%s", f->d_name);
            sms_readfile(h, fn);
            if (readdirqueue(d, h->queue))
                more = 1;              /* more to send */
        }
        closedir(d);
    }
    if (h->da[0]  ||  h->oa[0])             /* message to send */
    {
        uint8_t p = 2;
        tx_msg[0] = 0x80 | DLL_SMS_P1_DATA;
        if (h->smsc)
        {
            /* Deliver */
            tx_msg[p++] = (more)  ?  4  :  0;
            p += packaddress(tx_msg + p, h->oa);
            tx_msg[p++] = h->pid;
            tx_msg[p++] = h->dcs;
            packdate(tx_msg + p, h->scts);
            p += 7;
            p += packsms(h->dcs, tx_msg + p, h->udhl, h->udh, h->udl, h->ud);
        }
        else
        {
            /* Submit */
            tx_msg[p++] = 0x01
                        + (more  ?  4  :  0)
                        + (h->srr  ?  0x20  :  0)
                        + (h->rp  ?  0x80  :  0)
                        + (h->vp  ?  0x10  :  0)
                        + (h->udhi  ?  0x40  :  0);
            if (h->mr < 0)
                h->mr = message_ref++;
            tx_msg[p++] = h->mr;
            p += packaddress(tx_msg + p, h->da);
            tx_msg[p++] = h->pid;
            tx_msg[p++] = h->dcs;
            if (h->vp)           /* relative VP */
            {
                if (h->vp < 720)
                    tx_msg[p++] = (h->vp + 4) / 5 - 1;
                else if (h->vp < 1440)
                    tx_msg[p++] = (h->vp - 720 + 29) / 30 + 143;
                else if (h->vp < 43200)
                    tx_msg[p++] = (h->vp + 1439) / 1440 + 166;
                else if (h->vp < 635040)
                    tx_msg[p++] = (h->vp + 10079) / 10080 + 192;
                else
                    tx_msg[p++] = 255;        /* max */
            }
            p += packsms(h->dcs, tx_msg + p, h->udhl, h->udh, h->udl, h->ud);
        }
        tx_msg[1] = p - 2;
        put_message(h, tx_msg, p);
    }
    else
    {
        /* no message */
        tx_msg[0] = 0x80 | DLL_SMS_P1_REL;
        tx_msg[1] = 0;
        put_message(h, tx_msg, 2);
    }
}

static void sms_debug(char *dir, const uint8_t *msg, int len)
{
    char txt[259*3 + 1];
    char *p = txt;                         /* always long enough */
    int q;

    for (q = 0;  q < len  &&  q < 30;  p += 3)
        sprintf(p, " %02X", msg[q++]);
    if (q < len)
        sprintf(p, "...");
    if (option_verbose > 2)
        opbx_verbose(VERBOSE_PREFIX_3 "SMS %s%s\n", dir, txt);
}

static void put_adsi_msg_prot1(void *user_data, const uint8_t *msg, int len)
{
    uint8_t cause;
    uint8_t tx_msg[256];
    sms_t *h = (sms_t *) user_data;
    
    sms_debug("RX", msg, len);
    /* testing */
    switch (msg[0])
    {
    case 0x80 | DLL_SMS_P1_DATA:
        cause = sms_handleincoming(h, h->imsg, h->imsg[1] + 2);
        if (!cause)
        {
            sms_log(h, 'Y');
            tx_msg[0] = 0x80 | DLL_SMS_P1_ACK;
            tx_msg[1] = 2;
            tx_msg[2] = 0x00;       /* deliver report */
            tx_msg[3] = 0x00;       /* no parameters */
            put_message(h, tx_msg, 4);
        }
        else
        {
            sms_log(h, 'N');
            tx_msg[0] = 0x80 | DLL_SMS_P1_NACK;
            tx_msg[1] = 3;
            tx_msg[2] = 0x00;       /* delivery report */
            tx_msg[3] = cause;      /* cause */
            tx_msg[4] = 0x00;       /* no parameters */
            put_message(h, tx_msg, 5);
        }
        break;
    case 0x80 | DLL_SMS_P1_ERROR:
        /* Send whatever we sent again */
        h->err = 1;
        put_message(h, h->omsg, h->omsg_len);
        break;
    case 0x80 | DLL_SMS_P1_EST:
        sms_nextoutgoing(h);
        break;
    case 0x80 | DLL_SMS_P1_REL:
        h->hangup = 1;
        break;
    case 0x80 | DLL_SMS_P1_ACK:
        sms_log(h, 'Y');
        sms_nextoutgoing(h);
        break;
    case 0x80 | DLL_SMS_P1_NACK:
        h->err = 1;
        sms_log(h, 'N');
        sms_nextoutgoing(h);
        break;
    default:
        /* Unknown */
        tx_msg[0] = 0x80 | DLL_SMS_P1_ERROR;
        tx_msg[1] = 1;
        tx_msg[2] = DLL_SMS_ERROR_UNKNOWN_MESSAGE_TYPE;
        put_message(h, tx_msg, 3);
        break;
    }
}

static void put_message(sms_t *h, const uint8_t *msg, int len)
{
    /* Save the message, for possible retransmission */
    memcpy(h->omsg, msg, len);
    h->omsg_len = len;

    sms_debug("TX", msg, len);
    if (h->protocol != '2')
    {
        if (msg[0] == (0x80 | DLL_SMS_P1_EST))
            h->opause = 800*h->initial_delay;
        else
            h->opause = 800;
    }
    adsi_tx_put_message(&h->tx_adsi, msg, len);
    h->tx_active = TRUE;
}

static struct opbx_frame *sms_generate(struct opbx_channel *chan, void *data, int samples)
{
    sms_t *h = (sms_t *) data;
    int i;
    int j;
    int len;

    if (samples > sizeof(h->buf) / sizeof(h->buf[0]))
        samples = sizeof(h->buf) / sizeof(h->buf[0]);
    len = samples * sizeof(h->buf[0]) + OPBX_FRIENDLY_OFFSET;

    opbx_fr_init_ex(&h->f, OPBX_FRAME_VOICE, OPBX_FORMAT_SLINEAR, "app_sms");
    h->f.offset = OPBX_FRIENDLY_OFFSET;
    h->f.data = ((char *) h->buf) + OPBX_FRIENDLY_OFFSET;
    if (h->opause)
    {
        i = (h->opause >= samples)  ?  samples  :  h->opause;
        memset(h->f.data, 0, sizeof(int16_t)*i);
        h->opause -= i;
        if (i < samples)
        {
            if ((j = adsi_tx(&h->tx_adsi, ((int16_t *) h->f.data) + i, samples - i)) < samples - i)
                h->tx_active = FALSE;
            i += j;
        }
    }
    else
    {
        if ((i = adsi_tx(&h->tx_adsi, (int16_t *) h->f.data, samples)) < samples)
            h->tx_active = FALSE;
    }
    if (i < samples)
        memset(((int16_t *) h->f.data) + i, 0, sizeof(int16_t)*(samples - i));
    h->f.samples = samples;
    h->f.datalen = h->f.samples*sizeof(int16_t);

    return &h->f;
}

static void sms_process(sms_t *h, const int16_t *data, int samples)
{
    static const int16_t wave[] =
    {
        0, 392, 782, 1167, 1545, 1913, 2270, 2612, 2939, 3247, 3536, 3802, 4045, 4263, 4455, 4619, 4755, 4862, 4938, 4985,
        5000, 4985, 4938, 4862, 4755, 4619, 4455, 4263, 4045, 3802, 3536, 3247, 2939, 2612, 2270, 1913, 1545, 1167, 782, 392,
        0, -392, -782, -1167,
        -1545, -1913, -2270, -2612, -2939, -3247, -3536, -3802, -4045, -4263, -4455, -4619, -4755, -4862, -4938, -4985, -5000,
        -4985, -4938, -4862,
        -4755, -4619, -4455, -4263, -4045, -3802, -3536, -3247, -2939, -2612, -2270, -1913, -1545, -1167, -782, -392
    };
    uint8_t tx_msg[256];
    
    /* Ignore the input signal while transmitting */
    if (h->tx_active)
        return;
    //adsi_rx(&h->rx_adsi, data, samples);
    while (samples--)
    {
        unsigned long long m0, m1;

        if (abs(*data) > h->imag)
            h->imag = abs(*data);
        else
            h->imag = h->imag * 7 / 8;
        if (h->imag > 500)
        {
            h->idle = 0;
            h->ims0 = (h->ims0 * 6 + *data * wave[h->ips0]) / 7;
            h->imc0 = (h->imc0 * 6 + *data * wave[h->ipc0]) / 7;
            h->ims1 = (h->ims1 * 6 + *data * wave[h->ips1]) / 7;
            h->imc1 = (h->imc1 * 6 + *data * wave[h->ipc1]) / 7;
            m0 = h->ims0 * h->ims0 + h->imc0 * h->imc0;
            m1 = h->ims1 * h->ims1 + h->imc1 * h->imc1;
            if ((h->ips0 += 21) >= 80)
                h->ips0 -= 80;
            if ((h->ipc0 += 21) >= 80)
                h->ipc0 -= 80;
            if ((h->ips1 += 13) >= 80)
                h->ips1 -= 80;
            if ((h->ipc1 += 13) >= 80)
                h->ipc1 -= 80;
            {
                char bit;

                h->ibith <<= 1;
                if (m1 > m0)
                    h->ibith |= 1;
                if (h->ibith & 8)
                    h->ibitt--;
                if (h->ibith & 1)
                    h->ibitt++;
                bit = ((h->ibitt > 1) ? 1 : 0);
                if (bit != h->ibitl)
                    h->ibitc = 1;
                else
                    h->ibitc++;
                h->ibitl = bit;
                if (!h->ibitn  &&  h->ibitc == 4  &&  !bit)
                {
                    h->ibitn = 1;
                    h->iphasep = 0;
                }
                if (bit  &&  h->ibitc == 200)                           /* sync, restart message */
                {
                    h->ierr =
                    h->ibitn =
                    h->ibytep =
                    h->ibytec = 0;
                }
                if (h->ibitn)
                {
                    h->iphasep += 12;
                    if (h->iphasep >= 80)                       /* next bit */
                    {
                        h->iphasep -= 80;
                        if (h->ibitn++ == 9)                   /* end of byte */
                        {
                            if (!bit)  /* bad stop bit */
                            {
                                h->ierr = DLL_SMS_ERROR_UNSPECIFIED_ERROR;
                            }
                            else
                            {
                                if (h->ibytep < sizeof(h->imsg))
                                {
                                    h->imsg[h->ibytep] = h->ibytev;
                                    h->ibytec += h->ibytev;
                                    h->ibytep++;
                                }
                                else if (h->ibytep == sizeof(h->imsg))
                                {
                                    h->ierr = DLL_SMS_ERROR_WRONG_MESSAGE_LEN;
                                }
                                if (h->ibytep > 1  &&  h->ibytep == 3 + h->imsg[1]  &&  !h->ierr)
                                {
                                    if (!h->ibytec)
                                    {
                                        if (h->protocol == '2')
                                            put_adsi_msg_prot2(h, h->imsg, h->imsg[1] + 2);
                                        else
                                            put_adsi_msg_prot1(h, h->imsg, h->imsg[1] + 2);
                                    }
                                    else
                                    {
                                        h->ierr = DLL_SMS_ERROR_WRONG_CHECKSUM;
                                    }
                                }
                            }
                            h->ibitn = 0;
                        }
                        h->ibytev = (h->ibytev >> 1) + (bit  ?  0x80  :  0);
                    }
                }
            }
        }
        else
        {
            /* Lost carrier */
            if (h->idle++ == 80000)
            {
                /* Nothing happening */
                opbx_log(OPBX_LOG_EVENT, "No data, hanging up\n");
                h->hangup = 1;
                h->err = 1;
            }
            if (h->ierr)
            {
                /* Send error */
                h->err = DLL_SMS_ERROR_WRONG_CHECKSUM;
                tx_msg[0] = 0x80 | DLL_SMS_P1_ERROR;
                tx_msg[1] = 1;
                tx_msg[2] = h->ierr;
                put_message(h, tx_msg, 3);
            }
            h->ierr =
            h->ibitn =
            h->ibytep =
            h->ibytec = 0;
        }
        data++;
    }
}

static struct opbx_generator smsgen =
{
alloc:
    sms_alloc,
release:
    sms_release,
generate:
    sms_generate,
};

static int sms_exec(struct opbx_channel *chan, int argc, char **argv, char *result, size_t result_max)
{
    sms_t h = { 0 };
    int res = -1;
    struct localuser *u;
    struct opbx_frame *f;
    char *d;
    int answer;
    int original_read_fmt;
    int original_write_fmt;
    uint8_t tx_msg[256];

    if (argc < 1  ||  argc > 2)
        return opbx_function_syntax(sms_syntax);

    LOCAL_USER_ADD(u);

    h.ipc0 =
    h.ipc1 = 20;        /* phase for cosine */
    h.dcs = 0xF1;       /* default */

    if (chan->cid.cid_num)
        opbx_copy_string(h.cli, chan->cid.cid_num, sizeof(h.cli));

    answer = 0;

    if (strlen(argv[0]) >= sizeof(h.queue))
    {
        opbx_log(OPBX_LOG_ERROR, "Queue name too long\n");
        LOCAL_USER_REMOVE(u);
        return -1;
    }
    strcpy(h.queue, argv[0]);
    for (d = h.queue;  *d;  d++)
    {
        if (!isalnum(*d))
            *d = '-';              /* make very safe for filenames */
    }

    h.protocol = '1';
    h.initial_delay = 3;
    if (argc > 1)
    {
        for (d = argv[1];  *d;  d++)
        {
            switch (*d)
            {
            case 'a':
                /* We have to send the initial FSK sequence */
                answer = 1;
                break;
            case 'd':
                /* Initial delay for protocol 1 */
                h.initial_delay = *++d - '0';
                break;
            case 's':
                /* We are acting as a service centre talking to a phone */
                h.smsc = 1;
                break;
            case 'r':
                /* The following apply if there is an arg3/4 and apply to the created message file */
                h.srr = 1;
                break;
            case 'o':
                h.dcs |= 4;            /* octets */
                break;
            case 'p':
                /* Select protocol 1 or 2 from the ETSI spec. */
                h.protocol = *++d;
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                /* Set the pid for saved local message */
                h.pid = 0x40 + (*d & 0xF);
                break;
            }
        }
    }

    if (argc > 2)
    {
        char *p;

        /* Submitting a message, not taking call. */
        /* Deprecated, use smsq instead. */
        d = argv[2];
        h.scts = time(NULL);
        for (p = d;  *p  &&  *p != ',';  p++)
            ;
        if (*p)
            *p++ = 0;
        if (strlen((char *) d) >= sizeof(h.oa))
        {
            opbx_log(OPBX_LOG_ERROR, "Address too long %s\n", d);
            return 0;
        }
        if (h.smsc)
            opbx_copy_string(h.oa, (char *) d, sizeof(h.oa));
        else
            opbx_copy_string(h.da, (char *) d, sizeof(h.da));
        if (!h.smsc)
            opbx_copy_string(h.oa, h.cli, sizeof(h.oa));
        d = p;
        h.udl = 0;
        while (*p  &&  h.udl < SMSLEN)
            h.ud[h.udl++] = utf8decode((uint8_t **) &p);
        if (is7bit(h.dcs)  &&  packsms7(0, h.udhl, h.udh, h.udl, h.ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 7 bit GSM data\n");
        if (is8bit(h.dcs)  &&  packsms8(0, h.udhl, h.udh, h.udl, h.ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 8 bit data\n");
        if (is16bit(h.dcs)  &&  packsms16(0, h.udhl, h.udh, h.udl, h.ud) < 0)
            opbx_log(OPBX_LOG_WARNING, "Invalid 16 bit data\n");
        h.rx = 0;                  /* sent message */
        h.mr = -1;
        sms_writefile(&h);
        LOCAL_USER_REMOVE(u);
        return 0;
    }

    if (answer)
    {
        if (h.protocol == '2')
        {
        }
        else
        {
            /* Set up SMS_EST initial message */
            tx_msg[0] = 0x80 | DLL_SMS_P1_EST;
            tx_msg[1] = 0;
            put_message(&h, tx_msg, 2);
        }
    }

    if (chan->_state != OPBX_STATE_UP)
        opbx_answer(chan);

    original_read_fmt = chan->readformat;
    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_read_format(chan, OPBX_FORMAT_SLINEAR)) < 0)
        {
            opbx_log(OPBX_LOG_WARNING, "Unable to set to linear read mode, giving up\n");
            return -1;
        }
    }
    original_write_fmt = chan->writeformat;
    if (original_write_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_write_format(chan, OPBX_FORMAT_SLINEAR)) < 0)
        {
            opbx_log(OPBX_LOG_WARNING, "Unable to set to linear write mode, giving up\n");
            if ((res = opbx_set_read_format(chan, original_read_fmt)))
                opbx_log(OPBX_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
            return -1;
        }
    }

    if (res < 0)
    {
        opbx_log(OPBX_LOG_ERROR, "Unable to set to linear mode, giving up\n");
        LOCAL_USER_REMOVE(u);
        return -1;
    }

    adsi_tx_init(&h.tx_adsi, ADSI_STANDARD_CLIP);
    adsi_tx_set_preamble(&h.tx_adsi, (h.protocol == '2')  ?  -1  :  0, -1, -1, -1);
    adsi_rx_init(&h.rx_adsi, ADSI_STANDARD_CLIP, (h.protocol == '2')  ?  put_adsi_msg_prot2  :  put_adsi_msg_prot1, &h);
    
    if (opbx_generator_activate(chan, &chan->generator, &smsgen, &h) < 0)
    {
        opbx_log(OPBX_LOG_ERROR, "Failed to activate generator on '%s'\n", chan->name);
        LOCAL_USER_REMOVE(u);
        return -1;
    }

    while (opbx_waitfor(chan, -1) > -1  &&  !h.hangup)
    {
        if ((f = opbx_read(chan)) == NULL)
            break;
        if (f->frametype == OPBX_FRAME_VOICE)
            sms_process(&h, (int16_t *) f->data, f->samples);
        opbx_fr_free(f);
    }
    if (original_read_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_read_format(chan, original_read_fmt)))
            opbx_log(OPBX_LOG_WARNING, "Unable to restore read format on '%s'\n", chan->name);
    }
    if (original_write_fmt != OPBX_FORMAT_SLINEAR)
    {
        if ((res = opbx_set_write_format(chan, original_write_fmt)))
            opbx_log(OPBX_LOG_WARNING, "Unable to restore write format on '%s'\n", chan->name);
    }

    sms_log(&h, '?');              /* log incomplete message */

    LOCAL_USER_REMOVE(u);
    return (h.err);
}

static int unload_module(void)
{
    int res = 0;

    res |= opbx_unregister_function(sms_app);
    return res;
}

static int load_module(void)
{
    if (!smsgen.is_initialized)
        opbx_object_init(&smsgen, OPBX_OBJECT_CURRENT_MODULE, OPBX_OBJECT_NO_REFS);

    snprintf(log_file, sizeof(log_file), "%s/sms", opbx_config_OPBX_LOG_DIR);
    snprintf(spool_dir, sizeof(spool_dir), "%s/sms", opbx_config_OPBX_SPOOL_DIR);
    sms_app = opbx_register_function(sms_name, sms_exec, sms_synopsis, sms_syntax, sms_descrip);
    return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, sms_synopsis)
