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


static struct cw_channel *skel_request(const char *drvtype, int format, void *data, int *cause);
static int skel_fixup(struct cw_channel *oldchan, struct cw_channel *newchan);

static int skel_call(struct cw_channel *ast, const char *dest);
static int skel_answer(struct cw_channel *ast);
static int skel_hangup(struct cw_channel *ast);

static struct cw_frame *skel_read(struct cw_channel *ast);
static struct cw_frame *skel_exception(struct cw_channel *ast);
static int skel_write(struct cw_channel *ast, struct cw_frame *f);

static int skel_indicate(struct cw_channel *ast, int condition);

static int skel_digit(struct cw_channel *ast, char digit);


static struct cw_channel *skel_request(const char *drvtype, int format, void *data, int *cause)
{
	CW_UNUSED(drvtype);
	CW_UNUSED(format);
	CW_UNUSED(data);
	CW_UNUSED(cause);

	return NULL;
}


/*! Fix up a channel.
 *
 * \param oldchan	Old channel
 * \param newchan	New channel
 */
static int skel_fixup(struct cw_channel *oldchan, struct cw_channel *newchan)
{
	CW_UNUSED(oldchan);
	CW_UNUSED(newchan);

	return 0;
}


/*! Connect a channel to the given destination.
 *
 * Starts the process of connecting a call to the given destination.
 * There is no requirement that the call is actually connected when
 * skel_call returns. It is up to the caller to service the channel
 * and watch its state to see if the connection ultimately succeeds
 * or fails.
 *
 * \param chan		Channel to use
 * \param dest		Destination to connect to
 */
static int skel_call(struct cw_channel *chan, const char *dest)
{
	CW_UNUSED(chan);
	CW_UNUSED(dest);

	return -1;
}


/*! Answer an incoming call
 *
 * \param chan		Channel to use
 */
static int skel_answer(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	return -1;
}


/*! Hangup a channel.
 *
 * \param chan		Channel to hangup
 */
static int skel_hangup(struct cw_channel *chan)
{
	CW_UNUSED(chan);

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
	CW_UNUSED(chan);

	return &cw_null_frame;
}


static struct cw_frame  *skel_exception(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	return &cw_null_frame;
}


/*! Write a frame to a channel.
 *
 * \param chan		Channel to write to
 * \param frame		Frame to write
 */
static int skel_write(struct cw_channel *chan, struct cw_frame *frame)
{
	CW_UNUSED(chan);
	CW_UNUSED(frame);

	return -1;
}


/*! Indicate a condition (e.g. BUSY, RINGING, CONGESTION).
 *
 * \param chan		Channel to provide indication on
 * \param condition	Condition to be indicated
 */
static int skel_indicate(struct cw_channel *chan, int condition)
{
	CW_UNUSED(chan);
	CW_UNUSED(condition);

	return -1;
}


/*! Send a DTMF digit.
 *
 * \param chan		Channel to use
 * \param digit		Digit to send
 */
static int skel_digit(struct cw_channel *chan, char digit)
{
	CW_UNUSED(chan);
	CW_UNUSED(digit);

	return -1;
}


/*! Set an option.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_setoption(struct cw_channel *chan, int option, void *data, int datalen)
{
	CW_UNUSED(chan);
	CW_UNUSED(option);
	CW_UNUSED(data);
	CW_UNUSED(datalen);

	return 0;
}


/*! Query an option.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_queryoption(struct cw_channel *chan, int option, void *data, int *datalen)
{
	CW_UNUSED(chan);
	CW_UNUSED(option);
	CW_UNUSED(data);
	CW_UNUSED(datalen);

	return 0;
}


/*! Blind transfer.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static int skel_transfer(struct cw_channel *chan, const char *newdest)
{
	CW_UNUSED(chan);
	CW_UNUSED(newdest);

	return 0;
}


/*! Bridge two channels of the same type together.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static enum cw_bridge_result skel_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags,
	struct cw_frame **fo, struct cw_channel **rc, int timeoutms)
{
	CW_UNUSED(c0);
	CW_UNUSED(c1);
	CW_UNUSED(flags);
	CW_UNUSED(fo);
	CW_UNUSED(rc);
	CW_UNUSED(timeoutms);

	return CW_BRIDGE_COMPLETE;
}


/*! Find bridged channel.
 *
 * OPTIONAL - It is not necessary to implement this.
 */
static struct cw_channel *skel_bridgedchannel(struct cw_channel *chan, struct cw_channel *bridge)
{
	CW_UNUSED(chan);
	CW_UNUSED(bridge);

	return NULL;
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

	/* The following are optional and should be left NULL if not implemented. */
	.setoption = skel_setoption,
	.queryoption = skel_queryoption,
	.transfer = skel_transfer,
	.bridge = skel_bridge,
	.bridged_channel = skel_bridgedchannel,
};


static int reconfig_module(void)
{
	return 0;
}

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
	cw_channel_unregister(&skel_tech);
	return 0;
}

static void release_module(void)
{
}

MODULE_INFO(load_module, reconfig_module, unload_module, release_module, desc)
