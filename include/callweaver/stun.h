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

extern char stunserver_host[MAXHOSTNAMELEN];
extern struct sockaddr_in stunserver_ip;
extern int stunserver_portno;
extern int stundebug;               /*!< Are we debugging stun? */

rfc3489_addr_t *opbx_stun_find_request(rfc3489_trans_id_t *st);

rfc3489_request_t *opbx_udp_stun_bindrequest(int fdus,
                                             struct sockaddr_in *suggestion, 
                                             const char *username,
                                             const char *password);

int stun_remove_request(rfc3489_trans_id_t *st);

int stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len, rfc3489_state_t *st);

int stun_do_debug(int fd, int argc, char *argv[]);

int stun_no_debug(int fd, int argc, char *argv[]);

int opbx_stun_init(void);

//static void append_attr_string(rfc3489_attr_t **attr, int attrval, const char *s, int *len, int *left)
