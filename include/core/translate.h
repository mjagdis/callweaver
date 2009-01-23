/*
 * CallWeaver -- An open source telephony toolkit.
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
 * \brief Translate via the use of pseudo channels
 */

#ifndef _CALLWEAVER_TRANSLATE_PVT_H
#define _CALLWEAVER_TRANSLATE_PVT_H

#define MAX_FORMAT 32

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "callweaver/frame.h"

/*! data structure associated with a translator */
struct cw_translator {
	struct cw_object obj;
	struct cw_registry_entry *reg_entry;
	/*! Name of translator */
	char name[80];
	/*! Source format */
	int src_format;
	/*! Source sample rate */
	int src_rate;
	/*! Destination format */
	int dst_format;
	/*! Destination sample rate */
	int dst_rate;
	/*! Private data associated with the translator */
	void *(*newpvt)(void);
	/*! Input frame callback */
	int (*framein)(void *pvt, struct cw_frame *in);
	/*! Output frame callback */
	struct cw_frame * (*frameout)(void *pvt);
	/*! Destroy translator callback */
	void (*destroy)(void *pvt);
	/* For performance measurements */
	/*! Generate an example frame */
	struct cw_frame *(*sample)(int *index);
};

#endif /* _CALLWEAVER_TRANSLATE_H */
