/*	$Id$ */
/*
 * Copyright (c) 2016--2017 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/ioctl.h>
#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include <expat.h>

#include "parser.h"

int verbose = 0;

/*
 * UTF-8 output mode.
 * This makes nice boxes and uses pretty characters.
 */
static int utf8 = 0;

/*
 * Aggregate dives.
 * Assume that this is a sequence (say of free-dives) where we should
 * graph multiple entries alongside each other.
 */
static int aggr = 0;

enum	grapht {
	GRAPH_TEMP,
	GRAPH_DEPTH
};

/*
 * Used for computing averages.
 */
struct	avg {
	double		 accum;
	size_t		 sz;
};

/*
 * Drawing area size.
 * All values are absolute to the total screen size.
 */
struct	win {
	size_t		 rows; /* vertical space */
	size_t		 cols; /* horizontal space */
	size_t		 top; /* top offset */
	size_t		 left; /* left offset */
};

/*
 * A single graph type.
 */
struct	graph {
	size_t		*labels;
	size_t		*cols;
	size_t		 nsamps;
	double		 maxvalue;
	double		 minvalue;
};

static	const char *modes[] = {
	"Unknown-mode", /* MODE_NONE */
	"Free", /* MODE_FREEDIVE */
	"Gauge", /* MODE_GAUGE */
	"Open-circuit", /* MODE_OC */
	"Closed-circuit" /* MODE_CC */
};

/*
 * Print a set of averages.
 * Accepts the averages, "avgs", and the window in which to print,
 * "iwin".
 * The "min" and "max" are the total range of all points, and "mint" and
 * "maxt" is the x-axis extrema.
 * The "lbuf" tells us the left-hand buffer (for formatting axes.)
 * Finally, "dir" tells us whether the direction to show the y-axis
 * values, upward (1) or downward (0).
 */
static void
print_avgs(const struct avg *const *avgs, size_t avgsz,
	const struct win *iwin, double min, double max, 
	time_t mint, time_t maxt, size_t lbuf, int dir)
{
	size_t		 i, x, y, t, ytics, xtics;
	double		 v;

	/* 
	 * Make our y-axis and y-axis labels. 
	 * Start drawing the borders.
	 * Then draw the labels and tics.
	 */

	for (y = 0; y < iwin->rows; y++)
		if (utf8)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1, L'\x2502');
		else
			printf("\033[%zu;%zuH|", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1);

	if (utf8)
		printf("\033[%zu;%zuH%lc", 
			iwin->top + y + 1, 
			iwin->left - 1 + 1, L'\x2514');
	else
		printf("\033[%zu;%zuH\\", 
			iwin->top + y + 1, 
			iwin->left - 1 + 1);

	if (iwin->rows > 50)
		ytics = (iwin->rows - 1) / 8;
	else
		ytics = (iwin->rows - 1) / 4;

	for (y = 0; y < iwin->rows; y += ytics) {
		v = dir ?
			max - (max - min) * (double)y / iwin->rows :
			min + (max - min) * (double)y / iwin->rows;
		if (utf8)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1, L'\x251c');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + y + 1, 
				iwin->left - 1 + 1);
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + y + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1, v);
	}

	/* Make sure we have a value at the maximum. */

	if ((iwin->rows > 50 && 0 != (iwin->rows - 1) % 8) ||
	    0 != (iwin->rows - 1) % 4) {
		v = dir ?
			max - (max - min) * 
				(double)(iwin->rows - 1) / iwin->rows :
			min + (max - min) * 
				(double)(iwin->rows - 1) / iwin->rows;
        	printf("\033[%zu;%zuH%*.1f", 
			iwin->top + (iwin->rows - 1) + 1, 
			iwin->left - lbuf + 1, (int)lbuf - 1,
			v);
		if (utf8)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + (iwin->rows - 1) + 1, 
				iwin->left - 1 + 1, L'\x251c');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + (iwin->rows - 1) + 1, 
				iwin->left - 1 + 1);
	}

	/* 
	 * Now make the x-axis and x-axis label.
	 * Again, start with the border.
	 * Then the tics.
	 */

	for (x = 0; x < iwin->cols; x++)
		if (utf8)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + iwin->rows + 1, 
				iwin->left + x + 1, L'\x2500');
		else
			printf("\033[%zu;%zuH-", 
				iwin->top + iwin->rows + 1, 
				iwin->left + x + 1);

	if (iwin->cols > 100)
		xtics = (iwin->cols - 6) / 8;
	else
		xtics = (iwin->cols - 6) / 4;

	for (x = 0; x < iwin->cols; x += xtics) {
		t = (maxt - mint) * 
			((double)x / iwin->cols);
		if ( ! aggr)
			t += mint;
		if (utf8)
			printf("\033[%zu;%zuH%lc", 
				iwin->top + iwin->rows + 1, 
				iwin->left + x + 1, L'\x253c');
		else
			printf("\033[%zu;%zuH|", 
				iwin->top + iwin->rows + 1, 
				iwin->left + x + 1);
        	printf("\033[%zu;%zuH%03zu:%02zu", 
			iwin->top + iwin->rows + 2, 
			iwin->left + x + 1,
			t / 60, t % 60);
	}

	/* Draw graph values, if applicable. */

	for (i = 0; i < avgsz; i++)
		for (x = 0; x < iwin->cols; x++) {
			/* No values to sample? */

			if (0 == avgs[i][x].sz)
				continue;

			v = avgs[i][x].accum / (double)avgs[i][x].sz;
			y = dir ?
				(iwin->rows - 1) * 
				((max - v) / (max - min)) :
				(iwin->rows - 1) * 
				((v - min) / (max - min));

			if (utf8)
				printf("\033[1;3%zum\033[%zu;%zuH%lc", 
					(i % 7) + 1,
					iwin->top + y + 1, 
					iwin->left + x + 1, L'\x2022');
			else
				printf("\033[1;3%zum\033[%zu;%zuH+", 
					(i % 7) + 1,
					iwin->top + y + 1, 
					iwin->left + x + 1);
		}

	/* Reset terminal attributes. */

        printf("\033[0m");
}

