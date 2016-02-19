/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2007,2010, Eris Associates Limited, UK
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

#include <callweaver/dynstr.h>
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


#define CALLWEAVER_PROMPT "%H*CLI%# "


static struct {
	const char *on, *off;
} level_attr[] = {
	[CW_LOG_DEBUG]   = { NULL, NULL },
	[CW_LOG_EVENT]   = { NULL, NULL },
	[CW_LOG_NOTICE]  = { NULL, NULL },
	[CW_LOG_WARNING] = { NULL, NULL },
	[CW_LOG_ERROR]   = { NULL, NULL },
	[CW_LOG_VERBOSE] = { NULL, NULL },
	[CW_LOG_DTMF]    = { NULL, NULL },
};

static const char *bold_on, *bold_off;


static char *console_address;
static int console_sock;

static char remotehostname[MAXHOSTNAMELEN];
static unsigned int remotepid;
static char remoteversion[256];

static struct cw_dynstr prompt = CW_DYNSTR_INIT;

static char *clr_eol;

static int prompting;
static int progress;

static char **matches;
static int matches_space;
static int matches_count;


static void exit_prompt(void)
{
	if (prompting && (option_console || option_remote)) {
		prompting = 0;
		if (clr_eol) {
			fputs("\r", stdout);
			fputs(clr_eol, stdout);
		} else
			fputs("\r\n", stdout);
	}
}


static void smart_page(int page, const struct cw_dynstr *ds, int lines)
{
	exit_prompt();

	if (page) {
		int rows, cols;

		rl_get_screen_size(&rows, &cols);
		if (lines > rows) {
			const char *pager;
			FILE *fd;

			if (!(pager = getenv("PAGER")))
				pager = "more";

			if ((fd = popen(pager, "w"))) {
				int ok = (fwrite(ds->data, ds->used, 1, fd) == 1);
				if (pclose(fd) == 0 && ok)
					return;
			}
		}
	}

	fwrite(ds->data, 1, ds->used, stdout);
}


/*! Set an X-term or screen title */
static void set_title(const char *text)
{
	char *p;

	if ((p = getenv("TERM")) && strstr(p, "xterm")) {
		fprintf(stderr, "\033]2;%s\007", text);
		fflush(stderr);
	}
}


static int promptput(int c)
{
	cw_dynstr_printf(&prompt, "%c", c);
	return c;
}

static void promptattr(const char **attr)
{
	cw_dynstr_printf(&prompt, "%c", RL_PROMPT_START_IGNORE);
	terminal_write_attr(*attr, promptput);
	cw_dynstr_printf(&prompt, "%c", RL_PROMPT_END_IGNORE);
	free((void *)(*attr));
	*attr = NULL;
}

