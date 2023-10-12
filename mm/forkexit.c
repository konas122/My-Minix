#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"


PRIVATE void cleanup(struct proc * proc);


/*****************************************************************************
 *                                do_fork
 *****************************************************************************/
/**
 * Perform the fork() syscall.
 * 
 * @return  Zero if success, otherwise -1.
 *****************************************************************************/
PUBLIC int do_fork() {
    /* find a free slot in proc_table 
       先在proc_table[]中寻找一个空项，用于存放子进程的进程表。*/
	struct proc* p = proc_table;
	int i;
	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p++)
		if (p->p_flags == FREE_SLOT)
			break;

	int child_pid = i;
	assert(p == &proc_table[child_pid]);
	assert(child_pid >= NR_TASKS + NR_NATIVE_PROCS);
	if (i == NR_TASKS + NR_PROCS) /* no free slot */
		return -1;
	assert(i < NR_TASKS + NR_PROCS);

	/* duplicate the process table 
       将父进程的进程表原原本本地赋值给子进程。*/
	int pid = mm_msg.source;
	u16 child_ldt_sel = p->ldt_sel;
	*p = proc_table[pid];
	p->ldt_sel = child_ldt_sel;
	p->p_parent = pid;
	sprintf(p->name, "%s_%d", proc_table[pid].name, child_pid);

    /* duplicate the process: T, D & S 
       为子进程分配内存，首先需要得到父进程的内存占用情况，这由读取LDT来完成。
       总之，我们最终要让代码段、数据段和栈段都指向相同的地址空间。 */
    struct descriptor *ppd;

    /* Text segment */
    ppd = &proc_table[pid].ldts[INDEX_LDT_C];
    /* base of T-seg, in bytes */
    int caller_T_base = reassembly(
        ppd->base_high, 24,
        ppd->base_mid, 16,
        ppd->base_low);
    /* limit of T-seg, in 1 or 4096 bytes,
	   depending on the G bit of descriptor */
    int caller_T_limit = reassembly(
        0, 0,
        (ppd->limit_high_attr2 & 0XF), 16,
        ppd->limit_low);
    /* size of T-seg, in bytes */
    int caller_T_size = ((caller_T_limit + 1) *
                         ((ppd->limit_high_attr2 & (DA_LIMIT_4K >> 8)) ? 
                         4096 : 1));

    /* Data & Stack segments */
    ppd = &proc_table[pid].ldts[INDEX_LDT_RW];
    /* base of D&S-seg, in bytes */
    int caller_D_S_base = reassembly(
        ppd->base_high, 24,
        ppd->base_mid, 16,
        ppd->base_low);
    /* limit of D&S-seg, in 1 or 4096 bytes, depending on the G bit of descriptor */
    int caller_D_S_limit = reassembly(
        (ppd->limit_high_attr2 & 0xF), 16,
        0, 0,
        ppd->limit_low);
    /* size of D&S-seg, in bytes */
    int caller_D_S_size = ((caller_T_limit + 1) *
                           ((ppd->limit_high_attr2 & (DA_LIMIT_4K >> 8)) ? 
                           4096 : 1));
    
    /* 用assert来保证代码段、数据段和栈段都指向相同的地址空间 */
    /* we don't separate T, D & S segments, so we have: */
	assert((caller_T_base  == caller_D_S_base ) &&
	       (caller_T_limit == caller_D_S_limit) &&
	       (caller_T_size  == caller_D_S_size ));
    
    /* base of child proc, T, D & S segments share the same space,
	   so we allocate memory just once */
    int child_base = alloc_mem(child_pid, caller_T_size);
    /* int child_limit = caller_T_limit; */
    printl("{MM} 0x%x <- 0x%x (0x%x bytes)\n",
           child_base, caller_T_base, caller_T_size);
    /* child is a copy of the parent 
       将父进程的内存空间完整地复制了一份给新分配的空间，
       由于内存空间不同，所以等会还要更新一下子进程的LDT。*/
    phys_copy((void*)child_base, (void*)caller_T_base, caller_T_size);

    /* child's LDT 
       由于内存空间不同，更新子进程的LDT */
    init_descriptor(
        &p->ldts[INDEX_LDT_C],
        child_base,
        (PROC_IMAGE_SIZE_DEFAULT - 1) >> LIMIT_4K_SHIFT,
        DA_LIMIT_4K | DA_32 | DA_C | PRIVILEGE_USER << 5);
    init_descriptor(
        &p->ldts[INDEX_LDT_RW],
        child_base,
        (PROC_IMAGE_SIZE_DEFAULT - 1) >> LIMIT_4K_SHIFT,
        DA_LIMIT_4K | DA_32 | DA_DRW | PRIVILEGE_USER << 5);

    /* tell FS, see fs_fork() 
       我们知道，进程生出子进程的时候，可能已经打开了文件，
       因此需要让父子两进程共享文件，所以我们需要通知FS，让它完成相应的工作。*/
	MESSAGE msg2fs;
	msg2fs.type = FORK;
	msg2fs.PID = child_pid;
	send_recv(BOTH, TASK_FS, &msg2fs);

    /* child PID will be returned to the parent proc
        子进程由于使用了和父进程几乎一样的进程表，所以目前也是挂起状态，
       这时我们需要给它也发一个消息，这样不仅解除其阻塞，而且将0作为返回值
       传给子进程，让它知道自己的身份。
        对于父进程，do_fork()正常返回后，会由MM的主消息循环发送消息给它，
       我们只需给mm_msg.PID赋值就好了。 */
	mm_msg.PID = child_pid;

	/* birth of the child */
	MESSAGE m;
	m.type = SYSCALL_RET;
	m.RETVAL = 0;
	m.PID = 0;
	send_recv(SEND, child_pid, &m);

	return 0;
}



