/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Modified from app_zapbarge by David Troy <dave@toad.net>
 *
 * Special thanks to comphealth.com for sponsoring this
 * GPL application.
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
 * Zap Scanner
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/zaptel.h>
#else
#include <zaptel.h>
#endif /* __linux__ */

#include "openpbx.h"

OPENPBX_FILE_VERSION(__FILE__, "$Revision$")

#include "openpbx/lock.h"
#include "openpbx/file.h"
#include "openpbx/logger.h"
#include "openpbx/channel.h"
#include "openpbx/pbx.h"
#include "openpbx/module.h"
#include "openpbx/config.h"
#include "openpbx/app.h"
#include "openpbx/options.h"
#include "openpbx/utils.h"
#include "openpbx/cli.h"
#include "openpbx/say.h"

static char *tdesc = "Scan Zap channels application";

static char *app = "ZapScan";

static char *synopsis = "Scan Zap channels to monitor calls";

static char *descrip =
"  ZapScan([group]) allows a call center manager to monitor Zap channels in\n"
"a convenient way.  Use '#' to select the next channel and use '*' to exit\n"
"Limit scanning to a channel GROUP by setting the option group argument.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;


#define CONF_SIZE 160

static struct opbx_channel *get_zap_channel_locked(int num) {
	char name[80];
	
	snprintf(name,sizeof(name),"Zap/%d-1",num);
	return opbx_get_channel_by_name_locked(name);
}

static int careful_write(int fd, unsigned char *data, int len)
{
	int res;
	while(len) {
		res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				opbx_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else
				return 0;
		}
		len -= res;
		data += res;
	}
	return 0;
}