static void set_prompt(const char *pfmt)
{
	struct tm tm;
	struct timeval tv;
	const char *t, *p;
	const char *astart = NULL, *aend = NULL;

	cw_dynstr_reset(&prompt);

	if (!pfmt)
		pfmt = CALLWEAVER_PROMPT;

	t = pfmt;
	while ((p = strchr(t, '%'))) {
		cw_dynstr_printf(&prompt, "%.*s", (int)(p - t), t);
		t = p + 1;
#ifdef linux
		FILE *LOADAVG;
#endif

		switch (*t) {
			case 'C': { /* colour */
				char *q = NULL;
				int fg, bg, i;

				if (aend)
					promptattr(&aend);
				if (t[1] == '{' && (p = strchr(&t[2], '}'))) {
					q = alloca(p - &t[2] + 1);
					memcpy(q, &t[2], p - &t[2]);
					q[p - &t[2]] = '\0';
					t = p;
				} else if (t[1] == '*') {
					t++;
				} else if (sscanf(&t[1], "%d;%d%n", &fg, &bg, &i) >= 2 && fg >= 0 && fg <= 16 && bg >= 0 && bg <= 16) {
					q = alloca(sizeof("fg=XX,bg=XX"));
					sprintf(q, "fg=%d,bg=%d", fg, bg);
					t += i;
				} else if (sscanf(&t[1], "%d%n", &fg, &i) >= 1 && fg >= 0 && fg <= 16) {
					q = alloca(sizeof("fg=XX"));
					sprintf(q, "fg=%d", fg);
					t += i;
				}
				if (q) {
					terminal_highlight(&astart, &aend, q);
					if (astart)
						promptattr(&astart);
				}
				break;
			}
			case 'd': /* date */
				cw_dynstr_need(&prompt, sizeof("YYYY-MM-DD"));
				tv = cw_tvnow();
				if (localtime_r(&(tv.tv_sec), &tm))
					strftime(&prompt.data[prompt.used], prompt.size - prompt.used, "%Y-%m-%d", &tm);
				break;
			case 'H': /* short remote hostname */
				if ((p = strchr(remotehostname, '.'))) {
					cw_dynstr_printf(&prompt, "%.*s", (int)(p - remotehostname), remotehostname);
					break;
				}
				/* fall through */
			case 'h': /* remote hostname */
				cw_dynstr_printf(&prompt, "%s", remotehostname);
				break;
#ifdef linux
			case 'l': /* load avg */
				t++;
				if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
					float avg1, avg2, avg3;
					int actproc, totproc, npid, which;
					fscanf(LOADAVG, "%f %f %f %d/%d %d", &avg1, &avg2, &avg3, &actproc, &totproc, &npid);
					if (sscanf(t, "%d", &which) == 1) {
						switch (which) {
							case 1:
								cw_dynstr_printf(&prompt, "%.2f", avg1);
								break;
							case 2:
								cw_dynstr_printf(&prompt, "%.2f", avg2);
								break;
							case 3:
								cw_dynstr_printf(&prompt, "%.2f", avg3);
								break;
							case 4:
								cw_dynstr_printf(&prompt, "%d/%d", actproc, totproc);
								break;
							case 5:
								cw_dynstr_printf(&prompt, "%d", npid);
								break;
						}
					}
				}
				break;
#endif
			case 't': /* time */
				cw_dynstr_need(&prompt, sizeof("HH:MM:SS"));
				tv = cw_tvnow();
				if (localtime_r(&(tv.tv_sec), &tm))
					strftime(&prompt.data[prompt.used], prompt.size - prompt.used, "%H:%M:%S", &tm);
				break;
			case '#': /* process console or remote? */
				cw_dynstr_printf(&prompt, "%c", (option_remote ? '>' : '#'));
				break;
			case '%': /* literal % */
				cw_dynstr_printf(&prompt, "%c", '%');
				break;
			case '\0': /* % is last character - prevent bug */
				t--;
				break;
		}
		t++;
	}
	cw_dynstr_printf(&prompt, "%s", t);

	if (aend)
		promptattr(&aend);
}


