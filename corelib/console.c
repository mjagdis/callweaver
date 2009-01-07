/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Eris Associates Limited, UK
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/un.h>
#include <string.h>
#include <errno.h>

#if  defined(__FreeBSD__) || defined( __NetBSD__ ) || defined(SOLARIS)
# include <netdb.h>
#endif

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include <callweaver/app.h>
#include <callweaver/cli.h>
#include <callweaver/time.h>
#include <callweaver/options.h>
#include <callweaver/manager.h>
#include <callweaver/utils.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <console.h>
#include <terminal.h>


#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#define PF_LOCAL PF_UNIX
#endif


#define CALLWEAVER_PROMPT "*CLI> "

#define CALLWEAVER_PROMPT2 "%s*CLI> "


const char *rl_basic_word_break_characters = " \t";


static int console_sock;

static char remotehostname[MAXHOSTNAMELEN];
static int remotepid;
static char remoteversion[256];

static char *clr_eol;

static int prompt;
static int progress;

static char **matches;
static int matches_space;
static int matches_count;


static void smart_write(const char *buf, int len)
{
	if (prompt && (option_console || option_remote)) {
		prompt = 0;
		if (clr_eol) {
			terminal_write("\r", 1);
			fputs(clr_eol, stdout);
		} else
			terminal_write("\r\n", 2);
	}

	terminal_write(buf, len);
}


/*! Set an X-term or screen title */
static void set_title(char *text)
{
	char *p;

	if ((p = getenv("TERM")) && strstr(p, "xterm")) {
		fprintf(stderr, "\033]2;%s\007", text);
		fflush(stderr);
	}
}


static char *cli_prompt(void)
{
	static char prompt[200];
	char *pfmt;
	int color_used = 0;
#if 0
	char term_code[20];
#endif

	if ((pfmt = getenv("CALLWEAVER_PROMPT"))) {
		char *t = pfmt, *p = prompt;
		memset(prompt, 0, sizeof(prompt));
		while (*t != '\0' && *p < sizeof(prompt)) {
			if (*t == '%') {
#if 0
				int i;
#endif
				struct timeval tv;
				struct tm tm;
#ifdef linux
				FILE *LOADAVG;
#endif

				t++;
				switch (*t) {
					case 'C': /* color */
						t++;
#if 0
						if (sscanf(t, "%d;%d%n", &fgcolor, &bgcolor, &i) == 2) {
							strncat(p, cw_term_color_code(term_code, fgcolor, bgcolor, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						} else if (sscanf(t, "%d%n", &fgcolor, &i) == 1) {
							strncat(p, cw_term_color_code(term_code, fgcolor, 0, sizeof(term_code)),sizeof(prompt) - strlen(prompt) - 1);
							t += i - 1;
						}

						/* If the color has been reset correctly, then there's no need to reset it later */
						if ((fgcolor == COLOR_WHITE) && (bgcolor == COLOR_BLACK)) {
							color_used = 0;
						} else {
							color_used = 1;
						}
#endif
						break;
					case 'd': /* date */
						memset(&tm, 0, sizeof(struct tm));
						tv = cw_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%Y-%m-%d", &tm);
						}
						break;
					case 'h': /* remote hostname */
						strncat(p, remotehostname, sizeof(prompt) - strlen(prompt) - 1);
						break;
					case 'H': { /* short remote hostname */
						char *q = strchr(remotehostname, '.');
						int n = (q ? q - remotehostname : strlen(remotehostname));
						int l = sizeof(prompt) - strlen(prompt) - 1;
						strncat(p, remotehostname, n > l ? l : n);
						break;
					}
#ifdef linux
					case 'l': /* load avg */
						t++;
						if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
							float avg1, avg2, avg3;
							int actproc, totproc, npid, which;
							fscanf(LOADAVG, "%f %f %f %d/%d %d",
								&avg1, &avg2, &avg3, &actproc, &totproc, &npid);
							if (sscanf(t, "%d", &which) == 1) {
								switch (which) {
									case 1:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg1);
										break;
									case 2:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg2);
										break;
									case 3:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%.2f", avg3);
										break;
									case 4:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d/%d", actproc, totproc);
										break;
									case 5:
										snprintf(p, sizeof(prompt) - strlen(prompt), "%d", npid);
										break;
								}
							}
						}
						break;
#endif
					case 't': /* time */
						memset(&tm, 0, sizeof(struct tm));
						tv = cw_tvnow();
						if (localtime_r(&(tv.tv_sec), &tm)) {
							strftime(p, sizeof(prompt) - strlen(prompt), "%H:%M:%S", &tm);
						}
						break;
					case '#': /* process console or remote? */
						if (! option_remote) {
							strncat(p, "#", sizeof(prompt) - strlen(prompt) - 1);
						} else {
							strncat(p, ">", sizeof(prompt) - strlen(prompt) - 1);
						}
						break;
					case '%': /* literal % */
						strncat(p, "%", sizeof(prompt) - strlen(prompt) - 1);
						break;
					case '\0': /* % is last character - prevent bug */
						t--;
						break;
				}
				while (*p != '\0') {
					p++;
				}
				t++;
			} else {
				*p = *t;
				p++;
				t++;
			}
		}
		if (color_used) {
			/* Force colors back to normal at end */
#if 0
			cw_term_color_code(term_code, COLOR_WHITE, COLOR_BLACK, sizeof(term_code));
			if (strlen(term_code) > sizeof(prompt) - strlen(prompt)) {
				strncat(prompt + sizeof(prompt) - strlen(term_code) - 1, term_code, strlen(term_code));
			} else {
				strncat(p, term_code, sizeof(term_code));
			}
#endif
		}
	} else if (remotehostname)
		snprintf(prompt, sizeof(prompt), CALLWEAVER_PROMPT2, remotehostname);
	else
		snprintf(prompt, sizeof(prompt), CALLWEAVER_PROMPT);

	return (prompt);	
}


