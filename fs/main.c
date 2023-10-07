#include "type.h"
#include "config.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

// #include "hd.h"


// <Ring 1> The main loop of TASK FS. 
PUBLIC void task_fs() {
    printl("Task FS begins.\n");

    // open the device: hard disk
    MESSAGE driver_msg;

    /**
     * 这里我们不仅将`ROOT_DEV`的次设备号通过消息发给了驱动程序，
     * 而且使用哪个驱动程序也变成由`dd_map`来选择。这样一来，
     * 只要将`ROOT_DEV`定义好了，正确的消息便能发送给正确的驱动程序。
    */
    driver_msg.type = DEV_OPEN;
	driver_msg.DEVICE = MINOR(ROOT_DEV);    
	assert(dd_map[MAJOR(ROOT_DEV)].driver_nr != INVALID_DRIVER);    

    send_recv(BOTH, TASK_HD, &driver_msg);

    spin("FS");
}
