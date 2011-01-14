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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/sockaddr.h"
#include "callweaver/printf.h"
#include "callweaver/utils.h"


#if defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION)

#include <printf.h>

#else /* HAVE_REGISTER_PRINTF_FUNCTION */

struct printf_info {
	int prec;
	int width;
	unsigned int alt:1;
	unsigned int is_long:1;
	unsigned int is_short:1;
	unsigned int left:1;
};

#endif /* HAVE_REGISTER_PRINTF_FUNCTION */


static int sockaddr_fmt(char *buf, size_t len, const struct sockaddr *sa, const struct printf_info *info)
{
	struct sockaddr_in sinbuf;
	size_t space = len;
	int n, prec = info->prec;
	uint16_t port = 0;

	if (!info->is_long_double && cw_sockaddr_is_mapped(sa)) {
		cw_sockaddr_unmap(&sinbuf, (struct sockaddr_in6 *)sa);
		prec -= (sizeof(((struct sockaddr_in6 *)0)->sin6_addr) - sizeof(((struct sockaddr_in *)0)->sin_addr)) * 8;
		sa = (struct sockaddr *)&sinbuf;
	}

	switch (sa->sa_family) {
		case AF_INET: {
			struct sockaddr_in *sin = (struct sockaddr_in *)sa;

			if (!info->is_short) {
				inet_ntop(sin->sin_family, &sin->sin_addr, buf, space);
				n = strlen(buf);
				buf += n;
				space -= n;
			}

			if (prec >= 0) {
				n = sprintf(buf, "/%lu", (prec < sizeof(sin->sin_addr) * 8 ? prec : sizeof(sin->sin_addr) * 8));
				buf += n;
				space -= n;
			}

			port = htons(sin->sin_port);
			break;
		}

		case AF_INET6: {
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

			if (!info->is_short) {
				if (info->alt || (sin6->sin6_port && info->is_long)) {
					*(buf++) = '[';
					space--;
				}

				inet_ntop(sin6->sin6_family, &sin6->sin6_addr, buf, space);
				n = strlen(buf);
				buf += n;
				space -= n;

				if (prec >= 0) {
					n = sprintf(buf, "/%lu", (prec < sizeof(sin6->sin6_addr) * 8 ? prec : sizeof(sin6->sin6_addr) * 8));
					buf += n;
					space -= n;
				}

				if (info->alt || (sin6->sin6_port && info->is_long)) {
					buf[0] = ']';
					buf[1] = '\0';
					buf++;
					space--;
				}
			}

			port = htons(sin6->sin6_port);
			break;
		}
	}

	if (port || info->alt) {
		if (info->is_long) {
			*(buf++) = ':';
			space--;
		}
		if (info->is_long || info->is_short)
			space -= sprintf(buf, "%hu", port);
	}

	return len - space;
}


#if defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION)

int print_sockaddr(FILE *stream, const struct printf_info *info, const void *const *args)
{
	struct sockaddr *sa = *((struct sockaddr **)(args[0]));
	int ret = 0;

	if (sa) switch (sa->sa_family) {
		case AF_UNSPEC:
			ret = fprintf(stream, "%*s", (info->left ? -info->width : info->width), (info->is_short ? "" : "(Unspecified)"));
			break;

		case AF_LOCAL:
		case AF_PATHNAME:
		case AF_INTERNAL: {
			struct sockaddr_un *sun = (struct sockaddr_un *)sa;
			const char *domain;
			int n;

			switch (sa->sa_family) {
				case AF_LOCAL:
					domain = "local";
					ret = sizeof("local") - 1 + 1;
					break;
				case AF_PATHNAME:
					domain = "file";
					ret = sizeof("file") - 1 + 1;
					break;
				case AF_INTERNAL:
				default:
					domain = "internal";
					ret = sizeof("internal") - 1 + 1;
					break;
			}

			ret += strlen(sun->sun_path);
			n = info->width - ret;
			if (!info->left && n > 0) {
				ret += n;
				fprintf(stream, "%*s", n, "");
			}
			fprintf(stream, "%s:%s", domain, sun->sun_path);
			if (info->left && n > 0) {
				ret += n;
				fprintf(stream, "%*s", n, "");
			}
			break;
		}

		default: {
			char buf[CW_MAX_ADDRSTRLEN];

			int n = sockaddr_fmt(buf, sizeof(buf), sa, info);
			ret = fprintf(stream, "%*.*s", (info->left ? -info->width : info->width), n, buf);
			break;
		}
	} else
		ret = fprintf(stream, "%*s", (info->left ? -info->width : info->width), "(null)");

	return ret;
}


