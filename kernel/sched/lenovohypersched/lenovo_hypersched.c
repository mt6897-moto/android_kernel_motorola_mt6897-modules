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
#include "lenovo_hypermutex.h"
#include "lenovo_hyperrwsem.h"
#include "dynamic_prio.h"
#include "lenovo_hyperbandwidth.h"

#define CREATE_TRACE_POINTS
#include "ux_trace.h"

/* lenovo extra vendor hooks if u need rename a vendor hook use this */
#define REGISTER_TRACE_VH(vendor_hook, handler) \
	{ \
		int ret = 0; \
		ret = register_trace_##vendor_hook(handler, NULL); \
		if (ret) { \
			printk(KERN_ERR "failed to register_trace_"#vendor_hook", ret=%d\n", ret); \
			return; \
		} \
	}

#define UNREGISTER_TRACE_VH(vendor_hook, handler) \
	{ \
		unregister_trace_##vendor_hook(handler, NULL); \
	}

#define REGISTER_TRACE_RVH		REGISTER_TRACE_VH

#define UNREGISTER_TRACE_RVH	UNREGISTER_TRACE_VH
/* lenovo extra vendor hooks if u need rename a vendor hook use this */

#define MS_TO_NS 1000000
#define DYNAMIC_HYPER_SEC_WIDTH 8
#define DYNAMIC_HYPER_MASK_BASE 0x00000000ff

#define HYPER_PICK_MIN_EXEC_TIME 16000000UL
#define UX_TASK_LIMIT_ITER_NUM 20 // max 20 for an rq

bool hyper_sched_enable = false;
int hyper_min_sched_delay_granularity;	/* hyper thread delay upper bound(ms) */
int hyper_max_dynamic_granularity = 32;	/* hyper dynamic max exist time(ms) */
int hyper_min_migration_delay = 10;	/* hyper min migration delay time(ms) */
unsigned int sysctl_sched_migration_cost_lenovo;
bool hyper_module_modprobe_finish = false;
EXPORT_SYMBOL(hyper_module_modprobe_finish);

extern struct lenovo_ux_bandwidth def_ux_bandwidth;

#define dynamic_hyper_offset_of(type) (type * DYNAMIC_HYPER_SEC_WIDTH)
#define dynamic_hyper_mask_of(type) \
	((u64)(DYNAMIC_HYPER_MASK_BASE) << (dynamic_hyper_offset_of(type)))
#define dynamic_hyper_get_bits(value, type) \
	((value & dynamic_hyper_mask_of(type)) >> dynamic_hyper_offset_of(type))
#define dynamic_hyper_one(type) ((u64)1 << dynamic_hyper_offset_of(type))
#define HYPER_USE_CPU_ALLOWED 1

/*for rwsem sem_owner get*/
#define RWSEM_READER_OWNED	(1UL << 0)
#define RWSEM_NONSPINNABLE	(1UL << 1)
#define RWSEM_OWNER_FLAGS_MASK	(RWSEM_READER_OWNED | RWSEM_NONSPINNABLE)

int init_lenovo_static_task_struct(struct lenovo_task_struct *pltsk)
{
	struct lenovo_task_struct *ltsk = pltsk;
	ltsk->lsa = kmalloc(sizeof(struct lenovo_static_attribute), GFP_ATOMIC);
	if (!ltsk->lsa)
		return -ENOMEM;

	ltsk->lsa->static_hyper_ux = 0;
	atomic64_set(&(ltsk->lsa->dynamic_hyper_ux), 0);
	INIT_LIST_HEAD(&ltsk->hyper_entry);
	ltsk->lsa->hyper_ux_depth = 0;
	ltsk->lsa->enqueue_time = 0;
	ltsk->lsa->dynamic_hyper_ux_start = 0;
	return 0;
}

void init_lenovo_task_struct(struct task_struct *tsk)
{
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(tsk);
	if (!tsk || !ltsk)
		return;

	if (init_lenovo_static_task_struct(ltsk)) {
		printk(KERN_ERR
		       "[T%d] init failed, malloc static hyper mem failed!!!\n",
		       (int)tsk->pid);
	}
	if (init_lenovo_dynamic_task_struct(ltsk)) {
		printk(KERN_ERR
		       "[T%d] init failed, malloc dynamic hyper mem failed!!!\n",
		       (int)tsk->pid);
	}
}

static bool test_task_exist(struct task_struct *task, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;

	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);
	if (unlikely(!ltsk))
		return false;

	list_for_each_safe(pos, n, head) {
		if (pos == &ltsk->hyper_entry)
			return true;
	}
	return false;
}

