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
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")


#include <callweaver/logger.h>

#include <unistd.h>

/* term.h and curses.h have a lot of defines most of which do not
 * have leading uniques tags such as term_ or curses_. This causes
 * chaos if you include it in any substantial file...
 */
#include <curses.h>
#include <term.h>

#include "terminal.h"


static char *terminal_type;

static char *sgr, *setaf, *setab;

static struct {
	const char *on, *off;
} level_attr[] = {
	[__CW_LOG_DEBUG] = { NULL, NULL },
	[__CW_LOG_EVENT] = { NULL, NULL },
	[__CW_LOG_NOTICE] = { NULL, NULL },
	[__CW_LOG_WARNING] = { NULL, NULL },
	[__CW_LOG_ERROR] = { NULL, NULL },
	[__CW_LOG_VERBOSE] = { NULL, NULL },
	[__CW_LOG_DTMF] = { NULL, NULL },
};

static const char *col_defaults;
static int col_defaults_len;
static const char *attr_end;
static int attr_end_len;


static void highlight(const char **start, const char **end, const char *spec)
{
	static struct {
		int len;
		const char *name;
	} colours[] = {
		[0] = { 5, "black" },
		[1] = { 3, "red" },
		[2] = { 5, "green" },
		[3] = { 6, "yellow" },
		[4] = { 4, "blue" },
		[5] = { 7, "magenta" },
		[6] = { 4, "cyan" },
		[7] = { 5, "white" },
	};
	static struct {
		int len;
		const char *name;
	} attributes[] = {
		[0] = { 8, "standout" },
		[1] = { 9, "underline" },
		[2] = { 7, "reverse" },
		[3] = { 5, "blink" },
		[4] = { 3, "dim" },
		[5] = { 4, "bold" },
		[6] = { 5, "invis" },
		[7] = { 7, "protect" },
		[8] = { 10, "altcharset" },
	};
	int attr[sizeof(attributes) / sizeof(attributes[0])];
	const char *p, *attr_p, *fg_p, *bg_p;
	char *tmp;
	int fg, bg, have_attr, l, i;

	fg = bg = -1;
	have_attr = 0;
	memset(attr, 0, sizeof(attr));

	do {
		int *col;
		int known;

		p = strchr(spec, ',');
		l = (p ? p - spec : strlen(spec));

		known = 0;

		col = &fg;
		if (l > 3) {
			if (!strncmp(spec, "bg=", 3)) {
				col = &bg;
				spec += 3;
				l -= 3;
			} else if (!strncmp(spec, "fg=", 3)) {
				spec += 3;
				l -= 3;
			}
		}

		for (i = 0; i < sizeof(colours) / sizeof(colours[0]); i++) {
			if (l == colours[i].len && !strncmp(spec, colours[i].name, l)) {
				*col = i;
				known = 1;
				break;
			}
		}

		if (!known) {
			for (i = 0; i < sizeof(attributes) / sizeof(attributes[0]); i++) {
				if (l == attributes[i].len && !strncmp(spec, attributes[i].name, l)) {
					attr[i] = 1;
					have_attr = known = 1;
					break;
				}
			}
		}

		if (!known)
			fprintf(stderr, "attribute \"%*.*s\" is not recognisable\n", l, l, spec);
	} while ((spec = (p ? p + 1 : NULL)));

	l = 0;
	attr_p = fg_p = bg_p = "";

	if (attr_end && have_attr) {
		l += attr_end_len;
		attr_p = attr_end;
	}

	if (col_defaults && ((fg != -1 && setaf) || (bg != -1 && setab))) {
		l += col_defaults_len;
		fg_p = col_defaults;
	}

	if (l && (tmp = malloc(l + 1))) {
		strcpy(tmp, attr_p);
		strcat(tmp, fg_p);
		*end = tmp;

		attr_p = fg_p = bg_p = "";
		l = 0;

		if (sgr && have_attr) {
			p = tparm(sgr, attr[0], attr[1], attr[2], attr[3], attr[4], attr[5], attr[6], attr[7], attr[8]);
			i = strlen(p);
			attr_p = strcpy(alloca(i + 1), p);
			l += i;
		}

		if (setaf && fg != -1) {
			p = tparm(setaf, fg);
			i = strlen(p);
			fg_p = strcpy(alloca(i + 1), p);
			l += i;
		}

		if (setab && bg != -1) {
			p = tparm(setaf, bg);
			i = strlen(p);
			bg_p = strcpy(alloca(i + 1), p);
			l += i;
		}

		if (l && (tmp = malloc(l + 1))) {
			strcpy(tmp, attr_p);
			strcat(tmp, fg_p);
			strcat(tmp, bg_p);
			*start = tmp;
		}
	}
}


