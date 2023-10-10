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
#include "fs.h"

// // #include "hd.h"


/*****************************************************************************
 *                                search_file
 *****************************************************************************/
/**
 * Search the file and return the inode_nr.
 *
 * @param[in] path The full path of the file to search.
 * @return         Ptr to the i-node of the file if successful, otherwise zero.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PUBLIC int search_file(char * path) {
    int i, j;

    char filename[MAX_PATH];
    memset(filename, 0, MAX_FILENAME_LEN);
    struct inode *dir_inode;
    if (strip_path(filename, path, &dir_inode) != 0)
        return 0;
    
    if (filename[0] == 0)   // path: "/"
        return dir_inode->i_num;

	/**
	 * Search the dir for the file.
	 */
    int dir_blk0_nr     = dir_inode->i_start_sect;                              // 文件起始扇区
    int nr_dir_blks     = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;  // 文件占用的扇区数目
    int nr_dir_entries  = dir_inode->i_size / DIR_ENTRY_SIZE;     /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
    int m = 0;
    struct dir_entry *pde;
    for (i = 0; i < nr_dir_blks; i++) {
        RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
        pde = (struct dir_entry *)fsbuf;
        for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++, pde++) {
            if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0)
                return pde->inode_nr;
            if (++m > nr_dir_entries)
                break;
        }
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}

	/* file not found */
	return 0;
}
/**
 *  我们先通过strip_path()来得到文件所在目录的i-node，通过这个i-node来得目录所在的扇区，
*/



/*****************************************************************************
 *                                strip_path
 *****************************************************************************/
/**
 * Get the basename from the fullpath.
 *
 * In KONIX'S FS v1.0, all files are stored in the root directory.
 * There is no sub-folder thing.
 *
 * This routine should be called at the very beginning of file operations
 * such as open(), read() and write(). It accepts the full path and returns
 * two things: the basename and a ptr of the root dir's i-node.
 *
 * e.g. After stip_path(filename, "/blah", ppinode) finishes, we get:
 *      - filename: "blah"
 *      - *ppinode: root_inode
 *      - ret val:  0 (successful)
 *
 * Currently an acceptable pathname should begin with at most one `/'
 * preceding a filename.
 *
 * Filenames may contain any character except '/' and '\\0'.
 *
 * @param[out] filename The string for the result.
 * @param[in]  pathname The full pathname.
 * @param[out] ppinode  The ptr of the dir's inode will be stored here.
 * 
 * @return Zero if success, otherwise the pathname is not valid.
 *****************************************************************************/
PUBLIC int strip_path(char *filename, const char *pathname, struct inode **ppinode) {
    const char *s = pathname;
    char *t = filename;

    if (s == 0)
        return -1;

    if (*s == '/')
        s++;

    while (*s) {    // check each character
        if (*s == '/')
            return -1;
        *t++ = *s++;
        // if filename is too long, just truncate it
        if (t - filename >= MAX_FILENAME_LEN)
            break;
    }
    *t = 0;

    *ppinode = root_inode;
    return 0;
}
/**
 *  该函数调用成功后，filename里面将包含“纯文件名”，即不包含路径的文件名，
 * dir_node这个inode指针将指向文件所在文件夹的i-node。
 * 
 *  总而言之，`strip_path()`的主要作用便是定位直接包含给定文件的文件夹，
 * 并得到给定文件在此文件夹中的名称。
 *  由于当前我们的文件系统是扁平的，所以这个函数返回之后，`dir_inode`指向的
 * 将永远是根目录“/”的i-node。
*/
