/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * CallerID Generation support 
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>
#include <spandsp.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL: svn://svn.openpbx.org/openpbx/trunk/corelib/callerid.c $", "$Revision: 922 $")

#include "openpbx/ulaw.h"
#include "openpbx/alaw.h"
#include "openpbx/frame.h"
#include "openpbx/channel.h"
#include "openpbx/phone_no_utils.h"
#include "openpbx/logger.h"
#include "openpbx/fskmodem.h"
#include "openpbx/utils.h"
#include "openpbx/old_callerid.h"

struct callerid_state {
	fsk_data fskd;
	char rawdata[256];
	short oldstuff[160];
	int oldlen;
	int pos;
	int type;
	int cksum;
	char name[64];
	char number[64];
	int flags;
	int sawflag;
	int len;
};


float cid_dr[4], cid_di[4];
float clidsb = 8000.0 / 1200.0;
float sasdr, sasdi;
float casdr1, casdi1, casdr2, casdi2;

#define CALLERID_SPACE	2200.0		/* 2200 hz for "0" */
#define CALLERID_MARK	1200.0		/* 1200 hz for "1" */
#define SAS_FREQ		 440.0
#define CAS_FREQ1		2130.0
#define CAS_FREQ2		2750.0

static inline void gen_tones(unsigned char *buf, int len, int codec, float ddr1, float ddi1, float ddr2, float ddi2, float *cr1, float *ci1, float *cr2, float *ci2)
{
	int x;
	float t;
	for (x=0;x<len;x++) {
		t = *cr1 * ddr1 - *ci1 * ddi1;
		*ci1 = *cr1 * ddi1 + *ci1 * ddr1;
		*cr1 = t;
		t = 2.0 - (*cr1 * *cr1 + *ci1 * *ci1);
		*cr1 *= t;
		*ci1 *= t; 	

		t = *cr2 * ddr2 - *ci2 * ddi2;
		*ci2 = *cr2 * ddi2 + *ci2 * ddr2;
		*cr2 = t;
		t = 2.0 - (*cr2 * *cr2 + *ci2 * *ci2);
		*cr2 *= t;
		*ci2 *= t; 	
		buf[x] = OPBX_LIN2X((*cr1 + *cr2) * 2048.0);
	}
}

static inline void gen_tone(unsigned char *buf, int len, int codec, float ddr1, float ddi1, float *cr1, float *ci1)
{
	int x;
	float t;
	for (x=0;x<len;x++) {
		t = *cr1 * ddr1 - *ci1 * ddi1;
		*ci1 = *cr1 * ddi1 + *ci1 * ddr1;
		*cr1 = t;
		t = 2.0 - (*cr1 * *cr1 + *ci1 * *ci1);
		*cr1 *= t;
		*ci1 *= t; 	
		buf[x] = OPBX_LIN2X(*cr1 * 8192.0);
	}
}

int opbx_gen_cas(unsigned char *outbuf, int sendsas, int len, int codec)
{
	int pos = 0;
	int saslen=2400;
	float cr1 = 1.0;
	float ci1 = 0.0;
	float cr2 = 1.0;
	float ci2 = 0.0;
	if (sendsas) {
		if (len < saslen)
			return -1;
		gen_tone(outbuf, saslen, codec, sasdr, sasdi, &cr1, &ci1);
		len -= saslen;
		pos += saslen;
		cr2 = cr1;
		ci2 = ci1;
	}
	gen_tones(outbuf + pos, len, codec, casdr1, casdi1, casdr2, casdi2, &cr1, &ci1, &cr2, &ci2);
	return 0;
}