bool test_dynamic_hyper(struct task_struct *task, int type)
{
	u64 dynamic_hyper_ux;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	if (unlikely(!task || !ltsk->lsa))
		return false;

	dynamic_hyper_ux = atomic64_read(&ltsk->lsa->dynamic_hyper_ux);
	return dynamic_hyper_get_bits(dynamic_hyper_ux, (u32) type) > 0;
}

static inline void dynamic_hyper_inc(struct task_struct *task, int type)
{
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	if (unlikely(!task || !ltsk->lsa))
		return;

	atomic64_add(dynamic_hyper_one((u32) type), &ltsk->lsa->dynamic_hyper_ux);
}

static void __dynamic_hyper_enqueue(struct task_struct *task, int type,
				    int depth)
{
	struct lenovo_rq *lrq;
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	if (unlikely(!ltsk || !ltsk->lsa))
		return;

	rq = task_rq_lock(task, &flags);

	lrq = (struct lenovo_rq *)rq->android_oem_data1;

	if (!fair_policy(task->policy))
		goto out;

	if (unlikely(!list_empty(&ltsk->hyper_entry)))
		goto out;

	dynamic_hyper_inc(task, type);
	ltsk->lsa->dynamic_hyper_ux_start = jiffies_to_nsecs(jiffies);
	ltsk->lsa->hyper_ux_depth = ltsk->lsa->hyper_ux_depth >
	    (depth + 1) ? ltsk->lsa->hyper_ux_depth : (depth + 1);

	if (task->__state == TASK_RUNNING) {
		exist = test_task_exist(task, &lrq->hyper_ux_thread_list);
		if (!exist) {
			get_task_struct(task);
			list_add_tail(&ltsk->hyper_entry,
				      &lrq->hyper_ux_thread_list);
			lrq->num_hyper_ux_tasks++;
			HYPER_PRINTK(DYNAMIC_LOG,
				     KERN_ERR
				     "dynamic hyper task enqueue task-id=%d\n",
				     task->pid);
			trace_dynamic_ux_set(task, type, task->__state, atomic64_read(&ltsk->lsa->dynamic_hyper_ux),
								ltsk->lsa->hyper_ux_depth);
		}
	}

out:
	task_rq_unlock(rq, task, &flags);
}

void dynamic_hyper_enqueue(struct task_struct *task, int type, int depth)
{
	if (!task || type >= DYNAMIC_HYPER_MAX)
		return;

	__dynamic_hyper_enqueue(task, type, depth);
}

inline bool test_task_hyper(struct task_struct *task)
{
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	return task && ltsk->lsa && (ltsk->lsa->static_hyper_ux
				     || atomic64_read(&ltsk->lsa->
						      dynamic_hyper_ux))
	    && (task->nr_cpus_allowed > HYPER_USE_CPU_ALLOWED);
}

static inline void dynamic_hyper_dec(struct task_struct *task, int type)
{
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);
	if (unlikely(!ltsk->lsa))
		return;

	atomic64_sub(dynamic_hyper_one((u32) type), &ltsk->lsa->dynamic_hyper_ux);
}

static void __dynamic_hyper_dequeue(struct task_struct *task, int type)
{
	struct rq_flags flags;
	bool exist = false;
	struct rq *rq = NULL;
	u64 dynamic_hyper_ux;
	struct lenovo_rq *lrq = NULL;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	if (unlikely(!ltsk || !ltsk->lsa))
		return;

	rq = task_rq_lock(task, &flags);

	lrq = (struct lenovo_rq *)rq->android_oem_data1;
	dynamic_hyper_ux = atomic64_read(&ltsk->lsa->dynamic_hyper_ux);
	if (dynamic_hyper_ux <= 0)
		goto out;
	dynamic_hyper_dec(task, type);
	dynamic_hyper_ux = atomic64_read(&ltsk->lsa->dynamic_hyper_ux);
	if (dynamic_hyper_ux > 0)
		goto out;
	ltsk->lsa->hyper_ux_depth = 0;

	exist = test_task_exist(task, &lrq->hyper_ux_thread_list);
	if (exist) {
		HYPER_PRINTK(DYNAMIC_LOG,
			     KERN_ERR
			     "dynamic hyper task dequeue....task-id=%d\n",
			     task->pid);
		list_del_init(&ltsk->hyper_entry);
		lrq->num_hyper_ux_tasks--;
		trace_dynamic_ux_unset(task, type, task->__state, dynamic_hyper_ux,
								ltsk->lsa->hyper_ux_depth);
		put_task_struct(task);
	}

out:
	task_rq_unlock(rq, task, &flags);
}