#ifdef HAVE_REGISTER_PRINTF_SPECIFIER
int print_sockaddr_arginfo(const struct printf_info *info, size_t n, int *argtypes, int *sizes)
#else
int print_sockaddr_arginfo(const struct printf_info *info, size_t n, int *argtypes)
#endif
{
	CW_UNUSED(info);

	/* We always take exactly one argument and this is a pointer to a struct sockaddr */
	if (n > 0) {
		argtypes[0] = PA_POINTER;
#ifdef HAVE_REGISTER_PRINTF_SPECIFIER
		sizes[0] = sizeof(struct sockaddr *);
#endif
	}
	return 1;
}


#else /* defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION) */


static int print_sockaddr(char *str, size_t len, const struct sockaddr *sa, const struct printf_info *info)
{
	int ret = 0;

	if (sa) switch (sa->sa_family) {
		case AF_UNSPEC:
			ret = snprintf(str, len, "%*s", (info->left ? -info->width : info->width), (info->is_short ? "" : "(Unspecified)"));
			break;

		case AF_LOCAL:
		case AF_PATHNAME:
		case AF_INTERNAL: {
			struct sockaddr_un *sun = (struct sockaddr_un *)sa;
			const char *domain;
			int n;

			switch (sa->sa_family) {
				case AF_LOCAL:
					domain = "local";
					ret = sizeof("local") - 1 + 1;
					break;
				case AF_PATHNAME:
					domain = "file";
					ret = sizeof("file") - 1 + 1;
					break;
				case AF_INTERNAL:
				default:
					domain = "internal";
					ret = sizeof("internal") - 1 + 1;
					break;
			}

			ret += strlen(sun->sun_path);

			n = info->width - ret;
			if (!info->left && n > 0) {
				snprintf(str, len, "%*s", n, "");
				str += n;
				len = (len > n ? len - n : 0);
			}
			snprintf(str, len, "%s:%s", domain, sun->sun_path);
			if (info->left && n > 0)
				snprintf(str + ret, (len > ret ? len - ret : 0), "%*s", n, "");
			if (n > 0)
				ret += n;
			break;
		}

		default: {
			char buf[CW_MAX_ADDRSTRLEN];

			int n = sockaddr_fmt(buf, sizeof(buf), sa, info);
			ret = snprintf(str, len, "%*.*s", (info->left ? -info->width : info->width), n, buf);
			break;
		}
	} else
		ret = snprintf(str, len, "%*s", (info->left ? -info->width : info->width), "(null)");

	return ret;
}


static void scan_fmt(const char *fmt, const char *fields[5])
{
	/* scan "%[flags][width][.prec][length][conv]" */
	fields[0] = fmt;
	fmt += strspn(fmt, "#0- +'I");

	fields[1] = fmt;
	if (*fmt == '*')
		fmt++;
	else {
		if (*fmt == '-')
			fmt++;
		while (isdigit(*fmt))
			fmt++;
	}

	fields[2] = fmt;
	if (*fmt == '.') {
		fmt++;
		if (*fmt == '*')
			fmt++;
		else {
			while (isdigit(*fmt))
				fmt++;
		}
	}

	fields[3] = fmt;
	if ((fmt[0] == 'h' && fmt[1] == 'h') || (fmt[0] == 'l' && fmt[1] == 'l'))
		fmt += 2;
	else if (strchr("hlLqjzt", *fmt))
		fmt++;

	fields[4] = fmt;
}


int cw_vsnprintf(char *str, size_t len, const char *format, va_list ap)
{
	const char *fields[5], *p;
	char *fmt;
	ptrdiff_t n;
	size_t fmtlen;
	size_t space;

	fmt = NULL;
	fmtlen = 0;
	space = len;
	n = 0;
	p = format;
	while (*p) {
		va_list aq;

		va_copy(aq, ap);

		while (*p) {
			if (*(p++) == '%') {
				if (*p) {
					scan_fmt(p, fields);
					if (fields[4][0] == '@')
						break;
					if (fields[1][0] == '*')
						va_arg(ap, int);
					if (fields[2][0] == '.' && fields[2][1] == '*')
						va_arg(ap, int);
					if (strchr("diouxXcC", fields[4][0]))
						va_arg(ap, int);
					else if (strchr("eEfFgGaA", fields[4][0]))
						va_arg(ap, double);
					else if (strchr("sSpn", fields[4][0]))
						va_arg(ap, void *);
					p = fields[4] + 1;
				}
			}
		}

		if (!*p) {
			if ((n = vsnprintf(str, space, format, aq)) > 0)
				space -= n;
			break;
		} else {
			struct printf_info info;
			struct sockaddr *sa;
			char *nfmt;

			n = fields[0] - format - 1;
			if (n >= fmtlen && (nfmt = realloc(fmt, n + 1))) {
				fmtlen = n + 1;
				fmt = nfmt;
			}

			if (fmtlen > n) {
				memcpy(fmt, format, n);
				fmt[n] = '\0';

				if ((n = vsnprintf(str, space, fmt, aq)) < 0)
					break;
				str += n;
				space -= n;
			}

			info.alt = (fields[0][0] == '#');
			info.width = (fields[1][0] == '*' ? va_arg(ap, int) : atoi(fields[1]));
			if ((info.left = (info.width < 0)))
				info.width = -info.width;
			if (fields[2][0] != '.')
				info.prec = -1;
			else if (fields[2][1] == '*')
				info.prec = va_arg(ap, int);
			else
				info.prec = atoi(&fields[2][1]);
			info.is_long = (fields[3][0] == 'l');
			info.is_short = (fields[3][0] == 'h');

			sa = va_arg(ap, struct sockaddr *);
			n = print_sockaddr(str, space, sa, &info);
			str += n;
			space -= n;

			format = p = fields[4] + 1;
		}

		va_end(aq);
	}

	if (fmt)
		free(fmt);

	return (n >= 0 ? len - space : n);
}


