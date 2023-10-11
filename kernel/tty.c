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


#define TTY_FIRST	(tty_table)
#define TTY_END		(tty_table + NR_CONSOLES)


PRIVATE void    init_tty        (TTY* p_tty);
PRIVATE void	tty_do_read	    (TTY* tty, MESSAGE* msg);
PRIVATE void	tty_do_write	(TTY* tty, MESSAGE* msg);
PRIVATE void    put_key         (TTY* p_tty, u32 key);
PRIVATE void	tty_dev_read	(TTY* tty);
PRIVATE void	tty_dev_write	(TTY* tty);


PUBLIC void init_tty(TTY* p_tty) {
    p_tty->inbuf_count = 0;
    p_tty->inbuf_head = p_tty->inbuf_tail = p_tty->in_buf;

    init_screen(p_tty);
}


PUBLIC void task_tty() {
    TTY *   p_tty;
    MESSAGE msg;

    /* panic("in TTY"); */
	/* assert(0); */

    init_keyboard();
    for (p_tty = TTY_FIRST; p_tty < TTY_END; p_tty++) {
        init_tty(p_tty);
    }
    select_console(0);
    while (1) {
        for (p_tty = TTY_FIRST; p_tty < TTY_END; p_tty++) {
            do {
                tty_dev_read(p_tty);    // 从键盘缓冲区读入字符
                tty_dev_write(p_tty);   // 将字符送入进程P的缓冲区
            } while (p_tty->inbuf_count);
        }

        send_recv(RECEIVE, ANY, &msg);

        int src = msg.source;
        assert(src != TASK_TTY);

        TTY *ptty = &tty_table[msg.DEVICE];

        switch (msg.type) {
            case DEV_OPEN:
                reset_msg(&msg);
                msg.type = SYSCALL_RET;
                send_recv(SEND, src, &msg);
                break;
            case DEV_READ:
                tty_do_read(ptty, &msg);
                break;
            case DEV_WRITE:
                tty_do_write(ptty, &msg);
                break;
            case HARD_INT:
            /**
             * waked up by clock_handler -- a key was just pressed
             * @see clock_handler() inform_int()
            */
                key_pressed = 0;
                continue;
            default:
                dump_msg("TTY::unknown msg", &msg);
			    break;
        }
    }
}


/**
 * keyboard_read() will invoke this routine after having recognized a key press.
 * 
 * @param tty  The key press is for whom.
 * @param key  The integer key with metadata.
*/
PUBLIC void in_process(TTY* p_tty, u32 key) {

    if (!(key & FLAG_EXT)) {
        put_key(p_tty, key);
    } else {
        int raw_code = key & MASK_RAW;
        switch(raw_code) {
            case ENTER:
                put_key(p_tty, '\n');
                break;
            case BACKSPACE:
                put_key(p_tty, '\b');
                break;
            case UP:
                if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
				    scroll_screen(p_tty->console, SCR_DN);
                }
			    break;
            case DOWN:
                if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
                    scroll_screen(p_tty->console, SCR_UP);
                }
                break;

            case F1:
            case F2:
            case F3:
            case F4:
            case F5:
            case F6:
            case F7:
            case F8:
            case F9:
            case F10:
            case F11:
            case F12:
                /* Alt + F1~F12 */
                if ((key & FLAG_ALT_L) || (key & FLAG_ALT_R)) {
                    select_console(raw_code - F1);
                }
                break;

            default:
                break;
        }
    }
}


/**
 * Get chars from the keyboard buffer if the TTY::console is the `current'
 * console.
 *
 * @see keyboard_read()
 * 
 * @param tty  Ptr to TTY.
*/ 
PUBLIC void tty_dev_read(TTY* p_tty) {
    if (is_current_console(p_tty->console)) {
        keyboard_read(p_tty);
    }
}