void dynamic_hyper_dequeue(struct task_struct *task, int type)
{
	if (!task || type >= DYNAMIC_HYPER_MAX)
		return;

	__dynamic_hyper_dequeue(task, type);
}

static int entity_before(struct sched_entity *a, struct sched_entity *b)
{
	return (s64) (a->vruntime - b->vruntime) < 0;
}

void enqueue_hyper_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	bool exist = false;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(p);
	struct lenovo_rq *lrq = NULL;
	struct task_struct *leader = NULL;
	struct lenovo_task_struct *lleader = NULL;
	int leader_static_ux = 0;

	if (!rq || !ltsk || !ltsk->lsa || (p->flags & PF_EXITING) ||
		!list_empty(&ltsk->hyper_entry))
		return;

	lrq = (struct lenovo_rq *)rq->android_oem_data1;
	if (!lrq || lrq->hyper_ux_throttled)
		return;

	leader = p->group_leader;
	lleader = get_lenovo_task_struct(leader);
	if (lleader && lleader->lsa) {
		leader_static_ux = lleader->lsa->static_hyper_ux;
	}

	ltsk->lsa->enqueue_time = rq->clock;
	if (ltsk->lsa->static_hyper_ux || atomic64_read(&ltsk->lsa->dynamic_hyper_ux) || leader_static_ux == UX_FOR_ALL_THREADS) {
		HYPER_PRINTK(STATIC_LOG,
			     KERN_ERR "static_hyper_ux=1 task pid =%d\n", p->pid);
		list_for_each_safe(pos, n, &lrq->hyper_ux_thread_list) {
			if (pos == &ltsk->hyper_entry) {
				exist = true;
				break;
			}
		}
		if (!exist) {
			get_task_struct(p);
			list_add_tail(&ltsk->hyper_entry,
				      &lrq->hyper_ux_thread_list);
			lrq->num_hyper_ux_tasks++;
			HYPER_PRINTK(STATIC_LOG,
				     KERN_ERR
				     "enqueue static hyper task pid = %d\n",
				     p->pid);
		}
	}
}

void dequeue_hyper_thread(struct rq *rq, struct task_struct *p)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	u64 now = jiffies_to_nsecs(jiffies);
	u64 interval = (u64) hyper_max_dynamic_granularity * MS_TO_NS;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(p);
	struct lenovo_rq *lrq = NULL;

	if (!rq || !ltsk || !ltsk->lsa)
		return;
	lrq = (struct lenovo_rq *)rq->android_oem_data1;
	if (!lrq)
		return;

	ltsk->lsa->enqueue_time = 0;

	if (!list_empty(&ltsk->hyper_entry)) {
		if (atomic64_read(&ltsk->lsa->dynamic_hyper_ux) &&
			(now - ltsk->lsa->dynamic_hyper_ux_start) > interval) {
				atomic64_set(&ltsk->lsa->dynamic_hyper_ux, 0);
		}

		list_for_each_safe(pos, n, &lrq->hyper_ux_thread_list) {
			if (pos == &ltsk->hyper_entry) {
				ltsk->lsa->hyper_ux_depth = 0;
				list_del_init(&ltsk->hyper_entry);
				lrq->num_hyper_ux_tasks--;
				put_task_struct(p);
				HYPER_PRINTK(STATIC_LOG,
					     KERN_ERR
					     "dequeue static hyper task pid = %d\n",
					     p->pid);
				break;
			}
		}
	}
}

