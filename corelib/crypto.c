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
 *
 * \brief Provide Cryptographic Signature capability
 * 
 */
#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/file.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/say.h"
#include "callweaver/module.h"
#include "callweaver/options.h"
#include "callweaver/crypto.h"
#include "callweaver/cli.h"
#include "callweaver/io.h"
#include "callweaver/lock.h"
#include "callweaver/utils.h"

/*
 * CallWeaver uses RSA keys with SHA-1 message digests for its
 * digital signatures.  The choice of RSA is due to its higher
 * throughput on verification, and the choice of SHA-1 based
 * on the recently discovered collisions in MD5's compression 
 * algorithm and recommendations of avoiding MD5 in new schemes
 * from various industry experts.
 *
 * We use OpenSSL to provide our crypto routines, although we never
 * actually use full-up SSL
 *
 */

/*
 * XXX This module is not very thread-safe.  It is for everyday stuff
 *     like reading keys and stuff, but there are all kinds of weird
 *     races with people running reload and key init at the same time
 *     for example
 *
 * XXXX
 */

CW_MUTEX_DEFINE_STATIC(keylock);

#define KEY_NEEDS_PASSCODE (1 << 16)

struct cw_key {
	/* Name of entity */
	char name[80];
	/* File name */
	char fn[256];
	/* Key type (CW_KEY_PUB or CW_KEY_PRIV, along with flags from above) */
	int ktype;
	/* RSA structure (if successfully loaded) */
	RSA *rsa;
	/* Whether we should be deleted */
	int delme;
	/* FD for input (or -1 if no input allowed, or -2 if we needed input) */
	int infd;
	/* FD for output */
	int outfd;
	/* Last MD5 Digest */
	unsigned char md_value[CW_MAX_BINARY_MD_SIZE];
	unsigned int md_len;
	/* Next key */
	struct cw_key *next;
};

static struct cw_key *keys = NULL;


static int noecho(int fd)
{
	struct termios tios;
	int res = -1;

	if (isatty(fd) && !tcgetattr(fd, &tios)) {
		res = tios.c_lflag & (ECHO | ECHONL);
		tios.c_lflag &= ~ECHO;
		tios.c_lflag |= ECHONL;
		if (tcsetattr(fd, TCSAFLUSH, &tios))
			res = -1;
	}

	return res;
}


static void restore_tty(int fd, int oldstate)
{
	struct termios tios;

	if (!tcgetattr(fd, &tios)) {
		tios.c_lflag &= ~(ECHO | ECHONL);
		tios.c_lflag |= oldstate;
		tcsetattr(fd, TCSAFLUSH, &tios);
	}
}


static int pw_cb(char *buf, int size, int rwflag, void *userdata)
{
	char prompt[256];
	struct cw_key *key = (struct cw_key *)userdata;
	int tmp;

	CW_UNUSED(rwflag);

	if (key->infd > -1) {
		snprintf(prompt, sizeof(prompt), ">>>> passcode for %s key '%s': ",
			 key->ktype == CW_KEY_PRIVATE ? "PRIVATE" : "PUBLIC", key->name);
		write(key->outfd, prompt, strlen(prompt));
		memset(buf, 0, sizeof(buf));
		tmp = noecho(key->infd);
		memset(buf, 0, size);
		read(key->infd, buf, size);
		if (tmp != -1)
			restore_tty(key->infd, tmp);
		if (buf[strlen(buf) -1] == '\n')
			buf[strlen(buf) - 1] = '\0';
		return strlen(buf);
	} else {
		/* Note that we were at least called */
		key->infd = -2;
	}
	return -1;
}

struct cw_key *cw_key_get(const char *kname, int ktype)
{
	struct cw_key *key;
	cw_mutex_lock(&keylock);
	key = keys;
	while(key) {
		if (!strcmp(kname, key->name) &&
		    (ktype == key->ktype))
			break;
		key = key->next;
	}
	cw_mutex_unlock(&keylock);
	return key;
}

static struct cw_key *try_load_key(const char *dir, const char *fname, int ifd, int ofd, int *not2)
{
	int ktype = 0;
	char *c = NULL;
	char ffname[256];
	FILE *f;
	EVP_MD_CTX mdctx;
	unsigned char md_value[CW_MAX_BINARY_MD_SIZE];
	unsigned int md_len;
	struct cw_key *key;
	static int notice = 0;
	int found = 0;

	/* Make sure its name is a public or private key */

