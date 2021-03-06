/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

/*! \file
 *
 * \brief Utility functions
 *
 * \note These are important for portability and security,
 * so please use them in favour of other routines.
 * Please consult the CODING GUIDELINES for more information.
 */
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/evp.h>

#include <idna.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/strings.h"
#include "callweaver/time.h"
#include "callweaver/utils.h"
#include "callweaver/lock.h"
#include "callweaver/io.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/config.h"
#include "callweaver/module.h"


static char base64[64];
static char b2a[256];


int cw_writev_all(int fd, struct iovec *iov, int count)
{
	int n, written;

	n = written = 0;
	while (count && (n = writev(fd, iov, count)) > 0) {
		int x = n;

		written += n;

		while (x && x >= iov[0].iov_len) {
			x -= iov[0].iov_len;
			count--;
			iov++;
		}
		if (x) {
			iov[0].iov_base = (char *)iov[0].iov_base + x;
			iov[0].iov_len -= x;
		}
	}

	return (n <= 0 ? n : written);
}


int cw_write_all(int fd, const char *data, int len)
{
	int pending = len;
	int written = 0;

	while (pending && (written = write(fd, data, pending)) > 0) {
		data += written;
		pending -= written;
	}

	return (written <= 0 ? written : len);
}


#ifndef O_CLOEXEC
int open_cloexec(const char *pathname, int flags, mode_t mode)
{
	int fd = open(pathname, flags, mode);
	if (fd >= 0)
		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	return fd;
}
#endif


#ifndef SOCK_CLOEXEC
int socket_cloexec(int domain, int type, int protocol)
{
	int fd = socket(domain, type, protocol);
	if (fd >= 0)
		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	return fd;
}

int socketpair_cloexec(int domain, int type, int protocol, int sv[2])
{
	int res;

	if (!(res = socketpair(domain, type, protocol, sv))) {
		fcntl(sv[0], F_SETFD, fcntl(sv[0], F_GETFD, 0) | FD_CLOEXEC);
		fcntl(sv[1], F_SETFD, fcntl(sv[1], F_GETFD, 0) | FD_CLOEXEC);
	}
	return fd;
}
#endif

#if !defined(SOCK_CLOEXEC) || !defined(HAVE_ACCEPT4)
int accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int fd = accept(sockfd, addr, addrlen);
	if (fd >= 0)
		fcntl(fd, F_SETFD, fcntl(fd, F_GETFD, 0) | FD_CLOEXEC);
	return fd;
}
#endif


/*! \brief Re-entrant (thread safe) version of gethostbyname that replaces the 
   standard gethostbyname (which is not thread safe)
*/
struct hostent *cw_gethostbyname(const char *host, struct cw_hostent *hp)
{
	const char *s;
	struct hostent *result = NULL;
#if !defined(GETHOSTBYNAME_DOES_IDN) && HAVE_LIBIDN
	char *idnahost;
#endif
	int res;
	int herrno;
	int dots;

	if (host && hp) {
		/* Although it is perfectly legitimate to lookup a pure integer, for
		   the sake of the sanity of people who like to name their peers as
		   integers, we break with tradition and refuse to look up a
		   pure integer */
		dots = res = 0;
		for (s = host; *s; s++) {
			if (!isdigit(*s))
				break;
			if (*s == '.')
				dots++;
		}

		if (!*s) {
			/* Forge a reply for IP's to avoid octal IP's being interpreted as octal */
			if (dots != 3)
				goto out_fail;

			hp->hp.h_addrtype = AF_INET;
			hp->hp.h_length = sizeof(struct in_addr);
			hp->hp.h_addr_list = (char **)hp->buf;
			hp->hp.h_addr_list[0] = (char *)&hp->hp.h_addr_list[2];
			hp->hp.h_addr_list[1] = NULL;
			hp->hp.h_aliases = &hp->hp.h_addr_list[1];
			hp->hp.h_name = hp->hp.h_addr_list[0] + sizeof(struct in_addr);
			cw_copy_string(hp->hp.h_name, host, sizeof(hp->buf) - (hp->hp.h_name - hp->buf));

			if (inet_pton(AF_INET, host, hp->hp.h_addr_list[0]) <= 0)
				goto out_fail;

			return &hp->hp;
		}

#if !defined(GETHOSTBYNAME_DOES_IDN) && HAVE_LIBIDN
		if (idna_to_ascii_8z(host, &idnahost, 0) == IDNA_SUCCESS) {
#  ifdef SOLARIS
			result = gethostbyname_r(idnahost, &hp->hp, hp->buf, sizeof(hp->buf), &herrno);
			res = 0;
#  else
			res = gethostbyname_r(idnahost, &hp->hp, hp->buf, sizeof(hp->buf), &result, &herrno);
#  endif
			free(idnahost);
		}
#else
#  ifdef SOLARIS
			result = gethostbyname_r(host, &hp->hp, hp->buf, sizeof(hp->buf), &herrno);
			res = 0;
#  else
			res = gethostbyname_r(host, &hp->hp, hp->buf, sizeof(hp->buf), &result, &herrno);
#  endif
#endif

		if (res || !result || !hp->hp.h_addr_list || !hp->hp.h_addr_list[0])
			goto out_fail;

		return &hp->hp;
	}

out_fail:
	return NULL;
}