static char *cli_prompt(void)
{
	return prompt.data;
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
	} field[] = {
		[F_DATE]     = { "Date",      sizeof("Date") - 1,      sizeof(buf_date),     buf_date     },
		[F_LEVEL]    = { "Level",     sizeof("Level") - 1,     sizeof(buf_level),    buf_level    },
		[F_THREADID] = { "Thread ID", sizeof("Thread ID") - 1, sizeof(buf_threadid), buf_threadid },
		[F_FILE]     = { "File",      sizeof("File") - 1,      sizeof(buf_file),     buf_file     },
		[F_LINE]     = { "Line",      sizeof("Line") - 1,      sizeof(buf_line),     buf_line     },
		[F_FUNCTION] = { "Function",  sizeof("Function") - 1,  sizeof(buf_function), buf_function },
	};
	static int field_len[arraysize(field)];
	static char buf[32768];
	static int pos = 0;
	static int state = 0;
	static char *key, *val;
	static int lkey, lval = -1;
	static enum { MSG_UNKNOWN, MSG_EVENT, MSG_RESPONSE, MSG_FOLLOWS, MSG_VERSION, MSG_COMPLETION } msgtype;
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	int ds_lines = 0;
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

								if (!option_quiet) {
									putc('\n', stdout);
									fputs(key, stdout);
									putc('\n', stdout);

									while (lkey > 0) key[--lkey] = '=';
									fputs(key, stdout);
									fputs("\n\n", stdout);
								}
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
							if (lkey == sizeof("Message")-1 && !memcmp(key, "Message", sizeof("Message") - 1)) {
								exit_prompt();
								fwrite(val, 1, lval, stdout);
							}
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
								remotepid = strtoul(val, NULL, 10);
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
								exit_prompt();

								if (level == CW_LOG_PROGRESS) {
									/* Progress messages suppress input handling until
									 * we get a null progress message to signify the end
									 */
									if (lval) {
										fwrite(key, 1, lval, stdout);
										progress = 2;
									} else {
										putchar('\n');
										progress = 0;
									}
								} else {
									if (progress == 2) {
										putchar('\n');
										progress = 1;
									}

									key[lval++] = '\n';
									if (level != CW_LOG_VERBOSE) {
										fwrite(field[F_DATE].buf, 1, field_len[F_DATE], stdout);
										if (level >= 0 && level < arraysize(level_attr) && level_attr[level].on)
											terminal_write_attr(level_attr[level].on, putchar);
										fwrite(field[F_LEVEL].buf, 1, field_len[F_LEVEL], stdout);
										if (level >= 0 && level < arraysize(level_attr) && level_attr[level].off)
											terminal_write_attr(level_attr[level].off, putchar);
										putchar('[');
										fwrite(field[F_THREADID].buf, 1, field_len[F_THREADID], stdout);
										fwrite("]: ", 1, 3, stdout);
										fwrite(field[F_FILE].buf, 1, field_len[F_FILE], stdout);
										putchar(':');
										fwrite(field[F_LINE].buf, 1, field_len[F_LINE], stdout);
										putchar(' ');
										if (bold_on)
											terminal_write_attr(bold_on, putchar);
										fwrite(field[F_FUNCTION].buf, 1, field_len[F_FUNCTION], stdout);
										if (bold_off)
											terminal_write_attr(bold_off, putchar);
										fwrite(": ", 1, 2, stdout);
									}
									fwrite(key, 1, lval, stdout);
								}
							} else
								state = 0;
						} else
is_data:
						if (lval != sizeof("--END COMMAND--") - 1 || memcmp(key, "--END COMMAND--", sizeof("--END COMMAND--") - 1)) {
							if (msgtype == MSG_FOLLOWS) {
								key[lval++] = '\n';
								cw_dynstr_printf(&ds, "%.*s", lval, key);
								ds_lines++;
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
							if (msgtype == MSG_FOLLOWS) {
								if (!ds.error)
									smart_page((nresp >= 0), &ds, ds_lines);
								cw_dynstr_free(&ds);
								ds_lines = 0;
							}
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
			cw_log(CW_LOG_ERROR, "Console got an overlong line (> %lu bytes!)\n", (unsigned long)sizeof(buf));
			break;
		}
	} while (nresp);

	return 0;
}


static char *dummy_completer(void)
{
	return NULL;
}


