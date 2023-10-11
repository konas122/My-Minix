#ifndef _KONIX_TTY_H_
#define _KONIX_TTY_H_


#define TTY_IN_BYTES	256	    /* tty input queue size */
#define TTY_OUT_BUF_LEN	2	    /* tty output buffer size */


struct s_console;


/* TTY */
typedef struct s_tty
{
	u32	in_buf[TTY_IN_BYTES];	/* TTY 输入缓冲区 */
	u32*	inbuf_head;		    /* 指向缓冲区中下一个空闲位置 */
	u32*	inbuf_tail;		    /* 指向键盘任务应处理的键值 */
	int	    inbuf_count;		/* 缓冲区中已经填充了多少 */

    int	    tty_caller;
	int	    tty_procnr;
	void*	tty_req_buf;
	int	    tty_left_cnt;
	int	    tty_trans_cnt;

	struct  s_console*  console;
} TTY;


#endif 