void terminal_init(void)
{
	int i;

	if (isatty(fileno(stdout)) && (terminal_type = getenv("TERM"))) {
		if (setupterm(terminal_type, fileno(stderr), &i) != ERR) {
			if ((col_defaults = tigetstr("op")) && col_defaults != (char *)-1) {
				col_defaults_len = strlen(col_defaults);
				if ((setaf = tigetstr("setaf")) == (char *)-1)
					setaf = NULL;
				if ((setab = tigetstr("setab")) == (char *)-1)
					setab = NULL;
			} else
				col_defaults = NULL;

			if ((sgr = tigetstr("sgr")) && sgr != (char *)-1) {
				attr_end = strdup(tparm(sgr, 0, 0, 0, 0, 0, 0, 0, 0, 0));
				attr_end_len = strlen(attr_end);
			} else
				sgr = NULL;

			highlight(&level_attr[__CW_LOG_DEBUG].on, &level_attr[__CW_LOG_DEBUG].off, "fg=blue,bold");
			highlight(&level_attr[__CW_LOG_NOTICE].on, &level_attr[__CW_LOG_NOTICE].off, "fg=green,bold");
			highlight(&level_attr[__CW_LOG_WARNING].on, &level_attr[__CW_LOG_WARNING].off, "fg=yellow,bold");
			highlight(&level_attr[__CW_LOG_ERROR].on, &level_attr[__CW_LOG_ERROR].off, "fg=red,bold");
		}
	}
}


void terminal_set_icon(const char *s)
{
	if (strstr(terminal_type, "xterm")) {
		fputs("\033]1;Callweaver\007", stderr);
		fflush(stderr);
	}
}


void terminal_write(const char *buf, int len)
{
	static char word[80];
	static int windex = 0;
	static int state = 0;

	while (len) {
		if (state == 0) {
			if (isalnum(*buf) || (len > 1 && strchr("/_.", *buf) && isalnum(buf[1])))
				state = 1;
			else if (*buf == ' ')
				state = 2;
			else if (ispunct(*buf))
				state = 3;
			else
				state = 4;
		}

		switch (state) {
			case 1:
				while (len && (isalnum(*buf) || (len > 1 && strchr(":/_.", *buf) && isalnum(buf[1]))) && windex < sizeof(word)/sizeof(word[0])) {
					len--;
					word[windex++] = *(buf++);
				}
				break;

			case 2:
				while (len && *buf == ' ' && windex < sizeof(word)/sizeof(word[0])) {
					len--;
					word[windex++] = *(buf++);
				}
				break;

			case 3:
				while (len && ispunct(*buf) && (len <= 1 || !strchr("/_.", *buf) || !isalnum(buf[1])) && windex < sizeof(word)/sizeof(word[0])) {
					len--;
					word[windex++] = *(buf++);
				}
				break;

			case 4:
				//if (len && !isprint(*buf) && windex < sizeof(word)/sizeof(word[0])) {
				len--;
				word[windex++] = *(buf++);
				state = 0;
				//}
				break;
		}

		if (len && windex < sizeof(word)/sizeof(word[0]))
			state = 0;

		if (len || windex < sizeof(word)/sizeof(word[0])) {
#if 1
			int i = __CW_LOG_VERBOSE;

			if (!strncmp(word, "DEBUG", windex))
				i = __CW_LOG_DEBUG;
			else if (!strncmp(word, "EVENT", windex))
				i = __CW_LOG_EVENT;
			else if (!strncmp(word, "NOTICE", windex))
				i = __CW_LOG_NOTICE;
			else if (!strncmp(word, "WARNING", windex))
				i = __CW_LOG_WARNING;
			else if (!strncmp(word, "ERROR", windex))
				i = __CW_LOG_ERROR;
			else if (!strncmp(word, "VERBOSE", windex))
				i = __CW_LOG_VERBOSE;
			else if (!strncmp(word, "DTMF", windex))
				i = __CW_LOG_DTMF;

			if (level_attr[i].on)
				putp(level_attr[i].on);
			fwrite(word, sizeof(word[0]), windex, stdout);
			if (level_attr[i].off)
				putp(level_attr[i].off);
#else
			if (windex == 1 && !isprint(word[0])) {
				printf("0x%02x\n", word[0]);
			} else {
				fprintf(stdout, "%d \"", windex);
				fwrite(word, sizeof(word[0]), windex, stdout);
				fputs("\"\n", stdout);
			}
#endif

			windex = 0;
		}
	}
}
