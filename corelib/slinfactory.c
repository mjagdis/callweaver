/*
 * OpenPBX -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
 *
 * See http://www.openpbx.org for more information about
 * the OpenPBX project. Please do not directly contact
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
 * \brief A machine to gather up arbitrary frames and convert them
 * to raw slinear on demand.
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <string.h>

#include "openpbx.h"

OPENPBX_FILE_VERSION("$HeadURL: svn://svn.openpbx.org/openpbx/trunk/corelib/ $", "$Revision$")

#include "openpbx/slinfactory.h"
#include "openpbx/logger.h"
#include "openpbx/translate.h"


void opbx_slinfactory_init(struct opbx_slinfactory *sf) 
{
	memset(sf, 0, sizeof(struct opbx_slinfactory));
	sf->offset = sf->hold;
	sf->queue = NULL;
}

void opbx_slinfactory_destroy(struct opbx_slinfactory *sf) 
{
	struct opbx_frame *f;

	if (sf->trans) {
		opbx_translator_free_path(sf->trans);
		sf->trans = NULL;
	}

	while((f = sf->queue)) {
		sf->queue = f->next;
		opbx_frfree(f);
	}
}

int opbx_slinfactory_feed(struct opbx_slinfactory *sf, struct opbx_frame *f)
{
	struct opbx_frame *frame, *frame_ptr;

	if (!f) {
		return 0;
	}

	if (f->subclass != OPBX_FORMAT_SLINEAR) {
		if (sf->trans && f->subclass != sf->format) {
			opbx_translator_free_path(sf->trans);
			sf->trans = NULL;
		}
		if (!sf->trans) {
			if ((sf->trans = opbx_translator_build_path(OPBX_FORMAT_SLINEAR, f->subclass)) == NULL) {
				opbx_log(LOG_WARNING, "Cannot build a path from %s to slin\n", opbx_getformatname(f->subclass));
				return 0;
			} else {
				sf->format = f->subclass;
			}
		}
	}

	if (sf->trans) {
		frame = opbx_frdup(opbx_translate(sf->trans, f, 0));
	} else {
		frame = opbx_frdup(f);
	}

	if (frame) {
		int x = 0;
		for (frame_ptr = sf->queue; frame_ptr && frame_ptr->next; frame_ptr=frame_ptr->next) {
			x++;
		}
		if (frame_ptr) {
			frame_ptr->next = frame;
		} else {
			sf->queue = frame;
		}
		frame->next = NULL;
		sf->size += frame->datalen;	
		return x;
	}

	return 0;
	
}

int opbx_slinfactory_read(struct opbx_slinfactory *sf, short *buf, size_t bytes) 
{
	struct opbx_frame *frame_ptr;
	size_t sofar = 0, ineed, remain;
	short *frame_data, *offset = buf;

	while (sofar < bytes) {
		ineed = bytes - sofar;

		if (sf->holdlen) {
			if ((sofar + sf->holdlen) <= ineed) {
				memcpy(offset, sf->hold, sf->holdlen);
				sofar += sf->holdlen;
				offset += (sf->holdlen / sizeof(short));
				sf->holdlen = 0;
				sf->offset = sf->hold;
			} else {
				remain = sf->holdlen - ineed;
				memcpy(offset, sf->offset, ineed);
				sofar += ineed;
				sf->offset += (ineed / sizeof(short));
				sf->holdlen = remain;
			}
			continue;
		}
		
		if ((frame_ptr = sf->queue)) {
			sf->queue = frame_ptr->next;
			frame_data = frame_ptr->data;
			
			if ((sofar + frame_ptr->datalen) <= ineed) {
				memcpy(offset, frame_data, frame_ptr->datalen);
				sofar += frame_ptr->datalen;
				offset += (frame_ptr->datalen / sizeof(short));
			} else {
				remain = frame_ptr->datalen - ineed;
				memcpy(offset, frame_data, ineed);
				sofar += ineed;
				frame_data += (ineed / sizeof(short));
				memcpy(sf->hold, frame_data, remain);
				sf->holdlen = remain;
			}
			opbx_frfree(frame_ptr);
		} else {
			break;
		}
	}

	sf->size -= sofar;
	return sofar;
}