/**
 * Echo the char just pressed and transfer it to the waiting process.
 * 
 * @param tty   Ptr to a TTY struct.
*/
PUBLIC void tty_dev_write(TTY* tty) {
    while (tty->inbuf_count) {
        char ch = *(tty->inbuf_tail);
        tty->inbuf_tail++;
        // 若键盘处理到缓冲区尾部
        if (tty->inbuf_tail == tty->in_buf + TTY_IN_BYTES)
            tty->inbuf_tail = tty->in_buf;

        tty->inbuf_count--;

        if (tty->tty_left_cnt) {
			if (ch >= ' ' && ch <= '~') { /* printable */
				out_char(tty->console, ch);
				void * p = tty->tty_req_buf +
					   tty->tty_trans_cnt;
				phys_copy(p, (void *)va2la(TASK_TTY, &ch), 1);
				tty->tty_trans_cnt++;
				tty->tty_left_cnt--;
			}
			else if (ch == '\b' && tty->tty_trans_cnt) {
				out_char(tty->console, ch);
				tty->tty_trans_cnt--;
				tty->tty_left_cnt++;
			}

			if (ch == '\n' || tty->tty_left_cnt == 0) {
				out_char(tty->console, '\n');
				MESSAGE msg;
				msg.type = RESUME_PROC;
				msg.PROC_NR = tty->tty_procnr;
				msg.CNT = tty->tty_trans_cnt;
				send_recv(SEND, tty->tty_caller, &msg);
				tty->tty_left_cnt = 0;
			}
		}
    }
}


/**
 * Put a key into the in-buffer of TTY.
 *
 * @callergraph
 * 
 * @param tty  To which TTY the key is put.
 * @param key  The key. It's an integer whose higher 24 bits are metadata.
*/
PRIVATE void put_key(TTY* p_tty, u32 key) {
    // 若缓冲区未满
    if (p_tty->inbuf_count < TTY_IN_BYTES) {
		*(p_tty->inbuf_head) = key;
		p_tty->inbuf_head++;

       // 若缓冲区已满
		if (p_tty->inbuf_head == p_tty->in_buf + TTY_IN_BYTES) {
			p_tty->inbuf_head = p_tty->in_buf;
		}
		p_tty->inbuf_count++;
    }
}


/**
 * System calls accept four parameters. `printx' needs only two, so it wastes
 * the other two.
 *
 * @note `printx' accepts only one parameter -- `char* s', the other one --
 * `struct proc * proc' -- is pushed by kernel.asm::sys_call so that the
 * kernel can easily know who invoked the system call.
 *
 * @note s[0] (the first char of param s) is a magic char. if it equals
 * MAG_CH_PANIC, then this syscall was invoked by `panic()', which means
 * something goes really wrong and the system is to be halted; if it equals
 * MAG_CH_ASSERT, then this syscall was invoked by `assert()', which means
 * an assertion failure has occured. @see kernel/main lib/misc.c.
 * 
 * @param _unused1  Ignored.
 * @param _unused2  Ignored.
 * @param s         The string to be printed.
 * @param p_proc    Caller proc.
 * 
 * @return  Zero if success.
*/
PUBLIC int sys_printx(int _unused1, int _unused2, char* s, struct proc* p_proc) {
    const char *p;
    char ch;

    char reenter_err[] = "? k_reenter is incorrect for unknown reason";
    reenter_err[0] = MAG_CH_PANIC;

    /**
	 * @note Code in both Ring 0 and Ring 1~3 may invoke printx().
	 * If this happens in Ring 0, no linear-physical address mapping
	 * is needed.
	 *
	 * @attention The value of `k_reenter' is tricky here. When
	 *   -# printx() is called in Ring 0
	 *      - k_reenter > 0. When code in Ring 0 calls printx(),
	 *        an `interrupt re-enter' will occur (printx() generates
	 *        a software interrupt). Thus `k_reenter' will be increased
	 *        by `kernel.asm::save' and be greater than 0.
	 *   -# printx() is called in Ring 1~3
	 *      - k_reenter == 0.
	 */

    if (k_reenter == 0)         /* printx() called in Ring<1~3> */
		p = va2la(proc2pid(p_proc), s);
	else if (k_reenter > 0)     /* printx() called in Ring<0> */
		p = s;
	else	                    /* this should NOT happen */
		p = reenter_err;
    
    /**
	 * @note if assertion fails in any TASK, the system will be halted;
	 * if it fails in a USER PROC, it'll return like any normal syscall
	 * does.
	 */
    if ((*p == MAG_CH_PANIC) || 
        (*p == MAG_CH_ASSERT && p_proc_ready < &proc_table[NR_TASKS])) {
        disable_int();
        char *v = (char *)V_MEM_BASE;
        const char *q = p + 1;  /* +1: skip the magic char */

        while (v < (char*)(V_MEM_BASE + V_MEM_SIZE)) {
			*v++ = *q++;
			*v++ = RED_CHAR;
			if (!*q) {
				while (((int)v - V_MEM_BASE) % (SCR_WIDTH * 16)) {
					/* *v++ = ' '; */
					v++;
					*v++ = GRAY_CHAR;
				}
				q = p + 1;
			}
		}
		__asm__ __volatile__("hlt");
    }

    while ((ch = *p++) != 0) {
		if (ch == MAG_CH_PANIC || ch == MAG_CH_ASSERT)
			continue;           /* skip the magic char */

		out_char(tty_table[p_proc->nr_tty].console, ch);
    }
    return 0;
}


