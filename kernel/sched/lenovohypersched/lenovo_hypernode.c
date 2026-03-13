// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Lenovo. All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <proc/internal.h>
#include "lenovo_hypersched.h"
#include "dynamic_prio.h"
#include "lenovo_hyperbandwidth.h"

#define MAX_SET (128)
#define LENOVO_HYPERSCHED_PROC_DIR  "lenovo_hyperschedule"

extern struct lenovo_ux_bandwidth def_ux_bandwidth;

static int glo_pid = -1, last_stathyper_pid = -1;

DEFINE_MUTEX(hyper_proc_node_mutex);

/* add hyper log ctl: hyper_log_ctl
 * bit0: static log;
 * bit1: dynmic log;
 * bit2: mutex log;
 * bit3: rwsem log;
 * bit4: binder log;
 * bit5: cgroup log;
 */
int hyper_log_ctl = 0;		//default close
static int proc_hyper_log_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%x\n", hyper_log_ctl);
	return 0;
}

static int proc_hyper_log_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_hyper_log_show, inode);
}

static ssize_t proc_hyper_log_write(struct file *file, const char __user * buf,
				    size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char opt_str[64] = { "0" };
	int hyper_log = 0;
	int cnt = 0;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < 1)) {
		strlcpy(opt_str, token, sizeof(opt_str));
		cnt += 1;
	}

	err = kstrtoint(strstrip(opt_str), 0, &hyper_log);
	if (err)
		return err;

	mutex_lock(&hyper_proc_node_mutex);
	hyper_log_ctl = hyper_log;
	mutex_unlock(&hyper_proc_node_mutex);
	return count;
}

static ssize_t proc_hyper_log_read(struct file *file, char __user * buf,
				   size_t count, loff_t * ppos)
{
	char buffer[PROC_NUMBUF];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "0x%x\n", hyper_log_ctl);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops proc_hyper_log_ops = {
	.proc_open = proc_hyper_log_open,
	.proc_write = proc_hyper_log_write,
	.proc_read = proc_hyper_log_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int proc_ux_enable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "hyper_sched_enable: %s\n",
		hyper_sched_enable ? "enable" : "disable");
	return 0;
}

static int proc_ux_enable_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_ux_enable_show, inode);
}

static ssize_t proc_ux_enable_write(struct file *file, const char __user * buf,
				     size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char opt_str[64] = { "0" };
	int enable = 0;
	int cnt = 0;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < 1)) {
		strlcpy(opt_str, token, sizeof(opt_str));
		cnt += 1;
	}

	err = kstrtoint(strstrip(opt_str), 0, &enable);
	if (err)
		return err;

	//cannot disable after enable.
	if (hyper_sched_enable && !enable)
		return -EFAULT;

	if (hyper_sched_enable != enable) {
		mutex_lock(&hyper_proc_node_mutex);
		hyper_sched_enable = enable;
		mutex_unlock(&hyper_proc_node_mutex);
	}

	return count;
}

static const struct proc_ops proc_ux_enable_operations = {
	.proc_open = proc_ux_enable_open,
	.proc_write = proc_ux_enable_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

extern int init_lenovo_static_task_struct(struct lenovo_task_struct *pltsk);
void reinit_lenovo_task_struct(struct lenovo_task_struct *ltsk)
{
	if (init_lenovo_static_task_struct(ltsk))
		printk(KERN_ERR "reinit_lenovo_task_struct: failed!!!");

	return;
}

static int proc_static_hyper_ux_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p = NULL;
	struct lenovo_task_struct *ltsk;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;
	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lsa) {
		put_task_struct(p);
		return -ESRCH;
	}

	task_lock(p);
	seq_printf(m, "%d\n", ltsk->lsa->static_hyper_ux);
	task_unlock(p);
	put_task_struct(p);

	return 0;
}

static int proc_static_hyper_ux_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_static_hyper_ux_show, inode);
}

enum {
	LET_STR_TYPE = 0,
	LET_STR_PID,
	LET_STR_VAL,
	LET_STR_MAX = 3,
};

