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
 * \brief General CallWeaver channel definitions for image handling
 */

#ifndef _CALLWEAVER_IMAGE_H
#define _CALLWEAVER_IMAGE_H

#include "callweaver/object.h"
#include "callweaver/registry.h"
#include "callweaver/module.h"


/*! \brief structure associated with registering an image format */
struct opbx_imager {
	struct opbx_object obj;
	struct opbx_registry_entry imager_entry;
	/*! Name */
	char *name;						
	/*! Description */
	char *desc;						
	/*! Extension(s) (separated by ',' ) */
	char *exts;						
	/*! Image format */
	int format;						
	/*! Read an image from a file descriptor */
	struct opbx_frame *(*read_image)(int fd, int len);	
	/*! Identify if this is that type of file */
	int (*identify)(int fd);				
	/*! Returns length written */
	int (*write_image)(int fd, struct opbx_frame *frame); 	
};


extern struct opbx_registry imager_registry;


#define opbx_image_register(ptr) ({ \
	const typeof(ptr) __ptr = (ptr); \
	opbx_object_init_obj(&__ptr->obj, get_modinfo()->self); \
	__ptr->imager_entry.obj = &__ptr->obj; \
	opbx_registry_add(&imager_registry, &__ptr->imager_entry); \
})
#define opbx_image_unregister(ptr)	opbx_registry_del(&imager_registry, &(ptr)->imager_entry)


/*! Check for image support on a channel */
/*! 
 * \param chan channel to check
 * Checks the channel to see if it supports the transmission of images
 * Returns non-zero if image transmission is supported
 */
extern int opbx_supports_images(struct opbx_channel *chan);

/*! Sends an image */
/*!
 * \param chan channel to send image on
 * \param filename filename of image to send (minus extension)
 * Sends an image on the given channel.
 * Returns 0 on success, -1 on error
 */
extern int opbx_send_image(struct opbx_channel *chan, char *filename);

/*! Make an image */
/*! 
 * \param filename filename of image to prepare
 * \param preflang preferred language to get the image...?
 * \param format the format of the file
 * Make an image from a filename ??? No estoy positivo
 * Returns an opbx_frame on success, NULL on failure
 */
extern struct opbx_frame *opbx_read_image(char *filename, char *lang, int format);

/*! Initialize image stuff */
/*!
 * Initializes all the various image stuff.  Basically just registers the cli stuff
 * Returns 0 all the time
 */
extern int opbx_image_init(void);

#endif /* _CALLWEAVER_IMAGE_H */