void callerid_init(void)
{
	/* Initialize stuff for inverse FFT */
	cid_dr[0] = cos(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	cid_di[0] = sin(CALLERID_SPACE * 2.0 * M_PI / 8000.0);
	cid_dr[1] = cos(CALLERID_MARK * 2.0 * M_PI / 8000.0);
	cid_di[1] = sin(CALLERID_MARK * 2.0 * M_PI / 8000.0);
	sasdr = cos(SAS_FREQ * 2.0 * M_PI / 8000.0);
	sasdi = sin(SAS_FREQ * 2.0 * M_PI / 8000.0);
	casdr1 = cos(CAS_FREQ1 * 2.0 * M_PI / 8000.0);
	casdi1 = sin(CAS_FREQ1 * 2.0 * M_PI / 8000.0);
	casdr2 = cos(CAS_FREQ2 * 2.0 * M_PI / 8000.0);
	casdi2 = sin(CAS_FREQ2 * 2.0 * M_PI / 8000.0);
}

struct callerid_state *callerid_new(int cid_signalling)
{
	struct callerid_state *cid;
	cid = malloc(sizeof(struct callerid_state));
	if (cid) {
		memset(cid, 0, sizeof(struct callerid_state));
		cid->fskd.spb = 7;		/* 1200 baud */
		cid->fskd.hdlc = 0;		/* Async */
		cid->fskd.nbit = 8;		/* 8 bits */
		cid->fskd.nstop = 1;	/* 1 stop bit */
		cid->fskd.paridad = 0;	/* No parity */
		cid->fskd.bw=1;			/* Filter 800 Hz */
		if (cid_signalling == 2) { /* v23 signalling */
			cid->fskd.f_mark_idx =  4;	/* 1300 Hz */
			cid->fskd.f_space_idx = 5;	/* 2100 Hz */
		} else { /* Bell 202 signalling as default */ 
			cid->fskd.f_mark_idx =  2;	/* 1200 Hz */
			cid->fskd.f_space_idx = 3;	/* 2200 Hz */
		}
		cid->fskd.pcola = 0;		/* No clue */
		cid->fskd.cont = 0;			/* Digital PLL reset */
		cid->fskd.x0 = 0.0;
		cid->fskd.state = 0;
		memset(cid->name, 0, sizeof(cid->name));
		memset(cid->number, 0, sizeof(cid->number));
		cid->flags = CID_UNKNOWN_NAME | CID_UNKNOWN_NUMBER;
		cid->pos = 0;
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return cid;
}

void callerid_get(struct callerid_state *cid, char **name, char **number, int *flags)
{
	*flags = cid->flags;
	if (cid->flags & (CID_UNKNOWN_NAME | CID_PRIVATE_NUMBER))
		*name = NULL;
	else
		*name = cid->name;
	if (cid->flags & (CID_UNKNOWN_NUMBER | CID_PRIVATE_NUMBER))
		*number = NULL;
	else
		*number = cid->number;
}

void callerid_get_dtmf(char *cidstring, char *number, int *flags)
{
	int i;
	int code;

	/* "Clear" the number-buffer. */
	number[0] = 0;

	if (strlen(cidstring) < 2) {
		opbx_log(LOG_DEBUG, "No cid detected\n");
		*flags = CID_UNKNOWN_NUMBER;
		return;
	}
	
	/* Detect protocol and special types */
	if (cidstring[0] == 'B') {
		/* Handle special codes */
		code = atoi(&cidstring[1]);
		if (code == 0)
			*flags = CID_UNKNOWN_NUMBER;
		else if (code == 10) 
			*flags = CID_PRIVATE_NUMBER;
		else
			opbx_log(LOG_DEBUG, "Unknown DTMF code %d\n", code);
	} else if (cidstring[0] == 'D' && cidstring[2] == '#') {
		/* .DK special code */
		if (cidstring[1] == '1')
			*flags = CID_PRIVATE_NUMBER;
		if (cidstring[1] == '2' || cidstring[1] == '3')
			*flags = CID_UNKNOWN_NUMBER;
	} else if (cidstring[0] == 'D' || cidstring[0] == 'A') {
		/* "Standard" callerid */
		for (i = 1; i < strlen(cidstring); i++ ) {
			if (cidstring[i] == 'C' || cidstring[i] == '#')
				break;
			if (isdigit(cidstring[i]))
				number[i-1] = cidstring[i];
			else
				opbx_log(LOG_DEBUG, "Unknown CID digit '%c'\n",
					cidstring[i]);
		}
		number[i-1] = 0;
	} else if (isdigit(cidstring[0])) {
		/* It begins with a digit, so we parse it as a number and hope
		 * for the best */
		opbx_log(LOG_WARNING, "Couldn't detect start-character. CID "
			"parsing might be unreliable\n");
		for (i = 0; i < strlen(cidstring); i++) {
			if (isdigit(cidstring[i]))
                                number[i] = cidstring[i];
			else
				break;
		}
		number[i] = 0;
	} else {
		opbx_log(LOG_DEBUG, "Unknown CID protocol, start digit '%c'\n", 
			cidstring[0]);
		*flags = CID_UNKNOWN_NUMBER;
	}
}

int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len, int codec)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int res;
	int x;
	short *buf = malloc(2 * len + cid->oldlen);
	short *obuf = buf;
	if (!buf) {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(buf, 0, 2 * len + cid->oldlen);
	memcpy(buf, cid->oldstuff, cid->oldlen);
	mylen += cid->oldlen/2;
	for (x=0;x<len;x++) 
		buf[x+cid->oldlen/2] = OPBX_XLAW(ubuf[x]);
	while(mylen >= 160) {
		olen = mylen;
		res = fsk_serie(&cid->fskd, buf, &mylen, &b);
		if (mylen < 0) {
			opbx_log(LOG_ERROR, "fsk_serie made mylen < 0 (%d)\n", mylen);
			return -1;
		}
		buf += (olen - mylen);
		if (res < 0) {
			opbx_log(LOG_NOTICE, "fsk_serie failed\n");
			return -1;
		}
		if (res == 1) {
			/* Ignore invalid bytes */
			if (b > 0xff)
				continue;
			switch(cid->sawflag) {
			case 0: /* Look for flag */
				if (b == 'U')
					cid->sawflag = 2;
				break;
			case 2: /* Get lead-in */
				if ((b == 0x04) || (b == 0x80)) {
					cid->type = b;
					cid->sawflag = 3;
					cid->cksum = b;
				}
				break;
			case 3:	/* Get length */
				/* Not a lead in.  We're ready  */
				cid->sawflag = 4;
				cid->len = b;
				cid->pos = 0;
				cid->cksum += b;
				break;
			case 4: /* Retrieve message */
				if (cid->pos >= 128) {
					opbx_log(LOG_WARNING, "Caller ID too long???\n");
					return -1;
				}
				cid->rawdata[cid->pos++] = b;
				cid->len--;
				cid->cksum += b;
				if (!cid->len) {
					cid->rawdata[cid->pos] = '\0';
					cid->sawflag = 5;
				}
				break;
			case 5: /* Check checksum */
				if (b != (256 - (cid->cksum & 0xff))) {
					opbx_log(LOG_NOTICE, "Caller*ID failed checksum\n");
					/* Try again */
					cid->sawflag = 0;
					break;
				}
		
				cid->number[0] = '\0';
				cid->name[0] = '\0';
				/* If we get this far we're fine.  */
				if (cid->type == 0x80) {
					/* MDMF */
					/* Go through each element and process */
					for (x=0;x< cid->pos;) {
						switch(cid->rawdata[x++]) {
						case 1:
							/* Date */
							break;
						case 2: /* Number */
						case 3: /* Number (for Zebble) */
						case 4: /* Number */
							res = cid->rawdata[x];
							if (res > 32) {
								opbx_log(LOG_NOTICE, "Truncating long caller ID number from %d bytes to 32\n", cid->rawdata[x]);
								res = 32; 
							}
							if (opbx_strlen_zero(cid->number)) {
								memcpy(cid->number, cid->rawdata + x + 1, res);
								/* Null terminate */
								cid->number[res] = '\0';
							}
							break;
						case 6: /* Stentor Call Qualifier (ie. Long Distance call) */
							break;
						case 7: /* Name */
						case 8: /* Name */
							res = cid->rawdata[x];
							if (res > 32) {
								opbx_log(LOG_NOTICE, "Truncating long caller ID name from %d bytes to 32\n", cid->rawdata[x]);
								res = 32; 
							}
							memcpy(cid->name, cid->rawdata + x + 1, res);
							cid->name[res] = '\0';
							break;
						case 17: /* UK: Call type, 1=Voice Call, 2=Ringback when free, 129=Message waiting  */
						case 19: /* UK: Network message system status (Number of messages waiting) */
						case 22: /* Something French */
							break;
						default:
							opbx_log(LOG_NOTICE, "Unknown IE %d\n", cid->rawdata[x-1]);
						}
						x += cid->rawdata[x];
						x++;
					}
				} else {
					/* SDMF */
					opbx_copy_string(cid->number, cid->rawdata + 8, sizeof(cid->number));
				}
				/* Update flags */
				cid->flags = 0;
				if (!strcmp(cid->number, "P")) {
					strcpy(cid->number, "");
					cid->flags |= CID_PRIVATE_NUMBER;
				} else if (!strcmp(cid->number, "O") || opbx_strlen_zero(cid->number)) {
					strcpy(cid->number, "");
					cid->flags |= CID_UNKNOWN_NUMBER;
				}
				if (!strcmp(cid->name, "P")) {
					strcpy(cid->name, "");
					cid->flags |= CID_PRIVATE_NAME;
				} else if (!strcmp(cid->name, "O") || opbx_strlen_zero(cid->name)) {
					strcpy(cid->name, "");
					cid->flags |= CID_UNKNOWN_NAME;
				}
				return 1;
				break;
			default:
				opbx_log(LOG_ERROR, "Dunno what to do with a digit in sawflag %d\n", cid->sawflag);
			}
		}
	}
	if (mylen) {
		memcpy(cid->oldstuff, buf, mylen * 2);
		cid->oldlen = mylen * 2;
	} else
		cid->oldlen = 0;
	free(obuf);
	return 0;
}

void callerid_free(struct callerid_state *cid)
{
	free(cid);
}

static int callerid_genmsg(char *msg, int size, char *number, char *name, int flags)
{
	time_t t;
	struct tm tm;
	char *ptr;
	int res;
	int i,x;
	/* Get the time */
	time(&t);
	localtime_r(&t,&tm);
	
	ptr = msg;
	
	/* Format time and message header */
	res = snprintf(ptr, size, "\001\010%02d%02d%02d%02d", tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour, tm.tm_min);
	size -= res;
	ptr += res;
	if (!number || opbx_strlen_zero(number) || (flags & CID_UNKNOWN_NUMBER)) {
		/* Indicate number not known */
		res = snprintf(ptr, size, "\004\001O");
		size -= res;
		ptr += res;
	} else if (flags & CID_PRIVATE_NUMBER) {
		/* Indicate number is private */
		res = snprintf(ptr, size, "\004\001P");
		size -= res;
		ptr += res;
	} else {
		/* Send up to 16 digits of number MAX */
		i = strlen(number);
		if (i > 16) i = 16;
		res = snprintf(ptr, size, "\002%c", i);
		size -= res;
		ptr += res;
		for (x=0;x<i;x++)
			ptr[x] = number[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}

	if (!name || opbx_strlen_zero(name) || (flags & CID_UNKNOWN_NAME)) {
		/* Indicate name not known */
		res = snprintf(ptr, size, "\010\001O");
		size -= res;
		ptr += res;
	} else if (flags & CID_PRIVATE_NAME) {
		/* Indicate name is private */
		res = snprintf(ptr, size, "\010\001P");
		size -= res;
		ptr += res;
	} else {
		/* Send up to 16 digits of name MAX */
		i = strlen(name);
		if (i > 16) i = 16;
		res = snprintf(ptr, size, "\007%c", i);
		size -= res;
		ptr += res;
		for (x=0;x<i;x++)
			ptr[x] = name[x];
		ptr[i] = '\0';
		ptr += i;
		size -= i;
	}
	return (ptr - msg);
	
}

int vmwi_generate(unsigned char *buf, int active, int mdmf, int codec)
{
	unsigned char msg[256];
	int len=0;
	int sum;
	int x;
	int bytes = 0;
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	if (mdmf) {
		/* MDMF Message waiting */
		msg[len++] = 0x82;
		/* Length is 3 */
		msg[len++] = 3;
		/* IE is "Message Waiting Parameter" */
		msg[len++] = 0xb;
		/* Length of IE is one */
		msg[len++] = 1;
		/* Active or not */
		if (active)
			msg[len++] = 0xff;
		else
			msg[len++] = 0x00;
	} else {
		/* SDMF Message waiting */
		msg[len++] = 0x6;
		/* Length is 3 */
		msg[len++] = 3;
		if (active) {
			msg[len++] = 0x42;
			msg[len++] = 0x42;
			msg[len++] = 0x42;
		} else {
			msg[len++] = 0x6f;
			msg[len++] = 0x6f;
			msg[len++] = 0x6f;
		}
	}
	sum = 0;
	for (x=0;x<len;x++)
		sum += msg[x];
	sum = (256 - (sum & 255));
	msg[len++] = sum;
	/* Wait a half a second */
	for (x=0;x<4000;x++)
		PUT_BYTE(0x7f);
	/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
	for (x=0;x<30;x++)
		PUT_CLID(0x55);
	/* Send 170ms of callerid marks */
	for (x=0;x<170;x++)
		PUT_CLID_MARKMS;
	for (x=0;x<len;x++) {
		PUT_CLID(msg[x]);
	}
	/* Send 50 more ms of marks */
	for (x=0;x<50;x++)
		PUT_CLID_MARKMS;
	return bytes;
}

int callerid_generate(unsigned char *buf, char *number, char *name, int flags, int callwaiting, int codec)
{
	int bytes=0;
	int x, sum;
	int len;
	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;
	char msg[256];
	len = callerid_genmsg(msg, sizeof(msg), number, name, flags);
	if (!callwaiting) {
		/* Wait a half a second */
		for (x=0;x<4000;x++)
			PUT_BYTE(0x7f);
		/* Transmit 30 0x55's (looks like a square wave) for channel seizure */
		for (x=0;x<30;x++)
			PUT_CLID(0x55);
	}
	/* Send 150ms of callerid marks */
	for (x=0;x<150;x++)
		PUT_CLID_MARKMS;
	/* Send 0x80 indicating MDMF format */
	PUT_CLID(0x80);
	/* Put length of whole message */
	PUT_CLID(len);
	sum = 0x80 + strlen(msg);
	/* Put each character of message and update checksum */
	for (x=0;x<len; x++) {
		PUT_CLID(msg[x]);
		sum += msg[x];
	}
	/* Send 2's compliment of sum */
	PUT_CLID(256 - (sum & 255));

	/* Send 50 more ms of marks */
	for (x=0;x<50;x++)
		PUT_CLID_MARKMS;
	
	return bytes;
}

static int __opbx_callerid_generate(unsigned char *buf, char *name, char *number, int callwaiting, int codec)
{
	if (name && opbx_strlen_zero(name))
		name = NULL;
	if (number && opbx_strlen_zero(number))
		number = NULL;
	return callerid_generate(buf, number, name, 0, callwaiting, codec);
}

int opbx_callerid_generate(unsigned char *buf, char *name, char *number, int codec)
{
	return __opbx_callerid_generate(buf, name, number, 0, codec);
}

int opbx_callerid_callwaiting_generate(unsigned char *buf, char *name, char *number, int codec)
{
	return __opbx_callerid_generate(buf, name, number, 1, codec);
}

int tdd_decode_baudot(struct tdd_state *tdd,unsigned char data)	/* covert baudot into ASCII */
{
	static char ltrs[32]={'<','E','\n','A',' ','S','I','U',
				'\n','D','R','J','N','F','C','K',
				'T','Z','L','W','H','Y','P','Q',
				'O','B','G','^','M','X','V','^'};
	static char figs[32]={'<','3','\n','-',' ',',','8','7',
				'\n','$','4','\'',',','·',':','(',
				'5','+',')','2','·','6','0','1',
				'9','7','·','^','.','/','=','^'};
	int d;
	d=0;  /* return 0 if not decodeable */
	switch (data) {
	case 0x1f :	tdd->modo=0; break;
	case 0x1b : tdd->modo=1; break;
	default:	if (tdd->modo==0) d=ltrs[data]; else d=figs[data]; break;
	}
	return d;
}

struct tdd_state *tdd_new(void)
{
	struct tdd_state *tdd;
	tdd = malloc(sizeof(struct tdd_state));
	if (tdd) {
		memset(tdd, 0, sizeof(struct tdd_state));
		tdd->fskd.spb = 176;		/* 45.5 baud */
		tdd->fskd.hdlc = 0;		/* Async */
		tdd->fskd.nbit = 5;		/* 5 bits */
		tdd->fskd.nstop = 1.5;	/* 1.5 stop bits */
		tdd->fskd.paridad = 0;	/* No parity */
		tdd->fskd.bw=0;			/* Filter 75 Hz */
		tdd->fskd.f_mark_idx =  0;	/* 1400 Hz */
		tdd->fskd.f_space_idx = 1;	/* 1800 Hz */
		tdd->fskd.pcola = 0;		/* No clue */
		tdd->fskd.cont = 0;			/* Digital PLL reset */
		tdd->fskd.x0 = 0.0;
		tdd->fskd.state = 0;
		tdd->pos = 0;
		tdd->mode = 2;
	} else
		opbx_log(LOG_WARNING, "Out of memory\n");
	return tdd;
}

int opbx_tdd_gen_ecdisa(unsigned char *outbuf, int len)
{
    /* Generated from frequency 2100 by gentone.  80 samples  */
    static const unsigned char ecdisa[80] = {
    	255, 143,  58,  16, 171, 146,  34,  20, 
    	156, 151,  25,  26, 149, 159,  19,  38, 
    	145, 177,  16,  73, 143,  73,  16, 177, 
    	145,  38,  19, 159, 149,  26,  25, 151, 
    	156,  20,  34, 146, 171,  16,  58, 143, 
    	255,  15, 186, 144,  43,  18, 162, 148, 
    	 28,  23, 153, 154,  21,  31, 147, 166, 
    	 17,  49, 144, 201,  15, 201, 144,  49, 
    	 17, 166, 147,  31,  21, 154, 153,  23, 
    	 28, 148, 162,  18,  43, 144, 186,  15, 	
    };
	int pos = 0;
	int cnt;

	while(len) {
		cnt = len;
		if (cnt > sizeof(ecdisa))
			cnt = sizeof(ecdisa);
		memcpy(outbuf + pos, ecdisa, cnt);
		pos += cnt;
		len -= cnt;
	}
	return 0;
}

#define PUT_BYTE(a) do { \
	*(buf++) = (a); \
	bytes++; \
} while(0)

#define PUT_TDD_AUDIO_SAMPLE(y) do { \
	int index = (short)(rint(8192.0 * (y))); \
	*(buf++) = OPBX_LIN2MU(index); \
	bytes++; \
} while(0)
	
