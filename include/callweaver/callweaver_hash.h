/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Eris Associates Limited, UK
 * Copyright (C) 2006 Sunrise Telephone Systems Ltd. All rights reserved.
 *
 * Authors:
 *     Mike Jagdis <mjagdis@eris-associates.co.uk>
 *     Benjamin Kowarsch <benjamin at sunrise dash tel dot com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. Where software
 * packages making use of this software have a command line interface, the
 * above copyright notice and a reference to the license terms must be
 * displayed whenever the command line interface is invoked. Where software
 * packages making use of this software have a graphical user interface, the
 * above copyright notice and a reference to the license terms must be
 * displayed in the application's "about this software" window or equivalent.
 * Installers which install this software must display the above copyright
 * notice and these license terms in full.
 *
 * Under no circumstances is it permitted for any licensee to take credit for
 * the creation of this software, to claim authorship or ownership in this
 * software or in any other way give the impression that they have created
 * this software. Credit to the true authors and rights holders of this
 * software is absolutely mandatory and must be given at all times.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * In countries and territories where the above no-warranty disclaimer is
 * not permissible by applicable law, the following terms apply:
 *
 * NO PERMISSION TO USE THE SOFTWARE IS GRANTED AND THE SOFTWARE MUST NOT BE
 * USED AT ALL IN SUCH COUNTRIES AND TERRITORIES WHERE THE ABOVE NO-WARRANTY
 * DISCLAIMER IS NOT PERMISSIBLE AND INVALIDATED BY APPLICABLE LAW. HOWEVER,
 * THE COPYRIGHT HOLDERS HEREBY WAIVE THEIR RIGHT TO PURSUE OFFENDERS AS LONG
 * AS THEY OTHERWISE ABIDE BY THE TERMS OF THE LICENSE AS APPLICABLE FOR USE
 * OF THE SOFTWARE IN COUNTRIES AND TERRITORIES WHERE THE ABOVE NO-WARRANTY
 * DISCLAIMER IS PERMITTED BY APPLICABLE LAW. THIS WAIVER DOES NOT CONSTITUTE
 * A LICENSE TO USE THE SOFTWARE IN COUNTRIES AND TERRITORIES WHERE THE ABOVE
 * NO-WARRANTY DISCLAIMER IS NOT PERMISSIBLE AND INVALIDATED BY APPLICABLE
 * LAW. ANY LIABILITY OF ANY KIND IS CATEGORICALLY RULED OUT AT ALL TIMES.
 */

/*! \file
 * \brief API to use string hashes for keywords in place of strcmp()
 */

#ifndef _CW_HASH_H
#define _CW_HASH_H


#define cw_hash_add(hash, val)	((val) + ((hash) << 6) + ((hash) << 16) - (hash))


/*! \brief Returns the hash value of the null terminated C string 'string' using the
 * SDBM hash algorithm.
 *
 * \param string  the string to hash
 *
 * \return 0 if 'string' is a zero-length string or NULL.
 */
static inline __attribute__ ((pure)) unsigned int cw_hash_string(const char *string)
{
	unsigned int hash;

	hash = 0;
	if (string) {
		while (*string)
			hash = cw_hash_add(hash, *(string++));
	}

	return hash;
}

#endif

// END OF FILE
