/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eris Associates Limited, UK
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

#include <pthread.h>
#include <strings.h>
#include <curl/curl.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/curl.h"


static CURLSH *cw_curl_share;

static pthread_key_t cw_curl_session_key;

static pthread_mutex_t cw_curl_mutex = PTHREAD_MUTEX_INITIALIZER;


static void cw_curl_lock(CURL *handle, curl_lock_data lock_data, curl_lock_access lock_access, void *userptr)
{
	pthread_mutex_t *mutex = userptr;

	CW_UNUSED(handle);
	CW_UNUSED(lock_data);
	CW_UNUSED(lock_access);

	pthread_mutex_lock(mutex);
}


static void cw_curl_unlock(CURL *handle, curl_lock_data lock_data, void *userptr)
{
	pthread_mutex_t *mutex = userptr;

	CW_UNUSED(handle);
	CW_UNUSED(lock_data);

	pthread_mutex_unlock(mutex);
}


int cw_curl_do(const char *url, const char *post, size_t (*write_data)(void *ptr, size_t size, size_t nmemb, void *data), void *write_args)
{
	CURL *curl;

	if (!(curl = pthread_getspecific(cw_curl_session_key))) {
		if ((curl = curl_easy_init())) {
			if (cw_curl_share)
				curl_easy_setopt(curl, CURLOPT_SHARE, cw_curl_share);
			curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
#ifdef CURLOPT_NOPROGRESS
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
#endif
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "Callweaver-libcurl/1.0");
			curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5);

			pthread_setspecific(cw_curl_session_key, curl);
		} else
			cw_log(CW_LOG_ERROR, "cURL initialization failed\n");
	}

	if (curl) {
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, write_args);

		if (!strncasecmp(url, "https", 5)) {
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
		}

		curl_easy_setopt(curl, CURLOPT_URL, url);

		if (post) {
			curl_easy_setopt(curl, CURLOPT_POST, 1);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
		}

		curl_easy_perform(curl);

		if (post) {
			curl_easy_setopt(curl, CURLOPT_POST, 0);
			curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
		}

		return 0;
	}

	return -1;
}


void cw_curl_init(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
	cw_curl_share = curl_share_init();
	curl_share_setopt(cw_curl_share, CURLSHOPT_LOCKFUNC, cw_curl_lock);
	curl_share_setopt(cw_curl_share, CURLSHOPT_UNLOCKFUNC, cw_curl_unlock);
	curl_share_setopt(cw_curl_share, CURLSHOPT_USERDATA, &cw_curl_mutex);
	curl_share_setopt(cw_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
	curl_share_setopt(cw_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
	curl_share_setopt(cw_curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	pthread_key_create(&cw_curl_session_key, curl_easy_cleanup);
}