static ssize_t proc_static_hyper_ux_write(struct file *file,
				       const char __user * buf, size_t count,
				       loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char letsk_str[LET_STR_MAX][8] = { "0", "0", "0" };
	int cnt = 0;
	int pid = 0;
	int static_hyper_ux = 0;
	int err = 0;
	struct lenovo_task_struct *ltsk;

	if (!hyper_sched_enable) {
		return -EFAULT;
	}

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < LET_STR_MAX)) {
		strlcpy(letsk_str[cnt], token, sizeof(letsk_str[cnt]));
		cnt += 1;
	}

	err = kstrtoint(strstrip(letsk_str[LET_STR_PID]), 10, &pid);
	if (err)
		return err;

	err = kstrtoint(strstrip(letsk_str[LET_STR_VAL]), 10, &static_hyper_ux);
	if (err)
		return err;

	mutex_lock(&hyper_proc_node_mutex);
	if (!strncmp(letsk_str[LET_STR_TYPE], "p", 1) && (static_hyper_ux >= 0)) {
		struct task_struct *hyper_task = NULL;

		if (pid > 0 && pid <= PID_MAX_DEFAULT) {
			rcu_read_lock();
			last_stathyper_pid = pid;
			hyper_task = find_task_by_vpid(pid);
			ltsk = get_lenovo_task_struct(hyper_task);
			if (!ltsk) {
				rcu_read_unlock();
				printk(KERN_ERR
				       "proc_static_hyper_ux_write: the pid[%d] is err,because task is NULL!!!",
				       pid);
				mutex_unlock(&hyper_proc_node_mutex);
				return -EFAULT;
			}

			if (unlikely(!ltsk->lsa)) {
				reinit_lenovo_task_struct(ltsk);
			}
			if (likely(ltsk->lsa)) {
				ltsk->lsa->static_hyper_ux = static_hyper_ux;
			} else {
				rcu_read_unlock();
				printk(KERN_ERR
				       "proc_static_hyper_ux_write: the pid[%d] is err,because ltsk->lsa is NULL!!!",
				       pid);
				mutex_unlock(&hyper_proc_node_mutex);
				return -ENOMEM;
			}
			rcu_read_unlock();
		}
	}

	mutex_unlock(&hyper_proc_node_mutex);
	return count;
}

static ssize_t proc_static_hyper_ux_read(struct file *file, char __user * buf,
				      size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	struct task_struct *task = NULL;
	int static_hyper_ux = -1, taskpid = -1;
	size_t len = 0;
	struct lenovo_task_struct *ltsk;

	if (last_stathyper_pid == -1)
		return 0;

	rcu_read_lock();
	task = find_task_by_vpid(last_stathyper_pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	ltsk = get_lenovo_task_struct(task);
	if (!ltsk || !ltsk->lsa) {
		put_task_struct(task);
		return -ESRCH;
	}

	static_hyper_ux = ltsk->lsa->static_hyper_ux;
	taskpid = task->pid;
	put_task_struct(task);
	len = snprintf(buffer, sizeof(buffer), "p pid=%d static_hyper_ux=%d\n",
		     taskpid, static_hyper_ux);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops proc_static_hyper_ux_operations = {
	.proc_open = proc_static_hyper_ux_open,
	.proc_write = proc_static_hyper_ux_write,
	.proc_read = proc_static_hyper_ux_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int proc_hyper_prio_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	struct task_struct *p = NULL;
	struct lenovo_task_struct *ltsk;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;
	ltsk = get_lenovo_task_struct(p);
	if (!ltsk || !ltsk->lda) {
		put_task_struct(p);
		return -ESRCH;
	}

	task_lock(p);
	seq_printf(m, "%d\n", ltsk->lda->normal_hyper_ux_prio);
	task_unlock(p);
	put_task_struct(p);

	return 0;
}

static int proc_hyper_prio_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_hyper_prio_show, inode);
}

static ssize_t proc_hyper_prio_write(struct file *file, const char __user * buf,
				     size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char letsk_str[LET_STR_MAX][8] = { "0", "0", "0" };
	int cnt = 0;
	int pid = 0;
	int hyper_ux_prio = 0;
	int err = 0;

	if (!hyper_sched_enable) {
		return -EFAULT;
	}
	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < LET_STR_MAX)) {
		strlcpy(letsk_str[cnt], token, sizeof(letsk_str[cnt]));
		cnt += 1;
	}
	err = kstrtoint(strstrip(letsk_str[LET_STR_PID]), 10, &pid);
	if (err)
		return err;

	err = kstrtoint(strstrip(letsk_str[LET_STR_VAL]), 10, &hyper_ux_prio);
	if (err)
		return err;
	printk("create hyper_ux_prio  proc node is :  %d,%d\n", pid, hyper_ux_prio);

	mutex_lock(&hyper_proc_node_mutex);
	if (!strncmp(letsk_str[LET_STR_TYPE], "p", 1) && (hyper_ux_prio >= 0)) {
		struct task_struct *hyper_task = NULL;

		if (pid > 0 && pid <= PID_MAX_DEFAULT) {
			rcu_read_lock();
			glo_pid = pid;
			hyper_task = find_task_by_vpid(pid);
			if (!hyper_task) {
				rcu_read_unlock();
				printk(KERN_ERR
				       "proc_hyper_prio_write: the pid[%d] is err,because task is NULL!!!",
				       pid);
				mutex_unlock(&hyper_proc_node_mutex);
				return -EFAULT;
			}
			err = set_hyper_prio(hyper_task, hyper_ux_prio);
			rcu_read_unlock();
		}
	}

	mutex_unlock(&hyper_proc_node_mutex);
	if (err)
		return err;
	return count;
}