static int read_message(int s, int nresp)
{
	static char buf_level[16];
	static char buf_date[256];
	static char buf_threadid[32];
	static char buf_file[256];
	static char buf_line[6];
	static char buf_function[256];
	enum { F_DATE = 0, F_LEVEL, F_THREADID, F_FILE, F_LINE, F_FUNCTION, F_MESSAGE };
	static const struct {
		const char *name;
		const int name_len;
		const int buf_len;
		char *buf;
		const char *tail;
		const int tail_len;
	} field[] = {
		[F_DATE]     = { "Date",      sizeof("Date") - 1,      sizeof(buf_date),     buf_date,     NULL,  0 },
		[F_LEVEL]    = { "Level",     sizeof("Level") - 1,     sizeof(buf_level),    buf_level,    "[",   1 },
		[F_THREADID] = { "Thread ID", sizeof("Thread ID") - 1, sizeof(buf_threadid), buf_threadid, "]: ", 3 },
		[F_FILE]     = { "File",      sizeof("File") - 1,      sizeof(buf_file),     buf_file,     ":",   1 },
		[F_LINE]     = { "Line",      sizeof("Line") - 1,      sizeof(buf_line),     buf_line,     " ",   1 },
		[F_FUNCTION] = { "Function",  sizeof("Function") - 1,  sizeof(buf_function), buf_function, ": ",  2 },
	};
	static int field_len[arraysize(field)];
	static char buf[32768];
	static int pos = 0;
	static int state = 0;
	static char *key, *val;
	static int lkey, lval = -1;
	static enum { MSG_UNKNOWN, MSG_EVENT, MSG_RESPONSE, MSG_FOLLOWS, MSG_VERSION, MSG_COMPLETION } msgtype;
	int level, res, i;

	level = 0;

	do {
		if ((res = read(s, buf + pos, sizeof(buf) - pos)) <= 0)
			return -1;

		for (; res; pos++, res--) {
			switch (state) {
				case 0: /* Start of header line */
					if (buf[pos] == '\r') {
						break;
					} else if (buf[pos] == '\n') {
						if (msgtype == MSG_FOLLOWS) {
							state = 4;
							lval = 0;
							goto is_data;
						}
						if (msgtype != MSG_UNKNOWN) {
							if (nresp > 0)
								nresp--;
							if (msgtype == MSG_VERSION) {
								lkey = sizeof(remoteversion) + sizeof(remotehostname) + sizeof(" running on  (pid 0000000000)") + 1;
								key = alloca(lkey);
								lkey = snprintf(key, lkey, "%s running on %s (pid %u)", remoteversion, remotehostname, remotepid);

								set_title(key);

								putc('\n', stdout);
								fputs(key, stdout);
								putc('\n', stdout);

								while (lkey > 0) key[--lkey] = '=';
								fputs(key, stdout);
								fputs("\n\n", stdout);
							}
						}
						msgtype = MSG_UNKNOWN;
						for (i = 0; i < arraysize(field); i++)
							field[i].buf[0] = '\0';
						memmove(buf, &buf[pos + 1], res - 1);
						lval = pos = -1;
						break;
					}
					key = &buf[pos];
					state = 1;
					/* Fall through - the key could be null */
				case 1: /* In header name, looking for ':' */
					if (buf[pos] != ':' && buf[pos] != '\r' && buf[pos] != '\n')
						break;
					/* End of header name, skip spaces to value */
					state = 2;
					lkey = &buf[pos] - key;
					if (buf[pos] == ':')
						break;
					/* Fall through all the way - no colon, no value - want end of line */
				case 2: /* Skipping spaces before value */
					if (buf[pos] == ' ' || buf[pos] == '\t')
						break;
					val = &buf[pos];
					state = 3;
					/* Fall through - we are on the start of the value and it may be blank */
				case 3: /* In value, looking for end of line */
					if ((buf[pos] == '\r' || buf[pos] == '\n') && lval < 0)
						lval = &buf[pos] - val;

					if (buf[pos] == '\n') {
						state = 0;

						if (msgtype == MSG_EVENT) {
							if (lkey == sizeof("Message")-1 && !memcmp(key, "Message", sizeof("Message") - 1)) {
								key = buf;
								state = 4;
							} else {
								for (i = 0; i < arraysize(field); i++) {
									if (lkey == field[i].name_len && !memcmp(key, field[i].name, field[i].name_len)) {
										if (i == F_LEVEL) {
											level = atol(val);
											while (*val != ' ')
												val++, lval--;
										}
										field_len[i] = (lval > field[i].buf_len ? field[i].buf_len : lval);
										memcpy(field[i].buf, val, field_len[i]);
										break;
									}
								}
							}
						} else if (msgtype == MSG_RESPONSE) {
							if (lkey == sizeof("Message")-1 && !memcmp(key, "Message", sizeof("Message") - 1))
								smart_write(val, lval);
						} else if (msgtype == MSG_FOLLOWS || msgtype == MSG_COMPLETION) {
							if (lkey != sizeof("Privilege")-1 || memcmp(key, "Privilege", sizeof("Privilege") - 1)) {
								state = 4;
								lval = &buf[pos] - key;
								if (lval > 0 && key[lval - 1] == '\r')
									lval--;
								goto is_data;
							}
						} else if (msgtype == MSG_VERSION) {
							if (lkey == sizeof("Hostname")-1 && !memcmp(key, "Hostname", sizeof("Hostname") - 1)) {
								if (lval > sizeof(remotehostname) - 1)
									lval = sizeof(remotehostname) - 1;
								memcpy(remotehostname, val, lval);
								val[lval] = '\0';
							} else if (lkey == sizeof("Pid")-1 && !memcmp(key, "Pid", sizeof("Pid") - 1))
								remotepid = atol(val);
							else if (lkey == sizeof("Version")-1 && !memcmp(key, "Version", sizeof("Version") - 1)) {
								if (lval > sizeof(remoteversion) - 1)
									lval = sizeof(remoteversion) - 1;
								memcpy(remoteversion, val, lval);
								val[lval] = '\0';
							}
						} else {
							/* Event and Response headers are guaranteed to come
							 * before any other header
							 */
							if (lkey == sizeof("Event") - 1 && !memcmp(key, "Event", sizeof("Event") - 1))
								msgtype = MSG_EVENT;
							else if (lkey == sizeof("Response") - 1 && !memcmp(key, "Response", sizeof("Response") - 1)) {
								if (lval == sizeof("Completion") - 1 && !memcmp(val, "Completion", sizeof("Completion") - 1))
									msgtype = MSG_COMPLETION;
								else if (lval == sizeof("Follows") - 1 && !memcmp(val, "Follows", sizeof("Follows") - 1))
									msgtype = MSG_FOLLOWS;
								else if (lval == sizeof("Version") - 1 && !memcmp(val, "Version", sizeof("Version") - 1))
									msgtype = MSG_VERSION;
								else
									msgtype = MSG_RESPONSE;
							}
						}

						memmove(buf, &buf[pos + 1], res - 1);
						lval = pos = -1;
					}
					break;
				case 4: /* Response data - want a line */
					if ((buf[pos] == '\r' || buf[pos] == '\n') && lval < 0)
						lval = &buf[pos] - key;

					if (buf[pos] == '\n') {
						if (msgtype == MSG_EVENT) {
							if (lval != sizeof("--END MESSAGE--") - 1 || memcmp(key, "--END MESSAGE--", sizeof("--END MESSAGE--") - 1)) {
								if (level == CW_EVENT_NUM_PROGRESS) {
									/* Progress messages suppress input handling until
									 * we get a null progress message to signify the end
									 */
									if (lval) {
										smart_write(key, lval);
										progress = 2;
									} else {
										smart_write("\n", 1);
										progress = 0;
									}
								} else {
									if (progress == 2) {
										progress = 1;
										smart_write("\n", 1);
									}

									key[lval++] = '\n';
									if (level != CW_EVENT_NUM_VERBOSE) {
										smart_write(field[F_DATE].buf, field_len[F_DATE]);
										for (i = 0; i < arraysize(field); i++) {
											terminal_write(field[i].buf, field_len[i]);
											terminal_write(field[i].tail, field[i].tail_len);
										}
									}
									smart_write(key, lval);
								}
							} else
								state = 0;
						} else
is_data:
						if (lval != sizeof("--END COMMAND--") - 1 || memcmp(key, "--END COMMAND--", sizeof("--END COMMAND--") - 1)) {
							if (msgtype == MSG_FOLLOWS) {
								key[lval++] = '\n';
								smart_write(key, lval);
							} else if (msgtype == MSG_COMPLETION) {
								key[lval] = '\0';
								if (matches_count == matches_space) {
									char **n = realloc(matches, (matches_space + 64) * sizeof(matches[0]));
									if (n) {
										matches_space += 64;
										matches = n;
									}
								}
								if (matches_count < matches_space && (matches[matches_count] = strdup(key))) {
									if (!matches[0])
										matches[0] = strdup(key);
									if (matches[0]) {
										for (lval = 0; matches[0][lval] && matches[0][lval] == matches[matches_count][lval]; lval++);
										matches[0][lval] = '\0';
									}
									matches[++matches_count] = NULL;
								}
							}
						} else {
							state = 0;
							msgtype = MSG_UNKNOWN;
							if (nresp > 0)
								nresp--;
						}

						memmove(buf, &buf[pos + 1], res - 1);
						lval = pos = -1;
						key = buf;
					}
					break;
			}
		}

		if (pos == sizeof(buf)) {
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			cw_log(CW_LOG_ERROR, "Console got an overlong line (> %d bytes!)\n", sizeof(buf));
			break;
		}
	} while (nresp);

	return 0;
}


