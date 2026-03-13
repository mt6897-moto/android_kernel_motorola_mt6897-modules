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
#include <linux/minmax.h>
#include "lenovo_hypersched.h"
#include "lenovo_hypermutex.h"
#include "lenovo_hyperrwsem.h"
#include "lenovo_hyperprio.h"
#include <linux/sched.h>
#include <linux/minmax.h>
#include <uapi/linux/sched/types.h>
#include "dynamic_prio.h"
#include "lenovo_hypersched.h"

static int sched_attr_set_hyper_prio(struct task_struct *p, unsigned int prio)
{
	struct sched_attr attr = {
		.sched_flags = SCHED_FLAG_KEEP_ROF(p),
		.sched_policy = SCHED_RR,
		.sched_priority = clamp_val(prio, 1, HYPER_PRIO_WIDTH),
	};

	return sched_setattr_dynamic(p, &attr);
}

/*
 * Can be called in interrupt or atomic context.
 * Must not hold rq lock or pi lock.
 */
int set_hyper_prio(struct task_struct *p, unsigned int prio)
{
	int ret = 0;
	unsigned long flags;
	struct lenovo_task_struct *ltsk;

	if (!p)
		return -EINVAL;

	if (prio > HYPER_PRIO_WIDTH)
		prio = HYPER_PRIO_WIDTH;

	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		return -ESRCH;
	}

	if (prio == ltsk->lda->normal_hyper_ux_prio)
		return 0;

	spin_lock_irqsave(&ltsk->lda->lock, flags);

	if (prio > 0) {
		/* Backup the non-hyper attr. */
		if (!ltsk->lda->normal_hyper_ux_prio) {
			if (backup_sched_attr(p))
				goto out;
		}

		ltsk->lda->normal_hyper_ux_prio = prio;
		/*
		 * Set the hyper flags before setting sched_attr for proper
		 * rt pushing behaviour.
		 */
		set_hyper_flags(p);

		ret = sched_attr_set_hyper_prio(p, prio);
	} else {
		/* Restore what it was before setting to hyper. */
		if (ltsk->lda->normal_hyper_ux_prio) {
			ltsk->lda->normal_hyper_ux_prio = 0;
			clear_hyper_flags(p);
			ret = restore_sched_attr(p);
		}
	}

out:
	spin_unlock_irqrestore(&ltsk->lda->lock, flags);
	if (ret)
		pr_err("error setting hyper. ret=%d task=%s(%d) prio=%u",
		       ret, p->comm, p->pid, prio);
	return ret;
}

void clear_hyper_boost(struct task_struct *p,
		       struct lenovo_dynamic_attribute *lad_p,
		       enum hyper_ux_boost_type type)
{
	unsigned long flags;
	unsigned int max_hyper_ux_boost = 0, new_hyper_ux_prio = 0;
	int ret = -ECHILD;

	if (!p || !lad_p || type >= HYPER_BOOST_TYPE_MAX)
		return;

	if (lad_p->hyper_ux_boost[type] == 0)
		return;

	spin_lock_irqsave(&lad_p->lock, flags);

	lad_p->hyper_ux_boost[type] = 0;

	max_hyper_ux_boost = get_max_hyper_boost(p);
	if (max_hyper_ux_boost == 0) {
		__clear_bit(HYPER_BOOSTED, &lad_p->dyn_switched_flag);

		if (!lad_p->normal_hyper_ux_prio)
			clear_hyper_flags(p);
	}

	new_hyper_ux_prio = max(max_hyper_ux_boost, lad_p->normal_hyper_ux_prio);
	if (new_hyper_ux_prio == task_hyper_prio(p))
		goto out_unlock;

	/*
	 * Reasons of deferring the restore for a finished binder:
	 * 1. The cost of __sched_setscheduler for a dequeued task
	 *    is much cheaper than a current queued task.
	 * 2. Prevent the binder caller preempts binder because of
	 *    lowering our priority. It will save one scheduling.
	 */
	if (type == HYPER_BOOST_BINDER && p->on_rq) {
		__set_bit(BINDER_DEFERRED_RESTORE, &lad_p->dyn_switched_flag);
		ret = -EAGAIN;
	} else {
		ret = restore_sched_attr(p);
	}
	if (ret)
		goto out_unlock;

out_unlock:
	spin_unlock_irqrestore(&lad_p->lock, flags);
}

void request_hyper_boost(struct task_struct *p,
			 struct lenovo_dynamic_attribute *lad_p,
			 unsigned int hyper_ux_prio, enum hyper_ux_boost_type type)
{
	unsigned long flags;
	int ret = -ECHILD;

	if (!p || !lad_p || type >= HYPER_BOOST_TYPE_MAX)
		return;

	if (hyper_ux_prio == 0 || hyper_ux_prio > HYPER_PRIO_WIDTH)
		return;

	if (hyper_ux_prio <= lad_p->hyper_ux_boost[type])
		return;

	/* Don't boost higher priority RT task (quick check). */
	if (p->rt_priority > HYPER_PRIO_WIDTH)
		return;

	spin_lock_irqsave(&lad_p->lock, flags);

	/* Double check inside the lock. */
	if (p->rt_priority > HYPER_PRIO_WIDTH ||
	    test_bit(LONG_RT_DOWNGRADE, &lad_p->dyn_switched_flag))
		goto out_unlock;

	/* Must backup before setting the dyn_switched_flag. */
	if (backup_sched_attr(p))
		goto out_unlock;

	lad_p->hyper_ux_boost[type] = (unsigned char)hyper_ux_prio;
	/*
	 * HYPER_BOOSTED bit must be consistent with max_hyper_ux_boost > 0.
	 * It works like normal_hyper_ux_prio. See restore_sched_attr().
	 */
	__set_bit(HYPER_BOOSTED, &lad_p->dyn_switched_flag);

	/* Already boosted higher. Set hyper_ux_boost[type] and go out. */
	if (task_hyper_prio(p) >= hyper_ux_prio)
		goto out_unlock;

	/*
	 * Set/clear the hyper flags except it's a normal hyper task
	 * or rt task who must have set the flag on its own and
	 * should keep it unchanged on hyper boost.
	 */
	if (!lad_p->normal_hyper_ux_prio)
		set_hyper_flags(p);

	ret = sched_attr_set_hyper_prio(p, hyper_ux_prio);
	if (ret)
		goto out_unlock;

out_unlock:
	spin_unlock_irqrestore(&lad_p->lock, flags);
}

void set_cgroup_hyper_prio(struct task_struct *p)
{
	unsigned int new_hyper_ux_prio;
	struct lenovo_task_struct *ltsk;
	struct lenovo_dynamic_attribute *lda_p;
	unsigned int curr_hyper_ux_prio;

	new_hyper_ux_prio = uclamp_hyper_prio(p);
	ltsk = get_lenovo_task_struct(p);
	if (!ltsk) {
		return;
	}

	lda_p = ltsk->lda;
	if (NULL == lda_p) {
		return;
	}

	curr_hyper_ux_prio = lda_p->hyper_ux_boost[HYPER_BOOST_CGROUP];

	if (likely(new_hyper_ux_prio == curr_hyper_ux_prio))
		return;

	if (new_hyper_ux_prio < curr_hyper_ux_prio)
		clear_hyper_boost(p, lda_p, HYPER_BOOST_CGROUP);

	request_hyper_boost(p, lda_p, new_hyper_ux_prio, HYPER_BOOST_CGROUP);
}