void sched_enqueue_hyper_thread(struct rq *rq, struct task_struct *p)
{
	enqueue_hyper_thread(rq, p);
}

void sched_dequeue_hyper_thread(struct rq *rq, struct task_struct *p)
{
	dequeue_hyper_thread(rq, p);
}

static struct task_struct *pick_first_hyper_thread(struct rq *rq)
{
	struct lenovo_rq *lrq = (struct lenovo_rq *)rq->android_oem_data1;
	struct list_head *hyper_ux_thread_list = &lrq->hyper_ux_thread_list;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct task_struct *temp = NULL;
	struct task_struct *left_most_task = NULL;
	struct task_struct *min_exec_start_task = NULL;
	struct lenovo_task_struct *ltemp;
	unsigned int task_cnt = 0;

	list_for_each_safe(pos, n, hyper_ux_thread_list) {
		ltemp = list_entry(pos, struct lenovo_task_struct, hyper_entry);
		/* ensure hyper task in current rq cpu otherwise delete it */
		temp = lenovotsk_to_tsk(ltemp);
		if (unlikely(task_cpu(temp) != rq->cpu) || unlikely(!task_on_rq_queued(temp))) {
			HYPER_PRINTK(STATIC_LOG,
				     KERN_WARNING
				     "task(%s,%d,%d) does not belong to cpu%d",
				     temp->comm, task_cpu(temp), temp->policy,
				     rq->cpu);
			list_del_init(&ltemp->hyper_entry);
			lrq->num_hyper_ux_tasks--;
			put_task_struct(temp);
			continue;
		}
		if (left_most_task == NULL)
			left_most_task = temp;
		else if (entity_before(&temp->se, &left_most_task->se))
			left_most_task = temp;

		if (min_exec_start_task == NULL)
			min_exec_start_task = temp;
		else if ((s64)(temp->se.exec_start - min_exec_start_task->se.exec_start) < 0)
			min_exec_start_task = temp;

		if (++task_cnt >= UX_TASK_LIMIT_ITER_NUM)
			break;
	}
	if (min_exec_start_task != left_most_task && min_exec_start_task != NULL)
	{
		u64 delta = rq_clock_task(rq) - min_exec_start_task->se.exec_start;
		if (delta > HYPER_PICK_MIN_EXEC_TIME) {
			return min_exec_start_task;
		}
	}

	return left_most_task;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)
#else	/* !CONFIG_FAIR_GROUP_SCHED */
#define for_each_sched_entity(se) \
		for (; se; se = NULL)
#endif

extern void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se);

void pick_hyper_thread(struct rq *rq, struct task_struct **p,
				struct sched_entity **se, bool *repick, bool simple)
{
	struct task_struct *key_task = NULL;
	struct lenovo_rq *lrq = NULL;

	if (!rq || !p || !se || *repick)
		return;
	lrq = (struct lenovo_rq *)rq->android_oem_data1;
	if (!lrq || lrq->hyper_ux_throttled || list_empty(&lrq->hyper_ux_thread_list))
		return;

	key_task = pick_first_hyper_thread(rq);
	if (!key_task || key_task->on_cpu == 1 || key_task->on_rq == 0 ||
		key_task->on_rq == TASK_ON_RQ_MIGRATING || task_cpu(key_task) != cpu_of(rq))
		return;

	*p = key_task;
	*se = &key_task->se;
	if (simple) {
		for_each_sched_entity((*se)) {
			set_next_entity(cfs_rq_of(*se), *se);
		}
	}
	*repick = true;
}

/* implement vender hook in kernel/sched*/
static void probe_android_rvh_enqueue_task_fair(void *ignore, struct rq *rq,
						struct task_struct *p,
						int flags)
{
	if (unlikely(!hyper_sched_enable))
		return;

	sched_enqueue_hyper_thread(rq, p);
}

static void probe_android_rvh_dequeue_task_fair(void *ignore, struct rq *rq,
						struct task_struct *p,
						int flags)
{
	if (unlikely(!hyper_sched_enable))
		return;

	sched_dequeue_hyper_thread(rq, p);
}

void android_rvh_replace_next_task_fair_handler(void *ignore, struct rq *rq,
						struct task_struct **p,
						struct sched_entity **se,
						bool *repick, bool simple,
						struct task_struct *prev)
{
	if (unlikely(!hyper_sched_enable))
		return;