CW_MUTEX_DEFINE_STATIC(test_lock);
CW_MUTEX_DEFINE_STATIC(test_lock2);
static pthread_t test_thread; 
static int lock_count = 0;
static int test_errors = 0;

/*! \brief This is a regression test for recursive mutexes.
   test_for_thread_safety() will return 0 if recursive mutex locks are
   working properly, and non-zero if they are not working properly. */
static void *test_thread_body(void *data) 
{ 
	CW_UNUSED(data);

	cw_mutex_lock(&test_lock);
	lock_count += 10;
	if (lock_count != 10) 
		test_errors++;
	cw_mutex_lock(&test_lock);
	lock_count += 10;
	if (lock_count != 20) 
		test_errors++;
	cw_mutex_lock(&test_lock2);
	cw_mutex_unlock(&test_lock);
	lock_count -= 10;
	if (lock_count != 10) 
		test_errors++;
	cw_mutex_unlock(&test_lock);
	lock_count -= 10;
	cw_mutex_unlock(&test_lock2);
	if (lock_count != 0) 
		test_errors++;
	return NULL;
} 

int test_for_thread_safety(void)
{ 
	cw_mutex_lock(&test_lock2);
	cw_mutex_lock(&test_lock);
	lock_count += 1;
	cw_mutex_lock(&test_lock);
	lock_count += 1;
	cw_pthread_create(&test_thread, &global_attr_default, test_thread_body, NULL); 
	usleep(100);
	if (lock_count != 2) 
		test_errors++;
	cw_mutex_unlock(&test_lock);
	lock_count -= 1;
	usleep(100); 
	if (lock_count != 1) 
		test_errors++;
	cw_mutex_unlock(&test_lock);
	lock_count -= 1;
	if (lock_count != 0) 
		test_errors++;
	cw_mutex_unlock(&test_lock2);
	usleep(100);
	if (lock_count != 0) 
		test_errors++;
	pthread_join(test_thread, NULL);
	return(test_errors);          /* return 0 on success. */
}

void cw_hash_to_hex(char *output, unsigned char *md_value, unsigned int md_len)
{
	int x;
	int len = 0;

	for (x = 0; x < md_len; x++)
		len += sprintf(output + len, "%2.2x", md_value[x]);
}

/*! \Brief cw_md5_hash_bin: Produce 16 char MD5 hash of value. ---*/
int cw_md5_hash_bin(unsigned char *md_value, unsigned char *input, unsigned int input_len)
{
	EVP_MD_CTX mdctx;
	unsigned int md_len;

	EVP_DigestInit(&mdctx, EVP_md5());
	EVP_DigestUpdate(&mdctx, input, input_len);
	EVP_DigestFinal(&mdctx, md_value, &md_len);

	return md_len;
}

void cw_md5_hash(char *output, char *input)
{
	unsigned int md_len;
	unsigned char md_value[CW_MAX_BINARY_MD_SIZE];
	
	md_len = cw_md5_hash_bin(md_value, (unsigned char *) input, strlen(input));

	cw_hash_to_hex(output, md_value, md_len);
}

int cw_md5_hash_two_bin(unsigned char *md_value,
			  unsigned char *input1, unsigned int input1_len,
			  unsigned char *input2, unsigned int input2_len)
{
	EVP_MD_CTX mdctx;
	unsigned int md_len;

	EVP_DigestInit(&mdctx, EVP_md5());
	EVP_DigestUpdate(&mdctx, input1, input1_len);
	EVP_DigestUpdate(&mdctx, input2, input2_len);
	EVP_DigestFinal(&mdctx, md_value, &md_len);
	
	return md_len;
}

void cw_md5_hash_two(char *output, char *input1, char *input2)
{
	unsigned int md_len;
	unsigned char md_value[CW_MAX_BINARY_MD_SIZE];
	
	md_len = cw_md5_hash_two_bin(md_value, (unsigned char *) input1, strlen(input1),
				       (unsigned char *) input2, strlen(input2));

	cw_hash_to_hex(output, md_value, md_len);
}

int cw_base64decode(unsigned char *dst, const char *src, int max)
{
	int cnt = 0;
	unsigned int byte = 0;
	unsigned int bits = 0;
	int incnt = 0;
#if 0
	unsigned char *odst = dst;
#endif
	while(*src && (cnt < max)) {
		/* Shift in 6 bits of input */
		byte <<= 6;
		byte |= (b2a[(int)(*src)]) & 0x3f;
		bits += 6;

		src++;
		incnt++;
		/* If we have at least 8 bits left over, take that character 
		   off the top */
		if (bits >= 8)  {
			bits -= 8;
			*dst = (byte >> bits) & 0xff;

			dst++;
			cnt++;
		}
	}
#if 0
	dump(odst, cnt);
#endif
	/* Dont worry about left over bits, they're extra anyway */
	return cnt;
}

