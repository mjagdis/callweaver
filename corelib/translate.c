/*
 * CallWeaver -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Eris Associates Limited, UK
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mike Jagdis <mjagdis@eris-associates.co.uk>
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
 *
 * \brief Translate via the use of pseudo channels
 * 
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#if _POSIX_MEMLOCK
  /* Some POSIX systems apparently need the following definition
   * to get mlockall flags out of sys/mman.h
   */
#  ifndef _P1003_1B_VISIBLE
#    define _P1003_1B_VISIBLE
#  endif
#  include <sys/mman.h>
#else
#  define mlockall(flags)
#  define munlockall()
#endif

#if HAVE_SETAFFINITY
#  include <sched.h>
#endif

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>

#include "callweaver.h"

CALLWEAVER_FILE_VERSION("$HeadURL$", "$Revision$")

#include "callweaver/lock.h"
#include "callweaver/channel.h"
#include "callweaver/logger.h"
#include "callweaver/options.h"
#include "callweaver/frame.h"
#include "callweaver/sched.h"
#include "callweaver/cli.h"

#include "core/translate.h"
#include "callweaver/translate.h"


/* Interval to time translators over in nanoseconds (must be < 1s) */
#define TIMING_INTERVAL 10000000


#ifdef __linux__
/* Opened by callweaver.c before privs are dropped */
extern int cw_cpu0_governor_fd;
#endif


/* This could all be done more efficiently *IF* we chained packets together
   by default, but it would also complicate virtually every application. */
   

struct cw_translator_dir
{
	struct cw_translator *step; /* Next step translator */
	unsigned int cost;          /* Complete cost to destination */
	int steps;                  /* Number of steps it takes */
};


struct trans_state {
	struct cw_object obj;
	unsigned int min_cost;
	struct cw_translator_dir matrix[MAX_FORMAT][MAX_FORMAT];
};

pthread_spinlock_t state_lock;
static struct trans_state *trans_state;


struct cw_frame_delivery
{
    struct cw_frame *f;
    struct cw_channel *chan;
    int fd;
    struct translator_pvt *owner;
    struct cw_frame_delivery *prev;
    struct cw_frame_delivery *next;
};


struct cw_trans_pvt
{
    struct cw_translator *step;
    struct cw_translator_pvt *state;
    struct cw_trans_pvt *next;
    struct timeval nextin;
    struct timeval nextout;
};


void cw_translator_free_path(struct cw_trans_pvt *p)
{
    struct cw_trans_pvt *pl;
    struct cw_trans_pvt *pn;

    pn = p;
    while (pn)
    {
        pl = pn;
        pn = pn->next;
        if (pl->state  &&  pl->step->destroy)
            pl->step->destroy(pl->state);
	cw_object_put(pl->step);
        free(pl);
    }
}

/* Build a set of translators based upon the given source and destination formats */
struct cw_trans_pvt *cw_translator_build_path(int dest, int dest_rate, int source, int source_rate)
{
	struct trans_state *tr;
	struct cw_trans_pvt *tmpr = NULL;
	struct cw_trans_pvt **next = &tmpr;
	struct cw_translator *t;

	source = bottom_bit(source);
	dest = bottom_bit(dest);
    
	pthread_spin_lock(&state_lock);
	tr = cw_object_dup(trans_state);
	pthread_spin_unlock(&state_lock);
	if (!tr)
		goto out;

	while (source != dest) {
		if (!(t = cw_object_dup(tr->matrix[source][dest].step))) {
			cw_log(CW_LOG_WARNING, "No translator path from %s to %s\n", cw_getformatname(1 << source), cw_getformatname(1 << dest));
			cw_translator_free_path(tmpr);
			tmpr = NULL;
			break;
		}

		if (!(*next = malloc(sizeof(*tmpr)))) {
			cw_log(CW_LOG_ERROR, "Out of memory\n");
			cw_object_put(t);
			cw_translator_free_path(tmpr);    
			tmpr = NULL;
			break;
		}

		(*next)->next = NULL;
		(*next)->nextin = (*next)->nextout = cw_tv(0, 0);
		(*next)->step = t;
		if (!((*next)->state = t->newpvt())) {
			cw_log(CW_LOG_WARNING, "Failed to build translator step from %d to %d\n", source, dest);
			cw_translator_free_path(tmpr);    
			tmpr = NULL;
			break;
		}

		if (option_debug)
			cw_log(CW_LOG_DEBUG, "translate %s to %s using %s\n", cw_getformatname(1 << source), cw_getformatname(t->dst_format), t->name);

		/* Keep going if this isn't the final destination */
		source = bottom_bit((*next)->step->dst_format);
		next = &(*next)->next;
	}

