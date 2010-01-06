/*
 * CallWeaver -- An open source telephony toolkit.
 *
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
#include <callweaver.h>

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <stdio.h>

#include "callweaver/channel.h"
#include <callweaver_addon/adsi.h>



static int stub_adsi_begin_download(struct cw_channel *chan, char *service, unsigned char *fdn, unsigned char *sec, int version)
{
	CW_UNUSED(chan);
	CW_UNUSED(service);
	CW_UNUSED(fdn);
	CW_UNUSED(sec);
	CW_UNUSED(version);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_end_download(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_channel_restore(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_print(struct cw_channel *chan, char **lines, int *align, int voice)
{
	CW_UNUSED(chan);
	CW_UNUSED(lines);
	CW_UNUSED(align);
	CW_UNUSED(voice);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_load_session(struct cw_channel *chan, unsigned char *app, int ver, int data)
{
	CW_UNUSED(chan);
	CW_UNUSED(app);
	CW_UNUSED(ver);
	CW_UNUSED(data);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_unload_session(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_transmit_messages(struct cw_channel *chan, unsigned char **msg, int *msglen, int *msgtype)
{
	CW_UNUSED(chan);
	CW_UNUSED(msg);
	CW_UNUSED(msglen);
	CW_UNUSED(msgtype);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_transmit_message(struct cw_channel *chan, unsigned char *msg, int msglen, int msgtype)
{
	CW_UNUSED(chan);
	CW_UNUSED(msg);
	CW_UNUSED(msglen);
	CW_UNUSED(msgtype);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_transmit_message_full(struct cw_channel *chan, unsigned char *msg, int msglen, int msgtype, int dowait)
{
	CW_UNUSED(chan);
	CW_UNUSED(msg);
	CW_UNUSED(msglen);
	CW_UNUSED(msgtype);
	CW_UNUSED(dowait);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_read_encoded_dtmf(struct cw_channel *chan, unsigned char *buf, int maxlen)
{
	CW_UNUSED(chan);
	CW_UNUSED(buf);
	CW_UNUSED(maxlen);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_connect_session(unsigned char *buf, unsigned char *fdn, int ver)
{
	CW_UNUSED(buf);
	CW_UNUSED(fdn);
	CW_UNUSED(ver);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_query_cpeid(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_query_cpeinfo(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_get_cpeid(struct cw_channel *chan, unsigned char *cpeid, int voice)
{
	CW_UNUSED(chan);
	CW_UNUSED(cpeid);
	CW_UNUSED(voice);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_get_cpeinfo(struct cw_channel *chan, int *width, int *height, int *buttons, int voice)
{
	CW_UNUSED(chan);
	CW_UNUSED(width);
	CW_UNUSED(height);
	CW_UNUSED(buttons);
	CW_UNUSED(voice);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_download_connect(unsigned char *buf, char *service, unsigned char *fdn, unsigned char *sec, int ver)
{
	CW_UNUSED(buf);
	CW_UNUSED(service);
	CW_UNUSED(fdn);
	CW_UNUSED(sec);
	CW_UNUSED(ver);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_disconnect_session(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_download_disconnect(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_data_mode(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_clear_soft_keys(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_clear_screen(unsigned char *buf)
{
	CW_UNUSED(buf);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_voice_mode(unsigned char *buf, int when)
{
	CW_UNUSED(buf);
	CW_UNUSED(when);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_available(struct cw_channel *chan)
{
	CW_UNUSED(chan);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_display(unsigned char *buf, int page, int line, int just, int wrap, const char *col1, const char *col2)
{
	CW_UNUSED(buf);
	CW_UNUSED(page);
	CW_UNUSED(line);
	CW_UNUSED(just);
	CW_UNUSED(wrap);
	CW_UNUSED(col1);
	CW_UNUSED(col2);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_set_line(unsigned char *buf, int page, int line)
{
	CW_UNUSED(buf);
	CW_UNUSED(page);
	CW_UNUSED(line);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_load_soft_key(unsigned char *buf, int key, const char *llabel, const char *slabel, const char *ret, int data)
{
	CW_UNUSED(buf);
	CW_UNUSED(key);
	CW_UNUSED(llabel);
	CW_UNUSED(slabel);
	CW_UNUSED(ret);
	CW_UNUSED(data);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_set_keys(unsigned char *buf, unsigned char *keys)
{
	CW_UNUSED(buf);
	CW_UNUSED(keys);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_input_control(unsigned char *buf, int page, int line, int display, int format, int just)
{
	CW_UNUSED(buf);
	CW_UNUSED(page);
	CW_UNUSED(line);
	CW_UNUSED(display);
	CW_UNUSED(format);
	CW_UNUSED(just);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}

static int stub_adsi_input_format(unsigned char *buf, int num, int dir, int wrap, const char *format1, const char *format2)
{
	CW_UNUSED(buf);
	CW_UNUSED(num);
	CW_UNUSED(dir);
	CW_UNUSED(wrap);
	CW_UNUSED(format1);
	CW_UNUSED(format2);

	cw_log(CW_LOG_NOTICE, "res_adsi not loaded!\n");
	return -1;
}



int (*adsi_begin_download)(struct cw_channel *chan, char *service, unsigned char *fdn, unsigned char *sec, int version) =
	stub_adsi_begin_download;

int (*adsi_end_download)(struct cw_channel *chan) =
	stub_adsi_end_download;

int (*adsi_channel_restore)(struct cw_channel *chan) =
	stub_adsi_channel_restore;

int (*adsi_print)(struct cw_channel *chan, char **lines, int *align, int voice) =
	stub_adsi_print;

int (*adsi_load_session)(struct cw_channel *chan, unsigned char *app, int ver, int data) =
	stub_adsi_load_session;

int (*adsi_unload_session)(struct cw_channel *chan) =
	stub_adsi_unload_session;

int (*adsi_transmit_messages)(struct cw_channel *chan, unsigned char **msg, int *msglen, int *msgtype) =
	stub_adsi_transmit_messages;

int (*adsi_transmit_message)(struct cw_channel *chan, unsigned char *msg, int msglen, int msgtype) =
	stub_adsi_transmit_message;

int (*adsi_transmit_message_full)(struct cw_channel *chan, unsigned char *msg, int msglen, int msgtype, int dowait) =
	stub_adsi_transmit_message_full;

int (*adsi_read_encoded_dtmf)(struct cw_channel *chan, unsigned char *buf, int maxlen) =
	stub_adsi_read_encoded_dtmf;

int (*adsi_connect_session)(unsigned char *buf, unsigned char *fdn, int ver) =
	stub_adsi_connect_session;

int (*adsi_query_cpeid)(unsigned char *buf) =
	stub_adsi_query_cpeid;

int (*adsi_query_cpeinfo)(unsigned char *buf) =
	stub_adsi_query_cpeinfo;

int (*adsi_get_cpeid)(struct cw_channel *chan, unsigned char *cpeid, int voice) =
	stub_adsi_get_cpeid;

int (*adsi_get_cpeinfo)(struct cw_channel *chan, int *width, int *height, int *buttons, int voice) =
	stub_adsi_get_cpeinfo;

int (*adsi_download_connect)(unsigned char *buf, char *service, unsigned char *fdn, unsigned char *sec, int ver) =
	stub_adsi_download_connect;

int (*adsi_disconnect_session)(unsigned char *buf) =
	stub_adsi_disconnect_session;

int (*adsi_download_disconnect)(unsigned char *buf) =
	stub_adsi_download_disconnect;

int (*adsi_data_mode)(unsigned char *buf) =
	stub_adsi_data_mode;

int (*adsi_clear_soft_keys)(unsigned char *buf) =
	stub_adsi_clear_soft_keys;

int (*adsi_clear_screen)(unsigned char *buf) =
	stub_adsi_clear_screen;

int (*adsi_voice_mode)(unsigned char *buf, int when) =
	stub_adsi_voice_mode;

int (*adsi_available)(struct cw_channel *chan) =
	stub_adsi_available;

int (*adsi_display)(unsigned char *buf, int page, int line, int just, int wrap, const char *col1, const char *col2) =
	stub_adsi_display;

int (*adsi_set_line)(unsigned char *buf, int page, int line) =
	stub_adsi_set_line;

int (*adsi_load_soft_key)(unsigned char *buf, int key, const char *llabel, const char *slabel, const char *ret, int data) =
	stub_adsi_load_soft_key;

int (*adsi_set_keys)(unsigned char *buf, unsigned char *keys) =
	stub_adsi_set_keys;

int (*adsi_input_control)(unsigned char *buf, int page, int line, int display, int format, int just) =
	stub_adsi_input_control;

int (*adsi_input_format)(unsigned char *buf, int num, int dir, int wrap, const char *format1, const char *format2) =
	stub_adsi_input_format;
