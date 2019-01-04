#include <sys/stat.h>
#include "kernel/calls.h"
#include "fs/proc.h"
#include "fs/fd.h"
#include "fs/tty.h"

static void proc_pid_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->pid);
}

static ssize_t proc_pid_stat_show(struct proc_entry *entry, char *buf) {
    lock(&pids_lock);
    struct task *task = pid_get_task(entry->pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    lock(&task->general_lock);
    lock(&task->group->lock);
    lock(&task->sighand->lock);

    size_t n = 0;
    n += sprintf(buf + n, "%d ", task->pid);
    n += sprintf(buf + n, "(%.16s) ", task->comm);
    n += sprintf(buf + n, "%c ",
            task->zombie ? 'Z' :
            task->group->stopped ? 'T' :
            'R'); // I have no visibility into sleep state at the moment
    n += sprintf(buf + n, "%d ", task->parent ? task->parent->pid : 0);
    n += sprintf(buf + n, "%d ", task->group->pgid);
    n += sprintf(buf + n, "%d ", task->group->sid);
    n += sprintf(buf + n, "%d ", task->group->tty ? task->group->tty->num : 0);
    n += sprintf(buf + n, "%d ", task->group->tty ? task->group->tty->fg_group : 0);
    n += sprintf(buf + n, "%u ", 0); // flags

    // page faults (no data available)
    n += sprintf(buf + n, "%lu ", 0l); // minor faults
    n += sprintf(buf + n, "%lu ", 0l); // children minor faults
    n += sprintf(buf + n, "%lu ", 0l); // major faults
    n += sprintf(buf + n, "%lu ", 0l); // children major faults

    // values that would be returned from getrusage
    // finding these for a given process isn't too easy
    n += sprintf(buf + n, "%lu ", 0l); // user time
    n += sprintf(buf + n, "%lu ", 0l); // system time
    n += sprintf(buf + n, "%ld ", 0l); // children user time
    n += sprintf(buf + n, "%ld ", 0l); // children system time

    n += sprintf(buf + n, "%ld ", 20l); // priority (not adjustable)
    n += sprintf(buf + n, "%ld ", 0l); // nice (also not adjustable)
    n += sprintf(buf + n, "%ld ", list_size(&task->group->threads));
    n += sprintf(buf + n, "%ld ", 0l); // itimer value (deprecated, always 0)
    n += sprintf(buf + n, "%lld ", 0ll); // jiffies on process start

    n += sprintf(buf + n, "%lu ", 0l); // vsize
    n += sprintf(buf + n, "%ld ", 0l); // rss
    n += sprintf(buf + n, "%lu ", 0l); // rss limit

    // bunch of shit that can only be accessed by a debugger
    n += sprintf(buf + n, "%lu ", 0l); // startcode
    n += sprintf(buf + n, "%lu ", 0l); // endcode
    n += sprintf(buf + n, "%lu ", 0l); // startstack
    n += sprintf(buf + n, "%lu ", 0l); // kstkesp
    n += sprintf(buf + n, "%lu ", 0l); // kstkeip

    n += sprintf(buf + n, "%lu ", (unsigned long) task->pending & 0xffffffff);
    n += sprintf(buf + n, "%lu ", (unsigned long) task->blocked & 0xffffffff);
    uint32_t ignored = 0;
    uint32_t caught = 0;
    for (int i = 0; i < 32; i++) {
        if (task->sighand->action[i].handler == SIG_IGN_)
            ignored |= 1l << i;
        else if (task->sighand->action[i].handler != SIG_DFL_)
            caught |= 1l << i;
    }
    n += sprintf(buf + n, "%lu ", (unsigned long) ignored);
    n += sprintf(buf + n, "%lu ", (unsigned long) caught);

    n += sprintf(buf + n, "%lu ", 0l); // wchan (wtf)
    n += sprintf(buf + n, "%lu ", 0l); // nswap
    n += sprintf(buf + n, "%lu ", 0l); // cnswap
    n += sprintf(buf + n, "%d", task->exit_signal);
    // that's enough for now
    n += sprintf(buf + n, "\n");

    unlock(&task->sighand->lock);
    unlock(&task->group->lock);
    unlock(&task->general_lock);
    unlock(&pids_lock);
    return n;
}

static struct proc_dir_entry proc_pid_fd;

static bool proc_pid_fd_readdir(struct proc_entry *entry, int *index, struct proc_entry *next_entry) {
    lock(&pids_lock);
    struct task *task = pid_get_task(entry->pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    lock(&task->files->lock);
    while (*index < task->files->size && task->files->files[*index] == NULL)
        (*index)++;
    fd_t f = (*index)++;
    bool any_left = f < task->files->size;
    unlock(&task->files->lock);
    unlock(&pids_lock);
    *next_entry = (struct proc_entry) {&proc_pid_fd, .pid = entry->pid, .fd = f};
    return any_left;
}

static void proc_pid_fd_getname(struct proc_entry *entry, char *buf) {
    sprintf(buf, "%d", entry->fd);
}

static int proc_pid_fd_readlink(struct proc_entry *entry, char *buf) {
    lock(&pids_lock);
    struct task *task = pid_get_task(entry->pid);
    if (task == NULL) {
        unlock(&pids_lock);
        return _ESRCH;
    }
    lock(&task->files->lock);
    struct fd *fd = fdtable_get(task->files, entry->fd);
    generic_getpath(fd, buf);
    unlock(&task->files->lock);
    unlock(&pids_lock);
    return 0;
}

struct proc_dir_entry proc_pid_entries[] = {
    {2, "stat", .show = proc_pid_stat_show},
    {3, "fd", S_IFDIR, .readdir = proc_pid_fd_readdir},
};

struct proc_dir_entry proc_pid = {1, NULL, S_IFDIR,
    .children = proc_pid_entries, .children_sizeof = sizeof(proc_pid_entries),
    .getname = proc_pid_getname};

static struct proc_dir_entry proc_pid_fd = {0, NULL, S_IFLNK,
    .getname = proc_pid_fd_getname, .readlink = proc_pid_fd_readlink};