static int conf_run(struct opbx_channel *chan, int confno, int confflags)
{
	int fd;
	struct zt_confinfo ztc;
	struct opbx_frame *f;
	struct opbx_channel *c;
	struct opbx_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int flags;
	int retryzap;
	int origfd;
	int ret = -1;
	char input[4];
	int ic=0;
	
	ZT_BUFFERINFO bi;
	char __buf[CONF_SIZE + OPBX_FRIENDLY_OFFSET];
	char *buf = __buf + OPBX_FRIENDLY_OFFSET;
	
	/* Set it into U-law mode (write) */
	if (opbx_set_write_format(chan, OPBX_FORMAT_ULAW) < 0) {
		opbx_log(LOG_WARNING, "Unable to set '%s' to write ulaw mode\n", chan->name);
		goto outrun;
	}
	
	/* Set it into U-law mode (read) */
	if (opbx_set_read_format(chan, OPBX_FORMAT_ULAW) < 0) {
		opbx_log(LOG_WARNING, "Unable to set '%s' to read ulaw mode\n", chan->name);
		goto outrun;
	}
	opbx_indicate(chan, -1);
	retryzap = strcasecmp(chan->type, "Zap");
 zapretry:
	origfd = chan->fds[0];
	if (retryzap) {
		fd = open("/dev/zap/pseudo", O_RDWR);
		if (fd < 0) {
			opbx_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		/* Make non-blocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
			opbx_log(LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
			close(fd);
                        goto outrun;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
			opbx_log(LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE;
		bi.txbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.rxbufpolicy = ZT_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		if (ioctl(fd, ZT_SET_BUFINFO, &bi)) {
			opbx_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
                nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = chan->fds[0];
		nfds = 0;
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Check to see if we're in a conference... */
        ztc.chan = 0;
        if (ioctl(fd, ZT_GETCONF, &ztc)) {
			opbx_log(LOG_WARNING, "Error getting conference\n");
			close(fd);
			goto outrun;
        }
        if (ztc.confmode) {
			/* Whoa, already in a conference...  Retry... */
			if (!retryzap) {
				opbx_log(LOG_DEBUG, "Zap channel is in a conference already, retrying with pseudo\n");
				retryzap = 1;
				goto zapretry;
			}
        }
        memset(&ztc, 0, sizeof(ztc));
        /* Add us to the conference */
        ztc.chan = 0;
        ztc.confno = confno;
        ztc.confmode = ZT_CONF_MONITORBOTH;
		
        if (ioctl(fd, ZT_SETCONF, &ztc)) {
                opbx_log(LOG_WARNING, "Error setting conference\n");
                close(fd);
                goto outrun;
        }
        opbx_log(LOG_DEBUG, "Placed channel %s in ZAP channel %d monitor\n", chan->name, confno);
		
        for(;;) {
			outfd = -1;
			ms = -1;
			c = opbx_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
			if (c) {
				if (c->fds[0] != origfd) {
					if (retryzap) {
						/* Kill old pseudo */
						close(fd);
					}
					opbx_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
					retryzap = 0;
                                goto zapretry;
				}
				f = opbx_read(c);
				if (!f)
					break;
				if(f->frametype == OPBX_FRAME_DTMF) {
					if(f->subclass == '#') {
						ret = 0;
						break;
					}
					else if (f->subclass == '*') {
						ret = -1;
						break;
						
					}
					else {
						input[ic++] = f->subclass;
					}
					if(ic == 3) {
						input[ic++] = '\0';
						ic=0;
						ret = atoi(input);
						opbx_verbose(VERBOSE_PREFIX_3 "Zapscan: change channel to %d\n",ret);
						break;
					}
				}
				
				if (fd != chan->fds[0]) {
					if (f->frametype == OPBX_FRAME_VOICE) {
						if (f->subclass == OPBX_FORMAT_ULAW) {
							/* Carefully write */
                                                careful_write(fd, f->data, f->datalen);
						} else
							opbx_log(LOG_WARNING, "Huh?  Got a non-ulaw (%d) frame in the conference\n", f->subclass);
					}
				}
				opbx_frfree(f);
			} else if (outfd > -1) {
				res = read(outfd, buf, CONF_SIZE);
				if (res > 0) {
					memset(&fr, 0, sizeof(fr));
					fr.frametype = OPBX_FRAME_VOICE;
					fr.subclass = OPBX_FORMAT_ULAW;
					fr.datalen = res;
					fr.samples = res;
					fr.data = buf;
					fr.offset = OPBX_FRIENDLY_OFFSET;
					if (opbx_write(chan, &fr) < 0) {
						opbx_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
						/* break; */
					}
				} else
					opbx_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
			}
        }
        if (fd != chan->fds[0])
			close(fd);
        else {
			/* Take out of conference */
			/* Add us to the conference */
			ztc.chan = 0;
			ztc.confno = 0;
			ztc.confmode = 0;
			if (ioctl(fd, ZT_SETCONF, &ztc)) {
				opbx_log(LOG_WARNING, "Error setting conference\n");
                }
        }
		
 outrun:
		
        return ret;
}

static int conf_exec(struct opbx_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	int confflags = 0;
	int confno = 0;
	char confstr[80] = "", *tmp = NULL;
	struct opbx_channel *tempchan = NULL, *lastchan = NULL,*ichan = NULL;
	struct opbx_frame *f;
	char *mygroup;
	char *desired_group;
	int input=0,search_group=0;
	
	LOCAL_USER_ADD(u);
	
	if (chan->_state != OPBX_STATE_UP)
		opbx_answer(chan);
	
	if((desired_group = opbx_strdupa((char *) data)) && !opbx_strlen_zero(desired_group)) {
		opbx_verbose(VERBOSE_PREFIX_3 "Scanning for group %s\n", desired_group);
		search_group = 1;
	}

	for (;;) {
		if (opbx_waitfor(chan, 100) < 0)
			break;
		
		f = opbx_read(chan);
		if (!f)
			break;
		if ((f->frametype == OPBX_FRAME_DTMF) && (f->subclass == '*')) {
			opbx_frfree(f);
			break;
		}
		opbx_frfree(f);
		ichan = NULL;
		if(input) {
			ichan = get_zap_channel_locked(input);
			input = 0;
		}
		
		tempchan = ichan ? ichan : opbx_channel_walk_locked(tempchan);
		
		if ( !tempchan && !lastchan )
			break;
		
		if (tempchan && search_group) {
			if((mygroup = pbx_builtin_getvar_helper(tempchan, "GROUP")) && (!strcmp(mygroup, desired_group))) {
				opbx_verbose(VERBOSE_PREFIX_3 "Found Matching Channel %s in group %s\n", tempchan->name, desired_group);
			} else {
				opbx_mutex_unlock(&tempchan->lock);
				lastchan = tempchan;
				continue;
			}
		}
		if ( tempchan && tempchan->type && (!strcmp(tempchan->type, "Zap")) && (tempchan != chan) ) {
			opbx_verbose(VERBOSE_PREFIX_3 "Zap channel %s is in-use, monitoring...\n", tempchan->name);
			opbx_copy_string(confstr, tempchan->name, sizeof(confstr));
			opbx_mutex_unlock(&tempchan->lock);
			if ((tmp = strchr(confstr,'-'))) {
				*tmp = '\0';
			}
			confno = atoi(strchr(confstr,'/') + 1);
			opbx_stopstream(chan);
			opbx_say_number(chan, confno, OPBX_DIGIT_ANY, chan->language, (char *) NULL);
			res = conf_run(chan, confno, confflags);
			if (res<0) break;
			input = res;
		} else if (tempchan)
			opbx_mutex_unlock(&tempchan->lock);
		lastchan = tempchan;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return opbx_unregister_application(app);
}

int load_module(void)
{
	return opbx_register_application(app, conf_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return OPENPBX_GPL_KEY;
}

