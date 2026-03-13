// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Lenovo. All rights reserved.
 */

#include <uapi/linux/sched/types.h>
#include <linux/sched.h>
#include <trace/events/sched.h>
#include "dynamic_prio.h"
#include "lenovo_hypersched.h"

#define INVALID_POLICY 0xbbad

/* This function bypass the priority mapping/extending. */
int sched_setattr_directly(struct task_struct *p,
         const struct sched_attr *attr)
{
	int ret;

	/* Pass to spare_hyper_width(). */
	set_tsk_thread_flag(current, TIF_NO_EXTEND);

	set_tsk_thread_flag(current, TIF_MODIFY_PI);
	/* No check and no PI (may be called from interrupt).  */
	ret = sched_setattr_nocheck(p, attr);

	clear_tsk_thread_flag(current, TIF_NO_EXTEND);
	clear_tsk_thread_flag(current, TIF_MODIFY_PI);

	return ret;
}

unsigned int get_max_hyper_boost(struct task_struct *p)
{
	unsigned int type, max = 0;
	struct lenovo_task_struct *ltsk;

	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		return -ESRCH;
	}

	for (type = 0; type < HYPER_BOOST_TYPE_MAX; type++)
		if (ltsk->lda->hyper_ux_boost[type] > max)
			max = ltsk->lda->hyper_ux_boost[type];
	return max;
}

static bool dyn_switched(struct task_struct *p)
{
	/*
	 * Setting hyper is a kind of dynamic switching but we didn't set a
	 * bit on the dyn_switched_flag to avoid redundancy. So that we
	 * check both normal_hyper_ux_prio and dyn_switched_flag here.
	 */
	struct lenovo_task_struct *ltsk;

	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		return -ESRCH;
	}

	return ltsk->lda->normal_hyper_ux_prio || ltsk->lda->dyn_switched_flag;
}

/*
 * One must backup sched attr before setting dynamic prio and the flags.
 * Must hold p->dyn_prio.lock.
 */
int backup_sched_attr(struct task_struct *p)
{
	struct lenovo_task_struct *ltsk;

	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		return -ESRCH;
	}

	if (task_has_dl_policy(p) || task_has_idle_policy(p)
	    || (cpu_rq(task_cpu(p))->stop == p))
		return -EINVAL;

	/* Already backed up. */
	if (dyn_switched(p))
		return 0;

	/* Only back up the priority before dynamic switched. */
	ltsk->lda->saved_policy = p->policy;
	ltsk->lda->saved_prio = p->normal_prio;

	return 0;
}

/* Must hold p->dyn_prio.lock. */
int restore_sched_attr(struct task_struct *p)
{
	unsigned int hyper_ux_prio;
	struct lenovo_task_struct *ltsk;

	struct sched_attr attr = {
		.sched_flags = SCHED_FLAG_KEEP_ROF(p),
	};

	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		return -ESRCH;
	}

	hyper_ux_prio = ltsk->lda->normal_hyper_ux_prio;
	hyper_ux_prio = max(hyper_ux_prio, get_max_hyper_boost(p));

	if (hyper_ux_prio > 0) {
		attr.sched_policy = SCHED_RR;
		attr.sched_priority = hyper_ux_prio;
	} else {
		unsigned int policy = ltsk->lda->saved_policy;
		unsigned int prio = ltsk->lda->saved_prio;

		attr.sched_policy = policy;
		if (fair_policy(policy))
			attr.sched_nice = PRIO_TO_NICE(prio);
		else if (rt_policy(policy))
			attr.sched_priority = MAX_RT_PRIO - 1 - prio;
		else
			return -EINVAL;
	}

	return sched_setattr_dynamic(p, &attr);
}

int sched_setattr_dynamic(struct task_struct *p,
        const struct sched_attr *attr)
{
	int ret;
	/*
	 * To let __sched_setscheduler() know we are setting dynamic prio.
	 * That makes __sched_setscheduler() able to acquire p->dyn_prio.lock
	 * and deal with dyn_prio flags for callers other than us.
	 */
	set_tsk_thread_flag(current, TIF_DYN_PRIO);
	/* No check, no PI, no priority mapping. */
	ret = sched_setattr_directly(p, attr);
	clear_tsk_thread_flag(current, TIF_DYN_PRIO);

	return ret;
}

static void sleeping_reset_func(struct irq_work *irq_work)
{
	struct lenovo_dynamic_attribute *lda_p =
	    container_of(irq_work, struct lenovo_dynamic_attribute,
			 sleeping_reset_work);
	struct lenovo_task_struct *ltsk = lda_p->ltsk;
	struct task_struct *p = lenovotsk_to_tsk(ltsk);

	spin_lock(&ltsk->lda->lock);

	if (!(ltsk->lda->dyn_switched_flag & RESTORE_ON_SLEEP))
		goto out;

	ltsk->lda->dyn_switched_flag &= ~RESTORE_ON_SLEEP;

	restore_sched_attr(p);

out:
	spin_unlock(&ltsk->lda->lock);
	put_task_struct(p);
}

void dynamic_prio_clear(struct lenovo_task_struct *ltsk)
{
	ltsk->lda->saved_policy = INVALID_POLICY;
	ltsk->lda->dyn_switched_flag = 0;
	ltsk->lda->normal_hyper_ux_prio = 0;
	memset(ltsk->lda->hyper_ux_boost, 0, sizeof(ltsk->lda->hyper_ux_boost));
}

int init_lenovo_dynamic_task_struct(struct lenovo_task_struct *ltsk)
{
	if (!ltsk || ltsk->lda)
		return -EINVAL;

	ltsk->lda = kmalloc(sizeof(struct lenovo_dynamic_attribute), GFP_ATOMIC);
	if (!ltsk->lda) {
		printk(KERN_ERR "hyper malloc dynamic task struct mem failed!\n");
		return -ENOMEM;
	}
	spin_lock_init(&ltsk->lda->lock);

	init_irq_work(&ltsk->lda->sleeping_reset_work, sleeping_reset_func);
	ltsk->lda->ltsk = ltsk;

	dynamic_prio_clear(ltsk);
	return 0;
}
