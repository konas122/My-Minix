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


PRIVATE struct inode * create_file(char * path, int flags);
PRIVATE int alloc_imap_bit(int dev);
PRIVATE int alloc_smap_bit(int dev, int nr_sects_to_alloc);
PRIVATE struct inode * new_inode(int dev, int inode_nr, int start_sect);
PRIVATE void new_dir_entry(struct inode * dir_inode, int inode_nr, char * filename);


/*****************************************************************************
 *                                do_open
 *****************************************************************************/
/**
 * Open a file and return the file descriptor.
 * 
 * @return File descriptor if successful, otherwise a negative error code.
 *****************************************************************************/
PUBLIC int do_open() {
    int fd = -1;                    // return value

    char pathname[MAX_PATH];

    // get parameters from the message
    int flags = fs_msg.FLAGS;       // access mode
    int name_len = fs_msg.NAME_LEN; // length of filename
    int src = fs_msg.source;        // caller proc nr
    assert(name_len < MAX_PATH);
    phys_copy((void *)va2la(TASK_FS, pathname),
              (void *)va2la(src, fs_msg.PATHNAME),
              name_len);
    pathname[name_len] = 0;

    // find a free slot in PROCESS::filp[]
    int i;
    for (i = 0; i < NR_FILES; i++) {
        if (pcaller->filp[i] == 0) {
            fd = i;
            break;
        }
    }
    if ((fd < 0) || (fd >= NR_FILES))
		panic("filp[] is full (PID:%d)", proc2pid(pcaller));
    
    // find a free slot in f_desc_table[]
    for (i = 0; i < NR_FILE_DESC; i++)
        if (f_desc_table[i].fd_inode == 0)
            break;
    if (i >= NR_FILE_DESC)
		panic("f_desc_table[] is full (PID:%d)", proc2pid(pcaller));

    int inode_nr = search_file(pathname);

    struct inode *pin = 0;
    if (flags & O_CREAT) {
		if (inode_nr) {
			printl("file exists.\n");
			return -1;
		}
		else {
			pin = create_file(pathname, flags);
		}
	}
    else {
        assert(flags & O_RDWR);

        char filename[MAX_PATH];
        struct inode *dir_inode;
        if (strip_path(filename, pathname, &dir_inode) != 0)
            return -1;
        pin = get_inode(dir_inode->i_dev, inode_nr);
    }
    if (pin) {
		/* connects proc with file_descriptor */
		pcaller->filp[fd] = &f_desc_table[i];

		/* connects file_descriptor with inode */
		f_desc_table[i].fd_inode = pin;

		f_desc_table[i].fd_mode = flags;
		/* f_desc_table[i].fd_cnt = 1; */
		f_desc_table[i].fd_pos = 0;

		int imode = pin->i_mode & I_TYPE_MASK;

		if (imode == I_CHAR_SPECIAL) {
			MESSAGE driver_msg;

			driver_msg.type = DEV_OPEN;
			int dev = pin->i_start_sect;
			driver_msg.DEVICE = MINOR(dev);
			assert(MAJOR(dev) == 4);
			assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);

			send_recv(BOTH,
				  dd_map[MAJOR(dev)].driver_nr,
				  &driver_msg);
		}
		else if (imode == I_DIRECTORY) {
			assert(pin->i_num == ROOT_INODE);
		}
		else {
			assert(pin->i_mode == I_REGULAR);
		}        
    }
    else {
        return -1;
    }

    return fd;
}
/**
 *  `open()`要返回的是进程表`filp[]`内的一个索引，所以一开始我们就在`filp[]`内寻找一个空项，
 * 用来存放即将打开的文件的fd。由于filp[]内只保存指针，所以我们还要从`f_desc_table[]`中找一个空项。
 * 
 *  这两项工作完成后，就调用`search_file()`来查看打开的文件是否已存在。
 *  若不存在，则调用`create_file()`。
 * 
 *  在`do_open()`中，调用`create_file()`之后文件其实已经创建完毕了，
 * 之所以函数还没立刻结束，是因为创建完后还要进行“打开”操作。
 *  上面代码中的`if(pin)`条件判断成立后，紧跟着就是连接进程表、`f_desc_table[]`和`inode_table[]`。
 * 这三步完成后，文件就能认为是被打开了。
 *  
 *  接着就是对文件属性进行判断，如果是字符设备特殊文件，就交给相应的驱动程序（这由文件的设备号决定）。
*/



/*****************************************************************************
 *                                create_file
 *****************************************************************************/
