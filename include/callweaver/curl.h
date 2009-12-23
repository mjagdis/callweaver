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

/*! \brief HTTP[S]/FTP/SCP data access interface
 *
 * \par See also:
 *     \arg \ref func_curl.c
 *     \arg \ref res_config_curl.c
 */

#ifdef HAVE_LIBCURL

#include <curl/curl.h>


/* \brief Request data using HTTP[S]/FTP/SCP (or any other protocol supported by libcurl)
 *
 * \param url
 * \param post
 * \param write_data
 * \param write_args
 *
 * \return 0 on success, -1 on error
 */
extern CW_API_PUBLIC int cw_curl_do(const char *url, const char *post, size_t (*write_data)(void *ptr, size_t size, size_t nmemb, void *data), void *write_args);

/* \brief Initialize libcurl support
 */
extern void cw_curl_init(void);
#endif

