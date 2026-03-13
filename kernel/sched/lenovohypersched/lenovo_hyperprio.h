#ifndef _LENOVOHYPER_PRIO_H
#define _LENOVOHYPER_PRIO_H

/*
 * Acts like a SCHED_FLAG_KEEP_RESET_ON_FORK flag.
 * We don't have to backup the ROF flag together with saved_prio. This
 * way works as well.
 * Should hold p->dyn_prio.lock.
 */

#define HYPER_PRIO_WIDTH 10

#define TIF_ENERGY_EFFICIENT	29	/* RT task that want energy efficient more than latency */
#define TIF_EXPECTED_HEAVY	30	/* RT task that is expected to be a heavy one */
#define TIF_NO_EXTEND		31	/* To let spare_vip_width() using actual priority */
#define TIF_DYN_PRIO		32	/* To distinguish dyn prio callers in __sched_setscheduler() */
#define TIF_MODIFY_PI      	33   /* Modify pi to false in __sched_setscheduler() */

static inline void clear_hyper_flags(struct task_struct *p)
{
	clear_tsk_thread_flag(p, TIF_ENERGY_EFFICIENT);
	clear_tsk_thread_flag(p, TIF_EXPECTED_HEAVY);
}

int set_hyper_prio(struct task_struct *p, unsigned int prio);
void set_cgroup_hyper_prio(struct task_struct *p);
#endif
