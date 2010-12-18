/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Utility functions
 */

#ifndef _CALLWEAVER_UTILS_H
#define _CALLWEAVER_UTILS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>	/* we want to override inet_ntoa */
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <openssl/evp.h>

#include "callweaver/dynstr.h"
#include "callweaver/time.h"
#include "callweaver/strings.h"
#include "callweaver/module.h"


#define arraysize(X) (sizeof(X)/sizeof(X[0]))


#ifndef offsetof
#  define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

#define container_of(ptr, type, member) ((type *)((size_t)(ptr) - offsetof(type, member)))


#ifdef __GNUC__
#  define likely(x)	__builtin_expect(!!(x), 1)
#  define unlikely(x)	__builtin_expect(!!(x), 0)
#else
#  define likely(x)	x
#  define unlikely(x)	x
#endif


#define AF_PATHNAME	(AF_MAX)
#define AF_INTERNAL	(AF_MAX + 1)


#ifdef DO_CRASH
#  define CRASH do { sleep(1); *((int *)(0)) = 1; } while(0)
#else
#  define CRASH do { } while(0)
#endif


#if defined(__solaris__)
#  define TIDCAST(tid) ((unsigned int)(tid))
#  define TIDFMT "%u"
#else
#  define TIDCAST(tid) ((unsigned long)(tid))
#  define TIDFMT "%lu"
#endif

#define GETTID() TIDCAST(pthread_self())


/*! \note
 \verbatim
   Note:
   It is very important to use only unsigned variables to hold
   bit flags, as otherwise you can fall prey to the compiler's
   sign-extension antics if you try to use the top two bits in
   your variable.

   The flag macros below use a set of compiler tricks to verify
   that the caller is using an "unsigned int" variable to hold
   the flags, and nothing else. If the caller uses any other
   type of variable, a warning message similar to this:

   warning: comparison of distinct pointer types lacks cast
   will be generated.

   The "dummy" variable below is used to make these comparisons.

   Also note that at -O2 or above, this type-safety checking
   does _not_ produce any additional object code at all.
 \endverbatim
*/

extern unsigned int __unsigned_int_flags_dummy;

#define cw_test_flag(p,flag) 		({ \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags & (flag)); \
					})

#define cw_set_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags |= (flag)); \
					} while(0)

#define cw_clear_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags &= ~(flag)); \
					} while(0)

#define cw_copy_flags(dest,src,flagz)	do { \
					typeof ((dest)->flags) __d = (dest)->flags; \
					typeof ((src)->flags) __s = (src)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__d == &__x); \
					(void) (&__s == &__x); \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define cw_set2_flag(p,value,flag)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

/* Non-type checking variations for non-unsigned int flags.  You
   should only use non-unsigned int flags where required by 
   protocol etc and if you know what you're doing :)  */
#define cw_test_flag_nonstd(p,flag) 		({ \
					((p)->flags & (flag)); \
					})

#define cw_set_flag_nonstd(p,flag) 		do { \
					((p)->flags |= (flag)); \
					} while(0)

#define cw_clear_flag_nonstd(p,flag) 		do { \
					((p)->flags &= ~(flag)); \
					} while(0)

#define cw_copy_flags_nonstd(dest,src,flagz)	do { \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define cw_set2_flag_nonstd(p,value,flag)	do { \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

#define CW_FLAGS_ALL UINT_MAX

struct cw_flags {
	unsigned int flags;
};



/*! writev equivalent that does not assume the descriptor used will never
 * return a short write.
 *
 * \note This assumes a BLOCKING descriptor.
 *
 * \param fd     file descriptor to write to
 * \param iov    an array of struct iovecs to be written
 * \param count the number of struct iovecs in the array
 *
 * \return -1 on error, otherwise number of bytes written
 */
extern CW_API_PUBLIC int cw_writev_all(int fd, struct iovec *iov, int count);


/*! write equivalent that does not assume the descriptor used will never
 * return a short write.
 *
 * \note This assumes a BLOCKING descriptor.
 *
 * \param fd   file descriptor to write to
 * \param data the array of bytes to be written
 * \param len  the number of bytes in the array
 *
 * \return -1 on error, otherwise number of bytes written
 */
extern CW_API_PUBLIC int cw_write_all(int fd, const char *data, int len);


/*! Open a file and set the close-on-exec flag
 *
 * If O_CLOEXEC exists we assume it is supported by the kernel.
 * However we cannot guarantee that someone hasn't been silly
 * and installed an old kernel under a new glibc.
 */
#ifdef O_CLOEXEC
#  define open_cloexec(pathname, flags, mode)	open((pathname), (flags) | O_CLOEXEC, (mode))
#else
#  define open_cloexec(pathname, flags, mode)	open_cloexec_compat((pathname), (flags), (mode))
extern CW_API_PUBLIC int open_cloexec_compat(const char *pathname, int flags, mode_t mode);
#endif


/*! Open a socket and set the close-on-exec flag
 *
 * If SOCK_CLOEXEC exists we assume it is supported by the kernel.
 * However we cannot guarantee that someone hasn't been silly
 * and installed an old kernel under a new glibc.
 */