/**
 * Create a file and return it's inode ptr.
 *
 * @param[in] path   The full path of the new file
 * @param[in] flags  Attribiutes of the new file
 *
 * @return           Ptr to i-node of the new file if successful, otherwise 0.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PRIVATE struct inode * create_file(char * path, int flags) {
    char filename[MAX_PATH];
    struct inode *dir_inode;

    if (strip_path(filename, path, &dir_inode) != 0)
        return 0;

    int inode_nr = alloc_imap_bit(dir_inode->i_dev);

    int free_sect_nr = alloc_smap_bit(dir_inode->i_dev, NR_DEFAULT_FILE_SECTS);

    struct inode *newino = new_inode(dir_inode->i_dev, inode_nr, free_sect_nr);

    new_dir_entry(dir_inode, newino->i_num, filename);

    return newino;
}
/**
 * 一个文件在文件系统中涉及的要素有五个：
 *  - 文件内容（数据）所占用的扇区
 *  - i-node
 *  - i-node在inode-map中占用的一位
 *  - 数据扇区在sector-map中占用的一位或多位
 *  - 文件在目录中占有的目录项（dir entry）
 * 
 * 相应地，我们创建一个文件，需要做以下几项工作：
 *  - 文件内容（数据）分配扇区               —— `alloc_smap_bit()`
 *  - 在inode_array中分配一个i-node         —— `new_inode()`
 *  - 在inode-map中分配一个i-node           —— `alloc_imap_bit()`
 *  - 在sector-map中分配一位或多位           —— `alloc_smap_bit()`
 *  - 在相应目录中写入一个目录项（dir entry） —— `new_dir_entry() `
*/



/*****************************************************************************
 *                                do_close
 *****************************************************************************/
/**
 * Handle the message CLOSE.
 * 
 * @return Zero if success.
 *****************************************************************************/
PUBLIC int do_close() {
    int fd = fs_msg.FD;
    put_inode(pcaller->filp[fd]->fd_inode);
    pcaller->filp[fd]->fd_inode = 0;
    pcaller->filp[fd] = 0;

    return 0;
}


/*****************************************************************************
 *                                do_lseek
 *****************************************************************************/
/**
 * Handle the message LSEEK.
 * 
 * @return The new offset in bytes from the beginning of the file if successful,
 *         otherwise a negative number.
 *****************************************************************************/
PUBLIC int do_lseek() {
    int fd = fs_msg.FD;
    int off = fs_msg.OFFSET;
    int whence = fs_msg.WHENCE;

    int pos = pcaller->filp[fd]->fd_pos;
    int f_size = pcaller->filp[fd]->fd_inode->i_size;

	switch (whence) {
        case SEEK_SET:
            pos = off;
            break;
        case SEEK_CUR:
            pos += off;
            break;
        case SEEK_END:
            pos = f_size + off;
            break;
        default:
            return -1;
            break;
	}
	if ((pos > f_size) || (pos < 0)) {
		return -1;
	}
	pcaller->filp[fd]->fd_pos = pos;
	return pos;    
}



/*****************************************************************************
 *                                alloc_imap_bit
 *****************************************************************************/
/**
 * Allocate a bit in inode-map.
 * 
 * @param dev  In which device the inode-map is located.
 * 
 * @return  I-node nr.
 *****************************************************************************/
PRIVATE int alloc_imap_bit(int dev) {
    int inode_nr = 0;
    int i, j, k;

    int imap_blk0_nr = 1 + 1;   /* 1 boot sector & 1 super block */
    struct super_block *sb = get_super_block(dev);

    for (i = 0; i < sb->nr_imap_sects; i++) {
        RD_SECT(dev, imap_blk0_nr + i);

        for (j = 0; j < SECTOR_SIZE; j++) {
            			/* skip `11111111' bytes */
			if (fsbuf[j] == 0xFF)
				continue;

			/* skip `1' bits */
			for (k = 0; ((fsbuf[j] >> k) & 1) != 0; k++) {}

			/* i: sector index; j: byte index; k: bit index */
			inode_nr = (i * SECTOR_SIZE + j) * 8 + k;
			fsbuf[j] |= (1 << k);

			/* write the bit to imap */
			WR_SECT(dev, imap_blk0_nr + i);
			break;
        }

        return inode_nr;
    }

    /* no free bit in imap */
	panic("inode-map is probably full.\n");

	return 0;
}

/*****************************************************************************
 *                                alloc_smap_bit
 *****************************************************************************/
/**
 * Allocate a bit in sector-map.
 *
 * @param dev  In which device the sector-map is located.
 * @param nr_sects_to_alloc  How many sectors are allocated.
 *
 * @return  The 1st sector nr allocated.
 *****************************************************************************/