	cw_object_put(tr);

out:
	return tmpr;
}

struct cw_frame *cw_translate(struct cw_trans_pvt *path, struct cw_frame *f, int consume)
{
    struct cw_trans_pvt *p;
    struct cw_frame *out;
    struct timeval delivery;
    int has_timing_info;
    long ts;
    long duration;
    int seq_no;
    
    has_timing_info = f->has_timing_info;
    ts = f->ts;
    duration = f->duration;
    seq_no = f->seq_no;

    p = path;
    /* Feed the first frame into the first translator */
    p->step->framein(p->state, f);
    if (!cw_tvzero(f->delivery))
    {
        if (!cw_tvzero(path->nextin))
        {
            /* Make sure this is in line with what we were expecting */
            if (!cw_tveq(path->nextin, f->delivery))
            {
                /* The time has changed between what we expected and this
                   most recent time on the new packet.  If we have a
                   valid prediction adjust our output time appropriately */
                if (!cw_tvzero(path->nextout))
                {
                    path->nextout = cw_tvadd(path->nextout,
                                               cw_tvsub(f->delivery, path->nextin));
                }
                path->nextin = f->delivery;
            }
        }
        else
        {
            /* This is our first pass.  Make sure the timing looks good */
            path->nextin = f->delivery;
            path->nextout = f->delivery;
        }
        /* Predict next incoming sample */
        path->nextin = cw_tvadd(path->nextin, cw_samp2tv(f->samples, 8000));
    }
    delivery = f->delivery;
    if (consume)
        cw_fr_free(f);
    while (p)
    {
        /* If we get nothing out, return NULL */
        if ((out = p->step->frameout(p->state)) == NULL)
            return NULL;
        /* If there is a next state, feed it in there.  If not,
           return this frame  */
        if (p->next)
        { 
            p->next->step->framein(p->next->state, out);
        }
        else
        {
            if (!cw_tvzero(delivery))
            {
                /* Regenerate prediction after a discontinuity */
                if (cw_tvzero(path->nextout))
                    path->nextout = cw_tvnow();

                /* Use next predicted outgoing timestamp */
                out->delivery = path->nextout;
                
                /* Predict next outgoing timestamp from samples in this
                   frame. */
                path->nextout = cw_tvadd(path->nextout, cw_samp2tv( out->samples, 8000));
            }
            else
            {
                out->delivery = cw_tv(0, 0);
            }
            /* Invalidate prediction if we're entering a silence period */
            if (out->frametype == CW_FRAME_CNG)
                path->nextout = cw_tv(0, 0);

            out->has_timing_info = has_timing_info;
            if (has_timing_info)
            {
                out->ts = ts;
                out->duration = duration;
                //out->duration = cw_codec_get_samples(out)/8;
                out->seq_no = seq_no;
            }

            return out;
        }
        p = p->next;
    }
    cw_log(CW_LOG_WARNING, "I should never get here...\n");
    return NULL;
}


struct rebuild_matrix_args {
	struct trans_state *tr;
	struct cw_translator *t;
	volatile int stop;
	unsigned int *cost;
	int oneshot;
	pthread_attr_t calc_attr, timer_attr;
	struct sched_param calc_param, timer_param;
};


static void *calc_cost_timer(void *data)
{
	struct timespec ts;
	struct rebuild_matrix_args *args = data;

	ts.tv_sec = 0;
	ts.tv_nsec = TIMING_INTERVAL;

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR);

	args->stop = 1;
	return NULL;
}


