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
 *
 * \brief Translate via the use of pseudo channels
 * 
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

#define MAX_RECALC 200 /* max sample recalc */

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
	int min_cost;
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
	int secs;
	unsigned int *cost;
};


static void *calc_cost(void *data)
{
	struct rebuild_matrix_args *args = data;
	struct cw_translator *t = args->t;
	struct cw_translator_pvt *pvt;
	struct cw_frame *f;
	struct cw_frame *out;
	struct timespec start, end;
	int samples = args->secs * t->dst_rate;
	int sofar;

	/* If we can't score it it's maximally expensive */
	*args->cost = INT_MAX;

	if (t->sample == NULL) {
		cw_log(CW_LOG_WARNING, "Translator '%s' does not produce sample frames.\n", t->name);
		return NULL;
	}

	if ((pvt = t->newpvt()) == NULL) {
		cw_log(CW_LOG_WARNING, "Translator '%s' appears to be broken and will probably fail.\n", t->name);
		return NULL;
	}

	if (!(f = t->sample())) {
		cw_log(CW_LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
		goto out;
	}

	/* Untimed first pass to make sure the cache is in a consistent (warm) state */
	if (t->framein(pvt, f) < 0) {
		cw_log(CW_LOG_ERROR, "Translator '%s' can't translate its own sample frame!\n", t->name);
		goto out;
	}
	cw_fr_free(f);
	while ((out = t->frameout(pvt))) {
		sofar += out->samples;
		cw_fr_free(out);
	}

	cw_clock_gettime(global_clock_monotonic, &start);

	/* Call the encoder until we've processed the required number of samples */
	for (sofar = 0;  sofar < samples;) {
		if ((f = t->sample()) == NULL) {
			cw_log(CW_LOG_WARNING, "Translator '%s' failed to produce a sample frame.\n", t->name);
			goto out;
		}
		t->framein(pvt, f);
		cw_fr_free(f);
		while ((out = t->frameout(pvt))) {
			sofar += out->samples;
			cw_fr_free(out);
		}
	}

	cw_clock_gettime(global_clock_monotonic, &end);

	*args->cost = (end.tv_sec - start.tv_sec) * 1000000000L + (end.tv_nsec - start.tv_nsec);
	if (*args->cost < args->tr->min_cost)
		args->tr->min_cost = *args->cost;

out:
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

	args->t = t;
	args->cost = &td->cost;

	if (!(ret = cw_pthread_create(&tid, &global_attr_fifo, calc_cost, args))) {
		pthread_join(tid, NULL);
		td->step = cw_object_dup(t);
		td->steps = 1;
	} else
		cw_log(CW_LOG_ERROR, "calc_cost thread: %d %s\n", ret, strerror(ret));

	return 0;
}

static int rebuild_matrix_norm(struct cw_object *obj, void *data)
{
	struct cw_translator *t = container_of(obj, struct cw_translator, obj);
	struct trans_state *tr = data;
	struct cw_translator_dir *td = &tr->matrix[bottom_bit(t->src_format)][bottom_bit(t->dst_format)];

	td->cost /= tr->min_cost;
	return 0;
}


static void rebuild_matrix(int secs)
{
	struct rebuild_matrix_args args;
	struct trans_state *old_tr, *new_tr;
	int changed, x, y, z;

	if (option_debug)
		cw_log(CW_LOG_DEBUG, "Reseting translation matrix\n");

	if (!(new_tr = malloc(sizeof(*new_tr)))) {
		cw_log(CW_LOG_ERROR, "Out of memory!\n");
		return;
	}

	cw_object_init(new_tr, NULL, 1);
	args.tr = new_tr;

	if (global_clock_monotonic_res.tv_nsec >= 1000000L)
		x = 10;
	else if (global_clock_monotonic_res.tv_nsec >= 100000L)
		x = 100;
	else
		x = 1000;

	args.secs = (secs ? secs : 1);
	for (;;) {
		memset(&new_tr->matrix, '\0', sizeof(new_tr->matrix));
		new_tr->min_cost = INT_MAX;
		cw_registry_iterate(&translator_registry, rebuild_matrix_one, &args);
		if (secs || new_tr->min_cost >= x * global_clock_monotonic_res.tv_nsec)
			break;
		args.secs *= (256 * ((x + 1) * global_clock_monotonic_res.tv_nsec - 1) / new_tr->min_cost);
		args.secs >>= 8;
	}

	if (new_tr->min_cost < INT_MAX) {
		new_tr->min_cost /= 10; /* We're using 1 fixed point decimal place... */
		cw_registry_iterate(&translator_registry, rebuild_matrix_norm, new_tr);
	}

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
								   is the sum of the transition from x to y and
								   from y to z */
								new_tr->matrix[x][z].step = cw_object_dup(new_tr->matrix[x][y].step);
								new_tr->matrix[x][z].cost = new_tr->matrix[x][y].cost + new_tr->matrix[y][z].cost;
								new_tr->matrix[x][z].steps = new_tr->matrix[x][y].steps + new_tr->matrix[y][z].steps;
								if (option_debug)
									cw_log(CW_LOG_DEBUG, "Discovered path from %s to %s, via %s with %d steps and cost %d\n", cw_getformatname(1 << x), cw_getformatname(1 << z), cw_getformatname(1 << y), new_tr->matrix[x][z].steps, new_tr->matrix[x][z].cost);
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

static int show_translation(int fd, int argc, char *argv[])
{
	struct trans_state *tr;
#define SHOW_TRANS 11
	int x, y, z;

	if (argc > 4)
		return RESULT_SHOWUSAGE;

	if (argv[2]  &&  !strcasecmp(argv[2], "recalc")) {
		z = argv[3] ? atoi(argv[3]) : 1;

		if (z < 0)
			z = 0;
		else if (z > MAX_RECALC) {
			cw_cli(fd,"         Maximum limit of recalc exceeded by %d, truncating value to %d\n", z - MAX_RECALC,MAX_RECALC);
			z = MAX_RECALC;
		}
		rebuild_matrix(z);
	}

	cw_cli(fd, "         Relative translation times between formats\n");
	cw_cli(fd, "         Source Format (Rows) Destination Format(Columns)\n\n");

	cw_cli(fd, "         ");
	for (x = 0;  x < SHOW_TRANS;  x++)
		cw_cli(fd, " %8s", cw_getformatname(1 << x));
	cw_cli(fd, "\n");

	pthread_spin_lock(&state_lock);
	tr = cw_object_dup(trans_state);
	pthread_spin_unlock(&state_lock);

	for (x = 0;  x < SHOW_TRANS;  x++) {
		cw_cli(fd, " %8s", cw_getformatname(1 << x));

		for (y = 0;  y < SHOW_TRANS;  y++) {
			if (tr->matrix[x][y].step)
				cw_cli(fd, " %6u.%01u", tr->matrix[x][y].cost / 10, tr->matrix[x][y].cost % 10);
			else
			    cw_cli(fd, "        -");
		}
		cw_cli(fd, "\n");
	}

	cw_object_put(tr);
	return RESULT_SUCCESS;
}

static char show_trans_usage[] =
"Usage: show translation [recalc] [<recalc seconds>]\n"
"       Displays known codec translators and the cost associated\n"
"with each conversion.  if the argument 'recalc' is supplied along\n"
"with optional number of seconds to test a new test will be performed\n"
"as the chart is being displayed.\n";

static struct cw_clicmd show_trans =
{
    .cmda = { "show", "translation", NULL },
    .handler = show_translation,
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
		rebuild_matrix(0);
}

struct cw_registry translator_registry = {
	.name = "Translator",
	.qsort_compare = cw_translator_qsort_compare_by_name,
	.onchange = translator_registry_onchange,
};


int cw_translator_init(void)
{
	pthread_spin_init(&state_lock, PTHREAD_PROCESS_PRIVATE);
	rebuild_matrix(0);
	cw_cli_register(&show_trans);
	return 0;
}
