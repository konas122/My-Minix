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


/*****************************************************************************
 *                                do_rdwt
 *****************************************************************************/
/**
 * Read/Write file and return byte count read/written.
 *
 * Sector map is not needed to update, since the sectors for the file have been
 * allocated and the bits are set when the file was created.
 * 
 *   主要功能是读取或写入文件，并返回已读/写的字节数。这个函数不需要更新扇区映射，
 * 因为文件的扇区在创建文件时已经分配，并且位已经设置。
 * 
 * @return How many bytes have been read/written.
 *****************************************************************************/
PUBLIC int do_rdwt() {
	int fd      = fs_msg.FD;	    /**< file descriptor. */
	void * buf  = fs_msg.BUF;       /**< r/w buffer */
	int len     = fs_msg.CNT;	    /**< r/w bytes */

	int src     = fs_msg.source;    /* caller proc nr. */

    assert((pcaller->filp[fd] >= &f_desc_table[0]) &&
           (pcaller->filp[fd] < &f_desc_table[NR_FILE_DESC]));
    
    if (!(pcaller->filp[fd]->fd_mode & O_RDWR))
        return -1;

    int pos = pcaller->filp[fd]->fd_pos;

    struct inode *pin = pcaller->filp[fd]->fd_inode;

    assert(pin >= &inode_table[0] && pin < &inode_table[NR_INODE]);

    int imode = pin->i_mode & I_TYPE_MASK;

    // 如果文件类型是字符设备
    if (imode == I_CHAR_SPECIAL) {
        // 设置变量t为设备读（DEV_READ）或设备写（DEV_WRITE）
        int t = fs_msg.type == READ ? DEV_READ : DEV_WRITE;
        fs_msg.type = t;

        int dev = pin->i_start_sect;
		assert(MAJOR(dev) == 4);

		fs_msg.DEVICE	= MINOR(dev);
		fs_msg.BUF	= buf;
		fs_msg.CNT	= len;
		fs_msg.PROC_NR	= src;
		assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
        // 向设备驱动程序发送文件系统消息，并接收回复
		send_recv(BOTH, dd_map[MAJOR(dev)].driver_nr, &fs_msg);
        // 断言文件系统消息的计数字段等于用户要求读/写的字节数
		assert(fs_msg.CNT == len);

        // 返回已读/写的字节数
		return fs_msg.CNT;
	}
	else {
        // 断言文件类型是常规文件（I_REGULAR）或目录（I_DIRECTORY）
		assert(pin->i_mode == I_REGULAR || pin->i_mode == I_DIRECTORY);
		assert((fs_msg.type == READ) || (fs_msg.type == WRITE));

		int pos_end;
        // 对于读操作，结束位置是当前位置加上要读的字节数，
        // 和文件大小中的较小值
		if (fs_msg.type == READ)
			pos_end = min(pos + len, pin->i_size);

        // 对于写操作，结束位置是当前位置加上要写的字节数，
        // 和文件扇区数乘以扇区大小中的较小值
		else		/* WRITE */
			pos_end = min(pos + len, pin->i_nr_sects * SECTOR_SIZE);

		int off = pos % SECTOR_SIZE;
		int rw_sect_min=pin->i_start_sect+(pos>>SECTOR_SIZE_SHIFT);
		int rw_sect_max=pin->i_start_sect+(pos_end>>SECTOR_SIZE_SHIFT);

        // 计算出需要读/写的扇区范围：
        // 确定每次循环读/写的扇区数（chunk）
		int chunk = min(rw_sect_max - rw_sect_min + 1,
				FSBUF_SIZE >> SECTOR_SIZE_SHIFT);
        // 初始化已读/写的字节数（bytes_rw）和剩余字节数（bytes_left）
		int bytes_rw = 0;
		int bytes_left = len;
		int i;
		for (i = rw_sect_min; i <= rw_sect_max; i += chunk) {
			/* read/write this amount of bytes every time */
			int bytes = min(bytes_left, chunk * SECTOR_SIZE - off);
			rw_sector(DEV_READ,
				  pin->i_dev,
				  i * SECTOR_SIZE,
				  chunk * SECTOR_SIZE,
				  TASK_FS,
				  fsbuf);

            // 若操作是读，会从文件系统缓冲区复制数据到用户缓冲区
			if (fs_msg.type == READ) {
				phys_copy((void*)va2la(src, buf + bytes_rw),
					  (void*)va2la(TASK_FS, fsbuf + off),
					  bytes);
			}
            // 若操作是写，会从用户缓冲区复制数据到
            // 文件系统缓冲区，并将数据写回磁盘
			else {	/* WRITE */
				phys_copy((void*)va2la(TASK_FS, fsbuf + off),
					  (void*)va2la(src, buf + bytes_rw),
					  bytes);

				rw_sector(DEV_WRITE,
					  pin->i_dev,
					  i * SECTOR_SIZE,
					  chunk * SECTOR_SIZE,
					  TASK_FS,
					  fsbuf);
			}
            // 每次循环后，会更新已读/写的字节数、文件位置和剩余字节数
			off = 0;
			bytes_rw += bytes;
			pcaller->filp[fd]->fd_pos += bytes;
			bytes_left -= bytes;
		}

        // 如果文件位置超过了当前文件大小，会更新inode
		if (pcaller->filp[fd]->fd_pos > pin->i_size) {
			/* update inode::size */
			pin->i_size = pcaller->filp[fd]->fd_pos;

			/* write the updated i-node back to disk */
			sync_inode(pin);
		}

		return bytes_rw;
	}
}

