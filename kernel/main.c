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
#include "proto.h"


void Init() {
    int fd_stdin    = open("/dev_tty0", O_RDWR);
    assert(fd_stdin == 0);

    int fd_stdout   = open("/dev_tty0", O_RDWR);
    assert(fd_stdout == 1);

	printf("Init() is running ...\n");

    // int pid = fork();
    // if (pid != 0)
    // { /* parent process */
    //     printf("parent is running, child pid:%d\n", pid);
    //     // int s;
    //     // int child = wait(&s);
    //     // printf("child (%d) exited with status: %d.\n", child, s);
    // }
    // else {          /* child process */
	// 	printf("child is running, pid:%d\n", getpid());
	// 	// exit(123);
	// }

    // while (1) {
	// 	int s;
	// 	int child = wait(&s);
	// 	printf("child (%d) exited with status: %d.\n", child, s);
	// }
	char rdbuf[128];
    printl("\n");
	while (1) {
		printf("$ ");
		int r = read(fd_stdin, rdbuf, 70);
		rdbuf[r] = 0;

		if (strcmp(rdbuf, "hello") == 0)
			printf("hello world!\n");
		else
			if (rdbuf[0])
				printf("{%s}\n", rdbuf);
	}

	assert(0); /* never arrive here */
    while (1) {}
    
}


/* jmp from kernel.asm::_start. */
PUBLIC int kernel_main()
{
    disp_str("-----\"kernel_main\" begins-----\n");

    int i, j, eflags, prio;
    u8  rpl;
    u8  priv;                   /* privilege */
    u16 selector_ldt = SELECTOR_LDT_FIRST;

	struct task * t;
	struct proc * p = proc_table;

	char * stk = task_stack + STACK_SIZE_TOTAL;

	for (i = 0; i < NR_TASKS + NR_PROCS; i++, p++, t++) {
		if (i >= NR_TASKS + NR_NATIVE_PROCS) {
			p->p_flags = FREE_SLOT;
			continue;
		}

	    if (i < NR_TASKS) {     /* TASK */
            t	    = task_table + i;
            priv	= PRIVILEGE_TASK;
            rpl     = RPL_TASK;
            eflags  = 0x1202;   /* IF=1, IOPL=1, bit 2 is always 1 */
			prio    = 15;
        }
        else {                  /* USER PROC */
            t	    = user_proc_table + (i - NR_TASKS);
            priv	= PRIVILEGE_USER;
            rpl     = RPL_USER;
            eflags  = 0x202;	/* IF=1, bit 2 is always 1 */
			prio    = 5;
        }

        /* name of the process */
		strcpy(p->name, t->name);	
		p->p_parent = NO_TASK;

		if (strcmp(t->name, "INIT") != 0) {
			p->ldts[INDEX_LDT_C]  = gdt[SELECTOR_KERNEL_CS >> 3];
			p->ldts[INDEX_LDT_RW] = gdt[SELECTOR_KERNEL_DS >> 3];

			/* change the DPLs */
			p->ldts[INDEX_LDT_C].attr1  = DA_C   | priv << 5;
			p->ldts[INDEX_LDT_RW].attr1 = DA_DRW | priv << 5;
		}
		else {		            /* INIT process */
			unsigned int k_base;
			unsigned int k_limit;
			int ret = get_kernel_map(&k_base, &k_limit);
			assert(ret == 0);
			init_descriptor(&p->ldts[INDEX_LDT_C],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_C | priv << 5);

			init_descriptor(&p->ldts[INDEX_LDT_RW],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_DRW | priv << 5);
		}
        p->ldt_sel = selector_ldt;

		p->regs.cs = INDEX_LDT_C << 3 |	SA_TIL | rpl;
		p->regs.ds =
		p->regs.es =
		p->regs.fs =
		p->regs.ss = INDEX_LDT_RW << 3 | SA_TIL | rpl;
		p->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p->regs.eip	= (u32)t->initial_eip;
		p->regs.esp	= (u32)stk;
		p->regs.eflags	= eflags;

		p->ticks = p->priority = prio;

		p->p_flags = 0;
		p->p_msg = 0;
		p->p_recvfrom = NO_TASK;
		p->p_sendto = NO_TASK;
		p->has_int_msg = 0;
		p->q_sending = 0;
		p->next_sending = 0;

		for (j = 0; j < NR_FILES; j++) {
			p->filp[j] = 0;
        }

		stk -= t->stacksize;
        selector_ldt += 1 << 3;
	}

    proc_table[NR_TASKS + 1].nr_tty = 2;
    proc_table[NR_TASKS + 2].nr_tty = 1;
    proc_table[NR_TASKS + 3].nr_tty = 2;

    // proc_table[2].nr_tty = 2;
    // proc_table[3].nr_tty = 2;

    k_reenter = 0;
    ticks = 0;

    p_proc_ready = proc_table;

    init_clock();
    init_keyboard();

    restart();

    while(1) {}
}


PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}


void TestA()
{
    int fd;
	int n;
	const char filename[] = "blah";
	const char bufw[] = "abcde";
	const int rd_bytes = 3;
	char bufr[rd_bytes];

	assert(rd_bytes <= strlen(bufw));

	/* create */
	fd = open(filename, O_CREAT | O_RDWR);
	assert(fd != -1);
	printl("File created. fd: %d\n", fd);

	/* write */
	n = write(fd, bufw, strlen(bufw));
	assert(n == strlen(bufw));

	/* close */
	close(fd);

	/* open */
	fd = open(filename, O_RDWR);
	assert(fd != -1);
	printl("File opened. fd: %d\n", fd);

	/* read */
	n = read(fd, bufr, rd_bytes);
	assert(n == rd_bytes);
	bufr[n] = 0;
	printl("%d bytes read: %s\n", n, bufr);

	/* close */
	close(fd);

    char * filenames[] = {"/foo", "/bar", "/baz"};

	/* create files */
	for (int i = 0; i < sizeof(filenames) / sizeof(filenames[0]); i++) {
		fd = open(filenames[i], O_CREAT | O_RDWR);
		assert(fd != -1);
		printl("File created: %s (fd %d)\n", filenames[i], fd);
		close(fd);
	}

	char * rfilenames[] = {"/bar", "/foo", "/baz"};

	/* remove files */
	for (int i = 0; i < sizeof(rfilenames) / sizeof(rfilenames[0]); i++) {
		if (unlink(rfilenames[i]) == 0)
			printl("File removed: %s\n", rfilenames[i]);
		else
			printl("Failed to remove file: %s\n", rfilenames[i]);
	}

    // spin("TestA");
    while (1) {}
    
}

void TestB()
{
	char tty_name[] = "/dev_tty1";

	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];

	while (1) {
		printf("$ ");
		int r = read(fd_stdin, rdbuf, 70);
		rdbuf[r] = 0;

		if (strcmp(rdbuf, "hello") == 0)
			printf("hello world!\n");
		else
			if (rdbuf[0])
				printf("{%s}\n", rdbuf);
	}

	assert(0); /* never arrive here */
    // for (;;)
    //     ;
}

void TestC()
{
	int i = 0x0100;
    printl("\n\n");
    while (i--) {
        printl("C");
        milli_delay(200);
    }

    char tty_name[] = "/dev_tty2";

	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];

    printf("\n");
    while (1)
    {
        printf("$ ");
		int r = read(fd_stdin, rdbuf, 70);
		rdbuf[r] = 0;

		if (strcmp(rdbuf, "hello") == 0)
			printf("hello world!\n");
		else
			if (rdbuf[0])
				printf("{%s}\n", rdbuf);
    }

    assert(0); /* never arrive here */
    while (1) {}
}

PUBLIC void panic(const char* fmt, ...) {
    int i;
    char buf[256];

    /* 4 is the size of fmt in the stack */
    va_list arg = (va_list)((char *)&fmt + 4);

    i = vsprintf(buf, fmt, arg);

    printl("return %i \n %c !!panic!! %s", i, MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}