	if ((c = strstr(fname, ".pub")) && !strcmp(c, ".pub")) {
		ktype = CW_KEY_PUBLIC;
	} else if ((c = strstr(fname, ".key")) && !strcmp(c, ".key")) {
		ktype = CW_KEY_PRIVATE;
	} else
		return NULL;

	/* Get actual filename */
	snprintf(ffname, sizeof(ffname), "%s/%s", dir, fname);

	cw_mutex_lock(&keylock);
	key = keys;
	while(key) {
		/* Look for an existing version already */
		if (!strcasecmp(key->fn, ffname)) 
			break;
		key = key->next;
	}
	cw_mutex_unlock(&keylock);

	/* Open file */
	f = fopen(ffname, "r");
	if (!f) {
		cw_log(CW_LOG_WARNING, "Unable to open key file %s: %s\n", ffname, strerror(errno));
		return NULL;
	}
	EVP_DigestInit(&mdctx, EVP_md5());
	while(!feof(f)) {
		/* Calculate a "whatever" quality md5sum of the key */
		char buf[256];
		memset(buf, 0, 256);
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			EVP_DigestUpdate(&mdctx, (unsigned char *) buf, strlen(buf));
		}
	}
	EVP_DigestFinal(&mdctx, md_value, &md_len);
	if (key) {
		/* If the MD5 sum is the same, and it isn't awaiting a passcode 
		   then this is far enough */
		if (!memcmp(md_value, key->md_value, md_len) &&
		    !(key->ktype & KEY_NEEDS_PASSCODE)) {
			fclose(f);
			key->delme = 0;
			return NULL;
		} else {
			/* Preserve keytype */
			ktype = key->ktype;
			/* Recycle the same structure */
			found++;
		}
	}

	/* Make fname just be the normal name now */
	*c = '\0';
	if (!key) {
		key = (struct cw_key *)malloc(sizeof(struct cw_key));
		if (!key) {
			cw_log(CW_LOG_WARNING, "Out of memory\n");
			fclose(f);
			return NULL;
		}
		memset(key, 0, sizeof(struct cw_key));
	}
	/* At this point we have a key structure (old or new).  Time to
	   fill it with what we know */
	/* Gotta lock if this one already exists */
	if (found)
		cw_mutex_lock(&keylock);
	/* First the filename */
	cw_copy_string(key->fn, ffname, sizeof(key->fn));
	/* Then the name */
	cw_copy_string(key->name, fname, sizeof(key->name));
	key->ktype = ktype;
	/* Yes, assume we're going to be deleted */
	key->delme = 1;
	/* Keep the key type */
	memcpy(key->md_value, md_value, md_len);
	key->md_len = md_len;
	/* Can I/O takes the FD we're given */
	key->infd = ifd;
	key->outfd = ofd;
	/* Reset the file back to the beginning */
	rewind(f);
	/* Now load the key with the right method */
	if (ktype == CW_KEY_PUBLIC)
		key->rsa = PEM_read_RSA_PUBKEY(f, NULL, pw_cb, key);
	else
		key->rsa = PEM_read_RSAPrivateKey(f, NULL, pw_cb, key);
	fclose(f);
	if (key->rsa) {
		if (RSA_size(key->rsa) == 128) {
			/* Key loaded okay */
			key->ktype &= ~KEY_NEEDS_PASSCODE;
			if (option_verbose > 2)
				cw_verbose(VERBOSE_PREFIX_3 "Loaded %s key '%s'\n", key->ktype == CW_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
			if (option_debug)
				cw_log(CW_LOG_DEBUG, "Key '%s' loaded OK\n", key->name);
			key->delme = 0;
		} else
			cw_log(CW_LOG_NOTICE, "Key '%s' is not expected size.\n", key->name);
	} else if (key->infd != -2) {
		cw_log(CW_LOG_WARNING, "Key load %s '%s' failed\n",key->ktype == CW_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
		if (ofd > -1) {
			ERR_print_errors_fp(stderr);
		} else
			ERR_print_errors_fp(stderr);
	} else {
		cw_log(CW_LOG_NOTICE, "Key '%s' needs passcode.\n", key->name);
		key->ktype |= KEY_NEEDS_PASSCODE;
		if (!notice) {
			if (!option_initcrypto) 
				cw_log(CW_LOG_NOTICE, "Add the '-i' flag to the callweaver command line if you want to automatically initialize passcodes at launch.\n");
			notice++;
		}
		/* Keep it anyway */
		key->delme = 0;
		/* Print final notice about "init keys" when done */
		*not2 = 1;
	}
	if (found)
		cw_mutex_unlock(&keylock);
	if (!found) {
		cw_mutex_lock(&keylock);
		key->next = keys;
		keys = key;
		cw_mutex_unlock(&keylock);
	}
	return key;
}

#if 0

static void dump(unsigned char *src, int len)
{
	int x; 
	for (x=0;x<len;x++)
		printf("%02x", *(src++));
	printf("\n");
}

static char *binary(int y, int len)
{
	static char res[80];
	int x;
	memset(res, 0, sizeof(res));
	for (x=0;x<len;x++) {
		if (y & (1 << x))
			res[(len - x - 1)] = '1';
		else
			res[(len - x - 1)] = '0';
	}
	return res;
}

#endif

int cw_sign_bin(struct cw_key *key, const char *msg, int msglen, unsigned char *dsig)
{
	unsigned char digest[20];
	unsigned int siglen = 128;
	int res;

	if (key->ktype != CW_KEY_PRIVATE) {
		cw_log(CW_LOG_WARNING, "Cannot sign with a public key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	res = RSA_sign(NID_sha1, digest, sizeof(digest), dsig, &siglen, key->rsa);
	
	if (!res) {
		cw_log(CW_LOG_WARNING, "RSA Signature (key %s) failed\n", key->name);
		return -1;
	}

	if (siglen != 128) {
		cw_log(CW_LOG_WARNING, "Unexpected signature length %d, expecting %d\n", (int)siglen, (int)128);
		return -1;
	}

	return 0;
	
}

int cw_decrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct cw_key *key)
{
	int res;
	int pos = 0;
	if (key->ktype != CW_KEY_PRIVATE) {
		cw_log(CW_LOG_WARNING, "Cannot decrypt with a public key\n");
		return -1;
	}

	if (srclen % 128) {
		cw_log(CW_LOG_NOTICE, "Tried to decrypt something not a multiple of 128 bytes\n");
		return -1;
	}
	while(srclen) {
		/* Process chunks 128 bytes at a time */
		res = RSA_private_decrypt(128, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING);
		if (res < 0)
			return -1;
		pos += res;
		src += 128;
		srclen -= 128;
		dst += res;
	}
	return pos;
}

int cw_encrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct cw_key *key)
{
	int res;
	int bytes;
	int pos = 0;
	if (key->ktype != CW_KEY_PUBLIC) {
		cw_log(CW_LOG_WARNING, "Cannot encrypt with a private key\n");
		return -1;
	}
	
	while(srclen) {
		bytes = srclen;
		if (bytes > 128 - 41)
			bytes = 128 - 41;
		/* Process chunks 128-41 bytes at a time */
		res = RSA_public_encrypt(bytes, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING);
		if (res != 128) {
			cw_log(CW_LOG_NOTICE, "How odd, encrypted size is %d\n", res);
			return -1;
		}
		src += bytes;
		srclen -= bytes;
		pos += res;
		dst += res;
	}
	return pos;
}

int cw_sign(struct cw_key *key, char *msg, char *sig)
{
	unsigned char dsig[128];
	int siglen = sizeof(dsig);
	int res;
	res = cw_sign_bin(key, msg, strlen(msg), dsig);
	if (!res)
		/* Success -- encode (256 bytes max as documented) */
		cw_base64encode(sig, dsig, siglen, 256);
	return res;
	
}

int cw_check_signature_bin(struct cw_key *key, const char *msg, int msglen, const unsigned char *dsig)
{
	unsigned char digest[20];
	int res;

	if (key->ktype != CW_KEY_PUBLIC) {
		/* Okay, so of course you really *can* but for our purposes
		   we're going to say you can't */
		cw_log(CW_LOG_WARNING, "Cannot check message signature with a private key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	res = RSA_verify(NID_sha1, digest, sizeof(digest), (unsigned char *) dsig, 128, key->rsa);
	
	if (!res) {
		cw_log(CW_LOG_DEBUG, "Key failed verification: %s\n", key->name);
		return -1;
	}
	/* Pass */
	return 0;
}

int cw_check_signature(struct cw_key *key, const char *msg, const char *sig)
{
	unsigned char dsig[128];
	int res;

	/* Decode signature */
	res = cw_base64decode(dsig, sig, sizeof(dsig));
	if (res != sizeof(dsig)) {
		cw_log(CW_LOG_WARNING, "Signature improper length (expect %d, got %d)\n", (int)sizeof(dsig), (int)res);
		return -1;
	}
	res = cw_check_signature_bin(key, msg, strlen(msg), dsig);
	return res;
}

static void crypto_load(int ifd, int ofd)
{
	struct cw_key *key, *nkey, *last;
	DIR *dir = NULL;
	struct dirent *ent;
	int note = 0;
	/* Mark all keys for deletion */
	cw_mutex_lock(&keylock);
	key = keys;
	while(key) {
		key->delme = 1;
		key = key->next;
	}
	cw_mutex_unlock(&keylock);
	/* Load new keys */
	dir = opendir(cw_config[CW_KEY_DIR]);
	if (dir) {
		while((ent = readdir(dir))) {
			try_load_key(cw_config[CW_KEY_DIR], ent->d_name, ifd, ofd, &note);
		}
		closedir(dir);
	} else
		cw_log(CW_LOG_WARNING, "Unable to open key directory '%s'\n", cw_config[CW_KEY_DIR]);
	if (note) {
		cw_log(CW_LOG_NOTICE, "Please run the command 'init keys' to enter the passcodes for the keys\n");
	}
	cw_mutex_lock(&keylock);
	key = keys;
	last = NULL;
	while(key) {
		nkey = key->next;
		if (key->delme) {
			cw_log(CW_LOG_DEBUG, "Deleting key %s type %d\n", key->name, key->ktype);
			/* Do the delete */
			if (last)
				last->next = nkey;
			else
				keys = nkey;
			if (key->rsa)
				RSA_free(key->rsa);
			free(key);
		} else 
			last = key;
		key = nkey;
	}
	cw_mutex_unlock(&keylock);
}

static int show_keys(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char sum[16 * 2 + 1];
	struct cw_key *key;
	int count_keys = 0;

	CW_UNUSED(argc);
	CW_UNUSED(argv);

	cw_mutex_lock(&keylock);
	key = keys;
	cw_dynstr_printf(ds_p, "%-18s %-8s %-16s %-33s\n", "Key Name", "Type", "Status", "Sum");
	while(key) {
		cw_hash_to_hex(sum, key->md_value, key->md_len);
		cw_dynstr_printf(ds_p, "%-18s %-8s %-16s %-33s\n", key->name,
			(key->ktype & 0xf) == CW_KEY_PUBLIC ? "PUBLIC" : "PRIVATE",
			key->ktype & KEY_NEEDS_PASSCODE ? "[Needs Passcode]" : "[Loaded]", sum);
				
		key = key->next;
		count_keys++;
	}
	cw_mutex_unlock(&keylock);
	cw_dynstr_printf(ds_p, "%d known RSA keys.\n", count_keys);
	return RESULT_SUCCESS;
}

static int init_keys(struct cw_dynstr *ds_p, int argc, char *argv[])
{
	char tmp[256] = "";
	struct cw_key *key;
	char *kn;
	int ign;

	CW_UNUSED(ds_p);
	CW_UNUSED(argc);
	CW_UNUSED(argv);

	key = keys;
	while(key) {
		/* Reload keys that need pass codes now */
		if (key->ktype & KEY_NEEDS_PASSCODE) {
			kn = key->fn + strlen(cw_config[CW_KEY_DIR]) + 1;
			cw_copy_string(tmp, kn, sizeof(tmp));
			try_load_key(cw_config[CW_KEY_DIR], tmp, -1, -1, &ign);
		}
		key = key->next;
	}
	return RESULT_SUCCESS;
}

static const char show_key_usage[] =
"Usage: show keys\n"
"       Displays information about RSA keys known by CallWeaver\n";

static const char init_keys_usage[] =
"Usage: init keys\n"
"       Initializes private keys (by reading in pass code from the user)\n";

static struct cw_clicmd cli_show_keys = {
	.cmda = { "show", "keys", NULL },
	.handler = show_keys,
	.summary = "Displays RSA key information",
	.usage = show_key_usage,
};

static struct cw_clicmd cli_init_keys = {
	.cmda = { "init", "keys", NULL },
	.handler = init_keys,
	.summary = "Initialize RSA key passcodes",
	.usage = init_keys_usage,
};

int cw_crypto_init(void)
{
	SSL_library_init();
	ERR_load_crypto_strings();
	cw_cli_register(&cli_show_keys);
	cw_cli_register(&cli_init_keys);

	if (option_initcrypto)
		crypto_load(STDIN_FILENO, STDOUT_FILENO);
	else
		crypto_load(-1, -1);

	return 0;
}

int cw_crypto_reload(void)
{
	crypto_load(-1, -1);
	return 0;
}
