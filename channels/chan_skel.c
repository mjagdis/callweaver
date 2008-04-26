/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<You Email Here>>
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
 * \brief Skeleton channel
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/module.h"
#include "callweaver/channel.h"


static const char desc[] = "Skeleton Channel";
static const char type[] = "Skeleton";
static const char tdesc[] = "Skeleton Channel Driver";


static struct cw_channel *skel_request(const char *type, int format, void *data, int *cause);
static int skel_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);

static int skel_call(struct cw_channel *ast, char *dest, int timeout);
static int skel_answer(struct cw_channel *ast);
static int skel_hangup(struct cw_channel *ast);

static struct cw_frame *skel_read(struct cw_channel *ast);
static struct cw_frame *skel_exception(struct cw_channel *ast);
static int skel_write(struct cw_channel *ast, struct cw_frame *f);

static int skel_indicate(struct cw_channel *ast, int condition);

static int skel_digit(struct cw_channel *ast, char digit);
static int skel_sendtext(struct cw_channel *chan, const char *text);
static int skel_sendhtml(struct cw_channel *ast, int subclass, const char *data, int datalen);
static int skel_sendimage(struct cw_channel *chan, struct cw_frame *frame);


static struct cw_channel *skel_request(const char *type, int format, void *data, int *cause)
{
	return NULL;
}


/*! Fix up a channel.
 *
 * \param oldchan	Old channel
 * \param newchan	New channel
 */
static int skel_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	return 0;
}


/*! Connect a channel to given destination or timeout.
 *
 * \param chan		Channel to use
 * \param dest		Destination to connect to
 * \param timeout	Number of seconds to give in after
 */
static int skel_call(struct cw_channel *chan, char *dest, int timeout)
{
	return -1;
}


/*! Answer an incoming call
 *
 * \param chan		Channel to use
 */
static int skel_answer(struct cw_channel *chan)
{
	return -1;
}


/*! Hangup a channel.
 *
 * \param chan		Channel to hangup
 */
static int skel_hangup(struct cw_channel *chan)
{
	return 0;
}


/*! Read a frame from a channel.
 *
 * \param chan		Channel to read from
 *
 * \return Frame read
 */
static struct cw_frame  *skel_read(struct cw_channel *chan)
{
	static struct cw_frame null = { CW_FRAME_NULL, };

	return &null;
}


static struct cw_frame  *skel_exception(struct cw_channel *chan)
{
	static struct cw_frame null = { CW_FRAME_NULL, };

	return &null;
}


/*! Write a frame to a channel.
 *
 * \param chan		Channel to write to
 * \param frame		Frame to write
 */
static int skel_write(struct cw_channel *chan, struct cw_frame *frame)
{
	return -1;
}


/*! Indicate a condition (e.g. BUSY, RINGING, CONGESTION).
 *
 * \param chan		Channel to provide indication on
 * \param condition	Condition to be indicated
 */
static int skel_indicate(struct cw_channel *chan, int condition)
{
	return -1;
}


/*! Send a DTMF digit.
 *
 * \param chan		Channel to use
 * \param digit		Digit to send
 */
static int skel_digit(struct cw_channel *chan, char digit)
{
	return -1;
}


/*! Display or send text.
 *
 * \param chan		Channel to send to
 * \param text		Text to send
 */
static int skel_sendtext(struct cw_channel *chan, const char *text)
{
	return -1;
}


/*! Display or send HTML.
 *
 * \param chan		Channel to send to
 * \param subclass
 * \param data
 * \param datalen
 */
static int skel_sendhtml(struct cw_channel *chan, int subclass, const char *data, int datalen)
{
	return -1;
}


/*! Display or send an image.
 *
 * \param chan		Channel to send to
 * \param frame		Frame containing the complete image data
 */
static int skel_sendimage(struct cw_channel *chan, struct cw_frame *frame)
{
	return -1;
}


/*! Write video
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_writevideo(struct cw_channel *chan, struct cw_frame *frame)
{
}


/*! Set an option.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_setoption(struct cw_channel *chan, int option, void *data, int datalen)
{
}


/*! Query an option.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_queryoption(struct cw_channel *chan, int option, void *data, int *datalen)
{
}


/*! Blind transfer.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_transfer(struct cw_channel *chan, const char *newdest)
{
}


/*! Bridge two channels of the same type together.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static enum cw_bridge_result skel_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags,
	struct cw_frame **fo, struct cw_channel **rc, int timeoutms)
{
}


/*! Find bridged channel.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static struct cw_channel *skel_bridgedchannel(struct cw_channel *chan, struct cw_channel *bridge)
{
}


/* PBX interface structure for channel registration */
static const struct cw_channel_tech skel_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = -1,

	.requester = skel_request,
	.fixup = skel_fixup,

	.call = skel_call,
	.answer = skel_answer,
	.hangup = skel_hangup,

	.read = skel_read,
	.exception = skel_exception,
	.write = skel_write,

	.indicate = skel_indicate,

	.send_digit = skel_digit,
	.send_text = skel_sendtext,
	.send_html = skel_sendhtml,
	.send_image = skel_sendimage,

	/* The following are optional and should be left NULL if not implemented. */
	.write_video = skel_writevideo,
	.setoption = skel_setoption,
	.queryoption = skel_queryoption,
	.transfer = skel_transfer,
	.bridge = skel_bridge,
	.bridged_channel = skel_bridgedchannel,
};


static int load_module(void)
{
	/* Make sure we can register our channel type */
	if (cw_channel_register(&skel_tech)) {
		cw_log(CW_LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

static int unload_module(void)
{
	struct local_pvt *p;

	cw_channel_unregister(&skel_tech);
	return 0;
}

MODULE_INFO(load_module, NULL, unload_module, NULL, desc)
