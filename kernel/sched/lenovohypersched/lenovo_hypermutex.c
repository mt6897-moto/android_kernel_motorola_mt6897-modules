// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Lenovo. All rights reserved.
 */

#include "lenovo_hypermutex.h"
#include "lenovo_hypersched.h"
#include <linux/version.h>
#include <linux/list.h>


#define MUTEX_FLAGS    0x07

static void mutex_list_add_hyper(struct list_head *entry,
				 struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct mutex_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct mutex_waiter, list);
		if (!test_task_hyper(waiter->task)) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}
	if (pos == head)
		list_add_tail(entry, head);
}

bool mutex_list_add(struct task_struct *task,
		    struct list_head *entry, struct list_head *head,
		    struct mutex *lock)
{
	bool is_hyper = false;

	if (!task || !entry || !head || !lock)
		return false;

	is_hyper = test_task_hyper(task);
	if (is_hyper) {
		mutex_list_add_hyper(entry, head);
		return true;
	}

	return false;
}

static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	return (struct task_struct *)(atomic_long_read(&lock->owner) &
				      ~MUTEX_FLAGS);
}

void mutex_dynamic_hyper_enqueue(struct mutex *lock, struct task_struct *task)
{
	bool is_hyper = false;
	struct task_struct *owner = NULL;
	struct lenovo_task_struct *ltsk = get_lenovo_task_struct(task);

	if (!lock)
		return;

	is_hyper = test_task_hyper(task);
	owner = __mutex_owner(lock);
	if (is_hyper && !test_dynamic_hyper(owner, DYNAMIC_HYPER_MUTEX) && owner
	    && !test_task_hyper(owner)) {
		dynamic_hyper_enqueue(owner, DYNAMIC_HYPER_MUTEX,
				      ltsk->lsa->hyper_ux_depth);
	}
}

void mutex_dynamic_hyper_dequeue(struct mutex *lock, struct task_struct *task)
{
	if (lock && test_dynamic_hyper(task, DYNAMIC_HYPER_MUTEX))
		dynamic_hyper_dequeue(task, DYNAMIC_HYPER_MUTEX);
}