int cw_snprintf(char *str, size_t len, const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = cw_vsnprintf(str, len, format, ap);
	va_end(ap);

	return ret;
}


#endif /* defined(HAVE_REGISTER_PRINTF_SPECIFIER) || defined(HAVE_REGISTER_PRINTF_FUNCTION) */


#ifdef TEST
int main(int argc, char *argv[])
{
	char buf[1024];
	struct cw_sockaddr_net addr;

#if defined(HAVE_REGISTER_PRINTF_SPECIFIER)
	register_printf_specifier('@', print_sockaddr, print_sockaddr_arginfo);
#elif defined(HAVE_REGISTER_PRINTF_FUNCTION)
	register_printf_function('@', print_sockaddr, print_sockaddr_arginfo);
#endif

	addr.sin.sin_family = AF_INET;
	inet_pton(AF_INET, "1.2.3.4", &addr.sin.sin_addr);
	addr.sin.sin_port = htons(5060);

	cw_snprintf(buf, sizeof(buf), "1.2.3.4                      => %@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "        1.2.3.4              => %32@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "1.2.3.4:5060                 => %l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "   1.2.3.4:5060              => %32l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "           5060              => %32h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "1.2.3.4/24:5060              => %.*l@\n", 24, &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "1.2.3.4/24:5060              => %32.*l@\n", 24, &addr.sa); fputs(buf, stdout);

	addr.sin6.sin6_family = AF_INET6;
	inet_pton(AF_INET6, "2001:db8::1", &addr.sin6.sin6_addr);
	addr.sin6.sin6_port = htons(5060);

	cw_snprintf(buf, sizeof(buf), "2001:db8::1                  => %@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "2001:db8::1                  => %32@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1]                => %#@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1]                => %#32@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1]:5060           => %l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1]:5060           => %32l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %32h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1/64]:5060        => %.*l@\n", 64, &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[2001:db8::1/64]:5060        => %32.*l@\n", 64, &addr.sa); fputs(buf, stdout);

	addr.sin6.sin6_family = AF_INET6;
	inet_pton(AF_INET6, "::ffff:192.168.3.4", &addr.sin6.sin6_addr);
	addr.sin6.sin6_port = htons(5060);

	cw_snprintf(buf, sizeof(buf), "::ffff:192.168.3.4           => %ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "::ffff:192.168.3.4           => %32ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]         => %#ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]         => %#32ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]:5060    => %ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]:5060    => %32ll@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %32h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4/64]:5060 => %.*ll@\n", 112, &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4/64]:5060 => %32.*ll@\n", 112, &addr.sa); fputs(buf, stdout);

	cw_snprintf(buf, sizeof(buf), "::ffff:192.168.3.4           => %@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "::ffff:192.168.3.4           => %32@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]         => %#@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]         => %#32@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]:5060    => %l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4]:5060    => %32l@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "5060                         => %32h@\n", &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4/64]:5060 => %.*l@\n", 112, &addr.sa); fputs(buf, stdout);
	cw_snprintf(buf, sizeof(buf), "[::ffff:192.168.3.4/64]:5060 => %32.*l@\n", 112, &addr.sa); fputs(buf, stdout);

	return 0;
}
#endif

/* cc -Wall -Wno-format -g -DTEST -DHAVE_CONFIG_H -Iinclude -I/home/mjagdis/build/callweaver/include -include include/global.h corelib/printf.c
 * icc -Wall -g -DTEST -DHAVE_CONFIG_H -Iinclude -I/home/mjagdis/build/callweaver/include -include include/global.h corelib/printf.c
 */