/* For debuf only. */
PUBLIC void dump_tty_buf()
{
	TTY * tty = &tty_table[1];

	static char sep[] = "--------------------------------\n";

	printl(sep);

	printl("head: %d\n", tty->inbuf_head - tty->in_buf);
	printl("tail: %d\n", tty->inbuf_tail - tty->in_buf);
	printl("cnt: %d\n", tty->inbuf_count);

	int pid = tty->tty_caller;
	printl("caller: %s (%d)\n", proc_table[pid].name, pid);
	pid = tty->tty_procnr;
	printl("caller: %s (%d)\n", proc_table[pid].name, pid);

	printl("req_buf: %d\n", (int)tty->tty_req_buf);
	printl("left_cnt: %d\n", tty->tty_left_cnt);
	printl("trans_cnt: %d\n", tty->tty_trans_cnt);

	printl("--------------------------------\n");

	strcpy(sep, "\n");
}


/*****************************************************************************
 *                                tty_do_read
 *****************************************************************************/
/**
 * Invoked when task TTY receives DEV_READ message.
 *
 * @note The routine will return immediately after setting some members of
 * TTY struct, telling FS to suspend the proc who wants to read. The real
 * transfer (tty buffer -> proc buffer) is not done here.
 * 
 * @param tty  From which TTY the caller proc wants to read.
 * @param msg  The MESSAGE just received.
 *****************************************************************************/
PRIVATE void tty_do_read(TTY* tty, MESSAGE* msg) {
    // tell the tty:
    tty->tty_caller     = msg->source;      // who called, usually FS
    tty->tty_procnr     = msg->PROC_NR;     // who wants the chars
    tty->tty_req_buf    = va2la(tty->tty_procnr,
				  msg->BUF);                // where the chars should be put
    tty->tty_left_cnt    = msg->CNT;         // how many chars are requested
    tty->tty_trans_cnt  = 0;                // how many chars have been transferred

    msg->type   = SUSPEND_PROC;
    msg->CNT    = tty->tty_left_cnt;
    send_recv(SEND, tty->tty_caller, msg);
}


/*****************************************************************************
 *                                tty_do_write
 *****************************************************************************/
/**
 * Invoked when task TTY receives DEV_WRITE message.
 * 
 * @param tty  To which TTY the calller proc is bound.
 * @param msg  The MESSAGE.
 *****************************************************************************/
PRIVATE void tty_do_write(TTY* tty, MESSAGE* msg) {
    char buf[TTY_OUT_BUF_LEN];
    char *p = (char *)va2la(msg->PROC_NR, msg->BUF);
    int i = msg->CNT;
    int j;

    while (i) {
        int bytes = min(TTY_OUT_BUF_LEN, i);
        phys_copy(va2la(TASK_TTY, buf), (void *)p, bytes);
        for (j = 0; j < bytes; j++)
            out_char(tty->console, buf[j]);
        i -= bytes;
        p += bytes;
    }

    msg->type = SYSCALL_RET;
    send_recv(SEND, msg->source, msg);
}


