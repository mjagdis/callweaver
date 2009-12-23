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
 * \brief Music on hold handling
 */

#ifndef _CALLWEAVER_MOH_H
#define _CALLWEAVER_MOH_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Turn on music on hold on a given channel */
extern CW_RES_API_PUBLIC int cw_moh_start(struct cw_channel *chan, const char *mclass);

/*! Turn off music on hold on a given channel */
extern void CW_RES_API_PUBLIC cw_moh_stop(struct cw_channel *chan);

extern void CW_RES_API_PUBLIC cw_install_music_functions(int (*start_ptr)(struct cw_channel *, const char *), void (*stop_ptr)(struct cw_channel *), void (*cleanup_ptr)(struct cw_channel *));
	
extern CW_RES_API_PUBLIC void cw_uninstall_music_functions(void);
extern CW_RES_API_PUBLIC void cw_moh_cleanup(struct cw_channel *chan);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _CALLWEAVER_MOH_H */
