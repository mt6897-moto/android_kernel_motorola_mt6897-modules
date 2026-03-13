#ifndef _LENOVOHYPER_MUTEX_H
#define _LENOVOHYPER_MUTEX_H

#include <linux/sched.h>
#include <linux/mutex.h>
#include <locking/mutex.h>

bool mutex_list_add(struct task_struct *task, struct list_head *entry,
		    struct list_head *head, struct mutex *lock);
void mutex_dynamic_hyper_enqueue(struct mutex *lock, struct task_struct *task);
void mutex_dynamic_hyper_dequeue(struct mutex *lock, struct task_struct *task);
#endif
