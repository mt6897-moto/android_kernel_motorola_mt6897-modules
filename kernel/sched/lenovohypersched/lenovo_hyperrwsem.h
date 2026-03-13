#ifndef _LENOVOHYPER_RWSEM_H
#define _LENOVOHYPER_RWSEM_H

#include <linux/sched.h>
#include <linux/rwsem.h>

bool rwsem_list_add(struct task_struct *tsk, struct list_head *entry,
		    struct list_head *head);
void rwsem_dynamic_hyper_enqueue(struct task_struct *tsk,
				 struct task_struct *waiter_task,
				 struct task_struct *owner,
				 struct rw_semaphore *sem);
void rwsem_dynamic_hyper_dequeue(struct rw_semaphore *sem,
				 struct task_struct *tsk);
#endif
