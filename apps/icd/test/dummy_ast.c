#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include "../../../include/asterisk/cli.h"
#include "../../../include/asterisk/lock.h"
#include "../../../include/asterisk/logger.h"
#include "../icd_types.h"

struct ast_channel { int x; };
struct ast_cdr { int x; };
struct ast_frame { int x; };
typedef struct ast_cdr ast_cdrbe;
int option_verbose;
struct ast_variable { int x; };
struct ast_config { int x; };
struct ast_app { int x; };

int icd_debug;

int icd_verbose;

icd_config_registry *app_icd_config_registry;


void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) {
    va_list ap;

    fprintf(stdout, "File %s, Line %d (%s): ", file, line, function);
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void ast_verbose(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}


void ast_cli(int fd, char *fmt, ...) {
}

int ast_hangup(struct ast_channel *chan) {
    return 0;
}

char ast_waitstream(struct ast_channel *c, char *breakon) {
    return '\0';
}

int ast_streamfile(struct ast_channel *c, char *filename, char *preflang) {
    return 0;
}

void ast_moh_stop(struct ast_channel *chan) {
}

int ast_moh_start(struct ast_channel *chan, char *class) {
    return 0;
}

char ast_waitfordigit(struct ast_channel *c, int ms) {
    return '\0';
}

int ast_autoservice_stop(struct ast_channel *chan) {
    return 0;
}

int ast_park_call(struct ast_channel *chan, struct ast_channel *host, int timeout, int *extout) {
    return 0;
}

char *ast_parking_ext(void) {
    return NULL;
}

int ast_write(struct ast_channel *chan, struct ast_frame *frame) {
    return 0;
}

void ast_cdr_free(struct ast_cdr *cdr) {
}

int ast_cdr_init(struct ast_cdr *cdr, struct ast_channel *chan) {
    return 0;
}

int ast_cdr_setcid(struct ast_cdr *cdr, struct ast_channel *chan) {
    return 0;
}

int ast_cdr_register(char *name, char *desc, ast_cdrbe be) {
    return 0;
}

void ast_cdr_unregister(char *name) {
}

void ast_cdr_start(struct ast_cdr *cdr) {
}

void ast_cdr_answer(struct ast_cdr *cdr) {
}

void ast_cdr_busy(struct ast_cdr *cdr) {
}

void ast_cdr_failed(struct ast_cdr *cdr) {
}

int ast_cdr_disposition(struct ast_cdr *cdr, int cause) {
    return 0;
}

void ast_cdr_end(struct ast_cdr *cdr) {
}

void ast_cdr_post(struct ast_cdr *cdr) {
}

void ast_cdr_setdestchan(struct ast_cdr *cdr, char *chan) {
}

void ast_cdr_setapp(struct ast_cdr *cdr, char *app, char *data) {
}

int ast_cdr_amaflags2int(char *flag) {
    return 0;
}

char *ast_cdr_disp2str(int disposition) {
    return  NULL;
}

void ast_cdr_reset(struct ast_cdr *cdr, int post) {
}

char *ast_cdr_flags2str(int flags) {
    return  NULL;
}

int ast_cdr_setaccount(struct ast_channel *chan, char *account) {
    return 0;
}

int ast_cdr_update(struct ast_channel *chan) {
    return 0;
}

void ast_frfree(struct ast_frame *fr) {
}

struct ast_cdr *ast_cdr_alloc(void) {
    return NULL;
}

struct ast_frame *ast_read(struct ast_channel *chan) {
    return NULL;
}

int ast_waitfor(struct ast_channel *chan, int ms) {
    return 0;
}

int ast_call(struct ast_channel *chan, char *addr, int timeout) {
    return 0;
}

struct ast_channel *ast_request(char *type, int format, void *data) {
    return NULL;
}

void ast_set_callerid(struct ast_channel *chan, char *callerid, int  anitoo) {
}

int ast_stopstream(struct ast_channel *c) {
    return 0;
}

int ast_async_goto(struct ast_channel *chan, char *context, char *exten, int priority, int needlock) {
    return 0;
}

int ast_channel_setoption(struct ast_channel *channel, int option, void *data, int datalen, int block) {
    return 0;
}

int ast_answer(struct ast_channel *chan) {
    return 0;
}

int ast_channel_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc) {
    return 0;
}

int ast_indicate(struct ast_channel *chan, int condition) {
    return 0;
}

int ast_autoservice_start(struct ast_channel *chan) {
    return 0;
}

int ast_matchmore_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid) {
    return 0;
}

int ast_exists_extension(struct ast_channel *c, char *context, char *exten, int priority, char *callerid) {
    return 0;
}

int ast_softhangup(struct ast_channel *chan, int cause) {
    return 0;
}

int ast_app_getdata(struct ast_channel *c, char *prompt, char *s, int maxlen, int timeout) {
    return 0;
}

int ast_best_codec(int fmts) {
    return 0;
}

int ast_set_read_format(struct ast_channel *chan, int format) {
    return 0;
}

int ast_set_write_format(struct ast_channel *chan, int format) {
    return 0;
}

int ast_channel_make_compatible(struct ast_channel *c0, struct ast_channel *c1) {
    return 0;
}


int ast_cli_register ( struct ast_cli_entry * e ) { 
    return 0; 
}

int ast_register_application ( char * app, int(* execute)(struct ast_channel *, void *), char * synopsis, char * description) {
    return 0;
}

int ast_unregister_application(char *app) {
    return 0;
}


int ast_true(char *val) {
    return 0;
}

void ast_update_use_count(void) {
}

int ast_safe_sleep(struct ast_channel *chan, int ms) {
    return 0;
}

char *ast_category_browse(struct ast_config *config, char *prev) {
    return NULL;
}

struct ast_variable *ast_variable_browse(struct ast_config *config, char *category) {
    return NULL;
}


void ast_destroy(struct ast_config *config) {
}

struct ast_config *ast_load(char *configfile) {
    return NULL;
}

int ast_say_digits(struct ast_channel *chan, int num, char *ints, char *lang) {
    return 0;
}

struct ast_channel *ast_channel_alloc(int needalertpipe) {
    return NULL;
}

int ast_channel_masquerade(struct ast_channel *original, struct ast_channel *clone) {
    return 0;
}

void ast_deactivate_generator(struct ast_channel *chan) {
}

int ast_say_number(struct ast_channel *chan, int num, char *ints, char *lang, char *options) {
    return 0;
}

int ast_check_hangup(struct ast_channel *chan) {
    return 0;
}

struct ast_app *pbx_findapp(char *app) {
 return NULL;
} 

int pbx_exec(struct ast_channel *c, struct ast_app *app, void *data, int newstrck) {
 return 0;
}
 
char *ast_state2str(int state) {
 return NULL;
}
 
struct ast_channel *ast_waitfor_nandfds(struct ast_channel **c, int n, int *fds, int nfds, int *exception,int *outfd, int *ms) {
	return NULL;
}

int ast_cli_unregister(struct ast_cli_entry *e) {
    return 0;
}