#define PUT_TDD_MARKMS do { \
	int x; \
	for (x=0;x<8;x++) \
		PUT_TDD_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, 1)); \
} while(0)

#define PUT_TDD_BAUD(bit) do { \
	while(scont < tddsb) { \
		PUT_TDD_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, bit)); \
		scont += 1.0; \
	} \
	scont -= tddsb; \
} while(0)

#define PUT_TDD_STOP do { \
	while(scont < (tddsb * 1.5)) { \
		PUT_TDD_AUDIO_SAMPLE(tdd_getcarrier(&cr, &ci, 1)); \
		scont += 1.0; \
	} \
	scont -= (tddsb * 1.5); \
} while(0)


#define PUT_TDD(byte) do { \
	int z; \
	unsigned char b = (byte); \
	PUT_TDD_BAUD(0); 	/* Start bit */ \
	for (z=0;z<5;z++) { \
		PUT_TDD_BAUD(b & 1); \
		b >>= 1; \
	} \
	PUT_TDD_STOP;	/* Stop bit */ \
} while(0);	

int tdd_generate(struct tdd_state *tdd, unsigned char *buf, const char *str)
{
	int bytes=0;
	int i,x;
	char	c;
	static unsigned char lstr[31] = "\000E\nA SIU\rDRJNFCKTZLWHYPQOBG\000MXV";
	static unsigned char fstr[31] = "\0003\n- \00787\r$4',!:(5\")2\0006019?&\000./;";
	/* Initial carriers (real/imaginary) */
	float cr = 1.0;
	float ci = 0.0;
	float scont = 0.0;

	for(x = 0; str[x]; x++) {
		c = toupper(str[x]);
#if	0
		printf("%c",c); fflush(stdout);
#endif
		if (c == 0) /* send null */
		   {
			PUT_TDD(0);
			continue;
		   }
		if (c == '\r') /* send c/r */
		   {
			PUT_TDD(8);
			continue;
		   }
		if (c == '\n') /* send c/r and l/f */
		   {
			PUT_TDD(8);
			PUT_TDD(2);
			continue;
		   }
		if (c == ' ') /* send space */
		   {
			PUT_TDD(4);
			continue;
		   }
		for(i = 0; i < 31; i++)
		   {
			if (lstr[i] == c) break;
		   }
		if (i < 31) /* if we found it */
		   {
			if (tdd->mode)  /* if in figs mode, change it */
			   { 
				PUT_TDD(31); /* Send LTRS */
				tdd->mode = 0;
			   }
			PUT_TDD(i);
			continue;
		   }
		for(i = 0; i < 31; i++)
		   {
			if (fstr[i] == c) break;
		   }
		if (i < 31) /* if we found it */
		   {
			if (tdd->mode != 1)  /* if in ltrs mode, change it */
			   {
				PUT_TDD(27); /* send FIGS */
				tdd->mode = 1;
			   }
			PUT_TDD(i);  /* send byte */
			continue;
		   }
	   }
	return bytes;
}