int cw_base64encode(char *dst, const unsigned char *src, int srclen, int max)
{
	int cnt = 0;
	unsigned int byte = 0;
	int bits = 0;
	int i;
	int cntin = 0;
#if 0
	char *odst = dst;
	dump(src, srclen);
#endif
	/* Reserve one bit for end */
	max--;
	while((cntin < srclen) && (cnt < max)) {
		byte <<= 8;

		byte |= *(src++);
		bits += 8;
		cntin++;
		while((bits >= 6) && (cnt < max)) {
			bits -= 6;
			/* We want only the top */
			i = (byte >> bits) & 0x3f;
			*dst = base64[i];

			dst++;
			cnt++;
		}
	}
	if (bits && (cnt < max)) {
		/* Add one last character for the remaining bits, 
		   padding the rest with 0 */
		byte <<= (6 - bits);
		i = (byte) & 0x3f;
		*(dst++) = base64[i];
		cnt++;
	}
	*dst = '\0';
	return cnt;
}

static void base64_init(void)
{
	int x;
	memset(b2a, -1, sizeof(b2a));
	/* Initialize base-64 Conversion table */
	for (x=0;x<26;x++) {
		/* A-Z */
		base64[x] = 'A' + x;
		b2a['A' + x] = x;
		/* a-z */
		base64[x + 26] = 'a' + x;
		b2a['a' + x] = x + 26;
		/* 0-9 */
		if (x < 10) {
			base64[x + 52] = '0' + x;
			b2a['0' + x] = x + 52;
		}
	}
	base64[62] = '+';
	base64[63] = '/';
	b2a[(int)'+'] = 62;
	b2a[(int)'/'] = 63;
}

/*! \brief  cw_uri_encode: Turn text string to URI-encoded %XX version ---*/
/* 	At this point, we're converting from ISO-8859-x (8-bit), not UTF8
	as in the SIP protocol spec 
	If doreserved == 1 we will convert reserved characters also.
	RFC 2396, section 2.4
	outbuf needs to have more memory allocated than the instring
	to have room for the expansion. Every char that is converted
	is replaced by three ASCII characters.

	Note: The doreserved option is needed for replaces header in
	SIP transfers.
*/
char *cw_uri_encode(const char *src, struct cw_dynstr *dst, int doreserved)
{
	static const char reserved[] = ";/?:@&=+$,# ";
	size_t mark;

	mark = dst->used;

	while (*src) {
		int n = 0;

		if (!doreserved)
			while (src[n] && src[n] <= 127) n++;
		else
			while (src[n] && src[n] <= 127 && !strchr(reserved, src[n])) n++;

		cw_dynstr_printf(dst, "%.*s", n, src);

		if (!src[n])
			break;

		cw_dynstr_printf(dst, "%%%02x", (unsigned char)src[n]);

		src += n + 1;
	}

	return &dst->data[mark];
}

/*! \brief  cw_uri_decode: Decode SIP URI, URN, URL (overwrite the string)  ---*/
void cw_uri_decode(char *s) 
{
	char *o;
	unsigned int tmp;

	for (o = s; *s; s++, o++) {
		if (*s == '%' && strlen(s) > 2 && sscanf(s + 1, "%2x", &tmp) == 1) {
			/* have '%', two chars and correct parsing */
			*o = tmp;
			s += 2;	/* Will be incremented once more when we break out */
		} else /* all other cases, just copy */
			*o = *s;
	}
	*o = '\0';
}

/*! \brief  cw_inet_ntoa: Recursive thread safe replacement of inet_ntoa */
const char *cw_inet_ntoa(char *buf, int bufsiz, struct in_addr ia)
{
	return inet_ntop(AF_INET, &ia, buf, bufsiz);
}


struct sched_param global_sched_param_default;
struct sched_param global_sched_param_rr;

pthread_mutexattr_t  global_mutexattr_errorcheck;
pthread_mutexattr_t  global_mutexattr_recursive;
pthread_mutexattr_t  global_mutexattr_simple;

clock_t global_cond_clock_monotonic;
pthread_condattr_t global_condattr_monotonic;

pthread_attr_t global_attr_default;
pthread_attr_t global_attr_detached;
pthread_attr_t global_attr_fifo;
pthread_attr_t global_attr_fifo_detached;
pthread_attr_t global_attr_rr;
pthread_attr_t global_attr_rr_detached;

pthread_key_t global_pthread_key_thread_info;


struct cw_pthread_info {
	pthread_t tid;
	const char *description;
#ifdef DEBUG_MUTEX
	const char *file;
	int lineno;
	const char *function;
	const char *mutex_name;
#endif
	struct cw_module *module;
	void *(*func)(void *);
	void *param;
#ifdef HAVE_RANDOM_R
	struct random_data rand;
	char random_state[256];
#endif
};


static pthread_mutex_t cw_random_lock = PTHREAD_MUTEX_INITIALIZER;

long int cw_random(void)
{
	int32_t res;
#ifdef HAVE_RANDOM_R
	struct cw_pthread_info *thread_info;

	if ((thread_info = pthread_getspecific(global_pthread_key_thread_info)))
		random_r(&thread_info->rand, &res);
	else
#endif
	{
		/* N.B. glibc puts a lock inside random(3), so that the results are thread-safe.
		 * However the docs say random(3) should not be used in multi-threaded code and
		 * other libc's do not have an internal lock so...
		 */
		pthread_mutex_lock(&cw_random_lock);
		res = random_nodeprecate();
		pthread_mutex_unlock(&cw_random_lock);
	}

	return res;
}