PRIVATE int alloc_smap_bit(int dev, int nr_sects_to_alloc) {
    /* int nr_sects_to_alloc = NR_DEFAULT_FILE_SECTS; */

	int i; /* sector index */
	int j; /* byte index */
	int k; /* bit index */

    struct super_block *sb = get_super_block(dev);

    int smap_blk0_nr = 1 + 1 + sb->nr_imap_sects;
    int free_sect_nr = 0;

    /* smap_blk0_nr + i : current sect nr */
    for (i = 0; i < sb->nr_smap_sects; i++) {   
        RD_SECT(dev, smap_blk0_nr + i);

        /* byte offset in current sect */
        for (j = 0; j < SECTOR_SIZE && nr_sects_to_alloc > 0; j++) {
            k = 0;
            if (!free_sect_nr) {
                /* loop until a free bits is found */
                if (fsbuf[j] == 0xFF) continue;
                for (; ((fsbuf[j] >> k) & 1) != 0; k++) {}
                free_sect_nr = (i * SECTOR_SIZE + j) * 8 +
                               k - 1 + sb->n_1st_sect;
            }

            for (; k < 8; k++) {    // repeat till enough bits are set
                assert(((fsbuf[j] >> k) & 1) == 0);
                fsbuf[j] |= (1 << k);
                if (--nr_sects_to_alloc == 0)
                    break;
            }
        }

		if (free_sect_nr)       /* free bit found, write the bits to smap */
			WR_SECT(dev, smap_blk0_nr + i);

		if (nr_sects_to_alloc == 0)
			break;
    }
    assert(nr_sects_to_alloc == 0);

    return free_sect_nr;
}
/**
 *  `alloc_imap_bit()`和`alloc_smap_bit()`都是对位图的操作，代码比较相似。
 *  在`sector-map`中分配扇区时，我们没考虑太多别的情况。换句话说，我们一旦找到一个未使用的扇区，
 * 就认为以它为开头存在连续`nr_sects_to_alloc`个扇区。（所以这代码乍一看感觉挺不合逻辑的）
 * 
 *  这种假设是有道理的，一方面，参数`nr_sects_to_alloc`应该始终为`NR_DEFAULT_FILE_SECTS`或者它的倍数；
 * 另一方面，只要我们每次都这样分配磁盘，那么理论上，除非遍历到一块磁盘的结尾处，我们不会遇到一处小于
 * `NR_DEFAULT_FILE_SECTS`的连续扇区。
*/



/*****************************************************************************
 *                                new_inode
 *****************************************************************************/
/**
 * Generate a new i-node and write it to disk.
 * 
 * @param dev  Home device of the i-node.
 * @param inode_nr  I-node nr.
 * @param start_sect  Start sector of the file pointed by the new i-node.
 * 
 * @return  Ptr of the new i-node.
 *****************************************************************************/
PRIVATE struct inode * new_inode(int dev, int inode_nr, int start_sect) 
{
	struct inode * new_inode = get_inode(dev, inode_nr);

	new_inode->i_mode = I_REGULAR;
	new_inode->i_size = 0;
	new_inode->i_start_sect = start_sect;
	new_inode->i_nr_sects = NR_DEFAULT_FILE_SECTS;

	new_inode->i_dev = dev;
	new_inode->i_cnt = 1;
	new_inode->i_num = inode_nr;

	/* write to the inode array */
	sync_inode(new_inode);

	return new_inode;    
}


/*****************************************************************************
 *                                new_dir_entry
 *****************************************************************************/
/**
 * Write a new entry into the directory.
 * 
 * @param dir_inode  I-node of the directory.
 * @param inode_nr   I-node nr of the new file.
 * @param filename   Filename of the new file.
 *****************************************************************************/
PRIVATE void new_dir_entry(struct inode *dir_inode,int inode_nr,char *filename) {
    /* write the dir_entry */
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE) / SECTOR_SIZE;
	int nr_dir_entries =
		dir_inode->i_size / DIR_ENTRY_SIZE; /**
						     * including unused slots
						     * (the file has been
						     * deleted but the slot
						     * is still there)
						     */
	int m = 0;
	struct dir_entry * pde;
	struct dir_entry * new_de = 0;

	int i, j;
    for (i = 0; i < nr_dir_blks; i++) {
        RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);

        pde = (struct dir_entry *)fsbuf;
        for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++, pde++) {
            if (++m > nr_dir_entries)
                break;
            
            if (pde->inode_nr == 0) {   // it's a free slot
                new_de = pde;
                break;
            }
        }
        if (m > nr_dir_entries ||/* all entries have been iterated or */
		    new_de)              /* free slot is found */
			break;
    }
	if (!new_de) { /* reached the end of the dir */
		new_de = pde;
		dir_inode->i_size += DIR_ENTRY_SIZE;
	}
	new_de->inode_nr = inode_nr;
    strcpy(new_de->name, filename);

	/* write dir block -- ROOT dir block */
	WR_SECT(dir_inode->i_dev, dir_blk0_nr + i);

	/* update dir inode */
	sync_inode(dir_inode);    
}