/*****************************************************************************
 *                                do_exit
 *****************************************************************************/
/**
 * Perform the exit() syscall.
 *
 * If proc A calls exit(), then MM will do the following in this routine:
 *     <1> inform FS so that the fd-related things will be cleaned up
 *     <2> free A's memory
 *     <3> set A.exit_status, which is for the parent
 *     <4> depends on parent's status. if parent (say P) is:
 *           (1) WAITING
 *                 - clean P's WAITING bit, and
 *                 - send P a message to unblock it
 *                 - release A's proc_table[] slot
 *           (2) not WAITING
 *                 - set A's HANGING bit
 *     <5> iterate proc_table[], if proc B is found as A's child, then:
 *           (1) make INIT the new parent of B, and
 *           (2) if INIT is WAITING and B is HANGING, then:
 *                 - clean INIT's WAITING bit, and
 *                 - send INIT a message to unblock it
 *                 - release B's proc_table[] slot
 *               else
 *                 if INIT is WAITING but B is not HANGING, then
 *                     - B will call exit()
 *                 if B is HANGING but INIT is not WAITING, then
 *                     - INIT will call wait()
 *
 * TERMs:
 *     - HANGING: everything except the proc_table entry has been cleaned up.
 *     - WAITING: a proc has at least one child, and it is waiting for the
 *                child(ren) to exit()
 *     - zombie: say P has a child A, A will become a zombie if
 *         - A exit(), and
 *         - P does not wait(), neither does it exit(). that is to say, P just
 *           keeps running without terminating itself or its child
 * 
 * @param status  Exiting status for parent.
 * 
 *****************************************************************************/
PUBLIC void do_exit(int status) {
    int i;
    int pid = mm_msg.source;    // PID of caller
    int parent_pid = proc_table[pid].p_parent;
    struct proc *p = &proc_table[pid];

    // tell FS, see fs_exit()
    MESSAGE msg2fs;
    msg2fs.type = EXIT;
    msg2fs.PID = pid;
    send_recv(BOTH, TASK_FS, &msg2fs);

    free_mem(pid);

    p->exit_status = status;

    if (proc_table[parent_pid].p_flags & WAITING) { /* parent is waiting */
		proc_table[parent_pid].p_flags &= ~WAITING;
		cleanup(&proc_table[pid]);
	}
    else { /* parent is not waiting */
		proc_table[pid].p_flags |= HANGING;
	}

    /* if the proc has any child, make INIT the new parent */
	for (i = 0; i < NR_TASKS + NR_PROCS; i++) {
		if (proc_table[i].p_parent == pid) { /* is a child */
			proc_table[i].p_parent = INIT;
			if ((proc_table[INIT].p_flags & WAITING) &&
			    (proc_table[i].p_flags & HANGING)) {
				proc_table[INIT].p_flags &= ~WAITING;
				cleanup(&proc_table[i]);
			}
		}
	}
}


