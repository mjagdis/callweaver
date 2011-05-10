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

#include <callweaver/sockaddr.h>


extern CW_API_PUBLIC int cw_stun_bindrequest(int s, const struct sockaddr *from, socklen_t fromlen, const struct sockaddr *to, socklen_t tolen, struct sockaddr_in *stunaddr);

extern CW_API_PUBLIC int cw_stun_handle_packet(int s, const struct sockaddr_in *src, const unsigned char *data, size_t len, struct sockaddr_in *sin);

int cw_stun_init(void);
