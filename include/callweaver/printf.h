/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Eris Associates Limited, UK
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

#ifndef CALLWEAVER_PRINTF_H
#define CALLWEAVER_PRINTF_H

/*! \file
 *
 * \brief Extended [v]snprintf to allow formatting of struct sockaddrs
 *
 * Allow %@ to be used to format struct sockaddrs in cw_[v]snprintf.
 *
 * %@   prints only the address part            - e.g. 2001:db8::1        or 192.168.3.4
 * %#@  prints bracketed IPv6 addresses         - e.g. [2001:db8::1]      or 192.168.3.4
 * %l@  prints "long" format including the port - e.g. [2001:db8::1]:5060 or 192.168.3.4:5060
 * %#l@ includes the port even if it is zero   - e.g. [2001:db8::1]:0    or 192.168.3.4:0
 * %h@  prints "short" format of just the port  - e.g. 5060
 * %#h@ prints the port even if it is zero     - e.g. 0
 *
 * Normally IPv4-mapped IPv6 addresses are printed as IPv4 addresses. The "long-long"
 * format (%ll@) overrides this and prints as for "long" format but without unmapping
 * IPv4-mapped addresses (i.e. they are printed as ::ffff:a.b.c.d).
 *
 * While subnet masks are not part of a struct sockaddr it is possible to include a
 * subnet by specifying it as the precision, either inline or as an argument:
 * e.g.
 *     snprintf(buf, len, "%.*l@", 64, sa) might give [2001:db8::1/32]:5060
 *
 * IPv4 (struct sockaddr_in), IPv6 (struct sockaddr_in6) and Local/Unix (struct sockaddr_un)
 * address types are supported along with the "file" and "internal" local-like extensions
 * used by Callweaver.
 *
 * \note Only cw_[v]snprintf are explicitly implemented. If you are using a GNU libc or uLibC
 * based system that allows new type specifiers to be registered then %@ will work with
 * the full range of *printf functions and you need to not use the cw_ prefix. However this
 * is unsupported and will not work on other system where no such extension capability
 * exists in libc.
 */


#if defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION)

#include <printf.h>

#define cw_vsnprintf(str, len, format, ap)	vsnprintf((str), (len), (format), (ap))
#define cw_snprintf(str, len, format, ...)	snprintf((str), (len), (format), __VA_ARGS__)

/* The following are extern solely so they can be registered on start up */
extern int print_sockaddr(FILE *stream, const struct printf_info *info, const void *const *args);
#ifdef HAVE_REGISTER_PRINTF_SPECIFIER
extern int print_sockaddr_arginfo(const struct printf_info *info, size_t n, int *argtypes, int *sizes);
#else
extern int print_sockaddr_arginfo(const struct printf_info *info, size_t n, int *argtypes);
#endif


#else /* defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION) */

extern CW_API_PUBLIC int cw_vsnprintf(char *str, size_t len, const char *format, va_list ap);
extern CW_API_PUBLIC int cw_snprintf(char *str, size_t len, const char *format, ...)
	__attribute__ ((format (printf, 3,4)));


#endif /* defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION) */

#endif /* CALLWEAVER_PRINTF_H */