static char *dummy_completer(char *text, int state)
{
	return NULL;
}


static char **cli_completion(const char *text, int start, int end)
{
	struct iovec iov[3];

	if ((matches = malloc(64 * sizeof(matches[0])))) {
		matches_space = 64 - 2;
		matches_count = 1;
		matches[0] = NULL;

		iov[0].iov_base = "Action: Complete\r\nCommand: ";
		iov[0].iov_len = sizeof("Action: Complete\r\nCommand: ") - 1;
		iov[1].iov_base = rl_line_buffer;
		iov[1].iov_len = strlen(rl_line_buffer);
		iov[2].iov_base = "\r\n\r\n";
		iov[2].iov_len = sizeof("\r\n\r\n") - 1;
		cw_writev_all(console_sock, iov, arraysize(iov));

		read_message(console_sock, 1);

		if (!matches[0]) {
			int i;
			for (i = 1; i < matches_count; i++)
				free(matches[i]);
			free(matches);
			matches = NULL;
		}
	}

	rl_attempted_completion_over = 1;

	return (matches);
}


static void console_cleanup(void *data)
{
	char filename[80];
	char *p;

	rl_callback_handler_remove();
	terminal_write("\r\n", 2);
	fflush(stdout);
	set_title("");

	if ((p = getenv("HOME"))) {
		snprintf(filename, sizeof(filename), "%s/.callweaver_history", getenv("HOME"));
		write_history(filename);
	}
}