static void cw_pthread_wrapper_cleanup(void *data)
{
	struct cw_pthread_info *thread_info = data;

	cw_object_put(thread_info->module);
	free(thread_info);
}

static void *cw_pthread_wrapper(void *data)
{
	struct cw_pthread_info *thread_info = data;
	void *ret;

	thread_info->tid = pthread_self();

#ifdef HAVE_RANDOM_R
	do {
		struct timespec ts;
		unsigned int seed;

		/* This makes the randomness of threads depend on the randomness of the
		 * main process which should have been set from /dev/random if available.
		 */
		pthread_mutex_lock(&cw_random_lock);
		seed = random_nodeprecate();
		pthread_mutex_unlock(&cw_random_lock);

		memset(&thread_info->rand, 0, sizeof(thread_info->rand));
		cw_clock_gettime(CLOCK_MONOTONIC, &ts);
		initstate_r(seed + (getppid() << 16) + getpid() + ts.tv_sec + ts.tv_nsec + (unsigned int)pthread_self(),
			thread_info->random_state, sizeof(thread_info->random_state), &thread_info->rand);
	} while (0);
#endif

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	pthread_cleanup_push(cw_pthread_wrapper_cleanup, thread_info);
	pthread_setspecific(global_pthread_key_thread_info, thread_info);
	ret = thread_info->func(thread_info->param);
	pthread_cleanup_pop(1);
	return ret;
}

#ifndef __linux__
#undef pthread_create /* For cw_pthread_create function only */
#endif /* !__linux__ */

int cw_pthread_create_module(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *data, struct cw_module *module, const char *description)
{
	int ret, n;

	struct cw_pthread_info *thread_info;

	if ((thread_info = malloc(sizeof(*thread_info)))) {
		thread_info->tid = CW_PTHREADT_NULL;
		thread_info->description = description;
#ifdef DEBUG_MUTEX
		thread_info->file = thread_info->function = thread_info->mutex_name = NULL;
		thread_info->lineno = 0;
#endif
		thread_info->module = cw_object_get(module);
		thread_info->func = start_routine;
		thread_info->param = data;
		ret = pthread_create(thread, attr, cw_pthread_wrapper, thread_info);
		if (ret == EPERM && !pthread_attr_getschedpolicy(attr, &n) && n != SCHED_OTHER) {
			struct sched_param sp;
			cw_log(CW_LOG_WARNING, "No permission for realtime scheduling - dropping to non-realtime\n");
			pthread_attr_setschedpolicy(attr, SCHED_OTHER);
			sp.sched_priority = 0;
			pthread_attr_setschedparam(attr, &sp);
			ret = pthread_create(thread, attr, cw_pthread_wrapper, thread_info);
		}
		if (ret)
			free(thread_info);
		return ret;
	}

	ret = errno;
	cw_log(CW_LOG_ERROR, "malloc: %s\n", strerror(errno));
	return errno;
}


#ifdef DEBUG_MUTEX

#define debug_mutex_log(...) do { \
	if (canlog) \
		cw_log(CW_LOG_ERROR, __VA_ARGS__); \
	else \
		fprintf(stderr, __VA_ARGS__); \
} while (0)

static void show_locks(int canlog, cw_mutex_t *t)
{
	int i;

	debug_mutex_log("locked by thread: %s (" TIDFMT ")\n", t->tinfo->description, TIDCAST(t->tinfo->tid));
	for (i = 0; i < t->reentrancy; i++)
		debug_mutex_log("    [%d] %s:%d %s\n", i, t->file[i], t->lineno[i], t->func[i]);
}


static void push_thread_info(int canlog, cw_mutex_t *t, const char *filename, int lineno, const char *func, const char *mutex_name, struct cw_pthread_info *thread_info)
{
	if (t->reentrancy < CW_MAX_REENTRANCY) {
		t->file[t->reentrancy] = filename;
		t->lineno[t->reentrancy] = lineno;
		t->func[t->reentrancy] = func;
		if (t->reentrancy == 0)
			t->tinfo = thread_info;
		t->reentrancy++;
	} else {
		debug_mutex_log("%s:%d %s: '%s' really deep reentrancy!\n",
			filename, lineno, func, mutex_name);
		show_locks(canlog, t);
	}
}


static void pop_thread_info(int canlog, cw_mutex_t *t, const char *filename, int lineno, const char *func, const char *mutex_name, const char *msg)
{
	if (--t->reentrancy < 0) {
		debug_mutex_log("%s:%d %s: mutex '%s' %s\n", filename, lineno, func, mutex_name, msg);
		t->reentrancy = 0;
	}

	if (t->reentrancy < CW_MAX_REENTRANCY) {
		t->file[t->reentrancy] = NULL;
		t->lineno[t->reentrancy] = 0;
		t->func[t->reentrancy] = NULL;
		if (t->reentrancy == 0)
			t->tinfo = NULL;
	}
}


int cw_mutex_init_attr_debug(cw_mutex_t *t, pthread_mutexattr_t *attr, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name)
{
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) != ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is already initialized\n",
			   filename, lineno, func, mutex_name);
		debug_mutex_log("%s:%d %s: previously initialized mutex '%s'\n",
			   t->file, t->lineno, t->func, mutex_name);
		CRASH;
		return 0;
	}