	pick_hyper_thread(rq, p, se, repick, simple);
}

EXPORT_SYMBOL(android_rvh_replace_next_task_fair_handler);
/* implement vender hook in kernel/sched*/

void probe_android_vh_dup_task_struct(void *ignore, struct task_struct *tsk,
				struct task_struct *orig)
{
	init_lenovo_task_struct(tsk);
}

/* implement vender hook in kernel/locking/mutex.c */

void probe_android_vh_alter_mutex_list_add(void *unused, struct mutex *lock,
			struct mutex_waiter *waiter, struct list_head *list, bool *already_on_list)
{
	if (unlikely(!hyper_sched_enable))
		return;

	if (*already_on_list)
		return;

	if (!lock || !waiter || !list)
		return;

	*already_on_list = mutex_list_add(current, &waiter->list, list, lock);
}

void probe_android_vh_mutex_wait_start(void *unused, struct mutex *lock)
{
	if (unlikely(!hyper_sched_enable))
		return;

	mutex_dynamic_hyper_enqueue(lock, current);
}

void probe_android_vh_mutex_unlock_slowpath(void *unused, struct mutex *lock)
{
	if (unlikely(!hyper_sched_enable))
		return;

	mutex_dynamic_hyper_dequeue(lock, current);
}

static inline struct task_struct *rwsem_owner_flags(struct rw_semaphore *sem,
						    unsigned long *pflags)
{
	unsigned long owner = atomic_long_read(&sem->owner);

	*pflags = owner & RWSEM_OWNER_FLAGS_MASK;
	return (struct task_struct *)(owner & ~RWSEM_OWNER_FLAGS_MASK);
}

/* implement vender hook in kernel/rwsem/rwsem.c */
void probe_android_vh_alter_rwsem_list_add(void *unused,
					   struct rwsem_waiter *waiter,
					   struct rw_semaphore *sem,
					   bool *already_on_list)
{
	if (unlikely(!hyper_sched_enable))
		return;

	if (*already_on_list)
		return;

	*already_on_list = rwsem_list_add(current, &waiter->list, &sem->wait_list);
}

void probe_android_vh_rwsem_wake(void *unused, struct rw_semaphore *sem)
{
	unsigned long task_flags;
	struct task_struct *sem_owner = NULL;

	if (unlikely(!hyper_sched_enable))
		return;

	sem_owner = rwsem_owner_flags(sem, &task_flags);

	if (sem_owner && !task_flags && !need_resched())
		rwsem_dynamic_hyper_enqueue(current, current, sem_owner, sem);	//may be need modify initial rwsem_dynamic_hyper_enqueue(current, waiter.task, sem_owner, sem);
}

void probe_android_vh_rwsem_wake_finish(void *unused, struct rw_semaphore *sem)
{
	if (unlikely(!hyper_sched_enable))
		return;

	rwsem_dynamic_hyper_dequeue(sem, current);
}

void probe_android_vh_free_task(void *unused, struct task_struct *tsk)
{
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(tsk);

	if (!ltsk) {
		return;
	}

	if (ltsk->lsa) {
		kfree(ltsk->lsa);
		ltsk->lsa = NULL;
	}

	if (ltsk->lda) {
		kfree(ltsk->lda);
		ltsk->lda = NULL;
	}
}

/*Traversal cfs tasks,before the ko loaded*/

/* rename and register some vendor hook for has been used by walt/others */
void register_lenovo_extra_vendor_hook(void)
{
	REGISTER_TRACE_RVH(android_rvh_replace_next_task_fair,
			   android_rvh_replace_next_task_fair_handler);
	//REGISTER_TRACE_VH(android_vh_alter_mutex_list_add, android_vh_alter_mutex_list_add_handler);
}

/* rename and register some vendor hook for has been used by walt/others */

void probe_android_rvh_wake_up_new_task(void *unused, struct task_struct *p)
{
	if (unlikely(!hyper_sched_enable))
		return;

	set_cgroup_hyper_prio(p);
}

void probe_android_rvh_try_to_wake_up(void *unused, struct task_struct *p)
{
	if (unlikely(!hyper_sched_enable))
		return;

	set_cgroup_hyper_prio(p);
}

