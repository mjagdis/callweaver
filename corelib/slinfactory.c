/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief A machine to gather up arbitrary frames and convert them
 * to raw slinear on demand.
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 * 
 */
#ifdef HAVE_CONFIG_H
#include "confdefs.h"
#endif

#include <string.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/slinfactory.h"
#include "callweaver/logger.h"
#include "callweaver/translate.h"


void opbx_slinfactory_init(struct opbx_slinfactory *sf) 
{
	memset(sf, 0, sizeof(struct opbx_slinfactory));
	sf->offset = sf->hold;
	sf->queue = NULL;
	opbx_mutex_init(&(sf->lock));
}

void opbx_slinfactory_destroy(struct opbx_slinfactory *sf) 
{
	struct opbx_frame *f;

	if (sf->trans)
    {
		opbx_translator_free_path(sf->trans);
		sf->trans = NULL;
	}

	while ((f = sf->queue))
    {
		sf->queue = f->next;
		opbx_fr_free(f);
	}
	opbx_mutex_destroy(&(sf->lock));

}

int opbx_slinfactory_feed(struct opbx_slinfactory *sf, struct opbx_frame *f)
{
	struct opbx_frame *frame;
	struct opbx_frame *frame_ptr;

	if (f == NULL)
		return 0;
	opbx_mutex_lock(&(sf->lock));
	if (f->subclass != OPBX_FORMAT_SLINEAR)
    {
		if (sf->trans  &&  f->subclass != sf->format)
        {
			opbx_translator_free_path(sf->trans);
			sf->trans = NULL;
		}
		if (sf->trans == NULL)
        {
			if ((sf->trans = opbx_translator_build_path(OPBX_FORMAT_SLINEAR, f->subclass)) == NULL)
            {
				opbx_log(LOG_WARNING, "Cannot build a path from %s to slin\n", opbx_getformatname(f->subclass));
				opbx_mutex_unlock(&(sf->lock));
				return 0;
			}
			sf->format = f->subclass;
		}
	}

	if (sf->trans) {
		if ((frame = opbx_translate(sf->trans, f, 0)))
			frame = opbx_frdup(frame);
	} else
		frame = opbx_frdup(f);
	if (frame)
    {
		int x = 0;

		frame->next = NULL;

		for (frame_ptr = sf->queue;  frame_ptr  &&  frame_ptr->next;  frame_ptr=frame_ptr->next)
			x++;
		if (frame_ptr)
			frame_ptr->next = frame;
		else
			sf->queue = frame;
		frame->next = NULL;
		sf->size += frame->datalen;
		opbx_mutex_unlock(&(sf->lock));
		return x;
	}
	opbx_mutex_unlock(&(sf->lock));

	return 0;
}

int opbx_slinfactory_read(struct opbx_slinfactory *sf, int16_t *buf, size_t bytes) 
{
	struct opbx_frame *frame_ptr;
	int sofar = 0;
	int ineed;
	int remain;
	int16_t *frame_data;
	int16_t *offset = buf;
	
	opbx_mutex_lock(&(sf->lock));

	while (sofar < bytes)
    {
		ineed = bytes - sofar;

		if (sf->holdlen)
        {
			if ((sf->holdlen) <= ineed)
            {
				memcpy(offset, sf->hold, sf->holdlen);
				sofar += sf->holdlen;
				offset += (sf->holdlen/sizeof(int16_t));
				sf->holdlen = 0;
				sf->offset = sf->hold;
			}
            else
            {
				remain = sf->holdlen - ineed;
				memcpy(offset, sf->offset, ineed);
				sofar += ineed;
				sf->offset += (ineed / sizeof(int16_t));
				sf->holdlen = remain;
			}
			continue;
		}
		
		if (sofar < bytes  &&  (frame_ptr = sf->queue))
        {
			sf->queue = frame_ptr->next;
			frame_data = frame_ptr->data;
			
			if ((frame_ptr->datalen) <= ineed)
            {
				memcpy(offset, frame_data, frame_ptr->datalen);
				sofar += frame_ptr->datalen;
				offset += (frame_ptr->datalen/sizeof(int16_t));
			}
            else
            {
				remain = frame_ptr->datalen - ineed;

				memcpy(offset, frame_data, ineed);
				sofar += ineed;
				frame_data += (ineed / sizeof(int16_t));
				memcpy(sf->hold, frame_data, remain);
				sf->holdlen = remain;
			}
			opbx_fr_free(frame_ptr);
		}
        else
        {
			break;
		}
	}

	sf->size -= sofar;
	opbx_mutex_unlock(&(sf->lock));
	return sofar;
}