#else
	CW_UNUSED(canlog);
	CW_UNUSED(mutex_name);
#endif

	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;
	t->tinfo = pthread_getspecific(global_pthread_key_thread_info);
	t->reentrancy = 0;

	return pthread_mutex_init(&t->mutex, attr);
}


int cw_mutex_destroy_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name)
{
	int res;

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER))
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is uninitialized\n", filename, lineno, func, mutex_name);
#endif

	switch ((res = pthread_mutex_trylock(&t->mutex))) {
		case 0:
			pthread_mutex_unlock(&t->mutex);
			break;
		case EBUSY:
			debug_mutex_log("%s:%d %s: Error: attempt to destroy locked mutex '%s'\n",
				filename, lineno, func, mutex_name);
			show_locks(canlog, t);
			break;
		default:
			debug_mutex_log("%s:%d %s: Error destroying mutex '%s': %s\n",
				filename, lineno, func, mutex_name, strerror(res));
			break;
	}

	if ((res = pthread_mutex_destroy(&t->mutex))) {
		debug_mutex_log("%s:%d %s: Error destroying mutex '%s': %s\n", filename, lineno, func, mutex_name, strerror(res));
		show_locks(canlog, t);
		CRASH;
	}
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
	else
		t->mutex = PTHREAD_MUTEX_INIT_VALUE;
#endif
	t->file[0] = filename;
	t->lineno[0] = lineno;
	t->func[0] = func;
	t->tinfo = pthread_getspecific(global_pthread_key_thread_info);

	return res;
}

int cw_mutex_lock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name)
{
	struct cw_pthread_info *thread_info;
	unsigned int delay = 100;
	int res;

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER)) {
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
		cw_mutex_logger("%s:%d %s: Error: mutex '%s' is uninitialized\n", filename, lineno, func, mutex_name);
#endif
		cw_mutex_init(t);
	}
#endif /* defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE) */

	thread_info = pthread_getspecific(global_pthread_key_thread_info);

	if ((res = pthread_mutex_trylock(&t->mutex)) == EBUSY) {
		if (thread_info) {
			thread_info->file = filename;
			thread_info->lineno = lineno;
			thread_info->function = func;
			thread_info->mutex_name = mutex_name;
		}

		do {
			int tries = 10;
			while ((res = pthread_mutex_trylock(&t->mutex)) == EBUSY && tries--)
				usleep(100);
			if (res == EBUSY) {
				debug_mutex_log("%s:%d %s: %slock on mutex '%s'? Sleeping %dms\n",
					filename, lineno, func,
					(t->tinfo && t->tinfo->mutex_name ? "Dead" : "Live"),
					mutex_name, delay);
				if (t->tinfo) {
					show_locks(canlog, t);
					if (t->tinfo && t->tinfo->mutex_name)
						debug_mutex_log("    blocking on %s at %s:%d %s\n", t->tinfo->mutex_name, t->tinfo->file, t->tinfo->lineno, t->tinfo->function);
					else
						debug_mutex_log("    not blocking on a mutex\n");
				}
				usleep(delay * 1000);
				if (delay < 3200)
					delay <<= 1;
			}
		} while (res == EBUSY);

		if (thread_info) {
			thread_info->file = thread_info->function = thread_info->mutex_name = NULL;
			thread_info->lineno = 0;
		}
	}

	if (!res) {
		push_thread_info(canlog, t, filename, lineno, func, mutex_name, thread_info);
	} else {
		debug_mutex_log("%s:%d %s: Error obtaining mutex: %s\n",
			filename, lineno, func, strerror(res));
		CRASH;
	}

	return res;
}

extern int cw_mutex_trylock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name)
{
	int res;

#if defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE)
	if ((t->mutex) == ((pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER)) {
#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is uninitialized\n",
			filename, lineno, func, mutex_name);
#endif
		cw_mutex_init(t);
	}
#endif /* defined(CW_MUTEX_INIT_W_CONSTRUCTORS) || defined(CW_MUTEX_INIT_ON_FIRST_USE) */

	if (!(res = pthread_mutex_trylock(&t->mutex)))
		push_thread_info(canlog, t, filename, lineno, func, mutex_name, pthread_getspecific(global_pthread_key_thread_info));

	return res;
}

int cw_mutex_unlock_debug(cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *mutex_name)
{
	struct cw_pthread_info *thread_info;
	int res;

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER))
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is uninitialized\n",
			filename, lineno, func, mutex_name);
#endif

	thread_info = pthread_getspecific(global_pthread_key_thread_info);

	if (t->reentrancy && t->tinfo != thread_info) {
		debug_mutex_log("%s:%d %s: attempted unlock mutex '%s' without owning it!\n",
			filename, lineno, func, mutex_name);
		show_locks(canlog, t);
		CRASH;
	}

	pop_thread_info(canlog, t, filename, lineno, func, mutex_name, "freed more times than we've locked!");

	if ((res = pthread_mutex_unlock(&t->mutex))) {
		debug_mutex_log("%s line %d (%s): Error releasing mutex: %s\n",
			filename, lineno, func, strerror(res));
		CRASH;
	}

	return res;
}