void probe_android_vh_sched_stat_runtime(void *unused, struct task_struct *p,
					 u64 delta, u64 vruntime)
{
	if (unlikely(!hyper_sched_enable))
		return;

	sched_hyper_stat_runtime_handle(p, delta);
}

void init_lenovo_rq_data(struct lenovo_rq *lrq)
{
	if (!lrq)
		return;

	INIT_LIST_HEAD(&lrq->hyper_ux_thread_list);
	lrq->num_hyper_ux_tasks = 0;
	lrq->hyper_ux_time = 0;
	lrq->hyper_ux_throttled = 0;
	lrq->hyper_ux_runtime = def_ux_bandwidth.hyper_ux_runtime;
	raw_spin_lock_init(&lrq->hyper_ux_runtime_lock);
}

static void init_all_already_exists_thread(void)
{
	struct task_struct *p, *g;
	u32 iter_cpu;

	printk("init all already exists thread\n");

	read_lock(&tasklist_lock);

	for_each_process_thread(g, p) {
		init_lenovo_task_struct(p);
	}

	for_each_possible_cpu(iter_cpu) {
		p = cpu_rq(iter_cpu)->idle;
		init_lenovo_task_struct(p);
	}

	read_unlock(&tasklist_lock);
}

static int __init init_lenovo_hypersched(void)
{
	int ret, cpu;
	struct rq *rq;
	struct lenovo_rq *lrq;
	static atomic_t already_inited = ATOMIC_INIT(0);

	if (atomic_cmpxchg(&already_inited, 0, 1))
		return 0;

	init_lenovo_rq_bandwidth(&def_ux_bandwidth, LENOVO_RQ_PERIOD,
				 LENOVO_RQ_RUNTIME);

	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		if (!rq) {
			printk("failed to init lenovo rq(%d)", cpu);
			continue;
		}
		lrq = (struct lenovo_rq *)rq->android_oem_data1;

		init_lenovo_rq_data(lrq);
	}

	init_all_already_exists_thread();

	ret = register_trace_android_vh_dup_task_struct
	    (probe_android_vh_dup_task_struct, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_rvh_enqueue_task_fair
	    (probe_android_rvh_enqueue_task_fair, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_rvh_dequeue_task_fair
	    (probe_android_rvh_dequeue_task_fair, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_mutex_wait_start(
				probe_android_vh_mutex_wait_start, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_alter_mutex_list_add(
				probe_android_vh_alter_mutex_list_add, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_mutex_unlock_slowpath(
				probe_android_vh_mutex_unlock_slowpath, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_alter_rwsem_list_add
	    (probe_android_vh_alter_rwsem_list_add, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_rwsem_wake(probe_android_vh_rwsem_wake,
						 NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_rwsem_wake_finish
	    (probe_android_vh_rwsem_wake_finish, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_vh_free_task(probe_android_vh_free_task,
						NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_rvh_wake_up_new_task
	    (probe_android_rvh_wake_up_new_task, NULL);
	if (ret)
		goto failed;

	ret = register_trace_android_rvh_try_to_wake_up
	    (probe_android_rvh_try_to_wake_up, NULL);
	if (ret)
		goto failed;

	ret = register_trace_sched_stat_runtime
	    (probe_android_vh_sched_stat_runtime, NULL);
	if (ret)
		goto failed;

	register_lenovo_extra_vendor_hook();

	ret = lenovo_hypernode_proc_init();
	if (ret) {
		printk(KERN_ERR "create hyperschedule proc node failed\n");
		goto failed;
	}

	hyper_module_modprobe_finish = true;

	printk(KERN_ERR "register lenovo tubro schedule hooks succ\n");

failed:
	if (ret) {
		hyper_sched_enable = false;
		printk(KERN_ERR
		       "register lenovo tubro schedule hooks failed\n");
	}

	return 0;
}

static void __exit exit_lenovo_hypersched(void)
{
	HYPER_PRINTK(STATIC_LOG, KERN_ERR "remove lenovo hyper sched\n");

	return;
}

module_init(init_lenovo_hypersched);
module_exit(exit_lenovo_hypersched);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hyperscheduler@lenovo.com");
MODULE_DESCRIPTION("hyper schedule init");