/*
 * Sort values into the column-width bins.
 * Returns the bins allocated of size "cols".
 */
static struct avg *
collect(const struct dive *d, size_t cols, 
	time_t mint, time_t maxt, enum grapht type)
{
	struct samp	*samp;
	double		 frac;
	size_t		 idx;
	time_t		 t;
	struct avg	*avg = NULL;

	if (NULL == (avg = calloc(cols, sizeof(struct avg))))
		err(EXIT_FAILURE, NULL);

	TAILQ_FOREACH(samp, &d->samps, entries) {
		if (GRAPH_DEPTH == type &&
		    ! (SAMP_DEPTH & samp->flags))
			continue;
		if (GRAPH_TEMP == type &&
		    ! (SAMP_TEMP & samp->flags))
			continue;
		
		/*
		 * If we're aggregating, the bucket is going to be
		 * as a fraction of the total time (mint, maxt).
		 * Otherwise it's just between 0 and maxt.
		 */

		t = aggr ? (d->datetime + samp->time) - mint : 
			samp->time;
		frac = (double)t / (maxt - mint);
		idx = floor(frac * (cols - 1));
		assert(idx < cols);

		if (GRAPH_DEPTH == type)
			avg[idx].accum += samp->depth;
		else if (GRAPH_TEMP == type)
			avg[idx].accum += samp->temp;
		avg[idx].sz++;
	}

	return(avg);
}

/*
 * Print all graphs to the screen.
 * This first checks to see which graphs it should produce (trying for
 * all of them, if the data's there), preprocesses the data, then puts
 * everything onto the screen.
 */