int cw_cond_wait_debug(cw_cond_t *cond, cw_mutex_t *t, int canlog, const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name)
{
	struct cw_pthread_info *thread_info;
	int res;

	CW_UNUSED(cond_name);

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER))
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is uninitialized\n",
			filename, lineno, func, mutex_name);
#endif

	thread_info = pthread_getspecific(global_pthread_key_thread_info);

	if (t->reentrancy && t->tinfo != thread_info) {
		debug_mutex_log("%s:%d %s: attempted cond wait using mutex '%s' without owning it!\n",
			filename, lineno, func, mutex_name);
		show_locks(canlog, t);
		CRASH;
	}

	if (t->reentrancy > 1) {
		debug_mutex_log("%s:%d %s: cond wait with mutex '%s' has nested locks!!\n",
			filename, lineno, func, mutex_name);
		show_locks(canlog, t);
	}

	pop_thread_info(canlog, t, filename, lineno, func, mutex_name, "should be locked!");

	res = pthread_cond_wait(cond, &t->mutex);

	push_thread_info(canlog, t, filename, lineno, func, mutex_name, thread_info);

	if (res) {
		debug_mutex_log("%s:%d %s: Error waiting on condition mutex '%s': %s\n",
			filename, lineno, func, mutex_name, strerror(res));
		CRASH;
	}

	return res;
}

int cw_cond_timedwait_debug(cw_cond_t *cond, cw_mutex_t *t, const struct timespec *abstime, int canlog, const char *filename, int lineno, const char *func, const char *cond_name, const char *mutex_name)
{
	struct cw_pthread_info *thread_info;
	int res;

	CW_UNUSED(cond_name);

#ifdef CW_MUTEX_INIT_W_CONSTRUCTORS
	if ((t->mutex) == ((pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER))
		debug_mutex_log("%s:%d %s: Error: mutex '%s' is uninitialized\n",
			filename, lineno, func, mutex_name);
#endif

	thread_info = pthread_getspecific(global_pthread_key_thread_info);

	if (t->reentrancy && t->tinfo != thread_info) {
		debug_mutex_log("%s:%d %s: attempted cond timedwait with mutex '%s' without owning it!\n",
			filename, lineno, func, mutex_name);
		debug_mutex_log("%s:%d %s: '%s' was locked here\n",
			t->file[t->reentrancy-1], t->lineno[t->reentrancy-1], t->func[t->reentrancy-1], mutex_name);
		CRASH;
	}

	if (t->reentrancy > 1) {
		debug_mutex_log("%s:%d %s: cond timedwait with mutex '%s' has nested locks!!\n",
			filename, lineno, func, mutex_name);
		show_locks(canlog, t);
	}

	pop_thread_info(canlog, t, filename, lineno, func, mutex_name, "should be locked!");

	res = pthread_cond_timedwait(cond, &t->mutex, abstime);

	push_thread_info(canlog, t, filename, lineno, func, mutex_name, thread_info);

	if (res && res != ETIMEDOUT) {
		debug_mutex_log("%s:%d %s: Error waiting on condition mutex '%s': %s\n",
			filename, lineno, func, mutex_name, strerror(res));
		CRASH;
	}

	return res;
}

#endif /* DEBUG_MUTEX */


int cw_true(const char *s)
{
	if (cw_strlen_zero(s))
		return 0;

	/* Determine if this is a true value */
	if (!strcasecmp(s, "yes") ||
	    !strcasecmp(s, "true") ||
	    !strcasecmp(s, "y") ||
	    !strcasecmp(s, "t") ||
	    !strcasecmp(s, "1") ||
	    !strcasecmp(s, "on"))
		return -1;

	return 0;
}

int cw_false(const char *s)
{
	if (cw_strlen_zero(s))
		return 0;

	/* Determine if this is a false value */
	if (!strcasecmp(s, "no") ||
	    !strcasecmp(s, "false") ||
	    !strcasecmp(s, "n") ||
	    !strcasecmp(s, "f") ||
	    !strcasecmp(s, "0") ||
	    !strcasecmp(s, "off"))
		return -1;

	return 0;
}

#define ONE_MILLION	1000000
/*
 * put timeval in a valid range. usec is 0..999999
 * negative values are not allowed and truncated.
 */
static struct timeval tvfix(struct timeval a)
{
	if (a.tv_usec >= ONE_MILLION) {
		cw_log(CW_LOG_WARNING, "warning too large timestamp %ld.%ld\n",
			a.tv_sec, (long int) a.tv_usec);
		a.tv_sec += a.tv_usec % ONE_MILLION;
		a.tv_usec %= ONE_MILLION;
	} else if (a.tv_usec < 0) {
		cw_log(CW_LOG_WARNING, "warning negative timestamp %ld.%ld\n",
				a.tv_sec, (long int) a.tv_usec);
		a.tv_usec = 0;
	}
	return a;
}

struct timeval cw_tvadd(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec += b.tv_sec;
	a.tv_usec += b.tv_usec;
	if (a.tv_usec >= ONE_MILLION) {
		a.tv_sec++;
		a.tv_usec -= ONE_MILLION;
	}
	return a;
}

struct timeval cw_tvsub(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec -= b.tv_sec;
	a.tv_usec -= b.tv_usec;
	if (a.tv_usec < 0) {
		a.tv_sec-- ;
		a.tv_usec += ONE_MILLION;
	}
	return a;
}
#undef ONE_MILLION

#ifndef HAVE_STRCASESTR
static char *upper(const char *orig, char *buf, int bufsize)
{
	int i = 0;

	while (i < (bufsize - 1) && orig[i]) {
		buf[i] = (char)toupper(orig[i]);
		i++;
	}

	buf[i] = '\0';

	return buf;
}

char *strcasestr(const char *haystack, const char *needle)
{
	char *offset;
	char *u1, *u2;
	int u1len = strlen(haystack) + 1, u2len = strlen(needle) + 1;

	if (u2len > u1len) {
		/* Needle bigger than haystack */
		return NULL;
	}
	u1 = alloca(u1len);
	u2 = alloca(u2len);
	offset = strstr(upper(haystack, u1, u1len), upper(needle, u2, u2len));
	if (offset) {
		/* Return the offset into the original string */
		return ((char *)((unsigned long)haystack + (unsigned long)(offset - u1)));
	} else {
		return NULL;
	}
}
#endif /* !HAVE_STRCASESTR */

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t n)
{
	size_t len;

	for (len=0; len < n; len++)
		if (s[len] == '\0')
			break;

	return len;
}
#endif /* !HAVE_STRNLEN */