static char **cli_completion(const char *text, int start, int end)
{
	struct iovec iov[3];

	CW_UNUSED(text);
	CW_UNUSED(start);
	CW_UNUSED(end);

	if ((matches = malloc(64 * sizeof(matches[0])))) {
		matches_space = 64 - 2;
		matches_count = 1;
		matches[0] = NULL;

		iov[0].iov_base = (void *)"Action: Complete\r\nCommand: ";
		iov[0].iov_len = sizeof("Action: Complete\r\nCommand: ") - 1;
		iov[1].iov_base = rl_line_buffer;
		iov[1].iov_len = strlen(rl_line_buffer);
		iov[2].iov_base = (void *)"\r\n\r\n";
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

	CW_UNUSED(data);

	rl_callback_handler_remove();
	fputs("\r\n", stdout);
	fflush(stdout);
	set_title("");

	if ((p = getenv("HOME"))) {
		snprintf(filename, sizeof(filename), "%s/.callweaver_history", p);
		write_history(filename);
	}
}


static void console_handler(char *s)
{
	if (s) {
		while (isspace(*s)) s++;

		if (*s) {
			HIST_ENTRY *last_he;
    
			history_set_pos(history_length);
			last_he = previous_history();
			if (!last_he || strcmp(last_he->line, s) != 0)
				add_history(s);

			if (s[0] == '!') {
				if (s[1])
					cw_safe_system(s+1);
				else
					cw_safe_system(getenv("SHELL") ? getenv("SHELL") : "/bin/sh");
			} else if (s[0] == '?') {
				static struct cw_dynstr ds = CW_DYNSTR_INIT;

				cw_cli_command(&ds, s);
				fwrite(ds.data, 1, ds.used, stdout);
				cw_dynstr_reset(&ds);
			} else if (option_remote && (!strcasecmp(s, "quit") || !strcasecmp(s, "exit"))) {
				console_cleanup(NULL);
				exit(0);
			} else {
				struct iovec iov[3];

				iov[0].iov_base = (void *)"Action: Command\r\nCommand: ";
				iov[0].iov_len = sizeof("Action: Command\r\nCommand: ") - 1;
				iov[1].iov_base = s;
				iov[1].iov_len = strlen(s);
				iov[2].iov_base = (void *)"\r\n\r\n";
				iov[2].iov_len = sizeof("\r\n\r\n") - 1;
				if (cw_writev_all(console_sock, iov, 3) < 1) {
					cw_log(CW_LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					pthread_detach(pthread_self());
					pthread_cancel(pthread_self());
				}
				read_message(console_sock, 1);
				prompting = 1;
			}
		}
	} else if (option_remote) {
		console_cleanup(NULL);
		exit(0);
	} else {
		/* Only shutdown if we aren't the built-in console */
		if (console_address)
			shutdown(console_sock, SHUT_WR);
		putc('\n', stdout);
	}
}


static int console_connect(const char *spec, int events, time_t timelimit)
{
	struct sockaddr_un *addr;
	socklen_t addrlen;
	int s = -1;
	int e;

	if (!spec) {
		int sv[2];
		struct mansession *sess;

		addrlen = CW_SOCKADDR_UN_SIZE(sizeof("console"));
		addr = alloca(addrlen);
		addr->sun_family = AF_INTERNAL;
		memcpy(addr->sun_path, "console", sizeof("console"));

		if (!socketpair_cloexec(AF_LOCAL, SOCK_STREAM, 0, sv)
		&& (sess = manager_session_start(manager_session_ami, sv[0], (struct sockaddr *)addr, addrlen, NULL, events, CW_EVENT_FLAG_COMMAND, events))) {
			s = sv[1];
			cw_object_put(sess);
		}
	} else {
		const int reconnects_per_second = 20;
		time_t start;
		int l, last = 0;

		l = strlen(spec) + 1;
		addrlen = CW_SOCKADDR_UN_SIZE(l);
		addr = alloca(addrlen);
		addr->sun_family = AF_LOCAL;
		memcpy(addr->sun_path, spec, l);

		time(&start);

		while (s < 0 && !last) {
			last = (timelimit == 0 || time(NULL) - start > timelimit);

			if ((s = socket_cloexec(addr->sun_family, SOCK_STREAM, 0)) < 0) {
				e = errno;
				if (last)
					fprintf(stderr, "Unable to create socket: %s\n", strerror(errno));
				errno = e;
			} else if (connect(s, addr, addrlen)) {
				e = errno;
				close(s);
				s = -1;
				if (last)
					fprintf(stderr, "Unable to connect to \"%s\": %s\n", spec, strerror(e));
				errno = e;
			}

			if (!last) {
				pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
				pthread_testcancel();
				usleep(1000000 / reconnects_per_second);
				pthread_testcancel();
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
			}
		}

		if (s < 0 && timelimit) {
			fprintf(stderr, "Failed to connect in %ld seconds. Quitting.\n", (long int)timelimit);
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
	char *p;
	int reconnect_time = 0;
	char c;

	console_address = data;
	console_sock = -1;

	pthread_cleanup_push(console_cleanup, NULL);

	terminal_init();

	terminal_highlight(&level_attr[CW_LOG_DEBUG].on, &level_attr[CW_LOG_DEBUG].off, "fg=blue,bold");
	terminal_highlight(&level_attr[CW_LOG_NOTICE].on, &level_attr[CW_LOG_NOTICE].off, "fg=green,bold");
	terminal_highlight(&level_attr[CW_LOG_WARNING].on, &level_attr[CW_LOG_WARNING].off, "fg=yellow,bold");
	terminal_highlight(&level_attr[CW_LOG_ERROR].on, &level_attr[CW_LOG_ERROR].off, "fg=red,bold");

	terminal_highlight(&bold_on, &bold_off, "bold");

	terminal_set_icon("Callweaver");

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGWINCH);
	pthread_sigmask(SIG_UNBLOCK, &sigs, NULL);

	rl_readline_name = "CallWeaver";
	rl_basic_word_break_characters = " \t";
	rl_completer_quote_characters = "\"";

	rl_initialize ();
	rl_editing_mode = 1;
	rl_completion_entry_function = (rl_compentry_func_t *)dummy_completer; /* The typedef varies between platforms */
	rl_attempted_completion_function = cli_completion;

	/* Setup history with 100 entries */
	using_history();
	stifle_history(100);

	clr_eol = rl_get_termcap("ce");

	if ((p = getenv("HOME"))) {
		snprintf(buf, sizeof(buf), "%s/.callweaver_history", p);
		read_history(buf);
	}

	do {
		if (console_address && !option_quiet)
			fprintf(stderr, "Connecting to Callweaver at %s...\n", console_address);

		if ((console_sock = console_connect(console_address, CW_EVENT_FLAG_LOG_ALL | CW_EVENT_FLAG_PROGRESS, reconnect_time)) < 0)
			break;

		reconnect_time = 30;

		/* Dump the connection banner. We don't need it here */
		while (read(console_sock, &c, 1) == 1 && c != '\n');

		cw_write_all(console_sock, "Action: Version\r\n\r\n", sizeof("Action: Version\r\n\r\n") - 1);
		read_message(console_sock, 1);

		if (!option_quiet) {
			/* Make sure verbose and debug settings are what we want or higher
			 * and enable events
			 */
			iov[0].iov_base = (void *)"Action: Events\r\nEventmask: log,progress\r\n\r\nAction: Command\r\nCommand: set verbose atleast ";
			iov[0].iov_len = sizeof("Action: Events\r\nEventmask: log,progress\r\n\r\nAction: Command\r\nCommand: set verbose atleast ") - 1;
			iov[1].iov_base = buf;
			iov[1].iov_len = snprintf(buf, sizeof(buf), "%d", option_verbose);
			iov[2].iov_base = (void *)"\r\n\r\n";
			iov[2].iov_len = sizeof("\r\n\r\n") - 1;
			iov[3].iov_base = (void *)"Action: Command\r\nCommand: set debug atleast ";
			iov[3].iov_len = sizeof("Action: Command\r\nCommand: set debug atleast ") - 1;
			iov[4].iov_base = buf + iov[1].iov_len;
			iov[4].iov_len = snprintf(buf + iov[1].iov_len, sizeof(buf) - iov[1].iov_len, "%d", option_debug);
			iov[5].iov_base = (void *)"\r\n\r\n";
			iov[5].iov_len = sizeof("\r\n\r\n") - 1;
			cw_writev_all(console_sock, iov, 6);
			read_message(console_sock, 3);
		}

		/* Ok, we're ready. If we are the internal console tell the core to boot if it hasn't already */
		if (option_console && !fully_booted)
			kill(cw_mainpid, SIGHUP);

		set_prompt(getenv("CALLWEAVER_PROMPT"));

		progress = 0;
		prompting = 1;
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

					if (!progress && !prompting) {
						prompting = 1;
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
		if (!option_quiet)
			fprintf(stderr, "\nDisconnected from CallWeaver server\n");
		set_title("");
		close(console_sock);
		console_sock = -1;
	} while (option_reconnect);

	pthread_cleanup_pop(1);
	return NULL;
}


int console_oneshot(const char *spec, const char *cmd)
{
	struct iovec iov[3];
	char c;
	int s, n = 1;

	if ((s = console_connect(spec, 0, 0)) >= 0) {
		/* Dump the connection banner. We don't need it here */
		while (read(s, &c, 1) == 1 && c != '\n');

		iov[0].iov_base = (void *)"Action: Command\r\nCommand: ";
		iov[0].iov_len = sizeof("Action: Command\r\nCommand: ") - 1;
		iov[1].iov_base = (void *)cmd;
		iov[1].iov_len = strlen(cmd);
		iov[2].iov_base = (void *)"\r\n\r\n";
		iov[2].iov_len = sizeof("\r\n\r\n") - 1;
		cw_writev_all(s, iov, 3);

		shutdown(s, SHUT_WR);

		while (!read_message(s, -1));
		close(s);
		n = 0;
	}

	return n;
}


/*----------------------------------------------------------------------------*/


static const char setprompt_help[] =
"Usage: ?set prompt <prompt>\n"
"       Set the command prompt used when the console is ready to\n"
"       read a command. CallWeaver allows the prompt string to be\n"
"       customized using the following special formats:\n"
"         %%C*                 - reset colours and attributes to defaults\n"
"         %%C<fg>              - set the foreground colour to colour <fg>\n"
"         %%C<fg>;<bg>         - set the foreground and background colours\n"
"                                to the given colour numbers\n"
"         %%C{fg=<fg>,bg=<bg>} - set the foreground and background colours to\n"
"                                the given colour numbers or names\n"
"                                the available colour numbers and names depend\n"
"                                on the terminal used but for ANSI compliant\n"
"                                terminals would be:\n"
"                                  0/black, 1/red, 2/green, 3/yellow, 4/blue,\n"
"                                  5/magenta, 6/cyan, 7/white\n"
"         %%C{attr,attr,...}   - set the specified attributes\n"
"                                note that attributes may be combined with the\n"
"                                colour name specification above but not all\n"
"                                combinations work on all terminals\n"
"                                the attributes known are:\n"
"                                  altcharset, blink, bold, dim, invis,\n"
"                                  protect, reverse, standout, underline\n"
"         %%d                  - current date as YYYY-MM-DD\n"
"         %%H                  - short remote hostname\n"
"         %%h                  - full remote hostname\n"
#ifdef linux
"         %%l<n>               - load average\n"
"                                  n = 1 - 1 minute load average\n"
"                                  n = 2 - 5 minute load average\n"
"                                  n = 3 - 15 minute load average\n"
#endif
"         %%t                  - time as HH:MM:SS\n"
"         %%#                  - '#' if this is the built in console\n"
"                                '>' if this is a remote console\n"
"         %%%%                 - a single '%'\n";

static int setprompt_handler(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	CW_UNUSED(ds_p);

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	set_prompt(argv[2]);
	rl_set_prompt(prompt.data);

	return RESULT_SUCCESS;
}


static const char setevents_help[] =
"Usage: ?set events <event>[,<event>...]\n"
"       Sets the events that this console is to receive.\n"
"       Events can be:\n"
"           error, warning, notice, verbose, event, dtmf, debug\n";

static int setevents_handler(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	struct cw_dynstr ds = CW_DYNSTR_INIT;
	int i;

	CW_UNUSED(ds_p);

	if (argc < 3)
		return RESULT_SHOWUSAGE;

	cw_dynstr_printf(&ds, "Action: Events\r\nEventmask: %s", argv[2]);
	for (i = 3; i < argc; i++)
		cw_dynstr_printf(&ds, ",%s", argv[i]);
	cw_dynstr_printf(&ds, "\r\n\r\n");

	cw_write_all(console_sock, ds.data, ds.used);
	read_message(console_sock, 1);

	return RESULT_SUCCESS;
}


static struct cw_clicmd builtins[] = {
    {
        .cmda = { "?set", "prompt", NULL },
        .handler = setprompt_handler,
        .summary = "Set the prompt",
        .usage = setprompt_help,
    },
    {
        .cmda = { "?set", "events", NULL },
        .handler = setevents_handler,
        .summary = "Set the events to be logged to this console",
        .usage = setevents_help,
    },
};


void cw_console_cli_init(void)
{
    cw_cli_register_multiple(builtins, arraysize(builtins));
}
