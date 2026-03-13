// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Lenovo. All rights reserved.
 */

#include <linux/version.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <trace/events/sched.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/rwsem.h>
#include <linux/spinlock.h>
#include "lenovo_hypersched.h"
#include "lenovo_hyperbandwidth.h"

#define HYPER_BORROW_MIN_RUNTIME 12000000UL

struct lenovo_ux_bandwidth def_ux_bandwidth;

static int do_sched_ux_period_timer(struct lenovo_ux_bandwidth *ux_b,
				    int overrun)
{
	int i, idle = 1, throttled = 0;

	for_each_online_cpu(i) {
		struct rq *rq = cpu_rq(i);
		struct lenovo_rq *lrq = (struct lenovo_rq *)rq->android_oem_data1;
		struct rq_flags rf;
		int skip;
		u64 runtime = RUNTIME_INF;

		if (!lrq)
			continue;
		raw_spin_lock(&lrq->hyper_ux_runtime_lock);
		if (lrq->hyper_ux_runtime != RUNTIME_INF) {
			runtime = lrq->hyper_ux_runtime;
			lrq->hyper_ux_runtime = ux_b->hyper_ux_runtime;
		}
		skip = !lrq->hyper_ux_time;
		raw_spin_unlock(&lrq->hyper_ux_runtime_lock);
		if (skip)
			continue;

		rq_lock(rq, &rf);
		update_rq_clock(rq);
		if (lrq->hyper_ux_time) {
			raw_spin_lock(&lrq->hyper_ux_runtime_lock);
			lrq->hyper_ux_time -= min(lrq->hyper_ux_time, overrun * runtime);
			if (lrq->hyper_ux_throttled && lrq->hyper_ux_time < runtime) {
				lrq->hyper_ux_throttled = 0;

			}
			if (lrq->hyper_ux_time || lrq->num_hyper_ux_tasks)
				idle = 0;
			raw_spin_unlock(&lrq->hyper_ux_runtime_lock);
		} else if (lrq->num_hyper_ux_tasks) {
			idle = 0;
		}
		if (lrq->hyper_ux_throttled)
			throttled = 1;

		rq_unlock(rq, &rf);
	}

	if (!throttled
	    && (ux_b->hyper_ux_runtime < 0
		|| ux_b->hyper_ux_runtime == RUNTIME_INF))
		return 1;

	return idle;
}

static enum hrtimer_restart sched_hyper_ux_period_timer(struct hrtimer *timer)
{
	struct lenovo_ux_bandwidth *ux_b =
	    container_of(timer, struct lenovo_ux_bandwidth,
			 hyper_ux_period_timer);
	int idle = 0;
	int overrun;
	raw_spin_lock(&ux_b->hyper_ux_runtime_lock);
	for (;;) {
		overrun = hrtimer_forward_now(timer, ux_b->hyper_ux_period);
		if (!overrun)
			break;

		raw_spin_unlock(&ux_b->hyper_ux_runtime_lock);
		idle = do_sched_ux_period_timer(ux_b, overrun);
		raw_spin_lock(&ux_b->hyper_ux_runtime_lock);
	}
	if (idle)
		ux_b->hyper_ux_period_active = 0;
	raw_spin_unlock(&ux_b->hyper_ux_runtime_lock);

	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}

void init_lenovo_rq_bandwidth(struct lenovo_ux_bandwidth *ux_b, u64 period,
			      u64 runtime)
{
	ux_b->hyper_ux_period = ns_to_ktime(period);
	ux_b->hyper_ux_runtime = runtime;
	ux_b->hyper_ux_period_active = 0;
	ux_b->hyper_ux_max_runtime = period - div_u64(period, 5); // period 80%

	raw_spin_lock_init(&ux_b->hyper_ux_runtime_lock);

	hrtimer_init(&ux_b->hyper_ux_period_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL_HARD);
	ux_b->hyper_ux_period_timer.function = sched_hyper_ux_period_timer;
}

static void start_lenovo_ux_bandwidth(struct lenovo_ux_bandwidth *ux_b)
{
	if (ux_b->hyper_ux_runtime < 0 || ux_b->hyper_ux_runtime == RUNTIME_INF)
		return;

	raw_spin_lock(&ux_b->hyper_ux_runtime_lock);
	if (!ux_b->hyper_ux_period_active) {
		ux_b->hyper_ux_period_active = 1;

		hrtimer_forward_now(&ux_b->hyper_ux_period_timer,
				    ux_b->hyper_ux_period);
		hrtimer_start_expires(&ux_b->hyper_ux_period_timer,
				      HRTIMER_MODE_ABS_PINNED_HARD);
	}
	raw_spin_unlock(&ux_b->hyper_ux_runtime_lock);
}