static void console_handler(char *s)
{
	if (s) {
		while (isspace(*s)) s++;

		if (*s) {
			HIST_ENTRY *last_he;
    
			last_he = previous_history();
			if (!last_he || strcmp(last_he->line, s) != 0)
				add_history(s);

			if (s[0] == '!') {
				if (s[1])
					cw_safe_system(s+1);
				else
					cw_safe_system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
			} else if (option_remote && (!strcasecmp(s, "quit") || !strcasecmp(s, "exit"))) {
				console_cleanup(NULL);
				exit(0);
			} else {
				struct iovec iov[3];

				iov[0].iov_base = "Action: Command\r\nCommand: ";
				iov[0].iov_len = sizeof("Action: Command\r\nCommand: ") - 1;
				iov[1].iov_base = s;
				iov[1].iov_len = strlen(s);
				iov[2].iov_base = "\r\n\r\n";
				iov[2].iov_len = sizeof("\r\n\r\n") - 1;
				if (cw_writev_all(console_sock, iov, 3) < 1) {
					cw_log(CW_LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					pthread_detach(pthread_self());
					pthread_cancel(pthread_self());
				}
				read_message(console_sock, 1);
				prompt = 1;
			}
		}
	} else if (option_remote) {
		console_cleanup(NULL);
		exit(0);
	} else {
		shutdown(console_sock, SHUT_WR);
		putc('\n', stdout);
	}
}


static int console_connect(char *spec, int events)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un sun;
	} u;
	socklen_t salen;
	int s = -1;

	if (!spec) {
		int sv[2];
		struct mansession *sess;

		if (!socketpair(AF_LOCAL, SOCK_STREAM, 0, sv)
		&& (sess = manager_session_start(manager_session_ami, sv[0], AF_INTERNAL, "console", sizeof("console") - 1, events, EVENT_FLAG_COMMAND, events))) {
			s = sv[1];
			cw_object_put(sess);
		}
	} else if (strlen(spec) > sizeof(u.sun.sun_path) - 1) {
		errno = EINVAL;
	} else {
		memset(&u, 0, sizeof(u));
		u.sun.sun_family = AF_LOCAL;
		strcpy(u.sun.sun_path, spec);
		salen = sizeof(u.sun);

		if ((s = socket(u.sa.sa_family, SOCK_STREAM, 0)) < 0) {
			fprintf(stderr, "Unable to create socket: %s\n", strerror(errno));
		} else if (connect(s, &u.sa, salen)) {
			close(s);
			s = -1;
		} else {
			fcntl(s, F_SETFD, fcntl(s, F_GETFD, 0) | FD_CLOEXEC);
		}
	}

	return s;
}


