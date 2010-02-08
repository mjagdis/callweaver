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

extern void terminal_highlight(const char **start, const char **end, const char *spec);
extern int terminal_write_attr(const char *str);
extern void terminal_init(void);
extern void terminal_set_icon(const char *s);
