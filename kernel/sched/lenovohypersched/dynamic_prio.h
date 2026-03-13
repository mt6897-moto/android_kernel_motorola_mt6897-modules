#ifndef _DYNAMIC_PRIO_H
#define _DYNAMIC_PRIO_H

#include <uapi/linux/sched.h>
#include <linux/sched.h>
#include <asm/thread_info.h>
#include "lenovo_hypersched.h"

extern int sched_setattr_nocheck(struct task_struct *p,
				 const struct sched_attr *attr);
extern int sched_setattr(struct task_struct *p, const struct sched_attr *attr);
int sched_setattr_directly(struct task_struct *p,
			   const struct sched_attr *attr);
int backup_sched_attr(struct task_struct *p);
int restore_sched_attr(struct task_struct *p);
int sched_setattr_dynamic(struct task_struct *p, const struct sched_attr *attr);
unsigned int get_raw_sched_priority(struct task_struct *p);
void get_raw_attr(struct task_struct *p, struct sched_attr *attr);
extern int init_lenovo_dynamic_task_struct(struct lenovo_task_struct *ltsk);

/*
 * Acts like a SCHED_FLAG_KEEP_RESET_ON_FORK flag.
 * We don't have to backup the ROF flag together with saved_prio. This
 * way works as well.
 * Should hold p->dyn_prio.lock.
 */
#define SCHED_FLAG_KEEP_ROF(p) \
	(p->sched_reset_on_fork ? SCHED_FLAG_RESET_ON_FORK : 0)

enum dyn_switched_flag_bits {
	RAISED_KTHREAD = 0,
	LONG_RT_DOWNGRADE,
	HYPER_BOOSTED,
	BINDER_DEFERRED_RESTORE,
};

/*
 * These flags (except HYPER_BOOSTED) are automatically cleared and
 * priority restored on dequeue sleep.
 */
#define RESTORE_ON_SLEEP ((unsigned long)((1<<RAISED_KTHREAD) |\
		(1<<LONG_RT_DOWNGRADE) | (1<<BINDER_DEFERRED_RESTORE)))

void reset_dyn_prio_on_sleep(struct task_struct *p);

#endif
