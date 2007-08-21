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
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

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

OPBX_MUTEX_DEFINE_STATIC(keylock);

#define KEY_NEEDS_PASSCODE (1 << 16)

struct opbx_key {
	/* Name of entity */
	char name[80];
	/* File name */
	char fn[256];
	/* Key type (OPBX_KEY_PUB or OPBX_KEY_PRIV, along with flags from above) */
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
	unsigned char md_value[OPBX_MAX_BINARY_MD_SIZE];
	unsigned int md_len;
	/* Next key */
	struct opbx_key *next;
};

static struct opbx_key *keys = NULL;

#if 0
static int fdprint(int fd, char *s)
{
        return write(fd, s, strlen(s) + 1);
}
#endif
static int pw_cb(char *buf, int size, int rwflag, void *userdata)
{
	struct opbx_key *key = (struct opbx_key *)userdata;
	char prompt[256];
	int res;
	int tmp;
	if (key->infd > -1) {
		snprintf(prompt, sizeof(prompt), ">>>> passcode for %s key '%s': ",
			 key->ktype == OPBX_KEY_PRIVATE ? "PRIVATE" : "PUBLIC", key->name);
		write(key->outfd, prompt, strlen(prompt));
		memset(buf, 0, sizeof(buf));
		tmp = opbx_hide_password(key->infd);
		memset(buf, 0, size);
		res = read(key->infd, buf, size);
		opbx_restore_tty(key->infd, tmp);
		if (buf[strlen(buf) -1] == '\n')
			buf[strlen(buf) - 1] = '\0';
		return strlen(buf);
	} else {
		/* Note that we were at least called */
		key->infd = -2;
	}
	return -1;
}

struct opbx_key *opbx_key_get(const char *kname, int ktype)
{
	struct opbx_key *key;
	opbx_mutex_lock(&keylock);
	key = keys;
	while(key) {
		if (!strcmp(kname, key->name) &&
		    (ktype == key->ktype))
			break;
		key = key->next;
	}
	opbx_mutex_unlock(&keylock);
	return key;
}

static struct opbx_key *try_load_key (char *dir, char *fname, int ifd, int ofd, int *not2)
{
	int ktype = 0;
	char *c = NULL;
	char ffname[256];
	FILE *f;
	EVP_MD_CTX mdctx;
	unsigned char md_value[OPBX_MAX_BINARY_MD_SIZE];
	unsigned int md_len;
	struct opbx_key *key;
	static int notice = 0;
	int found = 0;

	/* Make sure its name is a public or private key */

	if ((c = strstr(fname, ".pub")) && !strcmp(c, ".pub")) {
		ktype = OPBX_KEY_PUBLIC;
	} else if ((c = strstr(fname, ".key")) && !strcmp(c, ".key")) {
		ktype = OPBX_KEY_PRIVATE;
	} else
		return NULL;

	/* Get actual filename */
	snprintf(ffname, sizeof(ffname), "%s/%s", dir, fname);

	opbx_mutex_lock(&keylock);
	key = keys;
	while(key) {
		/* Look for an existing version already */
		if (!strcasecmp(key->fn, ffname)) 
			break;
		key = key->next;
	}
	opbx_mutex_unlock(&keylock);

