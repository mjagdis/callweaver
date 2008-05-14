#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include "callweaver/cli.h"
#include "callweaver/lock.h"
#include "callweaver/logger.h"
#include "callweaver/icd/icd_types.h"

struct cw_channel { int x; };
struct cw_cdr { int x; };
struct cw_frame { int x; };
typedef struct cw_cdr cw_cdrbe;
int option_verbose;
struct cw_variable { int x; };
struct cw_config { int x; };
struct cw_app { int x; };

int icd_debug;

int icd_verbose;

icd_config_registry *app_icd_config_registry;


void cw_log(int level, const char *file, int line, const char *function, const char *fmt, ...) {
    va_list ap;

    fprintf(stdout, "File %s, Line %d (%s): ", file, line, function);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void cw_verbose(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}


void cw_cli(int fd, char *fmt, ...) {
}

int cw_hangup(struct cw_channel *chan) {
    return 0;
}

char cw_waitstream(struct cw_channel *c, char *breakon) {
    return '\0';
}

int cw_streamfile(struct cw_channel *c, char *filename, char *preflang) {
    return 0;
}

void cw_moh_stop(struct cw_channel *chan) {
}

int cw_moh_start(struct cw_channel *chan, char *class) {
    return 0;
}

char cw_waitfordigit(struct cw_channel *c, int ms) {
    return '\0';
}

int cw_autoservice_stop(struct cw_channel *chan) {
    return 0;
}

int cw_park_call(struct cw_channel *chan, struct cw_channel *host, int timeout, int *extout) {
    return 0;
}

char *ast_parking_ext(void) {
    return NULL;
}

int cw_write(struct cw_channel *chan, struct cw_frame **frame) {
    return 0;
}

void cw_cdr_free(struct cw_cdr *cdr) {
}

int cw_cdr_init(struct cw_cdr *cdr, struct cw_channel *chan) {
    return 0;
}

int cw_cdr_setcid(struct cw_cdr *cdr, struct cw_channel *chan) {
    return 0;
}

int cw_cdr_register(struct module *module, struct cw_cdrbe_entry *entry) {
    return 0;
}

void cw_cdr_unregister(char *name) {
}

void cw_cdr_start(struct cw_cdr *cdr) {
}

void cw_cdr_answer(struct cw_cdr *cdr) {
}

void cw_cdr_busy(struct cw_cdr *cdr) {
}

void cw_cdr_failed(struct cw_cdr *cdr) {
}

int cw_cdr_disposition(struct cw_cdr *cdr, int cause) {
    return 0;
}

void cw_cdr_end(struct cw_cdr *cdr) {
}

void cw_cdr_post(struct cw_cdr *cdr) {
}

void cw_cdr_setdestchan(struct cw_cdr *cdr, char *chan) {
}

void cw_cdr_setapp(struct cw_cdr *cdr, char *app, char *data) {
}

int cw_cdr_amaflags2int(char *flag) {
    return 0;
}

char *ast_cdr_disp2str(int disposition) {
    return  NULL;
}

void cw_cdr_reset(struct cw_cdr *cdr, int post) {
}

char *ast_cdr_flags2str(int flags) {
    return  NULL;
}

int cw_cdr_setaccount(struct cw_channel *chan, char *account) {
    return 0;
}

int cw_cdr_update(struct cw_channel *chan) {
    return 0;
}

void cw_fr_free(struct cw_frame *fr) {
}

struct cw_cdr *ast_cdr_alloc(void) {
    return NULL;
}

struct cw_frame *ast_read(struct cw_channel *chan) {
    return NULL;
}

int cw_waitfor(struct cw_channel *chan, int ms) {
    return 0;
}

int cw_call(struct cw_channel *chan, char *addr, int timeout) {
    return 0;
}

struct cw_channel *ast_request(char *type, int format, void *data) {
    return NULL;
}

void cw_set_callerid(struct cw_channel *chan, char *callerid, int  anitoo) {
}

int cw_stopstream(struct cw_channel *c) {
    return 0;
}

int cw_async_goto_n(struct cw_channel *chan, char *context, char *exten, int priority) {
    return 0;
}

int cw_channel_setoption(struct cw_channel *channel, int option, void *data, int datalen) {
    return 0;
}

int cw_answer(struct cw_channel *chan) {
    return 0;
}

int cw_channel_bridge(struct cw_channel *c0, struct cw_channel *c1, int flags, struct cw_frame **fo, struct cw_channel **rc) {
    return 0;
}

int cw_indicate(struct cw_channel *chan, int condition) {
    return 0;
}

int cw_autoservice_start(struct cw_channel *chan) {
    return 0;
}

int cw_matchmore_extension(struct cw_channel *c, char *context, char *exten, int priority, char *callerid) {
    return 0;
}

int cw_exists_extension(struct cw_channel *c, char *context, char *exten, int priority, char *callerid) {
    return 0;
}

int cw_softhangup(struct cw_channel *chan, int cause) {
    return 0;
}

int cw_app_getdata(struct cw_channel *c, char *prompt, char *s, int maxlen, int timeout) {
    return 0;
}

int cw_best_codec(int fmts) {
    return 0;
}

int cw_set_read_format(struct cw_channel *chan, int format) {
    return 0;
}

int cw_set_write_format(struct cw_channel *chan, int format) {
    return 0;
}

int cw_channel_make_compatible(struct cw_channel *c0, struct cw_channel *c1) {
    return 0;
}


int cw_cli_register ( struct cw_clicmd * e ) { 
    return 0; 
}

int cw_register_function( const char * name, int(* execute)(struct cw_channel *, int, char **, char *, size_t, const char *), const char * synopsis, const char * syntax, const char * description) {
    return 0;
}

int cw_unregister_function(void *app) {
    return 0;
}


int cw_true(char *val) {
    return 0;
}

int cw_safe_sleep(struct cw_channel *chan, int ms) {
    return 0;
}

char *ast_category_browse(struct cw_config *config, char *prev) {
    return NULL;
}

struct cw_variable *ast_variable_browse(struct cw_config *config, char *category) {
    return NULL;
}


void cw_destroy(struct cw_config *config) {
}

struct cw_config *ast_load(char *configfile) {
    return NULL;
}

int cw_say_digits(struct cw_channel *chan, int num, char *ints, char *lang) {
    return 0;
}

struct cw_channel *ast_channel_alloc(int needalertpipe) {
    return NULL;
}

int cw_channel_masquerade(struct cw_channel *original, struct cw_channel *clone) {
    return 0;
}

void cw_deactivate_generator(struct cw_channel *chan) {
}

int cw_say_number(struct cw_channel *chan, int num, char *ints, char *lang, char *options) {
    return 0;
}

int cw_check_hangup(struct cw_channel *chan) {
    return 0;
}

int cw_function_exec_str(struct cw_channel *chan, unsigned int hash, const char *name, char *args, char *out, size_t outlen) {
 return 0;
}
 
char *ast_state2str(int state) {
 return NULL;
}
 
struct cw_channel *cw_waitfor_nandfds(struct cw_channel **c, int n, int *fds, int nfds, int *exception,int *outfd, int *ms) {
	return NULL;
}

int cw_cli_unregister(struct cw_clicmd *e) {
    return 0;
}
