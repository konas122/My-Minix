#include "type.h"
#include "const.h"
#include "protect.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"


PUBLIC int sys_get_ticks()
{
	return ticks;
}


PUBLIC void schedule() 
{
    PROCESS *p;
    int greatest_ticks = 0;

    while (!greatest_ticks) {
        for (p = proc_table; p < proc_table + NR_TASKS; p++)
            if (p->ticks > greatest_ticks) {
                // disp_str("<");
                // disp_int(p->ticks);
                // disp_str(">");
                greatest_ticks = p->ticks;
                p_proc_ready = p;
            }

            if (!greatest_ticks) {
                for (p = proc_table; p < proc_table + NR_TASKS; p++)
                    p->ticks = p->priority;
            }
    }
}