/*****************************************************************************
 *                                cleanup
 *****************************************************************************/
/**
 * Do the last jobs to clean up a proc thoroughly:
 *     - Send proc's parent a message to unblock it, and
 *     - release proc's proc_table[] entry
 * 
 * @param proc  Process to clean up.
 *****************************************************************************/
PRIVATE void cleanup(struct proc * proc)
{
	MESSAGE msg2parent;
	msg2parent.type = SYSCALL_RET;
	msg2parent.PID = proc2pid(proc);
	msg2parent.STATUS = proc->exit_status;
	send_recv(SEND, proc->p_parent, &msg2parent);

	proc->p_flags = FREE_SLOT;
}


/*****************************************************************************
 *                                do_wait
 *****************************************************************************/
/**
 * Perform the wait() syscall.
 *
 * If proc P calls wait(), then MM will do the following in this routine:
 *     <1> iterate proc_table[],
 *         if proc A is found as P's child and it is HANGING
 *           - reply to P (cleanup() will send P a messageto unblock it)
 *           - release A's proc_table[] entry
 *           - return (MM will go on with the next message loop)
 *     <2> if no child of P is HANGING
 *           - set P's WAITING bit
 *     <3> if P has no child at all
 *           - reply to P with error
 *     <4> return (MM will go on with the next message loop)
 *
 *****************************************************************************/
PUBLIC void do_wait()
{
	int pid = mm_msg.source;

	int i;
	int children = 0;
	struct proc* p_proc = proc_table;
	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p_proc++) {
		if (p_proc->p_parent == pid) {
			children++;
			if (p_proc->p_flags & HANGING) {
				cleanup(p_proc);
				return;
			}
		}
	}

	if (children) {
		/* has children, but no child is HANGING */
		proc_table[pid].p_flags |= WAITING;
	}
	else {
		/* no child at all */
		MESSAGE msg;
		msg.type = SYSCALL_RET;
		msg.PID = NO_TASK;
		send_recv(SEND, pid, &msg);
	}
}





/***
 *  想象得出，`do_exit()/do_wait()`跟`msg_send/msg_receive`这两对函数是
 * 有点类似的，它们最终都是实现了一次“握手”。
 * 
 * 假设进程P有子进程A，当A调用exit()，那么MM将会：
 *  1. 告诉FS：A退出，请做出相应处理
 *  2. 释放A占用的内存
 *  3. 判断P是否在WAITING
 *      · 如果是：
 *          - 清除P的WAITING位
 *          - 向P发送消息已解除阻塞（到此P的wait()函数结束）
 *          - 释放A进程表项（至此A的exit()函数结束）
 *      · 如果否：
 *          - 设置A的HANGING位
 *  4. 遍历proc_table[]，若发现A有子进程B，那么：
 *      · 将Init进程设置为B的父进程（将B过继给Init）
 *      · 判断是否满足Init正在WAITING且B正在HANGING：
 *          - 如果是：
 *              * 清除Init的WAITING位
 *              * 向Init发送消息以解除阻塞（到此Init的wait()函数结束）
 *              * 释放B的进程表项（至此B的exit()函数结束）
 *          - 如果否：
 *              * 如果Init正在WAITING但B并没有HANGING，那么“握手”会在将来B调用exit()时结束
 *              * 如果B正在HANGING但Init并没有WAITING，那么“握手”会在将来Init调用wait()时发生
 * 
 * 若P调用wait()，那么MM将会：
 *  1. 遍历proc_table[]，如果发现A是P的子进程，并且它正在HANGING，那么：
 *      - 向P发送消息以解除阻塞（至此P的wait()函数结束）
 *      - 释放A的进程表项（至此A的exit()函数结束）
 *  2. 如果P的子进程没有一个在HANGING，则：
 *      - 设P的WAITING位
 *  3. 如果P压根儿没有子进程，则：
 *      - 向P发送消息，消息携带一个表示出差的返回值（至此P的wait()结束）
*/

/**
 *  如果一个进程X被置为HANGING位，那么X所在的资源除了exit_status外，全被释放。
 * exit_status记录了X的返回值，只有当X的父进程通过调用wait()取走这个返回值，
 * X进程才会被完全释放。
 * 
 *  如果一个进程Y被置了WAITING位，意味着Y至少有一个子进程，并且正在等待某个子进程退出。
*/
