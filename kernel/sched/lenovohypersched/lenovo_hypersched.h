#ifndef _LENOVOHYPER_SCHED_H
#define _LENOVOHYPER_SCHED_H

#include <linux/sched.h>
#include <linux/irq_work.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include "lenovo_hyperprio.h"

//#include <linux/printk.h>
/*hyper log ctl
 * add hyper log ctl: hyper_log_ctl
 * bit0: static log;
 * bit1: dynmic log;
 * bit2: mutex log;
 * bit3: rwsem log;
 * bit4: binder log;
 * bit5: cgroup log;
 */
extern int hyper_log_ctl;
#define STATIC_LOG		(1<<0)
#define DYNAMIC_LOG		(1<<1)
#define MUTEX_LOG		(1<<2)
#define RWSEM_LOG		(1<<3)
#define BINDER_LOG		(1<<4)
#define CGROUP_LOG		(1<<5)
#define TEST_LOG		(1<<30)
#define NEED_LOG		(1<<31)
#define HYPER_PRINTK(u,fmt,...) { \
	if(hyper_log_ctl & u) printk(fmt,##__VA_ARGS__); \
}

extern bool hyper_sched_enable;

enum hyper_ux_boost_type {
	HYPER_BOOST_BINDER = 0,
	HYPER_BOOST_RWSEM,
	HYPER_BOOST_MUTEX,
	HYPER_BOOST_FUTEX,
	HYPER_BOOST_CGROUP,
	HYPER_BOOST_TYPE_MAX,
};

enum DYNAMIC_HYPER_TYPE {
	DYNAMIC_HYPER_BINDER = 0,
#ifdef CONFIG_FUSE_FS
	DYNAMIC_HYPER_FUSE = DYNAMIC_HYPER_BINDER,
#endif
	DYNAMIC_HYPER_RWSEM,
	DYNAMIC_HYPER_MUTEX,
	DYNAMIC_HYPER_SEM,
	DYNAMIC_HYPER_FUTEX,
	DYNAMIC_HYPER_MAX,
};

#define HYPER_MSG_LEN 64
#define HYPER_DEPTH_MAX 2

#define UX_FOR_ALL_THREADS 5

void enqueue_hyper_thread(struct rq *rq, struct task_struct *p);
void dequeue_hyper_thread(struct rq *rq, struct task_struct *p);
void pick_hyper_thread(struct rq *rq, struct task_struct **p,
                        struct sched_entity **se, bool *repick, bool simple);
void dynamic_hyper_dequeue(struct task_struct *task, int type);
void dynamic_hyper_enqueue(struct task_struct *task, int type, int depth);
bool test_task_hyper(struct task_struct *task);
bool test_dynamic_hyper(struct task_struct *task, int type);

extern int lenovo_hypernode_proc_init(void);

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;
	bool handoff_set;
};

struct lenovo_static_attribute {
	int static_hyper_ux;
	int hyper_ux_depth;
	atomic64_t dynamic_hyper_ux;
	u64 enqueue_time;
	u64 dynamic_hyper_ux_start;
};

struct lenovo_dynamic_attribute {
	spinlock_t lock;
	unsigned int saved_policy;
	int saved_prio;
	unsigned long dyn_switched_flag;
	unsigned int normal_hyper_ux_prio;
	struct irq_work sleeping_reset_work;
	unsigned char hyper_ux_boost[HYPER_BOOST_TYPE_MAX];
	struct lenovo_task_struct *ltsk;
};

struct lenovo_task_struct {
	struct list_head hyper_entry;
	struct lenovo_static_attribute *lsa;
	struct lenovo_dynamic_attribute *lda;
};

struct lenovo_rq {
	/*task list for hyper thread */
	struct list_head hyper_ux_thread_list;
	int num_hyper_ux_tasks;
	int hyper_ux_throttled;
	u64 hyper_ux_time;
	u64 hyper_ux_runtime;
	raw_spinlock_t hyper_ux_runtime_lock;
};

struct lenovo_task_group_struct {
	unsigned int hyper_ux_prio;
};

#define lenovotsk_to_tsk(ltsk) ({ \
		void *__mptr = (void *)(ltsk); \
		((struct task_struct *)(__mptr - \
		offsetof(struct task_struct, android_oem_data1))); })

static struct lenovo_task_struct *get_lenovo_task_struct(struct task_struct
							 *task)
{
	if (task == NULL)
		return NULL;
	return (struct lenovo_task_struct *)task->android_oem_data1;
}

static inline struct lenovo_task_group_struct
*get_lenovo_task_group_struct(struct task_group *tg)
{
	if (tg == NULL)
		return NULL;
	return (struct lenovo_task_group_struct *)(&tg->android_kabi_reserved1);
}

static inline unsigned int uclamp_hyper_prio(struct task_struct *p)
{
	struct cgroup_subsys_state *css = task_css(p, cpu_cgrp_id);
	struct task_group *tg;
	struct lenovo_task_group_struct *ltask_group;

	if (!css)
		return 0;
	tg = container_of(css, struct task_group, css);
	ltask_group = get_lenovo_task_group_struct(tg);
	return ltask_group->hyper_ux_prio;
}

static inline void set_hyper_flags(struct task_struct *p)
{
	set_tsk_thread_flag(p, TIF_ENERGY_EFFICIENT);
	set_tsk_thread_flag(p, TIF_EXPECTED_HEAVY);
}

static inline bool is_hyper_prio(int prio)
{
	return MAX_RT_PRIO - 1 - HYPER_PRIO_WIDTH <= prio
	    && prio < MAX_RT_PRIO - 1;
}

/* Task's effective hyper prio. */
static inline unsigned int task_hyper_prio(struct task_struct *p)
{
	unsigned int prio;
	return p && is_hyper_prio(prio = p->prio)
	    ? MAX_RT_PRIO - 1 - prio : 0;
}

unsigned int get_max_hyper_boost(struct task_struct *p);
#endif