#if !defined(HAVE_STRNDUP) && !defined(__CW_DEBUG_MALLOC)
char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *new = malloc(len + 1);

	if (!new)
		return NULL;

	new[len] = '\0';
	return memcpy(new, s, len);
}
#endif /* !defined(HAVE_STRNDUP) && !defined(__CW_DEBUG_MALLOC) */

#if !defined(HAVE_VASPRINTF) && !defined(__CW_DEBUG_MALLOC)
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int size;
	va_list ap2;
	char s;

	*strp = NULL;
	va_copy(ap2, ap);
	size = vsnprintf(&s, 1, fmt, ap2);
	va_end(ap2);
	*strp = malloc(size + 1);
	if (!*strp)
		return -1;
	vsnprintf(*strp, size + 1, fmt, ap);

	return size;
}
#endif /* !defined(HAVE_VASPRINTF) && !defined(__CW_DEBUG_MALLOC) */

#ifndef HAVE_STRTOQ
#ifndef LONG_MIN
#define LONG_MIN        (-9223372036854775807L-1L)
	                                 /* min value of a "long int" */
#endif
#ifndef LONG_MAX
#define LONG_MAX        9223372036854775807L
	                                 /* max value of a "long int" */
#endif

/*
 * Convert a string to a quad integer.
 *
 * Ignores `locale' stuff.  Assumes that the upper and lower case
 * alphabets and digits are each contiguous.
 */
uint64_t strtoq(const char *nptr, char **endptr, int base)
{
	 const char *s;
	 uint64_t acc;
	 unsigned char c;
	 uint64_t qbase, cutoff;
	 int neg, any, cutlim;

	 /*
	  * Skip white space and pick up leading +/- sign if any.
	  * If base is 0, allow 0x for hex and 0 for octal, else
	  * assume decimal; if base is already 16, allow 0x.
	  */
	 s = nptr;
	 do {
	         c = *s++;
	 } while (isspace(c));
	 if (c == '-') {
	         neg = 1;
	         c = *s++;
	 } else {
	         neg = 0;
	         if (c == '+')
	                 c = *s++;
	 }
	 if ((base == 0 || base == 16) &&
	     c == '\0' && (*s == 'x' || *s == 'X')) {
	         c = s[1];
	         s += 2;
	         base = 16;
	 }
	 if (base == 0)
	         base = c == '\0' ? 8 : 10;

	 /*
	  * Compute the cutoff value between legal numbers and illegal
	  * numbers.  That is the largest legal value, divided by the
	  * base.  An input number that is greater than this value, if
	  * followed by a legal input character, is too big.  One that
	  * is equal to this value may be valid or not; the limit
	  * between valid and invalid numbers is then based on the last
	  * digit.  For instance, if the range for quads is
	  * [-9223372036854775808..9223372036854775807] and the input base
	  * is 10, cutoff will be set to 922337203685477580 and cutlim to
	  * either 7 (neg==0) or 8 (neg==1), meaning that if we have
	  * accumulated a value > 922337203685477580, or equal but the
	  * next digit is > 7 (or 8), the number is too big, and we will
	  * return a range error.
	  *
	  * Set any if any `digits' consumed; make it negative to indicate
	  * overflow.
	  */
	 qbase = (unsigned)base;
	 cutoff = neg ? (uint64_t)-(LONG_MIN + LONG_MAX) + LONG_MAX : LONG_MAX;
	 cutlim = cutoff % qbase;
	 cutoff /= qbase;
	 for (acc = 0, any = 0;; c = *s++) {
	         if (!isascii(c))
	                 break;
	         if (isdigit(c))
	                 c -= '\0';
	         else if (isalpha(c))
	                 c -= isupper(c) ? 'A' - 10 : 'a' - 10;
	         else
	                 break;
	         if (c >= base)
	                 break;
	         if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
	                 any = -1;
	         else {
	                 any = 1;
	                 acc *= qbase;
	                 acc += c;
	         }
	 }
	 if (any < 0) {
	         acc = neg ? LONG_MIN : LONG_MAX;
	 } else if (neg)
	         acc = -acc;
	 if (endptr != 0)
	         *((const char **)endptr) = any ? s - 1 : nptr;
	 return acc;
}
#endif /* !HAVE_STRTOQ */

