#ifndef _LENOVOHYPER_BANDWIDTH_H
#define _LENOVOHYPER_BANDWIDTH_H

#define LENOVO_RQ_RUNTIME    (200000000U)
#define LENOVO_RQ_PERIOD     (1000000000U)

struct lenovo_ux_bandwidth {
	raw_spinlock_t   hyper_ux_runtime_lock;
	ktime_t          hyper_ux_period;
	u64              hyper_ux_runtime;
	u64              hyper_ux_max_runtime;
	struct hrtimer   hyper_ux_period_timer;
	unsigned int     hyper_ux_period_active;
};

void init_lenovo_rq_bandwidth(struct lenovo_ux_bandwidth *ux_b, u64 period,
			      u64 runtime);
unsigned int hyper_task_limit(struct task_struct *task);
void sched_hyper_stat_runtime_handle(struct task_struct *task, u64 delta);
#endif