static ssize_t proc_hyper_prio_read(struct file *file, char __user * buf,
				    size_t count, loff_t * ppos)
{
	char buffer[64];
	struct task_struct *task = NULL;
	int hyper_ux_prio = -1, taskpid = -1;
	size_t len = 0;
	struct lenovo_task_struct *ltsk;

	if (glo_pid == -1)
		return 0;

	rcu_read_lock();
	task = find_task_by_vpid(glo_pid);
	if (!task){
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	ltsk = get_lenovo_task_struct(task);
	if (!ltsk || !ltsk->lda) {
		put_task_struct(task);
		return -ESRCH;
	}

	hyper_ux_prio = ltsk->lda->normal_hyper_ux_prio;
	taskpid = task->pid;
	put_task_struct(task);
	len = snprintf(buffer, sizeof(buffer), "p pid=%d hyper_ux_prio=%d\n",
		     taskpid, hyper_ux_prio);
  
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops proc_hyper_prio_operations = {
	.proc_open = proc_hyper_prio_open,
	.proc_write = proc_hyper_prio_write,
	.proc_read = proc_hyper_prio_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

#ifdef CONFIG_CGROUP_SCHED0
struct task_group *last_task_cgroup;
extern struct list_head task_groups;
static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

static int proc_cgroup_hyper_show(struct seq_file *m, void *v)
{
	struct lenovo_task_group_struct *ltsk_group;
	struct task_group *tg;

	if (last_task_cgroup) {
		tg = last_task_cgroup;
		ltsk_group = get_lenovo_task_group_struct(tg);
		if (!ltsk_group)
			return 0;
		seq_printf(m, "%d\n", ltsk_group->hyper_ux_prio);
	} else
		seq_printf(m, "%s\n", "NULL");

	return 0;
}

static int proc_cgroup_hyper_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_cgroup_hyper_show, inode);
}

static ssize_t proc_cgroup_hyper_write(struct file *file,
				       const char __user * buf, size_t count,
				       loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;

	char letsk_str[LET_STR_MAX][64] = { "0", "0", "0" };
	int cnt = 0;
	int cgroup_hyper = 0;
	int err = 0;
	struct cgroup *cgroup;
	struct task_group *tg;
	struct lenovo_task_group_struct *ltsk_group;

	if (!hyper_sched_enable) {
		return -EFAULT;
	}
	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < LET_STR_MAX)) {
		strlcpy(letsk_str[cnt], token, sizeof(letsk_str[cnt]));
		cnt += 1;
	}

	//parse cgroup hyper
	err = kstrtoint(strstrip(letsk_str[LET_STR_PID]), 10, &cgroup_hyper);
	if (err)
		return err;

	//parse cgroup name
	str = letsk_str[LET_STR_TYPE];
	if (cgroup_hyper > 10 || cgroup_hyper < 0)
		return -EINVAL;

	rcu_read_lock();
	list_for_each_entry_rcu(tg, &task_groups, list) {
		if (tg) {
			cgroup = tg->css.cgroup;
			if (cgroup && !strcmp(cgroup->kn->name, str)) {
				ltsk_group = get_lenovo_task_group_struct(tg);
				ltsk_group->hyper_ux_prio = cgroup_hyper;
				last_task_cgroup = tg;
			}
		}
	}
	rcu_read_unlock();
	return count;
}

static ssize_t proc_cgroup_hyper_read(struct file *file, char __user * buf,
				      size_t count, loff_t * ppos)
{
	size_t len = 0;
	char buffer[PROC_NUMBUF];
	struct lenovo_task_group_struct *ltsk_group;
	struct task_group *tg;
	struct cgroup *cgroup;

	if (last_task_cgroup) {
		tg = last_task_cgroup;
		cgroup = tg->css.cgroup;
		ltsk_group = get_lenovo_task_group_struct(tg);
		len = snprintf(buffer, sizeof(buffer), "%s %d\n",
			     cgroup->kn->name, ltsk_group->hyper_ux_prio);
		return simple_read_from_buffer(buf, count, ppos, buffer, len);
	}

	return 0;
}