	/* Open file */
	f = fopen(ffname, "r");
	if (!f) {
		opbx_log(LOG_WARNING, "Unable to open key file %s: %s\n", ffname, strerror(errno));
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
		key = (struct opbx_key *)malloc(sizeof(struct opbx_key));
		if (!key) {
			opbx_log(LOG_WARNING, "Out of memory\n");
			fclose(f);
			return NULL;
		}
		memset(key, 0, sizeof(struct opbx_key));
	}
	/* At this point we have a key structure (old or new).  Time to
	   fill it with what we know */
	/* Gotta lock if this one already exists */
	if (found)
		opbx_mutex_lock(&keylock);
	/* First the filename */
	opbx_copy_string(key->fn, ffname, sizeof(key->fn));
	/* Then the name */
	opbx_copy_string(key->name, fname, sizeof(key->name));
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
	if (ktype == OPBX_KEY_PUBLIC)
		key->rsa = PEM_read_RSA_PUBKEY(f, NULL, pw_cb, key);
	else
		key->rsa = PEM_read_RSAPrivateKey(f, NULL, pw_cb, key);
	fclose(f);
	if (key->rsa) {
		if (RSA_size(key->rsa) == 128) {
			/* Key loaded okay */
			key->ktype &= ~KEY_NEEDS_PASSCODE;
			if (option_verbose > 2)
				opbx_verbose(VERBOSE_PREFIX_3 "Loaded %s key '%s'\n", key->ktype == OPBX_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
			if (option_debug)
				opbx_log(LOG_DEBUG, "Key '%s' loaded OK\n", key->name);
			key->delme = 0;
		} else
			opbx_log(LOG_NOTICE, "Key '%s' is not expected size.\n", key->name);
	} else if (key->infd != -2) {
		opbx_log(LOG_WARNING, "Key load %s '%s' failed\n",key->ktype == OPBX_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
		if (ofd > -1) {
			ERR_print_errors_fp(stderr);
		} else
			ERR_print_errors_fp(stderr);
	} else {
		opbx_log(LOG_NOTICE, "Key '%s' needs passcode.\n", key->name);
		key->ktype |= KEY_NEEDS_PASSCODE;
		if (!notice) {
			if (!option_initcrypto) 
				opbx_log(LOG_NOTICE, "Add the '-i' flag to the callweaver command line if you want to automatically initialize passcodes at launch.\n");
			notice++;
		}
		/* Keep it anyway */
		key->delme = 0;
		/* Print final notice about "init keys" when done */
		*not2 = 1;
	}
	if (found)
		opbx_mutex_unlock(&keylock);
	if (!found) {
		opbx_mutex_lock(&keylock);
		key->next = keys;
		keys = key;
		opbx_mutex_unlock(&keylock);
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

int opbx_sign_bin(struct opbx_key *key, const char *msg, int msglen, unsigned char *dsig)
{
	unsigned char digest[20];
	unsigned int siglen = 128;
	int res;

	if (key->ktype != OPBX_KEY_PRIVATE) {
		opbx_log(LOG_WARNING, "Cannot sign with a public key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	res = RSA_sign(NID_sha1, digest, sizeof(digest), dsig, &siglen, key->rsa);
	
	if (!res) {
		opbx_log(LOG_WARNING, "RSA Signature (key %s) failed\n", key->name);
		return -1;
	}

	if (siglen != 128) {
		opbx_log(LOG_WARNING, "Unexpected signature length %d, expecting %d\n", (int)siglen, (int)128);
		return -1;
	}

	return 0;
	
}

int opbx_decrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct opbx_key *key)
{
	int res;
	int pos = 0;
	if (key->ktype != OPBX_KEY_PRIVATE) {
		opbx_log(LOG_WARNING, "Cannot decrypt with a public key\n");
		return -1;
	}

	if (srclen % 128) {
		opbx_log(LOG_NOTICE, "Tried to decrypt something not a multiple of 128 bytes\n");
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

int opbx_encrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct opbx_key *key)
{
	int res;
	int bytes;
	int pos = 0;
	if (key->ktype != OPBX_KEY_PUBLIC) {
		opbx_log(LOG_WARNING, "Cannot encrypt with a private key\n");
		return -1;
	}
	
	while(srclen) {
		bytes = srclen;
		if (bytes > 128 - 41)
			bytes = 128 - 41;
		/* Process chunks 128-41 bytes at a time */
		res = RSA_public_encrypt(bytes, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING);
		if (res != 128) {
			opbx_log(LOG_NOTICE, "How odd, encrypted size is %d\n", res);
			return -1;
		}
		src += bytes;
		srclen -= bytes;
		pos += res;
		dst += res;
	}
	return pos;
}

int opbx_sign(struct opbx_key *key, char *msg, char *sig)
{
	unsigned char dsig[128];
	int siglen = sizeof(dsig);
	int res;
	res = opbx_sign_bin(key, msg, strlen(msg), dsig);
	if (!res)
		/* Success -- encode (256 bytes max as documented) */
		opbx_base64encode(sig, dsig, siglen, 256);
	return res;
	
}

int opbx_check_signature_bin(struct opbx_key *key, const char *msg, int msglen, const unsigned char *dsig)
{
	unsigned char digest[20];
	int res;

	if (key->ktype != OPBX_KEY_PUBLIC) {
		/* Okay, so of course you really *can* but for our purposes
		   we're going to say you can't */
		opbx_log(LOG_WARNING, "Cannot check message signature with a private key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	res = RSA_verify(NID_sha1, digest, sizeof(digest), (unsigned char *) dsig, 128, key->rsa);
	
	if (!res) {
		opbx_log(LOG_DEBUG, "Key failed verification: %s\n", key->name);
		return -1;
	}
	/* Pass */
	return 0;
}

int opbx_check_signature(struct opbx_key *key, const char *msg, const char *sig)
{
	unsigned char dsig[128];
	int res;

	/* Decode signature */
	res = opbx_base64decode(dsig, sig, sizeof(dsig));
	if (res != sizeof(dsig)) {
		opbx_log(LOG_WARNING, "Signature improper length (expect %d, got %d)\n", (int)sizeof(dsig), (int)res);
		return -1;
	}
	res = opbx_check_signature_bin(key, msg, strlen(msg), dsig);
	return res;
}

static void crypto_load(int ifd, int ofd)
{
	struct opbx_key *key, *nkey, *last;
	DIR *dir = NULL;
	struct dirent *ent;
	int note = 0;
	/* Mark all keys for deletion */
	opbx_mutex_lock(&keylock);
	key = keys;
	while(key) {
		key->delme = 1;
		key = key->next;
	}
	opbx_mutex_unlock(&keylock);
	/* Load new keys */
	dir = opendir((char *)opbx_config_OPBX_KEY_DIR);
	if (dir) {
		while((ent = readdir(dir))) {
			try_load_key((char *)opbx_config_OPBX_KEY_DIR, ent->d_name, ifd, ofd, &note);
		}
		closedir(dir);
	} else
		opbx_log(LOG_WARNING, "Unable to open key directory '%s'\n", (char *)opbx_config_OPBX_KEY_DIR);
	if (note) {
		opbx_log(LOG_NOTICE, "Please run the command 'init keys' to enter the passcodes for the keys\n");
	}
	opbx_mutex_lock(&keylock);
	key = keys;
	last = NULL;
	while(key) {
		nkey = key->next;
		if (key->delme) {
			opbx_log(LOG_DEBUG, "Deleting key %s type %d\n", key->name, key->ktype);
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
	opbx_mutex_unlock(&keylock);
}

static int show_keys(int fd, int argc, char *argv[])
{
	struct opbx_key *key;
	char sum[16 * 2 + 1];
	int count_keys = 0;

	opbx_mutex_lock(&keylock);
	key = keys;
	opbx_cli(fd, "%-18s %-8s %-16s %-33s\n", "Key Name", "Type", "Status", "Sum");
	while(key) {
		opbx_hash_to_hex(sum, key->md_value, key->md_len);
		opbx_cli(fd, "%-18s %-8s %-16s %-33s\n", key->name, 
			(key->ktype & 0xf) == OPBX_KEY_PUBLIC ? "PUBLIC" : "PRIVATE",
			key->ktype & KEY_NEEDS_PASSCODE ? "[Needs Passcode]" : "[Loaded]", sum);
				
		key = key->next;
		count_keys++;
	}
	opbx_mutex_unlock(&keylock);
	opbx_cli(fd, "%d known RSA keys.\n", count_keys);
	return RESULT_SUCCESS;
}

static int init_keys(int fd, int argc, char *argv[])
{
	struct opbx_key *key;
	int ign;
	char *kn;
	char tmp[256] = "";

	key = keys;
	while(key) {
		/* Reload keys that need pass codes now */
		if (key->ktype & KEY_NEEDS_PASSCODE) {
			kn = key->fn + strlen(opbx_config_OPBX_KEY_DIR) + 1;
			opbx_copy_string(tmp, kn, sizeof(tmp));
			try_load_key((char *)opbx_config_OPBX_KEY_DIR, tmp, fd, fd, &ign);
		}
		key = key->next;
	}
	return RESULT_SUCCESS;
}

static char show_key_usage[] =
"Usage: show keys\n"
"       Displays information about RSA keys known by CallWeaver\n";

static char init_keys_usage[] =
"Usage: init keys\n"
"       Initializes private keys (by reading in pass code from the user)\n";

static struct opbx_clicmd cli_show_keys = {
	.cmda = { "show", "keys", NULL },
	.handler = show_keys,
	.summary = "Displays RSA key information",
	.usage = show_key_usage,
};

static struct opbx_clicmd cli_init_keys = {
	.cmda = { "init", "keys", NULL },
	.handler = init_keys,
	.summary = "Initialize RSA key passcodes",
	.usage = init_keys_usage,
};

int opbx_crypto_init(void)
{
	SSL_library_init();
	ERR_load_crypto_strings();
	opbx_cli_register(&cli_show_keys);
	opbx_cli_register(&cli_init_keys);

	if (option_initcrypto)
		crypto_load(STDIN_FILENO, STDOUT_FILENO);
	else
		crypto_load(-1, -1);

	return 0;
}

int opbx_crypto_reload(void)
{
	crypto_load(-1, -1);
	return 0;
}