static int
print_all(const struct diveq *dq, const struct winsize *ws)
{
	struct dive	 *d;
	struct graph	  temp, depth;
	struct samp	 *samp;
	int		  c, dtemp = 0, ddepth = 0;
	struct win	  win, iwin;
	size_t		  i, lbuf, titlesz, avgsz;
	struct avg	**avg;
	char		 *title;
	char		  buf[128];
	time_t		  mint = 0, maxt = 0, t;

	memset(&temp, 0, sizeof(struct graph));
	memset(&depth, 0, sizeof(struct graph));

	depth.minvalue = temp.minvalue = DBL_MAX;
	depth.maxvalue = temp.maxvalue = -DBL_MAX;

	assert( ! TAILQ_EMPTY(dq));

	/*
	 * Determine the boundaries for the depth and temperature.
	 * While here, also compute the number of samples for both of
	 * these measurements.
	 */

	avgsz = 0;
	TAILQ_FOREACH(d, dq, entries) {
		avgsz++;
		TAILQ_FOREACH(samp, &d->samps, entries) {
			if (SAMP_DEPTH & samp->flags) {
				depth.nsamps++;
				if (samp->depth > depth.maxvalue)
					depth.maxvalue = samp->depth;
				if (samp->depth < depth.minvalue)
					depth.minvalue = samp->depth;
			}
			if (SAMP_TEMP & samp->flags) {
				temp.nsamps++;
				if (samp->temp > temp.maxvalue)
					temp.maxvalue = samp->temp;
				if (samp->temp < temp.minvalue)
					temp.minvalue = samp->temp;
			}
		}
	}

	/*
	 * Next, accumulate time boundaries.
	 * For non-aggregate measurement, we're going to just look for
	 * maximum duration.
	 * For aggregate measurement, we compute the maximum by
	 * offsetting from the minimum value.
	 * We only allow dives with dates and times for aggregates.
	 */

	if (aggr) {
		TAILQ_FOREACH(d, dq, entries) {
			if (0 == d->datetime) {
				warnx("date and time required");
				return(0);
			}
			if (0 == mint || d->datetime < mint)
				mint = d->datetime;
		}
		TAILQ_FOREACH(d, dq, entries)
			TAILQ_FOREACH(samp, &d->samps, entries)
				if (SAMP_TEMP & samp->flags ||
				    SAMP_DEPTH & samp->flags) {
					t = d->datetime + samp->time;
					if (t > maxt)
						maxt = t;
				}
	} else
		TAILQ_FOREACH(d, dq, entries) 
			TAILQ_FOREACH(samp, &d->samps, entries)
				if (SAMP_DEPTH & samp->flags ||
				    SAMP_TEMP & samp->flags) {
					t = samp->time;
					if (t > maxt)
						maxt = t;
				}

	/*
	 * Establish whether we should do any graphing at all for the
	 * temperature or depth.
	 * Requirements are (1) at least 2 samples and (2) a non-zero
	 * difference between maximum and minimum.
	 */

	if (temp.nsamps >= 2 && 
	    fabs(temp.maxvalue - temp.minvalue) > FLT_EPSILON)
		dtemp = 1;
	if (depth.nsamps >= 2 && 
	    fabs(depth.maxvalue - depth.minvalue) > FLT_EPSILON)
		ddepth = 1;

	if (0 == dtemp && 0 == ddepth) {
		warnx("nothing to graph");
		return(0);
	}

	if (NULL == (avg = calloc(avgsz, sizeof(struct avg *))))
		err(EXIT_FAILURE, NULL);

	/*
	 * Begin by clearing our workspace.
	 */

	printf("\e[1;1H\e[2J");

	/* 
	 * Include a margin: 4 vertical (one at the top, two on the
	 * bottom, one being for the next prompt) and 2 horizontal (one
	 * on either side).
	 */

	win.rows = ws->ws_row - 4;
	win.cols = ws->ws_col - 2;
	win.top = 1;
	win.left = 1;

	i = 0;
	TAILQ_FOREACH(d, dq, entries) {
		if (d->datetime) {
			ctime_r(&d->datetime, buf);
			buf[strlen(buf) - 1] = '\0';
		}
		c = 0 != d->datetime ?
			asprintf(&title, "%s Dive #%zu on %s",
				modes[d->mode], d->num, buf) :
			asprintf(&title, "%s Dive #%zu",
				modes[d->mode], d->num);
		if (c < 0)
			err(EXIT_FAILURE, NULL);

		/* Title: centre, bold.  Room for legend. */

		titlesz = strlen(title) + 2;
		if (titlesz >= win.cols) {
			title[win.cols - 4] = '\0';
			titlesz = strlen(title) + 2;
		}

		if (utf8)
			printf("\033[%zu;%zuH\033[1m%s \033[3%zum%lc\033[0m", 
				win.top + i + 1, 
				win.left + ((win.cols - titlesz) / 2) + 1,
				title, i + 1, L'\x2022');
		else
			printf("\033[%zu;%zuH\033[1m%s \033[3%zum+\033[0m", 
				win.top + i + 1, 
				win.left + ((win.cols - titlesz) / 2) + 1,
				title, i + 1);

		i++;
	}

	lbuf = (dtemp && temp.maxvalue >= 100.0) || 
	       (ddepth && depth.maxvalue >= 100.0) ? 6 : 5;

	if (dtemp) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		iwin.top += avgsz;
		if (ddepth)
			iwin.rows = (win.rows / 2) - 3;
		else
			iwin.rows -= (avgsz + 1);

		i = 0;
		TAILQ_FOREACH(d, dq, entries) 
			avg[i++] = collect(d, iwin.cols, 
				mint, maxt, GRAPH_TEMP);

		print_avgs((const struct avg *const *)avg, avgsz, 
			&iwin, temp.minvalue, temp.maxvalue, 
			mint, maxt, lbuf, 1);
		for (i = 0; i < avgsz; i++)
			free(avg[i]);
	}

	if (ddepth) {
		iwin = win;
		iwin.cols -= lbuf;
		iwin.left += lbuf;
		if (dtemp) {
			iwin.top = (win.rows / 2) + avgsz + 1;
			iwin.rows = (win.rows / 2) - (avgsz + 1);
		} else {
			iwin.rows -= (avgsz + 1);
			iwin.top += avgsz;
		}

		i = 0;
		TAILQ_FOREACH(d, dq, entries)
			avg[i++] = collect(d, iwin.cols, 
				mint, maxt, GRAPH_DEPTH);
		print_avgs((const struct avg *const *)avg, avgsz, 
			&iwin, depth.minvalue, depth.maxvalue, 
			mint, maxt, lbuf, 0);
		for (i = 0; i < avgsz; i++)
			free(avg[i]);
	}

	/* Put as at the bottom of the screen. */

        printf("\033[%d;%dH", ws->ws_row, 0);

	free(avg);
	free(title);
	return(1);
}