static const struct proc_ops proc_cgroup_hyper_operations = {
	.proc_open = proc_cgroup_hyper_open,
	.proc_write = proc_cgroup_hyper_write,
	.proc_read = proc_cgroup_hyper_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#endif

static int proc_hyper_stat_show(struct seq_file *m, void *v)
{
	ulong hyper_task = 0;
	ulong hyper_ux_boost_task = 0;
	struct task_struct *p = NULL;
	struct lenovo_task_struct *ltsk;
	int cpu;
	struct rq *rq;
	struct lenovo_rq *lrq;

	if (!hyper_sched_enable) {
		seq_printf(m, "hypersched is disable.\n");
		return 0;
	}

	rcu_read_lock();
	for_each_process(p) {
		ltsk = get_lenovo_task_struct(p);
		if (!ltsk || !ltsk->lsa)
			continue;
		if (ltsk->lsa->static_hyper_ux) {
			struct task_struct *t = NULL;
			struct lenovo_task_struct *t_ltsk;
			hyper_task++;
			seq_printf(m,
				   "pid:%d,type:static_ux, value:%d, on_cpu:%d,comm:%s\n",
				   p->pid, ltsk->lsa->static_hyper_ux, task_cpu(p), p->comm);
			for_each_thread(p, t) {
				if (t == p)
					continue;
				t_ltsk = get_lenovo_task_struct(t);
				if (!t_ltsk || !t_ltsk->lsa)
					continue;
				if (t_ltsk->lsa->static_hyper_ux) {
					seq_printf(m,
					   "  tid:%d,type:static_ux, value:%d, on_cpu:%d, comm:%s\n",
					   t->pid, t_ltsk->lsa->static_hyper_ux, task_cpu(t), t->comm);
				}
			}
		}
		if (atomic64_read(&ltsk->lsa->dynamic_hyper_ux)) {
			hyper_ux_boost_task++;
			seq_printf(m, "pid:%d,type:boost_ux,on_cpu:%d,comm:%s\n",
				   p->pid, task_cpu(p), p->comm);
		}
	}
	seq_printf(m, "hyper_task:%ld;hyper_ux_boost_task:%ld\n", hyper_task,
		   hyper_ux_boost_task);
	seq_printf(m, "\n");
	for_each_possible_cpu(cpu) {
		rq = cpu_rq(cpu);
		if (!rq)
			continue;
		lrq = (struct lenovo_rq *)rq->android_oem_data1;
		if (!lrq)
			continue;
		seq_printf(m, "cpu:%d,ux_list:%d\n", cpu, lrq->num_hyper_ux_tasks);
	}
	rcu_read_unlock();
	return 0;
}

static int proc_hyper_stat_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_hyper_stat_show, inode);
}

static const struct proc_ops proc_hyper_stat_operations = {
	.proc_open = proc_hyper_stat_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int proc_ux_runtime_show(struct seq_file *m, void *v)
{
	seq_printf(m, "hyper_ux_runtime: %lluns\n",
		   def_ux_bandwidth.hyper_ux_runtime);
	return 0;
}

static int proc_ux_runtime_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_ux_runtime_show, inode);
}

static ssize_t proc_ux_runtime_write(struct file *file, const char __user * buf,
				     size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char opt_str[64] = { "0" };
	u64 runtime = 0;
	int cnt = 0;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < 1)) {
		strlcpy(opt_str, token, sizeof(opt_str));
		cnt += 1;
	}

	err = kstrtou64(strstrip(opt_str), 0, &runtime);
	if (err)
		return err;

	mutex_lock(&hyper_proc_node_mutex);
	def_ux_bandwidth.hyper_ux_runtime = runtime;
	mutex_unlock(&hyper_proc_node_mutex);

	return count;
}

static const struct proc_ops proc_ux_runtime_operations = {
	.proc_open = proc_ux_runtime_open,
	.proc_write = proc_ux_runtime_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int proc_ux_period_show(struct seq_file *m, void *v)
{
	seq_printf(m, "hyper_ux_period: %lluns\n",
		   def_ux_bandwidth.hyper_ux_period);
	return 0;
}

static int proc_ux_period_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, proc_ux_period_show, inode);
}

