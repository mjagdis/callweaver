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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

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


static int console_sock;
static char *remotehostname;


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


static char **cw_rl_strtoarr(char *buf)
{
	char **match_list = NULL, *retstr;
	size_t match_list_len;
	int matches = 0;

	match_list_len = 1;
	while ( (retstr = strsep(&buf, " ")) != NULL) {

		if (!strcmp(retstr, CW_CLI_COMPLETE_EOF))
			break;
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			match_list = realloc(match_list, match_list_len * sizeof(char *));
		}

		match_list[matches++] = strdup(retstr);
	}

	if (!match_list)
		return NULL;

	if (matches>= match_list_len)
		match_list = realloc(match_list, (match_list_len + 1) * sizeof(char *));

	match_list[matches] = NULL;

	return match_list;
}


static char *dummy_completer(char *text, int state)
{
    return NULL;
}


static char **cli_completion(const char *text, int start, int end)
{
    int nummatches = 0;
    char buf[2048];
    char **matches;
    int res;

    matches = NULL;
    if (option_remote)
    {
        res = snprintf(buf, sizeof(buf), "_COMMAND NUMMATCHES \"%s\" \"%s\"", (char *)rl_line_buffer, (char *)text);
        if (res < sizeof(buf)) {
            write(console_sock, buf, res);
            res = read(console_sock, buf, sizeof(buf));
            buf[res] = '\0';
            nummatches = atoi(buf);
        }

        if (nummatches > 0)
        {
            char *mbuf;
            int mlen = 0, maxmbuf = 2048;
            // Start with a 2048 byte buffer
            mbuf = malloc(maxmbuf);
            if (!mbuf)
                return (matches);

            mbuf[0] = '\0';
            res = snprintf(buf, sizeof(buf),"_COMMAND MATCHESARRAY \"%s\" \"%s\"", (char *)rl_line_buffer, (char *)text);
	    if (res < sizeof(buf)) {
                write(console_sock, buf, res);
                res = 0;

                while (!strstr(mbuf, CW_CLI_COMPLETE_EOF) && res != -1)
                {
                    if (mlen + 1024 > maxmbuf)
                    {
                        // Every step increment buffer 1024 bytes
                        maxmbuf += 1024;
                        mbuf = realloc(mbuf, maxmbuf);
                        if (!mbuf)
                            return (matches);
                    }
                    // Only read 1024 bytes at a time
                    res = read(console_sock, mbuf + mlen, 1024);
                    if (res > 0)
                        mlen += res;
                }
                mbuf[mlen] = '\0';

                matches = cw_rl_strtoarr(mbuf);
            }
            free(mbuf);
        }

    }
    else
    {
        nummatches = cw_cli_generatornummatches((char *)rl_line_buffer, (char*)text);

        if (nummatches > 0 )
            matches = cw_cli_completion_matches((char*)rl_line_buffer, (char*)text);
    }
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
				if (write(console_sock, s, strlen(s)) < 1 || write(console_sock, "\n", 1) < 1) {
					cw_log(CW_LOG_WARNING, "Unable to write: %s\n", strerror(errno));
					pthread_detach(pthread_self());
					pthread_cancel(pthread_self());
				}
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


static int console_connect(char *spec)
{
	union {
		struct sockaddr sa;
		struct sockaddr_un sun;
	} u;
	socklen_t salen;
	int s = -1;

	if (strlen(spec) > sizeof(u.sun.sun_path) - 1) {
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
	char banner[80];
	sigset_t sigs;
	char *spec = data;
	char *clr_eol;
	char *stringp;
	char *version;
	char *p;
	int update_delay;
	int res;
	int pid;

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

		fprintf(stderr, "Connecting to Callweaver at %s...\n", spec);
		for (tries = 0; console_sock < 0 && tries < 30 * reconnects_per_second; tries++) {
			if ((console_sock = console_connect(spec)) < 0) {
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

		/* Read the welcome line that contains hostname, version and pid */
		p = banner;
		while (p - banner < sizeof(banner) - 1 && read(console_sock, p, 1) == 1 && p[0] != '\r' && p[0] != '\n')
			p++;
		*p = '\0';

		/* Make sure verbose and debug settings are what we want or higher
		 * and enable events
		 */
		res = snprintf(buf, sizeof(buf), "set verbose atleast %d\nset debug atleast %d\n\020events\n", option_verbose, option_debug);
		write(console_sock, buf, (res <= sizeof(buf) ? res : sizeof(buf)));

		stringp = banner;
		remotehostname = strsep(&stringp, "/");
		p = strsep(&stringp, "/");
		version = strsep(&stringp, "\n");
		if (!version)
			version = "Callweaver <Version Unknown>";
		stringp = remotehostname;
		strsep(&stringp, ".");
		pid = (p ? atoi(p) : -1);

		res = snprintf(buf, sizeof(buf), "%s running on %s (pid %u)", version, remotehostname, pid);
		if (res < 0 || res >= sizeof(buf))
			buf[sizeof(buf)-1] = '\0';

		set_title(buf);

		putc('\n', stdout);
		fputs(buf, stdout);
		putc('\n', stdout);
		for (p = buf; *p; p++)
			putc('=', stdout);
		fputs("\n\n", stdout);

		update_delay = -1;

		if (option_console || option_remote)
			rl_callback_handler_install(cli_prompt(), console_handler);

		for (;;) {
			struct pollfd pfd[2];
			int ret;

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();

			pfd[0].fd = console_sock;
			pfd[1].fd = fileno(stdin);
			pfd[0].events = pfd[1].events = POLLIN;
			pfd[0].revents = pfd[1].revents = 0;

			ret = poll(pfd, (option_console || option_remote ? 2 : 1), update_delay);

			pthread_testcancel();
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			if (ret == 0) {
				if (update_delay >= 0) {
					rl_forced_update_display();
					update_delay = -1;
				}
			} else if (ret >= 0) {
				if (pfd[0].revents) {
					if ((ret = read(console_sock, buf, sizeof(buf) - 1)) > 0) {
						if (update_delay < 0 && (option_console || option_remote)) {
							if (clr_eol) {
								terminal_write("\r", 1);
								fputs(clr_eol, stdout);
							} else
								terminal_write("\r\n", 2);
						}

						terminal_write(buf, ret);

						/* If we have clear to end of line we can redisplay the input line
						 * every time the output ends in a new line. Otherwise we want to
						 * wait and see if there's more output coming because we don't
						 * have any way of backing up and replacing the current line.
						 * Of course, if we don't care about input there's no problem...
						 */
						if (option_console || option_remote) {
							if (clr_eol && buf[ret - 1] == '\n') {
								rl_forced_update_display();
								update_delay = -1;
							} else
								update_delay = 100;
						}
						fflush(stdout);
					} else
						break;
				}

				if (pfd[1].revents) {
					if (update_delay >= 0) {
						rl_forced_update_display();
						update_delay = -1;
					}
					rl_callback_read_char();
				}
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
	char buf[1024];
	int s, n;

	if ((s = console_connect(spec)) >= 0) {
		/* Dump the connection banner. We don't need it here */
		read(s, buf, sizeof(buf));

		write(s, cmd, strlen(cmd));
		write(s, "\n", 2);
		shutdown(s, SHUT_WR);

		while ((n = read(s, buf, sizeof(buf))) > 0)
			write(STDOUT_FILENO, buf, n);
		close(s);
		n = 0;
	} else {
		fprintf(stderr, "Unable to connect to Callweaver at %s\n", spec);
		n = 1;
	}

	return n;
}