int
main(int argc, char *argv[])
{
	int		 c, rc = 0;
	size_t		 i;
	XML_Parser	 p;
	struct diveq	 dq;
	struct divestat	 st;
	struct winsize	 ws;

	setlocale(LC_ALL, "");

	/* Pledge us early: only reading files. */

#if HAVE_PLEDGE
	if (-1 == pledge("stdio rpath", NULL))
		err(EXIT_FAILURE, "pledge");
#endif
	while (-1 != (c = getopt(argc, argv, "auv")))
		switch (c) {
		case ('a'):
			aggr = 1;
			break;
		case ('u'):
			utf8 = 1;
			break;
		case ('v'):
			verbose = 1;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	parse_init(&p, &dq, &st, GROUP_NONE);

	/* 
	 * Handle all files or stdin.
	 * File "-" is interpreted as stdin.
	 */

	if (0 == argc)
		rc = parse("-", p, &dq, &st);

	for (i = 0; i < (size_t)argc; i++)
		if ( ! (rc = parse(argv[i], p, &dq, &st)))
			break;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio", NULL))
		err(EXIT_FAILURE, "pledge");
#endif

	XML_ParserFree(p);

	if ( ! rc)
		goto out;
	if (TAILQ_EMPTY(&dq)) {
		warnx("no dives to display");
		goto out;
	}

	/*
	 * Initialise screen real estate.
	 * By default, pretend we're a 80x25, which will also be our
	 * minimum size.
	 * Otherwise, use the terminal's reported dimensions.
	 */

	if (-1 == ioctl(0, TIOCGWINSZ, &ws)) {
		warnx("TIOCGWINSZ");
		ws.ws_row = 25;
		ws.ws_col = 80;
	}

	if (ws.ws_row < 25)
		ws.ws_row = 25;
	if (ws.ws_col < 80)
		ws.ws_col = 80;

	if ( ! TAILQ_EMPTY(&dq))
		rc = print_all(&dq, &ws);

	/* Free all memory from the dives. */
out:
	parse_free(&dq, &st);
	return(rc ? EXIT_SUCCESS : EXIT_FAILURE);
usage:
	fprintf(stderr, "usage: %s [-auv] [file]\n", getprogname());
	return(EXIT_FAILURE);
}
