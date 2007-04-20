#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL: svn+ssh://svn@svn.callweaver.org/callweaver/trunk/apps/app_dial.c $", "$Revision: 878 $")

#include "callweaver/lock.h"
#include "callweaver/file.h"
#include "callweaver/logger.h"
#include "callweaver/channel.h"
#include "callweaver/pbx.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/logger.h"

#include "../channels/chan_visdn.h"

static char *tdesc = "vISDN ppp RAS module";
static char *app = "vISDNppp";
static char *synopsis = "Runs pppd and connects channel to visdn-ppp gateway";

static char *descrip = 
"  vISDNppp(args): Executes a RAS server using pppd on the given channel.\n"
"The channel must be a clear channel (i.e. PRI source) and a Zaptel\n"
"channel to be able to use this function (No modem emulation is included).\n"
"Your pppd must be patched to be zaptel aware. Arguments should be\n"
"separated by | characters.  Always returns -1.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define PPP_MAX_ARGS	32
#define PPP_EXEC	"/usr/sbin/pppd"

static int get_max_fds(void)
{
#ifdef OPEN_MAX
	return OPEN_MAX;
#else
	int max;

	max = sysconf(_SC_OPEN_MAX);
	if (max <= 0)
		return 1024;

	return max;
#endif
}

static pid_t spawn_ppp(
	struct opbx_channel *chan,
	const char *argv[],
	int argc)
{
	/* Start by forking */
	pid_t pid = fork();
	if (pid)
		return pid;

	dup2(chan->fds[0], STDIN_FILENO);

	int i;
	int max_fds = get_max_fds();
	for (i=STDERR_FILENO + 1; i < max_fds; i++)
		close(i);

	/* Restore original signal handlers */
	for (i=0; i<NSIG; i++)
		signal(i, SIG_DFL);

	/* Finally launch PPP */
	execv(PPP_EXEC, (char * const *)argv);
	fprintf(stderr, "Failed to exec pppd!: %s\n", strerror(errno));
	exit(1);
}


static int visdn_ppp_exec(struct opbx_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	struct opbx_frame *f;
	LOCAL_USER_ADD(u);

	if (chan->_state != OPBX_STATE_UP)
		opbx_answer(chan);

	opbx_mutex_lock(&chan->lock);

	if (strcmp(chan->type, "VISDN")) {
		opbx_log(LOG_WARNING,
			"Only VISDN channels may be connected to"
			" this application\n");

		opbx_mutex_unlock(&chan->lock);
		return -1;
	}

	struct visdn_chan *visdn_chan = chan->tech_pvt;

	if (!strlen(visdn_chan->visdn_chanid)) {
		opbx_log(LOG_WARNING,
			"vISDN crossconnector channel ID not present\n");
		opbx_mutex_unlock(&chan->lock);
		return -1;
	}

	const char *argv[PPP_MAX_ARGS] = { };
	int argc = 0;

	argv[argc++] = PPP_EXEC;
	argv[argc++] = "nodetach";

	char *stringp = strdup(data);
	char *arg;
	while((arg = strsep(&stringp, "|"))) {

		if (!strlen(arg))
			break;

		if (argc >= PPP_MAX_ARGS - 4)
			break;

		argv[argc++] = arg;
	}

	argv[argc++] = "plugin";
	argv[argc++] = "visdn.so";
	argv[argc++] = visdn_chan->visdn_chanid;

	opbx_mutex_unlock(&chan->lock);

#if 0
	int i;
	for (i=0;i<argc;i++) {
		opbx_log(LOG_NOTICE, "Arg %d: %s\n", i, argv[i]);
	}
#endif

	signal(SIGCHLD, SIG_DFL);

	pid_t pid = spawn_ppp(chan, argv, argc);
	if (pid < 0) {
		opbx_log(LOG_WARNING, "Failed to spawn pppd\n");
		return -1;
	}

	while(opbx_waitfor(chan, -1) > -1) {

		f = opbx_read(chan);
		if (!f) {
			opbx_log(LOG_NOTICE,
				"Channel '%s' hungup."
				" Signalling PPP at %d to die...\n",
				chan->name, pid);

			kill(pid, SIGTERM);

			break;
		}

		opbx_fr_free(f);

		int status;
		res = wait4(pid, &status, WNOHANG, NULL);
		if (res < 0) {
			opbx_log(LOG_WARNING,
				"wait4 returned %d: %s\n",
				res, strerror(errno));

			break;
		} else if (res > 0) {
			if (option_verbose > 2) {
				if (WIFEXITED(status)) {
					opbx_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated with status %d\n",
						chan->name, WEXITSTATUS(status));
				} else if (WIFSIGNALED(status)) {
					opbx_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated with signal %d\n", 
						chan->name, WTERMSIG(status));
				} else {
					opbx_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated weirdly.\n", chan->name);
				}
			}

			break;
		}
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
	return opbx_register_application(app, visdn_ppp_exec, synopsis, descrip);
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
