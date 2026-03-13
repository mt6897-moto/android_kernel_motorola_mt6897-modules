// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Lenovo. All rights reserved.
 */

#include "lenovo_hyperrwsem.h"
#include "lenovo_hypersched.h"
#include <linux/list.h>

#define RWSEM_READER_OWNED ((struct task_struct *)1UL)
#define RWSEM_OWNER_BOOST       (1UL << 7)

static inline bool rwsem_owner_is_writer(struct task_struct *owner)
{
	return owner && owner != RWSEM_READER_OWNED;
}

static void rwsem_list_add_hyper(struct list_head *entry,
				 struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
		if (!test_task_hyper(waiter->task)) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	if (pos == head)
		list_add_tail(entry, head);
}

bool rwsem_list_add(struct task_struct *tsk,
		    struct list_head *entry, struct list_head *head)
{
	bool is_hyper = test_task_hyper(tsk);

	if (!entry || !head)
		return false;
    HYPER_PRINTK(RWSEM_LOG,KERN_ERR "rwsem_list_add  task-id=%d\n",tsk->pid);

	if (is_hyper)
		rwsem_list_add_hyper(entry, head);
	else
		list_add_tail(entry, head);

	return true;
}

static bool is_rwsem_owner_boost(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->count) & RWSEM_OWNER_BOOST;
}

static void rwsem_owner_set_boost(struct rw_semaphore *sem)
{
	unsigned long count;

	count = atomic_long_read(&sem->count);
	do {
		if (count & RWSEM_OWNER_BOOST)
			break;
	} while (!atomic_long_try_cmpxchg(&sem->count, &count,
					  count | RWSEM_OWNER_BOOST));
}

static void rwsem_owner_clear_boost(struct rw_semaphore *sem)
{
	unsigned long count;

	count = atomic_long_read(&sem->count);
	do {
		if (!(count & RWSEM_OWNER_BOOST))
			break;
	} while (!atomic_long_try_cmpxchg(&sem->count, &count,
					  count & ~RWSEM_OWNER_BOOST));
}

void rwsem_dynamic_hyper_enqueue(struct task_struct *tsk,
				 struct task_struct *waiter_task,
				 struct task_struct *owner,
				 struct rw_semaphore *sem)
{
	bool is_hyper = test_task_hyper(tsk);
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(tsk);
    HYPER_PRINTK(RWSEM_LOG,KERN_ERR "rwsem_dynamic_hyper_ux_enqueuetask-id=%d\n",tsk->pid);

	if (waiter_task && is_hyper) {
		if (rwsem_owner_is_writer(owner) &&
		    !test_task_hyper(owner) && sem && !is_rwsem_owner_boost(sem)
		    && fair_policy(owner->policy)) {
			dynamic_hyper_enqueue(owner, DYNAMIC_HYPER_RWSEM,
					      ltsk->lsa->hyper_ux_depth);
			rwsem_owner_set_boost(sem);
		}
	}
}

void rwsem_dynamic_hyper_dequeue(struct rw_semaphore *sem,
				 struct task_struct *tsk)
{
	HYPER_PRINTK(RWSEM_LOG,KERN_ERR "rwsem_dynamic_hyper_ux_dequeue....task-id=%d\n",tsk->pid);
	if (tsk && sem && is_rwsem_owner_boost(sem)) {
		dynamic_hyper_dequeue(tsk, DYNAMIC_HYPER_RWSEM);
		rwsem_owner_clear_boost(sem);
	}
}