#ifdef SOCK_CLOEXEC
#  define socket_cloexec(domain, type, protocol)	socket((domain), (type) | SOCK_CLOEXEC, (protocol))
#  define socketpair_cloexec(domain, type, protocol, sv)	socketpair((domain), (type) | SOCK_CLOEXEC, (protocol), (sv))
#else
extern CW_API_PUBLIC int socket_cloexec(int domain, int type, int protocol);
extern CW_API_PUBLIC int socketpair_cloexec(int domain, int type, int protocol, int sv[2]);
#endif
#if defined(SOCK_CLOEXEC) && defined(HAVE_ACCEPT4)
#  define accept_cloexec(sockfd, addr, addrlen)	accept4((sockfd), (addr), (addrlen), SOCK_CLOEXEC)
#else
extern CW_API_PUBLIC int accept_cloexec(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
#endif


struct cw_hostent {
	struct hostent hp;
	char buf[1024];
};

extern CW_API_PUBLIC struct hostent *cw_gethostbyname(const char *host, struct cw_hostent *hp);

#define CW_MAX_BINARY_MD_SIZE EVP_MAX_MD_SIZE
#define CW_MAX_HEX_MD_SIZE ((EVP_MAX_MD_SIZE * 2) + 1)

/* cw_hash_to_hex
   \brief Convert binary message digest to hex-encoded version. */
extern CW_API_PUBLIC void cw_hash_to_hex(char *output, unsigned char *md_value, unsigned int md_len);

extern CW_API_PUBLIC int cw_md5_hash_bin(unsigned char *md_value, unsigned char *input, unsigned int input_len);

/* cw_md5_hash 
	\brief Produces MD5 hash based on input string */
extern CW_API_PUBLIC void cw_md5_hash(char *output, char *input);

extern CW_API_PUBLIC int cw_md5_hash_two_bin(unsigned char *output,
				 unsigned char *input1, unsigned int input1_len,
				 unsigned char *input2, unsigned int input2_len);

/* cw_md5_hash_two 
	\brief Produces MD5 hash based on two input strings */
extern CW_API_PUBLIC void cw_md5_hash_two(char *output, char *input1, char *input2);

extern CW_API_PUBLIC int cw_base64encode(char *dst, const unsigned char *src, int srclen, int max);
extern CW_API_PUBLIC int cw_base64decode(unsigned char *dst, const char *src, int max);

/*! \brief Turn text string to URI-encoded %XX version
 *
 * At this point, we're converting from ISO-8859-x (8-bit), not UTF8
 * as in the SIP protocol spec
 * If doreserved == 1 we will convert reserved characters also.
 * RFC 2396, section 2.4
 *
 * \param string      String to be converted
 * \param result      Resulting encoded dynamic string
 * \param doreserved  Convert reserved characters
 */

extern CW_API_PUBLIC char *cw_uri_encode(const char *string, struct cw_dynstr *result, int doreserved);

/*! \brief Decode URI, URN, URL (overwrite string)
 *
 * \param s           String to be decoded
 */
extern CW_API_PUBLIC void cw_uri_decode(char *s);

extern int test_for_thread_safety(void);

extern CW_API_PUBLIC const char *cw_inet_ntoa(char *buf, int bufsiz, struct in_addr ia);

#ifdef inet_ntoa
#undef inet_ntoa
#endif
#define inet_ntoa __dont__use__inet_ntoa__use__cw_inet_ntoa__instead__

extern int cw_utils_init(void);

/*! Compares the source address and port of two sockaddr_in */
static inline int inaddrcmp(const struct sockaddr_in *sin1, const struct sockaddr_in *sin2)
{
	return ((sin1->sin_addr.s_addr != sin2->sin_addr.s_addr) 
		|| (sin1->sin_port != sin2->sin_port));
}


extern CW_API_PUBLIC struct sched_param global_sched_param_default;
extern CW_API_PUBLIC struct sched_param global_sched_param_rr;
extern CW_API_PUBLIC pthread_attr_t global_attr_default;
extern CW_API_PUBLIC pthread_attr_t global_attr_detached;
extern CW_API_PUBLIC pthread_attr_t global_attr_fifo_detached;
extern CW_API_PUBLIC pthread_attr_t global_attr_fifo;
extern CW_API_PUBLIC pthread_attr_t global_attr_rr_detached;
extern CW_API_PUBLIC pthread_attr_t global_attr_rr;


/*! cw_pthread_create pins a reference to the module it is called from
 * for the life of the thread. Hence the thread function MUST be in the
 * same module, i.e. you cannot have a globally visible function foo in
 * module A and call cw_pthread_create(..., foo, ...) from module B.
 */
#define CW_STACKSIZE 256 * 1024
#define cw_pthread_create(thread, attr, func, data) cw_pthread_create_module((thread), (attr), (func), (data), get_modinfo()->self, #func)
extern CW_API_PUBLIC int cw_pthread_create_module(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *data, struct cw_module *module, const char *description)
	__attribute__ ((nonnull (1,2,3)));

#ifdef linux
#define cw_random random
#else
extern CW_API_PUBLIC long int cw_random(void);
#endif

/*!
  \brief Disable PMTU discovery on a socket
  \param sock The socket to manipulate
  \return Nothing

  On Linux, UDP sockets default to sending packets with the Dont Fragment (DF)
  bit set. This is supposedly done to allow the application to do PMTU
  discovery, but CallWeaver does not do this.

  Because of this, UDP packets sent by CallWeaver that are larger than the MTU
  of any hop in the path will be lost. This function can be called on a socket
  to ensure that the DF bit will not be set.
 */
extern CW_API_PUBLIC void cw_enable_packet_fragmentation(int sock);

extern CW_API_PUBLIC size_t cw_strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr, const struct timespec *ts, int gmt);

#endif /* _CALLWEAVER_UTILS_H */
