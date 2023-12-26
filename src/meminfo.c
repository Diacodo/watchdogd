/* Memory leak monitor
 *
 * Copyright (C) 2015-2023  Joachim Wiberg <troglobit@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include "wdt.h"
#include "script.h"

#define PROC_FILE "/proc/meminfo"

static int logmark = 0;
static char *exec = NULL;
static uev_t watcher;

/* Default: disabled -- recommended 0.9, 0.95 */
static double warning  = 0.0;
static double critical = 0.0;

#define MEMTOTAL   0
#define MEMFREE    1
#define MEMCACHED  2
#define SWAPTOTAL  3
#define SWAPFREE   4
#define SWAPCACHED 5

typedef struct {
	char     *str;
	uint32_t  val;
} meminfo_t;

/*
 * Check available memory, only warn/reboot if no swap (left).  On an
 * embedded system there may not be a swap, so memory leaks can be quite
 * nasty.  On a "regular" system you do not want to run out of swap.
 */
static void cb(uev_t *w, void *arg, int events)
{
	char buf[80];
	FILE *fp;
	meminfo_t meminfo[] = {
		{ "MemTotal:",   0 },
		{ "MemFree:",    0 },
		{ "Cached:",     0 },
		{ "SwapCached:", 0 },
		{ "SwapTotal:",  0 },
		{ "SwapFree:",   0 },
		{ NULL, 0 }
	};

	fp = fopen(PROC_FILE, "r");
	if (!fp) {
		DEBUG("Cannot read %s, maybe /proc is not mounted yet", PROC_FILE);
		return;
	}

	/*
	 * $ cat /proc/meminfo
	 * MemTotal:        8120568 kB
	 * MemFree:         2298932 kB
	 * Cached:          1907240 kB
	 * SwapCached:            0 kB
	 * SwapTotal:      15859708 kB
	 * SwapFree:       15859708 kB
	 */
	while (fgets(buf, sizeof(buf), fp)) {
		int i;

		for (i = 0; meminfo[i].str; i++) {
			size_t len = strlen(meminfo[i].str);

			if (!strncmp(buf, meminfo[i].str, len)) {
				char *ptr = buf + len + 1;
				uint32_t val;

				while (isspace(*ptr))
					ptr++;

				if (sscanf(ptr, "%u kB", &val) == 1)
					meminfo[i].val = val;
			}
		}
	}
	fclose(fp);

//	LOG("Total RAM: %u kB, free: %u kB, cached: %u kB, Total Swap: %u kB, free: %u kB, cached: %u kB",
//	    meminfo[MEMTOTAL].val, meminfo[MEMFREE].val, meminfo[MEMCACHED].val,
//	    meminfo[SWAPTOTAL].val, meminfo[SWAPFREE].val, meminfo[SWAPCACHED].val);
	if (logmark)
		LOG("Memory usage: %u kB, cached: %u kB, total: %u kB",
		    meminfo[MEMFREE].val, meminfo[MEMCACHED].val, meminfo[MEMTOTAL].val);

	/* Enable trigger warnings by default only on systems without swap */
	if (meminfo[SWAPTOTAL].val == 0) {
		double level;
		uint32_t free = meminfo[MEMFREE].val + meminfo[MEMCACHED].val;
		uint32_t used = meminfo[MEMTOTAL].val - free;

		level = (double)used / (double)meminfo[MEMTOTAL].val;
		DEBUG("RAM usage level %.0f%%, warning: %.0f%%, critical: %.0f%%",
		      level * 100, warning * 100, critical * 100);

		if (level > warning) {
			if (critical > 0.0 && level > critical) {
				EMERG("Memory usage too high, %.2f > %0.2f, rebooting system ...", level, critical);
				if (checker_exec(exec, "meminfo", 1, level, warning, critical))
					wdt_forced_reset(w->ctx, getpid(), PACKAGE ":meminfo", 0);
				return;
			}

			WARN("Memory use very high, %.2f > %0.2f, possible leak!", level, warning);
			checker_exec(exec, "meminfo", 0, level, warning, critical);
		}
	}
}

int meminfo_init(uev_ctx_t *ctx, const char *name, int T, int mark,
		 float warn, float crit, char *script)
{
	(void)name;

	if (!T) {
		INFO("Memory leak monitor disabled.");
		return uev_timer_stop(&watcher);
	}

	INFO("Memory leak monitor, period %d sec, warning: %.2f%%, reboot: %.2f%%",
	     T, warning * 100, critical * 100);
	logmark = mark;
	warning = warn;
	critical = crit;
	if (script) {
		if (exec)
			free(exec);
		exec = strdup(script);
	}

	uev_timer_stop(&watcher);
	return uev_timer_init(ctx, &watcher, cb, NULL, 1000, T * 1000);
}

/**
 * Local Variables:
 *  c-file-style: "linux"
 *  indent-tabs-mode: t
 * End:
 */