static void *calc_cost(void *data)
{
	pthread_t tid;
	struct rebuild_matrix_args *args = data;
	struct cw_translator *t = args->t;
	struct cw_translator_pvt *pvt = NULL;
	struct cw_frame *f;
	struct timespec start, end;
	double samples;
	double interval;
	int ret;

	/* If we can't score it it's maximally expensive */
	*args->cost = UINT_MAX;

	if (t->sample == NULL) {
		cw_log(CW_LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		return NULL;
	}

	if ((pvt = t->newpvt()) == NULL) {
		cw_log(CW_LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		return NULL;
	}

	/* If we are doing an initial single frame pass to fault pages
	 * in, grow the stack, warm the caches etc. then we are already
	 * stopped. If not, we need to ask for a wake up call.
	 */
	if (!args->stop) {
		if ((ret = cw_pthread_create(&tid, &args->timer_attr, calc_cost_timer, args)))
			goto out;
	}

	samples = 0.0;

	cw_clock_gettime(global_clock_monotonic, &start);

	do {
		if ((f = t->sample())) {
			t->framein(pvt, f);
			samples += f->samples;
			cw_fr_free(f);
			while ((f = t->frameout(pvt)))
				cw_fr_free(f);
			continue;
		}

		cw_log(CW_LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
		goto out;
	} while (!args->stop);

	cw_clock_gettime(global_clock_monotonic, &end);

	interval = (double)(end.tv_sec - start.tv_sec) * 1000000000.0 + (double)(end.tv_nsec - start.tv_nsec);

	interval = t->src_rate * interval / samples;

	/* If it takes longer than 1s to translate 1s of audio it isn't usable! */
	if (interval < 1000000000.0) {
		*args->cost = (unsigned int)interval;
		if (*args->cost < args->tr->min_cost)
			args->tr->min_cost = *args->cost;
	}

out:
	if (pvt)
		t->destroy(pvt);
	return NULL;
}

static int rebuild_matrix_one(struct cw_object *obj, void *data)
{
	struct cw_translator *t = container_of(obj, struct cw_translator, obj);
	struct rebuild_matrix_args *args = data;
	struct cw_translator_dir *td = &args->tr->matrix[bottom_bit(t->src_format)][bottom_bit(t->dst_format)];
	pthread_t tid;
	int ret;

	args->stop = args->oneshot;
	args->t = t;
	args->cost = &td->cost;
	td->cost = UINT_MAX;

	/* We _could_ just call calc_cost here, but we want to do it with FIFO
	 * scheduling and we don't want to run the whole registry iterate with
	 * elevated priority.
	 */
	if (!(ret = cw_pthread_create(&tid, &args->calc_attr, calc_cost, args))) {
		pthread_join(tid, NULL);
	} else {
		cw_log(CW_LOG_ERROR, "calc_cost thread: %d %s\n", ret, strerror(ret));
	}

	if (td->cost != UINT_MAX) {
		td->step = cw_object_dup(t);
		td->steps = 1;
	} else 
		cw_log(CW_LOG_ERROR, "translator %s is not usable and has been disabled\n", args->t->name);

	return 0;
}


static void rebuild_matrix(void)
{
#ifdef __linux__
	char governor[sizeof("performance")];
#endif
#if HAVE_SETAFFINITY
	cpu_set_t old_cpuset, new_cpuset;
#endif
	pthread_mutex_t serialize = PTHREAD_MUTEX_INITIALIZER;
	struct rebuild_matrix_args args;
	struct trans_state *old_tr, *new_tr;
	int affinity;
	int changed, x, y, z;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Reseting translation matrix\n");

	if (!(new_tr = malloc(sizeof(*new_tr)))) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		return;
	}

	pthread_attr_init(&args.calc_attr);
	pthread_attr_setstacksize(&args.calc_attr, CW_STACKSIZE);
	pthread_attr_setinheritsched(&args.calc_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&args.calc_attr, SCHED_FIFO);
	args.calc_param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 2;
	pthread_attr_setschedparam(&args.calc_attr, &args.calc_param);

	pthread_attr_init(&args.timer_attr);
	pthread_attr_setstacksize(&args.timer_attr, CW_STACKSIZE);
	pthread_attr_setinheritsched(&args.timer_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&args.timer_attr, SCHED_FIFO);
	args.timer_param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
	pthread_attr_setschedparam(&args.timer_attr, &args.timer_param);
	pthread_attr_setdetachstate(&args.timer_attr, PTHREAD_CREATE_DETACHED);

	cw_object_init(new_tr, NULL, 1);
	args.tr = new_tr;
	new_tr->min_cost = UINT_MAX;
	memset(&new_tr->matrix, '\0', sizeof(new_tr->matrix));

	pthread_mutex_lock(&serialize);

#if HAVE_SETAFFINITY
	/* Bind to a specific CPU if possible to avoid migrations */
	affinity = CPU_SETSIZE;
	if (!sched_getaffinity(0, sizeof(old_cpuset), &old_cpuset)) {
		CPU_ZERO(&new_cpuset);
		for (affinity = 0; affinity < CPU_SETSIZE; affinity++) {
			if (CPU_ISSET(affinity, &old_cpuset)) {
				CPU_SET(affinity, &new_cpuset);
				if (!sched_setaffinity(0, sizeof(new_cpuset), &new_cpuset))
					break;
				CPU_CLR(affinity, &new_cpuset);
			}
		}
	} else
#else
		cw_log(CW_LOG_WARNING, "CPU affinity not supported - translation timings may be affected\n");
#endif

#ifdef __linux__
	/* If this is CPU0 and has a cpufreq governor save the current setting
	 * and switch it to "performance" for the translator timings.
	 * Note: we're reliant on a descriptor opened to the CPU0 governor before
	 * privileges were dropped. A more general solution allowing for the
	 * fact that CPU0 could be disabled, offline or just not in our set
	 * would require root privs with the current (kernel 2.6.25) sysfs.
	 */
	governor[0] = '\0';
	if (affinity == 0
	&& lseek(cw_cpu0_governor_fd, SEEK_SET, 0) == 0
	&& read(cw_cpu0_governor_fd, governor, sizeof(governor) - 1) > 0
	&& governor[strlen(governor) - 1] == '\n') {
		lseek(cw_cpu0_governor_fd, SEEK_SET, 0);
		write(cw_cpu0_governor_fd, "performance\n", sizeof("performance\n") - 1);
	} else
		governor[0] = '\0';
#endif

	/* Do a dummy run of each translator to grow heap/stack space appropriately
	 * in advance.
	 */
	args.oneshot = 1;
	cw_registry_iterate(&translator_registry, rebuild_matrix_one, &args);

	/* Avoid paging (if possible) while we're timing things */
	mlockall(MCL_CURRENT);

	/* Time each translator */
	args.oneshot = 0;
	cw_registry_iterate(&translator_registry, rebuild_matrix_one, &args);

	munlockall();

#ifdef __linux__
	/* Restore the original cpufreq governor */
	if (governor[0])
		write(cw_cpu0_governor_fd, governor, strlen(governor));
#endif

#if HAVE_SETAFFINITY
	/* Restore the original CPU affinity */
	if (affinity != -1)
		sched_setaffinity(0, sizeof(old_cpuset), &old_cpuset);
#endif

	pthread_mutex_unlock(&serialize);

	pthread_attr_destroy(&args.timer_attr);
	pthread_attr_destroy(&args.calc_attr);

	do {
		changed = 0;
		/* Don't you just love O(N^3) operations? */
		for (x = 0;  x < MAX_FORMAT;  x++) {
		    for (y = 0;  y < MAX_FORMAT;  y++) {
				if (x != y) {
					for (z = 0;  z < MAX_FORMAT;  z++) {
						if ((x != z)  &&  (y != z)) {
							if (new_tr->matrix[x][y].step
							&& new_tr->matrix[y][z].step
							&& (!new_tr->matrix[x][z].step
							|| (new_tr->matrix[x][y].cost + new_tr->matrix[y][z].cost < new_tr->matrix[x][z].cost))) {
								/* We can get from x to z via y with a cost that
								 * is the sum of the transition from x to y and
								 * from y to z. At least, we can if it's fast enough!
								 */
								new_tr->matrix[x][z].cost = new_tr->matrix[x][y].cost + new_tr->matrix[y][z].cost;
								if (new_tr->matrix[x][z].cost < 1000000000) {
									new_tr->matrix[x][z].step = cw_object_dup(new_tr->matrix[x][y].step);
									new_tr->matrix[x][z].steps = new_tr->matrix[x][y].steps + new_tr->matrix[y][z].steps;
								}
								if (option_debug)
									cw_log(CW_LOG_DEBUG, "Discovered path from %s to %s, via %s with %d steps and cost %u\n", cw_getformatname(1 << x), cw_getformatname(1 << z), cw_getformatname(1 << y), new_tr->matrix[x][z].steps, new_tr->matrix[x][z].cost);
								changed++;
							}
						}
					}
				}
			}
		}
	} while (changed);

	pthread_spin_lock(&state_lock);
	old_tr = cw_object_dup(trans_state);
	trans_state = new_tr;
	pthread_spin_unlock(&state_lock);

	if (old_tr) {
		for (x = 0; x < MAX_FORMAT; x++) {
			for (y = 0; y < MAX_FORMAT; y++) {
				if (old_tr->matrix[x][y].step)
					cw_object_put(old_tr->matrix[x][y].step);
			}
		}

		cw_object_put(old_tr);
	}
}


static void show_translation_generator(int fd, char *argv[], int lastarg, int lastarg_len)
{
	static const char *args[] = {
		"recalc", "rel", "raw", "ns", "us", "ms"
	};
	int i;

	for (i = 0; i < arraysize(args); i++)
		if (!strncmp(args[i], argv[lastarg], lastarg_len))
			cw_cli(fd, "%s\n", args[i]);
}


static int show_translation(int fd, int argc, char *argv[])
{
	static const char *scale[] = { "nano", "micro", "milli", "" };
	struct trans_state *tr;
#define SHOW_TRANS 11
	unsigned int mult;
	unsigned int cost;
	int x, y;

	if (argc > 4)
		return RESULT_SHOWUSAGE;

	if (argv[2] && !strcmp(argv[2], "recalc")) {
		argv[2] = NULL;
		rebuild_matrix();
	}

	pthread_spin_lock(&state_lock);
	tr = cw_object_dup(trans_state);
	pthread_spin_unlock(&state_lock);

	if (argv[2]) {
		if (!strcmp(argv[2], "rel")) {
			x = 0;
			mult = 0;
		} else if (!strcmp(argv[2], "raw") || !strcmp(argv[2], "ns")) {
			x = 0;
			mult = 1;
		} else if (!strcmp(argv[2], "us")) {
			x = 1;
			mult = 100;
		} else if (!strcmp(argv[2], "ms")) {
			x = 2;
			mult = 100000;
		} else {
			cw_object_put(tr);
			return RESULT_SHOWUSAGE;
		}
	} else {
		x = 0;
		mult = 1;
		while (tr->min_cost + (mult / 2) >= 1000 * mult && x < arraysize(scale) - 1) {
			mult *= 1000;
			x++;
		}
		if (mult > 1)
			mult /= 10;
	}

	if (mult)
		cw_cli(fd, "         Translation times between formats (time in %sseconds to translate 1s of audio)\n", scale[x]);
	else
		cw_cli(fd, "         Relative translation times between formats\n");

	cw_cli(fd, "         Source Format (Rows) Destination Format(Columns)\n\n");

	cw_cli(fd, "         ");
	for (x = 0;  x < SHOW_TRANS;  x++)
		cw_cli(fd, " %8s", cw_getformatname(1 << x));
	cw_cli(fd, "\n");

	for (x = 0;  x < SHOW_TRANS;  x++) {
		cw_cli(fd, " %8s", cw_getformatname(1 << x));

		for (y = 0;  y < SHOW_TRANS;  y++) {
			if (tr->matrix[x][y].step) {
				if (mult == 1) {
					cw_cli(fd, " %8u", tr->matrix[x][y].cost);
				} else {
					if (mult)
						cost = (tr->matrix[x][y].cost + (mult / 2)) / mult;
					else
						cost = (2 * tr->matrix[x][y].cost) / (tr->min_cost / 5);
					cw_cli(fd, " %6u.%01u", cost / 10, cost % 10);
				}
			} else
			    cw_cli(fd, "        -");
		}
		cw_cli(fd, "\n");
	}

	cw_object_put(tr);
	return RESULT_SUCCESS;
}

static char show_trans_usage[] =
"Usage: show translation [recalc | rel | raw | ns | us | ms]\n"
"       Displays known codec translators and the cost associated with each conversion.\n"
"       If no argument is supplied the costs will be shown in the \"most appropriate\" units.\n"
"\n"
"       Options:\n"
"       recalc - Recalculate the translation timings\n"
"       rel    - Show relative costs (lower cheaper)\n"
"       raw    - Show the raw timings (normally nanoseconds)\n"
"       ns     - Show timings in nanoseconds\n"
"       us     - Show timings in microseconds\n"
"       ms     - Show timings in milliseconds\n"
"\n";


static struct cw_clicmd show_trans =
{
    .cmda = { "show", "translation", NULL },
    .handler = show_translation,
    .generator = show_translation_generator,
    .summary = "Display translation matrix",
    .usage = show_trans_usage,
};

int cw_translator_best_choice(int *dst, int *srcs)
{
    /* Calculate our best source format, given costs, and a desired destination */
	struct trans_state *tr;
    int x;
    int y;
    int best = -1;
    int bestdst = 0;
    int cur = 1;
    int besttime = INT_MAX;
    int beststeps = INT_MAX;
    int common;

    if ((common = (*dst) & (*srcs)))
    {
        /* We have a format in common */
        for (y = 0;  y < MAX_FORMAT;  y++)
        {
            if (cur & common)
            {
                /* This is a common format to both.  Pick it if we don't have one already */
                besttime = 0;
                bestdst = cur;
                best = cur;
            }
            cur <<= 1;
        }
    }
    else
    {
        /* We will need to translate */
        pthread_spin_lock(&state_lock);
        tr = cw_object_dup(trans_state);
        pthread_spin_unlock(&state_lock);

        for (y = 0;  y < MAX_FORMAT;  y++)
        {
            if (cur & *dst)
            {
                for (x = 0;  x < MAX_FORMAT;  x++)
                {
                    if ((*srcs & (1 << x))          /* x is a valid source format */
                        &&
                        tr->matrix[x][y].step        /* There's a step */
                        &&
                        (tr->matrix[x][y].cost < besttime
			 || (tr->matrix[x][y].cost == besttime && tr->matrix[x][y].steps < beststeps)))
                    {
                        /* It's better than what we have so far */
                        best = 1 << x;
                        bestdst = cur;
                        besttime = tr->matrix[x][y].cost;
                    }
                }
            }
            cur <<= 1;
        }
        cw_object_put(tr);
    }
    if (best > -1)
    {
        *srcs = best;
        *dst = bestdst;
        best = 0;
    }
    return best;
}


static int cw_translator_qsort_compare_by_name(const void *a, const void *b)
{
	const struct cw_object * const *objp_a = a;
	const struct cw_object * const *objp_b = b;
	const struct cw_translator *translator_a = container_of(*objp_a, struct cw_translator, obj);
	const struct cw_translator *translator_b = container_of(*objp_b, struct cw_translator, obj);

	return strcmp(translator_a->name, translator_b->name);
}

static void translator_registry_onchange(void)
{
	if (trans_state)
		rebuild_matrix();
}

struct cw_registry translator_registry = {
	.name = "Translator",
	.qsort_compare = cw_translator_qsort_compare_by_name,
	.onchange = translator_registry_onchange,
};


int cw_translator_init(void)
{
	pthread_spin_init(&state_lock, PTHREAD_PROCESS_PRIVATE);
	rebuild_matrix();
	cw_cli_register(&show_trans);
	return 0;
}