static ssize_t proc_ux_period_write(struct file *file, const char __user * buf,
				     size_t count, loff_t * ppos)
{
	char buffer[MAX_SET];
	char *str, *token;
	char opt_str[64] = { "0" };
	u64 period = 0;
	int cnt = 0;
	int err = 0;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		return -EFAULT;
	}

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < 1)) {
		strlcpy(opt_str, token, sizeof(opt_str));
		cnt += 1;
	}

	err = kstrtou64(strstrip(opt_str), 0, &period);
	if (err)
		return err;

	raw_spin_lock(&def_ux_bandwidth.hyper_ux_runtime_lock);
	def_ux_bandwidth.hyper_ux_period = period;
	def_ux_bandwidth.hyper_ux_max_runtime = period - div_u64(period, 5); // period 80%
	raw_spin_unlock(&def_ux_bandwidth.hyper_ux_runtime_lock);

	return count;
}

static const struct proc_ops proc_ux_period_operations = {
	.proc_open = proc_ux_period_open,
	.proc_write = proc_ux_period_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int lenovo_hypernode_proc_init(void)
{
	struct proc_dir_entry *lenovo_hypersched_node;
	struct proc_dir_entry *proc_hyper_enable_node;
	struct proc_dir_entry *proc_hyper_log_node;
	struct proc_dir_entry *static_proc_node;
	struct proc_dir_entry *prio_proc_node;
	struct proc_dir_entry *proc_node;

	lenovo_hypersched_node = proc_mkdir(LENOVO_HYPERSCHED_PROC_DIR, NULL);
	if (!lenovo_hypersched_node) {
		printk("failed to create proc dir lenovo hyperschedule\n");
		goto err_create_hyperschedule;
	}

	proc_hyper_enable_node = proc_create("enable", 0666, lenovo_hypersched_node,
			&proc_ux_enable_operations);
	if (!proc_hyper_enable_node) {
		printk("failed to create enable proc node \n");
		goto err_create_hyper_enable_node;
	}

	proc_hyper_log_node = proc_create("ux_logctl", 0666, lenovo_hypersched_node,
			&proc_hyper_log_ops);
	if (!proc_hyper_log_node) {
		printk("failed to create ux_logctl proc node \n");
		goto err_create_hyper_logctl_node;
	}

	static_proc_node = proc_create("static_ux", 0666, lenovo_hypersched_node,
			&proc_static_hyper_ux_operations); 
	if (!static_proc_node) {
		printk("failed to create static_ux proc node \n");
		goto err_create_static_hyper_ux_node;
	}

	prio_proc_node = proc_create("ux_prio", 0666, lenovo_hypersched_node,
			&proc_hyper_prio_operations);
	if (!prio_proc_node) {
		printk("failed to create ux_prio proc node \n");
		goto err_create_hyper_prio_node;
	}
#ifdef CONFIG_CGROUP_SCHED0
	proc_node =  proc_create("cgroup_ux", 0666, lenovo_hypersched_node,
			&proc_cgroup_hyper_operations);
	if (!proc_node) {
		printk("failed to create cgroup ux proc node \n");
		goto err_create_cgroup_hyper_node;
	}
#endif
	proc_node = proc_create("ux_stat", 0444, lenovo_hypersched_node,
			&proc_hyper_stat_operations);
	if (!proc_node) {
		printk("failed to create ux stat proc node \n");
		goto err_create_ux_stat_node;
	}

	proc_node = proc_create("ux_runtime", 0666, lenovo_hypersched_node,
			&proc_ux_runtime_operations);
	if (!proc_node) {
		printk("failed to create ux_runtime proc node \n");
		goto err_create_ux_runtime_node;
	}

	proc_node = proc_create("ux_period", 0666, lenovo_hypersched_node,
			&proc_ux_period_operations);
	if (!proc_node) {
		printk("failed to create ux_period proc node \n");
		goto err_create_ux_period_node;
	}

	return 0;


err_create_ux_period_node:
	remove_proc_entry("ux_runtime", lenovo_hypersched_node);
err_create_ux_runtime_node:
	remove_proc_entry("ux_stat", lenovo_hypersched_node);
err_create_ux_stat_node:
#ifdef CONFIG_CGROUP_SCHED0
	remove_proc_entry("cgroup_ux", lenovo_hypersched_node);
err_create_cgroup_hyper_node:
#endif
	remove_proc_entry("ux_prio", lenovo_hypersched_node);
err_create_hyper_prio_node:
	remove_proc_entry("static_ux", lenovo_hypersched_node);
err_create_static_hyper_ux_node:
	remove_proc_entry("ux_logctl", lenovo_hypersched_node);
err_create_hyper_logctl_node:
	remove_proc_entry("enable", lenovo_hypersched_node);
err_create_hyper_enable_node:
	remove_proc_entry(LENOVO_HYPERSCHED_PROC_DIR, NULL);
err_create_hyperschedule:
	return -ENOENT;
}