void *console(void *data)
{
	char buf[1024];
	struct iovec iov[6];
	struct pollfd pfd[2];
	sigset_t sigs;
	char *spec = data;
	char *p;
	char c;

	console_sock = -1;

	terminal_init();
	terminal_set_icon("Callweaver");

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGWINCH);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	pthread_cleanup_push(console_cleanup, NULL);

	rl_initialize ();
	rl_editing_mode = 1;
	rl_completion_entry_function = (void *)dummy_completer; /* The typedef varies between platforms */
	rl_attempted_completion_function = (CPPFunction *)cli_completion;

	/* Setup history with 100 entries */
	using_history();
	stifle_history(100);

	clr_eol = rl_get_termcap("ce");

	if ((p = getenv("HOME"))) {
		snprintf(buf, sizeof(buf), "%s/.callweaver_history", p);
		read_history(buf);
	}

	do {
		const int reconnects_per_second = 20;
		int tries;

		if (spec)
			fprintf(stderr, "Connecting to Callweaver at %s...\n", spec);
		for (tries = 0; console_sock < 0 && tries < 30 * reconnects_per_second; tries++) {
			if ((console_sock = console_connect(spec, EVENT_FLAG_LOG_ALL | EVENT_FLAG_PROGRESS)) < 0) {
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				pthread_testcancel();
				usleep(1000000 / reconnects_per_second);
				pthread_testcancel();
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			}
		}
		if (console_sock < 0) {
			fprintf(stderr, "Failed to connect in 30 seconds. Quitting.\n");
			break;
		}

		/* Dump the connection banner. We don't need it here */
		while (read(console_sock, &c, 1) == 1 && c != '\n');

		/* Make sure verbose and debug settings are what we want or higher
		 * and enable events
		 */
		iov[0].iov_base = "Action: Version\r\n\r\nAction: Events\r\nEventmask: log,progress\r\n\r\nAction: Command\r\nCommand: set verbose atleast ";
		iov[0].iov_len = sizeof("Action: Version\r\n\r\nAction: Events\r\nEventmask: log,progress\r\n\r\nAction: Command\r\nCommand: set verbose atleast ") - 1;
		iov[1].iov_base = buf;
		iov[1].iov_len = snprintf(buf, sizeof(buf), "%d", option_verbose);
		iov[2].iov_base = "\r\n\r\n";
		iov[2].iov_len = sizeof("\r\n\r\n") - 1;
		iov[3].iov_base = "Action: Command\r\nCommand: set debug atleast ";
		iov[3].iov_len = sizeof("Action: Command\r\nCommand: set debug atleast ") - 1;
		iov[4].iov_base = buf + iov[1].iov_len;
		iov[4].iov_len = snprintf(buf + iov[1].iov_len, sizeof(buf) - iov[1].iov_len, "%d", option_debug);
		iov[5].iov_base = "\r\n\r\n";
		iov[5].iov_len = sizeof("\r\n\r\n") - 1;
		cw_writev_all(console_sock, iov, 6);
		read_message(console_sock, 4);

		/* Ok, we're ready. If we are the internal console tell the core to boot if it hasn't already */
		if (option_console && !fully_booted)
			kill(cw_mainpid, SIGHUP);

		progress = 0;
		prompt = 1;
		rl_callback_handler_install(cli_prompt(), console_handler);

		pfd[0].fd = console_sock;
		pfd[1].fd = fileno(stdin);
		pfd[0].events = pfd[1].events = POLLIN;
		pfd[0].revents = pfd[1].revents = 0;

		for (;;) {
			int ret;

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();

			ret = poll(pfd, (progress ? 1 : 2), -1);

			pthread_testcancel();
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			if (ret >= 0) {
				if (pfd[0].revents) {
					if (read_message(console_sock, 0))
						break;

					if (!progress && !prompt) {
						prompt = 1;
						rl_forced_update_display();
						fflush(stdout);
					}
				}

				if (pfd[1].revents)
					rl_callback_read_char();
			} else if (errno != EINTR) {
				perror("poll");
				break;
			}
		}

		rl_callback_handler_remove();
		fflush(stdout);
		fprintf(stderr, "\nDisconnected from CallWeaver server\n");
		set_title("");
		close(console_sock);
		console_sock = -1;
	} while (option_reconnect);

	pthread_cleanup_pop(1);
	return NULL;
}


int console_oneshot(char *spec, char *cmd)
{
	struct iovec iov[3];
	char c;
	int s, n;

	if ((s = console_connect(spec, 0)) >= 0) {
		/* Dump the connection banner. We don't need it here */
		while (read(s, &c, 1) == 1 && c != '\n');

		iov[0].iov_base = "Action: Command\r\nCommand: ";
		iov[0].iov_len = sizeof("Action: Command\r\nCommand: ") - 1;
		iov[1].iov_base = cmd;
		iov[1].iov_len = strlen(cmd);
		iov[2].iov_base = "\r\n\r\n";
		iov[2].iov_len = sizeof("\r\n\r\n") - 1;
		cw_writev_all(s, iov, 3);

		shutdown(s, SHUT_WR);

		while (!read_message(s, -1));
		close(s);
		n = 0;
	} else {
		fprintf(stderr, "Unable to connect to Callweaver at %s\n", spec);
		n = 1;
	}

	return n;
}