#if (!defined(getloadavg))
#ifdef linux
/* Alternative method of getting load avg on Linux only */
int getloadavg(double *list, int nelem)
{
	FILE *LOADAVG;
	double avg[3] = { 0.0, 0.0, 0.0 };
	int i, res = -1;

	if ((LOADAVG = fopen("/proc/loadavg", "r"))) {
		fscanf(LOADAVG, "%lf %lf %lf", &avg[0], &avg[1], &avg[2]);
		res = 0;
		fclose(LOADAVG);
	}

	for (i = 0; (i < nelem) && (i < 3); i++) {
		list[i] = avg[i];
	}

	return res;
}
#else /* !linux */
/* Return something that won't cancel the call, but still return -1, in case
 * we correct the implementation to check return value */
int getloadavg(double *list, int nelem)
{
	int i;

	for (i = 0; i < nelem; i++) {
		list[i] = 0.1;
	}
	return -1;
}
#endif /* linux */
#endif /* !defined(getloadavg) */


void cw_enable_packet_fragmentation(int sock)
{
#ifdef __linux__
	int val = IP_PMTUDISC_DONT;
	
	if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)))
		cw_log(CW_LOG_WARNING, "Unable to disable PMTU discovery. Large UDP packets may fail to be delivered when sent from this socket.\n");
#endif
}


int cw_utils_init(void)
{
	global_sched_param_default.sched_priority = 0;
	global_sched_param_rr.sched_priority = 50;

	pthread_mutexattr_init(&global_mutexattr_simple);
	pthread_mutexattr_setprotocol(&global_mutexattr_simple, PTHREAD_PRIO_INHERIT);

	pthread_mutexattr_init(&global_mutexattr_errorcheck);
	pthread_mutexattr_settype(&global_mutexattr_errorcheck, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutexattr_setprotocol(&global_mutexattr_errorcheck, PTHREAD_PRIO_INHERIT);

	pthread_mutexattr_init(&global_mutexattr_recursive);
	pthread_mutexattr_settype(&global_mutexattr_recursive, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setprotocol(&global_mutexattr_recursive, PTHREAD_PRIO_INHERIT);

	pthread_condattr_init(&global_condattr_monotonic);
#if HAVE_PTHREAD_CONDATTR_SETCLOCK
	global_cond_clock_monotonic = global_clock_monotonic;
	pthread_condattr_setclock(&global_condattr_monotonic, global_cond_clock_monotonic);
#endif

	pthread_attr_init(&global_attr_default);
	pthread_attr_setstacksize(&global_attr_default, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_default, PTHREAD_INHERIT_SCHED);
	pthread_attr_setdetachstate(&global_attr_detached, PTHREAD_CREATE_JOINABLE);

	pthread_attr_init(&global_attr_detached);
	pthread_attr_setstacksize(&global_attr_detached, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_detached, PTHREAD_INHERIT_SCHED);
	pthread_attr_setdetachstate(&global_attr_detached, PTHREAD_CREATE_DETACHED);

	pthread_attr_init(&global_attr_fifo);
	pthread_attr_setstacksize(&global_attr_fifo, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_fifo, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&global_attr_fifo, SCHED_FIFO);
	pthread_attr_setschedparam(&global_attr_fifo, &global_sched_param_rr);
	pthread_attr_setdetachstate(&global_attr_fifo, PTHREAD_CREATE_JOINABLE);

	pthread_attr_init(&global_attr_fifo_detached);
	pthread_attr_setstacksize(&global_attr_fifo_detached, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_fifo_detached, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&global_attr_fifo_detached, SCHED_FIFO);
	pthread_attr_setschedparam(&global_attr_fifo_detached, &global_sched_param_rr);
	pthread_attr_setdetachstate(&global_attr_fifo_detached, PTHREAD_CREATE_DETACHED);

	pthread_attr_init(&global_attr_rr);
	pthread_attr_setstacksize(&global_attr_rr, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_rr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&global_attr_rr, SCHED_RR);
	pthread_attr_setschedparam(&global_attr_rr, &global_sched_param_rr);
	pthread_attr_setdetachstate(&global_attr_rr, PTHREAD_CREATE_JOINABLE);

	pthread_attr_init(&global_attr_rr_detached);
	pthread_attr_setstacksize(&global_attr_rr_detached, CW_STACKSIZE);
	pthread_attr_setinheritsched(&global_attr_rr_detached, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&global_attr_rr_detached, SCHED_RR);
	pthread_attr_setschedparam(&global_attr_rr_detached, &global_sched_param_rr);
	pthread_attr_setdetachstate(&global_attr_rr_detached, PTHREAD_CREATE_DETACHED);

	pthread_key_create(&global_pthread_key_thread_info, NULL);

#ifndef linux
	pthread_mutex_init(&randomlock, &global_mutexattr_simple);
#endif

	base64_init();
	return 0;
}