/* This function needs to be called while holding the hyper_ux_runtime_lock */
static void sched_hyper_ux_balance_runtime(struct lenovo_rq *lrq)
{
	if (lrq->hyper_ux_time > lrq->hyper_ux_runtime &&
		lrq->hyper_ux_runtime < def_ux_bandwidth.hyper_ux_max_runtime) {
		int i;
		struct lenovo_rq *lrq_max = lrq;
		s64 max_diff = 0;

		raw_spin_unlock(&lrq->hyper_ux_runtime_lock);
		for_each_online_cpu(i) {
			struct rq *rq = cpu_rq(i);
			struct lenovo_rq *iter = (struct lenovo_rq *)rq->android_oem_data1;
			s64 diff;

			if (iter == lrq)
				continue;
			raw_spin_lock(&iter->hyper_ux_runtime_lock);

			if (iter->hyper_ux_runtime == RUNTIME_INF)
				goto next;

			diff = iter->hyper_ux_runtime - iter->hyper_ux_time;
			if (diff > max_diff) {
				lrq_max = iter;
				max_diff = diff;
			}
next:
			raw_spin_unlock(&iter->hyper_ux_runtime_lock);
		}
		raw_spin_lock(&lrq->hyper_ux_runtime_lock);

		if (lrq_max != lrq && max_diff > HYPER_BORROW_MIN_RUNTIME) {
			u64 surplus_runtime = max_diff >> 1;
			u64 max_runtime = def_ux_bandwidth.hyper_ux_max_runtime;

			raw_spin_lock(&lrq_max->hyper_ux_runtime_lock);
			if (lrq->hyper_ux_runtime + surplus_runtime > max_runtime)
				surplus_runtime = max_runtime - lrq->hyper_ux_runtime;
			if (lrq->hyper_ux_runtime + surplus_runtime > lrq->hyper_ux_time) {
				lrq_max->hyper_ux_runtime -= surplus_runtime;
				lrq->hyper_ux_runtime += surplus_runtime;
			}
			raw_spin_unlock(&lrq_max->hyper_ux_runtime_lock);
		}
	}
}

static int sched_hyper_ux_runtime_exceeded(struct lenovo_rq *lrq)
{
	u64 runtime = def_ux_bandwidth.hyper_ux_runtime;

	if (lrq->hyper_ux_throttled)
		return lrq->hyper_ux_throttled;

	if (runtime >= ktime_to_ns(def_ux_bandwidth.hyper_ux_period))
		return 0;

	sched_hyper_ux_balance_runtime(lrq);
	runtime = lrq->hyper_ux_runtime;
	if (runtime == RUNTIME_INF)
		return 0;

	if (lrq->hyper_ux_time > runtime) {
		if (likely(def_ux_bandwidth.hyper_ux_runtime)) {
			lrq->hyper_ux_throttled = 1;
			//printk_deferred_once("sched: hyper ux throttling activated\n");
			//printk("%s: hyper ux throttling activated lrq->hyper_ux_time = %llu\n", __func__, lrq->hyper_ux_time);
		} else {
			lrq->hyper_ux_time = 0;
		}

		if (lrq->hyper_ux_throttled)
			return 1;
	}

	return 0;
}

void sched_hyper_stat_runtime_handle(struct task_struct *task, u64 delta)
{
	int exceeded;
	struct rq *rq = task_rq(task);
	struct lenovo_rq *lrq = NULL;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);
	lrq = (struct lenovo_rq *)rq->android_oem_data1;

	if (!rq || !lrq || !task || !ltsk || !ltsk->lsa)
		return;
	if ((!ltsk->lsa->static_hyper_ux
	     && !atomic64_read(&ltsk->lsa->dynamic_hyper_ux))
	    || list_empty(&lrq->hyper_ux_thread_list)) {
		return;
	}

	if (lrq->hyper_ux_runtime != RUNTIME_INF) {
		raw_spin_lock(&lrq->hyper_ux_runtime_lock);
		lrq->hyper_ux_time += delta;
		exceeded = sched_hyper_ux_runtime_exceeded(lrq);
		if (exceeded)
			resched_curr(rq);
		raw_spin_unlock(&lrq->hyper_ux_runtime_lock);
		if (exceeded)
			start_lenovo_ux_bandwidth(&def_ux_bandwidth);
	}
}
