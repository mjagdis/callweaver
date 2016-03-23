/*
 * Devstate application
 * 
 * Since we like the snom leds so much, a little app to
 * light the lights on the snom on demand ....
 *
 * Copyright (C) 2005, Druid Software
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/callweaver_db.h"
#include "callweaver/utils.h"
#include "callweaver/cli.h"
#include "callweaver/manager.h"
#include "callweaver/devicestate.h"


static char type[] = "DS";
static const char tdesc[] = "Application for sending device state messages";

static void *devstate_app;
static const char devstate_name[] = "DevState";
static const char devstate_synopsis[] = "Generate a device state change event given the input parameters";
static const char devstate_syntax[] = "DevState(device, state)";
static const char devstate_descrip[] = "Generate a device state change event given the input parameters. Returns 0. State values match the callweaver device states. They are 0 = unknown, 1 = not inuse, 2 = inuse, 3 = busy, 4 = invalid, 5 = unavailable, 6 = ringing\n";

static const char devstate_cli_usage[] =
"Usage: DevState device state\n" 
"       Generate a device state change event given the input parameters.\n";

static int devstate_cli(struct cw_dynstr *ds_p, int argc, char *argv[]);
static struct cw_clicmd  cli_dev_state = {
	.cmda = { "devstate", NULL },
	.handler = devstate_cli,
	.summary = "Set the device state on one of the \"pseudo devices\".",
	.usage = devstate_cli_usage,
};


static int devstate_cli(struct cw_dynstr *ds_p, int argc, char *argv[])
{
    if ((argc != 3) && (argc != 4) && (argc != 5))
        return RESULT_SHOWUSAGE;

    if (cw_db_put("DEVSTATES", argv[1], argv[2]))
        cw_dynstr_printf(ds_p, "cw_db_put failed\n");

    cw_device_state_changed("DS/%s", argv[1]);
    
    return RESULT_SUCCESS;
}

static int devstate_exec(struct cw_channel *chan, int argc, char **argv, struct cw_dynstr *result)
{
    struct localuser *u;

    CW_UNUSED(result);

    if (argc != 2)
        return cw_function_syntax(devstate_syntax);

    LOCAL_USER_ADD(u);
    
    if (cw_db_put("DEVSTATES", argv[0], argv[1])) {
        cw_log(CW_LOG_DEBUG, "cw_db_put failed\n");
    }

    cw_device_state_changed("DS/%s", argv[0]);

    LOCAL_USER_REMOVE(u);
    return 0;
}


static int ds_devicestate(void *data)
{
    struct cw_dynstr ds = CW_DYNSTR_INIT;
    char *dest = data;
    int res = 0;

    if (!cw_db_get("DEVSTATES", dest, &ds) && !ds.error)
    {
        res = atoi(ds.data);
        cw_log(CW_LOG_DEBUG, "ds_devicestate dev=%s returning state %d\n", dest, res);
    }
    else
    {
        cw_log(CW_LOG_DEBUG, "ds_devicestate couldnt get state in cwdb\n");
    }

    cw_dynstr_free(&ds);
    return res;
}

static struct cw_channel_tech devstate_tech = {
	.type = type,
	.description = tdesc,
	.capabilities = ((CW_FORMAT_MAX_AUDIO << 1) - 1),
	.devicestate = ds_devicestate,
	.requester = NULL,
	.send_digit = NULL,
	.call = NULL,
	.hangup = NULL,
	.answer = NULL,
	.read = NULL,
	.write = NULL,
	.bridge = NULL,
	.exception = NULL,
	.indicate = NULL,
	.fixup = NULL,
	.setoption = NULL,
};

static char mandescr_devstate[] = 
"Description: Put a value into cwdb\n"
"Variables: \n"
"	Family: ...\n"
"	Key: ...\n"
"	Value: ...\n";

static struct cw_manager_message *action_devstate(struct mansession *sess, const struct message *req)
{
	struct cw_manager_message *msg;
        char *devstate = cw_manager_msg_header(req, "Devstate");
        char *value = cw_manager_msg_header(req, "Value");

	CW_UNUSED(sess);

	if (!cw_strlen_zero(devstate)) {
		if (!cw_strlen_zero(value)) {
			if (!cw_db_put("DEVSTATES", devstate, value)) {
				cw_device_state_changed("DS/%s", devstate);
				msg = cw_manager_response("Success", NULL);
			} else {
				cw_log(CW_LOG_DEBUG, "cw_db_put failed\n");
				msg = cw_manager_response("Error", "cw_db_put failed");
			}
		} else
			msg = cw_manager_response("Error", "No Value specified");
	} else
		msg = cw_manager_response("Error", "No Devstate specified");

	return msg;
}

static struct manager_action manager_actions[] = {
	{
		.action = "Devstate",
		.authority = CW_EVENT_FLAG_CALL,
		.func = action_devstate,
		.synopsis = "Change a device state",
		.description = mandescr_devstate,
	},
};

static int load_module(void)
{
    if (cw_channel_register(&devstate_tech)) {
        cw_log(CW_LOG_DEBUG, "Unable to register channel class %s\n", type);
        return -1;
    }
    cw_cli_register(&cli_dev_state);  
    cw_manager_action_register_multiple(manager_actions, arraysize(manager_actions));
    devstate_app = cw_register_function(devstate_name, devstate_exec, devstate_synopsis, devstate_syntax, devstate_descrip);
    return 0;
}

static int unload_module(void)
{
    int res = 0;

    cw_manager_action_unregister_multiple(manager_actions, arraysize(manager_actions));
    cw_cli_unregister(&cli_dev_state);
    res |= cw_unregister_function(devstate_app);
    cw_channel_unregister(&devstate_tech);    
    return res;
}


MODULE_INFO(load_module, NULL, unload_module, NULL, tdesc)
