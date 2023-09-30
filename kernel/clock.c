#include "type.h"
#include "const.h"
#include "protect.h"
#include "proto.h"
#include "string.h"
#include "proc.h"
#include "global.h"


PUBLIC void clock_handler(int irq) {
    // disp_str("#");
    ticks++;
    p_proc_ready->ticks--;

    if (k_reenter != 0) {
        // disp_str("!");
        return;
    }
    // if (p_proc_ready->ticks > 0)
    //     return;

    // p_proc_ready++;
    // if (p_proc_ready >= proc_table + NR_TASKS)
    //     p_proc_ready = proc_table;
    schedule();
}

PUBLIC void milli_delay(int milli_sec) {
    int t = get_ticks();
    while((get_ticks() - t) * 1000 / HZ < milli_sec) {}
}