int tdd_feed(struct tdd_state *tdd, unsigned char *ubuf, int len)
{
	int mylen = len;
	int olen;
	int b = 'X';
	int res;
	int c,x;
	short *buf = malloc(2 * len + tdd->oldlen);
	short *obuf = buf;
	if (!buf) {
		opbx_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	memset(buf, 0, 2 * len + tdd->oldlen);
	memcpy(buf, tdd->oldstuff, tdd->oldlen);
	mylen += tdd->oldlen/2;
	for (x=0;x<len;x++) 
		buf[x+tdd->oldlen/2] = OPBX_MULAW(ubuf[x]);
	c = res = 0;
	while(mylen >= 1320) { /* has to have enough to work on */
		olen = mylen;
		res = fsk_serie(&tdd->fskd, buf, &mylen, &b);
		if (mylen < 0) {
			opbx_log(LOG_ERROR, "fsk_serie made mylen < 0 (%d) (olen was %d)\n", mylen,olen);
			free(obuf);
			return -1;
		}
		buf += (olen - mylen);
		if (res < 0) {
			opbx_log(LOG_NOTICE, "fsk_serie failed\n");
			free(obuf);
			return -1;
		}
		if (res == 1) {
			/* Ignore invalid bytes */
			if (b > 0x7f)
				continue;
			c = tdd_decode_baudot(tdd,b);
			if ((c < 1) || (c > 126)) continue; /* if not valid */
			break;
		}
	}
	if (mylen) {
		memcpy(tdd->oldstuff, buf, mylen * 2);
		tdd->oldlen = mylen * 2;
	} else
		tdd->oldlen = 0;
	free(obuf);
	if (res)  {
		tdd->mode = 2; /* put it in mode where it
			reliably puts teleprinter in correct shift mode */
		return(c);
	}
	return 0;
}

void tdd_free(struct tdd_state *tdd)
{
	free(tdd);
}

void tdd_init(void)
{
	/* Initialize stuff for inverse FFT */
	dr[0] = cos(TDD_SPACE*2.0*M_PI/8000.0);
	di[0] = sin(TDD_SPACE*2.0*M_PI/8000.0);
	dr[1] = cos(TDD_MARK*2.0*M_PI/8000.0);
	di[1] = sin(TDD_MARK*2.0*M_PI/8000.0);
}
