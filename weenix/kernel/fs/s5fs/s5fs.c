/*
 *   FILE: s5fs.c
 * AUTHOR: afenn
 *  DESCR: S5FS entry points
 */

#include "kernel.h"
#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "proc/kmutex.h"

#include "fs/s5fs/s5fs_subr.h"
#include "fs/s5fs/s5fs.h"
#include "fs/dirent.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/stat.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"

#include "mm/kmalloc.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"

/* Diagnostic/Utility: */
static int s5_check_super(s5_super_t *super);
static int s5fs_check_refcounts(fs_t *fs);

/* fs_t entry points: */
static void s5fs_read_vnode(vnode_t *vnode);
static void s5fs_delete_vnode(vnode_t *vnode);
static int  s5fs_query_vnode(vnode_t *vnode);
static int  s5fs_umount(fs_t *fs);

/* vnode_t entry points: */
static int  s5fs_read(vnode_t *vnode, off_t offset, void *buf, size_t len);
static int  s5fs_write(vnode_t *vnode, off_t offset, const void *buf, size_t len);
static int  s5fs_mmap(vnode_t *file, vmarea_t *vma, mmobj_t **ret);
static int  s5fs_create(vnode_t *vdir, const char *name, size_t namelen, vnode_t **result);
static int  s5fs_mknod(struct vnode *dir, const char *name, size_t namelen, int mode, devid_t devid);
static int  s5fs_lookup(vnode_t *base, const char *name, size_t namelen, vnode_t **result);
static int  s5fs_link(vnode_t *src, vnode_t *dir, const char *name, size_t namelen);
static int  s5fs_unlink(vnode_t *vdir, const char *name, size_t namelen);
static int  s5fs_mkdir(vnode_t *vdir, const char *name, size_t namelen);
static int  s5fs_rmdir(vnode_t *parent, const char *name, size_t namelen);
static int  s5fs_readdir(vnode_t *vnode, int offset, struct dirent *d);
static int  s5fs_stat(vnode_t *vnode, struct stat *ss);
static int  s5fs_fillpage(vnode_t *vnode, off_t offset, void *pagebuf);
static int  s5fs_dirtypage(vnode_t *vnode, off_t offset);
static int  s5fs_cleanpage(vnode_t *vnode, off_t offset, void *pagebuf);

fs_ops_t s5fs_fsops = {
        s5fs_read_vnode,
        s5fs_delete_vnode,
        s5fs_query_vnode,
        s5fs_umount
};

/* vnode operations table for directory files: */
static vnode_ops_t s5fs_dir_vops = {
        .read = NULL,
        .write = NULL,
        .mmap = NULL,
        .create = s5fs_create,
        .mknod = s5fs_mknod,
        .lookup = s5fs_lookup,
        .link = s5fs_link,
        .unlink = s5fs_unlink,
        .mkdir = s5fs_mkdir,
        .rmdir = s5fs_rmdir,
        .readdir = s5fs_readdir,
        .stat = s5fs_stat,
        .fillpage = s5fs_fillpage,
        .dirtypage = s5fs_dirtypage,
        .cleanpage = s5fs_cleanpage
};

/* vnode operations table for regular files: */
static vnode_ops_t s5fs_file_vops = {
        .read = s5fs_read,
        .write = s5fs_write,
        .mmap = s5fs_mmap,
        .create = NULL,
        .mknod = NULL,
        .lookup = NULL,
        .link = NULL,
        .unlink = NULL,
        .mkdir = NULL,
        .rmdir = NULL,
        .readdir = NULL,
        .stat = s5fs_stat,
        .fillpage = s5fs_fillpage,
        .dirtypage = s5fs_dirtypage,
        .cleanpage = s5fs_cleanpage
};

/*
 * Read fs->fs_dev and set fs_op, fs_root, and fs_i.
 *
 * Point fs->fs_i to an s5fs_t*, and initialize it.  Be sure to
 * verify the superblock (using s5_check_super()).  Use vget() to get
 * the root vnode for fs_root.
 *
 * Return 0 on success, negative on failure.
 */
int
s5fs_mount(struct fs *fs)
{
        int num;
        blockdev_t *dev;
        s5fs_t *s5;
        pframe_t *vp;

        KASSERT(fs);

        if (sscanf(fs->fs_dev, "disk%d", &num) != 1) {
                return -EINVAL;
        }

        if (!(dev = blockdev_lookup(MKDEVID(1, num)))) {
                return -EINVAL;
        }

        /* allocate and initialize an s5fs_t: */
        s5 = (s5fs_t *)kmalloc(sizeof(s5fs_t));

        if (!s5)
                return -ENOMEM;

        /*     init s5f_disk: */
        s5->s5f_bdev  = dev;

        /*     init s5f_super: */
        pframe_get(S5FS_TO_VMOBJ(s5), S5_SUPER_BLOCK, &vp);

        KASSERT(vp);

        s5->s5f_super = (s5_super_t *)(vp->pf_addr);

        if (s5_check_super(s5->s5f_super)) {
                /* corrupt */
                kfree(s5);
                return -EINVAL;
        }

        pframe_pin(vp);

        /*     init s5f_mutex: */
        kmutex_init(&s5->s5f_mutex);

        /*     init s5f_fs: */
        s5->s5f_fs = fs;


        /* Init the members of fs that we (the fs-implementation) are
         * responsible for initializing: */
        fs->fs_i = s5;
        fs->fs_op = &s5fs_fsops;
        fs->fs_root = vget(fs, s5->s5f_super->s5s_root_inode);
        
        return 0;
}

/* Implementation of fs_t entry points: */

/*
 * MACROS
 *
 * There are several macros which we have defined for you that
 * will make your life easier. Go find them, and use them.
 * Hint: Check out s5fs(_subr).h
 */


/*
 * See the comment in vfs.h for what is expected of this function.
 *
 * When this function returns, the inode link count should be incremented.
 * Note that most UNIX filesystems don't do this, they have a separate
 * flag to indicate that the VFS is using a file. However, this is
 * simpler to implement.
 *
 * To get the inode you need to use pframe_get then use the pf_addr
 * and the S5_INODE_OFFSET(vnode) to get the inode
 *
 * Don't forget to update linkcounts and pin the page.
 *
 * Note that the devid is stored in the indirect_block in the case of
 * a char or block device
 *
 * Finally, the main idea is to do special initialization based on the
 * type of inode (i.e. regular, directory, char/block device, etc').
 *
 */

/* vn_fs and vn_vno as been filled */

static void
s5fs_read_vnode(vnode_t *vnode)
{

    KASSERT(vnode != NULL);
    s5fs_t* s5 = VNODE_TO_S5FS(vnode);
    struct mmobj* s5obj = S5FS_TO_VMOBJ(s5);

    /*  get the content of inode */
    int inode_block = S5_INODE_BLOCK( vnode -> vn_vno);
    int inode_offset = S5_INODE_OFFSET( vnode -> vn_vno);
    
    /* the the frame of inode */
    pframe_t* inode_frame;
    pframe_get(s5obj, inode_block, &inode_frame);
    pframe_pin(inode_frame);

    /* read inode the structure out */
    s5_inode_t* inode = (s5_inode_t*)inode_frame -> pf_addr + inode_offset;
    
    inode -> s5_linkcount ++;
    

    /*  initialization */
    uint16_t type = inode -> s5_type;
    if(type == S5_TYPE_DATA){
        vnode -> vn_ops = &s5fs_file_vops;
        vnode -> vn_mode = S_IFREG;
    }
    else if(type == S5_TYPE_DIR){
        vnode -> vn_ops = &s5fs_dir_vops;
        vnode -> vn_mode = S_IFDIR;
    }
    else if(type == S5_TYPE_CHR){
        devid_t dev =  inode -> s5_indirect_block;
        vnode -> vn_devid = dev;
        vnode -> vn_cdev = bytedev_lookup(dev);
        vnode -> vn_mode = S_IFCHR;

    }
    else if(type == S5_TYPE_BLK){
        devid_t dev = inode -> s5_indirect_block;
        vnode -> vn_devid = dev;
        vnode -> vn_bdev =  blockdev_lookup(dev);
        vnode -> vn_mode = S_IFBLK;
    }

    vnode -> vn_len = inode -> s5_size;
    vnode -> vn_i = inode;
    
    s5_dirty_inode(s5,inode);
}

/*
 * See the comment in vfs.h for what is expected of this function.
 *
 * When this function returns, the inode refcount should be decremented.
 *
 * You probably want to use s5_free_inode() if there are no more links to
 * the inode, and dont forget to unpin the page
 */
static void
s5fs_delete_vnode(vnode_t *vnode)
{

    /*  get the inode  */   
    KASSERT(vnode != NULL);
    s5fs_t* s5 = VNODE_TO_S5FS(vnode);
    struct mmobj* s5obj = S5FS_TO_VMOBJ(s5);

    /*  get the content of inode */
    int inode_block = S5_INODE_BLOCK( vnode -> vn_vno);
    int inode_offset = S5_INODE_OFFSET( vnode -> vn_vno);
    
    /* the the frame of inode */
    pframe_t* inode_frame;
    pframe_get(s5obj, inode_block, &inode_frame);
    pframe_unpin(inode_frame);
    s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
    
    inode -> s5_linkcount --;
    
    if(inode -> s5_linkcount == 0){
        s5_free_inode(vnode);
    }

        
    s5_dirty_inode(s5,inode);
}

/*
 * See the comment in vfs.h for what is expected of this function.
 *
 * The vnode still exists on disk if it has a linkcount greater than 1.
 * (Remember, VFS takes a reference on the inode as long as it uses it.)
 *
 */
static int
s5fs_query_vnode(vnode_t *vnode)
{
    s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
    if(inode -> s5_linkcount >  1){
        return 1;
    }
    else
        return 0;
}

/*
 * s5fs_check_refcounts()
 * vput root vnode
 */
static int
s5fs_umount(fs_t *fs)
{
        s5fs_t *s5 = (s5fs_t *)fs->fs_i;
        blockdev_t *bd = s5->s5f_bdev;
        pframe_t *sbp;
        int ret;

        if (s5fs_check_refcounts(fs)) {
                dbg(DBG_PRINT, "s5fs_umount: WARNING: linkcount corruption "
                    "discovered in fs on block device with major %d "
                    "and minor %d!!\n", MAJOR(bd->bd_id), MINOR(bd->bd_id));
        }
        if (s5_check_super(s5->s5f_super)) {
                dbg(DBG_PRINT, "s5fs_umount: WARNING: corrupted superblock "
                    "discovered on fs on block device with major %d "
                    "and minor %d!!\n", MAJOR(bd->bd_id), MINOR(bd->bd_id));
        }

        vnode_flush_all(fs);

        vput(fs->fs_root);

        if (0 > (ret = pframe_get(S5FS_TO_VMOBJ(s5), S5_SUPER_BLOCK, &sbp))) {
                panic("s5fs_umount: failed to pframe_get super block. "
                      "This should never happen (the page should already "
                      "be resident and pinned, and even if it wasn't, "
                      "block device readpage entry point does not "
                      "fail.\n");
        }

        KASSERT(sbp);

        pframe_unpin(sbp);

        kfree(s5);

        blockdev_flush_all(bd);

        return 0;
}




/* Implementation of vnode_t entry points: */

/*
 * Unless otherwise mentioned, these functions should leave all refcounts net
 * unchanged.
 */

/*
 * You will need to lock the vnode's mutex before doing anything that can block.
 * pframe functions can block, so probably what you want to do
 * is just lock the mutex in the s5fs_* functions listed below, and then not
 * worry about the mutexes in s5fs_subr.c.
 *
 * Note that you will not be calling pframe functions directly, but
 * s5fs_subr.c functions will be, so you need to lock around them.
 *
 * DO NOT TRY to do fine grained locking your first time through,
 * as it will break, and you will cry.
 *
 * Finally, you should read and understand the basic overview of
 * the s5fs_subr functions. All of the following functions might delegate,
 * and it will make your life easier if you know what is going on.
 */


/* Simply call s5_read_file. */
static int
s5fs_read(vnode_t *vnode, off_t offset, void *buf, size_t len)
{
    KASSERT(vnode != NULL);
    kmutex_lock(&vnode -> vn_mutex);
    int res = s5_read_file(vnode, offset, buf, len);
    kmutex_unlock(&vnode -> vn_mutex);
    return res;
}

/* Simply call s5_write_file. */
static int
s5fs_write(vnode_t *vnode, off_t offset, const void *buf, size_t len)
{
    KASSERT(vnode != NULL);
    kmutex_lock(&vnode -> vn_mutex);
    int res = s5_write_file(vnode, offset, buf, len);
    kmutex_unlock(&vnode -> vn_mutex);
    return res;

}

/* This function is deceptivly simple, just return the vnode's
 * mmobj_t through the ret variable. Remember to watch the
 * refcount.
 *
 * Don't worry about this until VM.
 */
static int
s5fs_mmap(vnode_t *file, vmarea_t *vma, mmobj_t **ret)
{
    return 0;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * When this function returns, the inode refcount of the file should be 2
 * and the vnode refcount should be 1.
 *
 * You probably want to use s5_alloc_inode(), s5_link(), and vget().
 */
static int
s5fs_create(vnode_t *dir, const char *name, size_t namelen, vnode_t **result)
{
    KASSERT(dir != NULL);
    KASSERT(name != NULL);

    kmutex_lock(&dir->vn_mutex);   
    fs_t* fs = dir -> vn_fs;
    
    /*  whether the dir has been filled? */
    if(dir->vn_len >= (int)S5_MAX_FILE_SIZE){
        kmutex_unlock(&dir -> vn_mutex);
        return -ENOSPC;
    }

    /*  has created a new inode for the file */
    int new_ino = s5_alloc_inode(fs, S5_TYPE_DATA, 0);
    if(new_ino < 0){
        kmutex_unlock(&dir->vn_mutex);
        return new_ino;
    }

    /*  get the child vnode */
    vnode_t* new_vnode = vget(fs, new_ino);
    s5_inode_t* new_inode = VNODE_TO_S5INODE(new_vnode);
    /* s5_link will increase the linkcount of the child */
    int res = s5_link(dir, new_vnode, name, namelen);
    if(res < 0){
        kmutex_unlock(&dir -> vn_mutex);
        return res;
    }
    *result = new_vnode;
    kmutex_unlock(&dir -> vn_mutex);
    return 0;
}


/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * This function is similar to s5fs_create, but it creates a special
 * file specified by 'devid'.
 *
 * You probably want to use s5_alloc_inode, s5_link(), vget(), and vput().
 */
static int
s5fs_mknod(vnode_t *dir, const char *name, size_t namelen, int mode, devid_t devid)
{
    KASSERT(dir != NULL);
    KASSERT(name != NULL);
    
    kmutex_lock(&dir->vn_mutex);   

    fs_t* fs = dir -> vn_fs;
    /*  has created a new inode for the file */
    uint16_t type;
    /*  convert mode to type */
    if(mode == S_IFCHR){
        type = S5_TYPE_CHR;
    }
    else if(mode == S_IFBLK){
        type = S5_TYPE_BLK;
    }

    int new_ino = s5_alloc_inode(fs, type, devid);
    if(new_ino < 0){
        kmutex_unlock(&dir->vn_mutex);
        return new_ino;
    }

    /*  get the child vnode */
    vnode_t* new_vnode = vget(fs, new_ino);
    s5_inode_t* new_inode = VNODE_TO_S5INODE(new_vnode);
    int res = s5_link(dir, new_vnode, name, namelen);
    if(res < 0){
        kmutex_unlock(&dir->vn_mutex);   
        return res;
    }
    
    vput(new_vnode);
    kmutex_unlock(&dir -> vn_mutex);

    return 0;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * You probably want to use s5_find_dirent() and vget().
 */
int
s5fs_lookup(vnode_t *base, const char *name, size_t namelen, vnode_t **result)
{
    KASSERT(base != NULL);
    KASSERT(name != NULL);
   
    kmutex_lock(&base -> vn_mutex);
    fs_t* fs = base -> vn_fs;

    int ino = s5_find_dirent(base, name ,namelen);
    if(ino < 0){
        kmutex_unlock(&base -> vn_mutex);
        return ino;
    }
    vnode_t* vnode = vget(fs, ino);
    kmutex_unlock(&base -> vn_mutex);
    *result = vnode;
    return 0;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * When this function returns, the inode refcount of the linked file
 * should be incremented.
 *
 * You probably want to use s5_link().
 */
static int
s5fs_link(vnode_t *src, vnode_t *dir, const char *name, size_t namelen)
{
    KASSERT(src != NULL);
    KASSERT(dir != NULL);
    KASSERT(name != NULL);
    
    kmutex_lock(&dir->vn_mutex);

    if(dir -> vn_len >= (int) S5_MAX_FILE_SIZE){
        kmutex_unlock(&dir->vn_mutex);
        return -ENOSPC;
    }
    int res = s5_link(dir, src, name, namelen);
    kmutex_unlock(&dir->vn_mutex);

    return res;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * When this function returns, the inode refcount of the unlinked file
 * should be decremented.
 *
 * You probably want to use s5_remove_dirent().
 */
static int
s5fs_unlink(vnode_t *dir, const char *name, size_t namelen)
{
        KASSERT(dir != NULL);
        KASSERT(name != NULL);
        kmutex_lock(&dir-> vn_mutex);
        int res = s5_remove_dirent(dir, name, namelen);
        kmutex_unlock(&dir->vn_mutex);
        return res;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * You need to create the "." and ".." directory entries in the new
 * directory. These are simply links to the new directory and its
 * parent.
 *
 * When this function returns, the inode refcount on the parent should
 * be incremented, and the inode refcount on the new directory should be
 * 1. It might make more sense for the inode refcount on the new
 * directory to be 2 (since "." refers to it as well as its entry in the
 * parent dir), but convention is that empty directories have only 1
 * link.
 *
 * You probably want to use s5_alloc_inode, and s5_link().
 *
 * Assert, a lot.
 */
static int
s5fs_mkdir(vnode_t *dir, const char *name, size_t namelen)
{
        KASSERT(dir != NULL);
        KASSERT(name != NULL);

        if(dir->vn_len >= (int) S5_MAX_FILE_SIZE){
            return -ENOSPC;
        }

        /* create a new directory inode */
        kmutex_lock(&dir->vn_mutex);  
        s5_inode_t* dir_inode = VNODE_TO_S5INODE(dir);
        fs_t* fs = dir -> vn_fs;
        
        int ino = s5_alloc_inode(fs, S5_TYPE_DIR, 0);
        if(ino < 0){
            kmutex_unlock(&dir->vn_mutex);
            return ino;
        }
        
        /* write entries to the new dir */
        vnode_t* new_dir_vnode = vget(fs, ino);
        s5_inode_t* new_dir_inode = VNODE_TO_S5INODE(new_dir_vnode);
        /* construct the two entries */
        /* end1 for '.' */
        int res = s5_link(new_dir_vnode, new_dir_vnode, ".", 1);
        if(res < 0){
            kmutex_unlock(&dir -> vn_mutex);
            return res;
        }
        
        /*  ent2 for '..' */
        res = s5_link(new_dir_vnode, dir, "..", 2);
        if(res < 0){
            kmutex_unlock(&dir -> vn_mutex);
            return res;
        }
        
        /*  link the new dir into the parent */
        res = s5_link(dir, new_dir_vnode, name, namelen);
        if(res < 0){
            kmutex_unlock(&dir -> vn_mutex);
            return res;
        }
        
        new_dir_inode -> s5_linkcount --; 
       /*   s5_dirty_inode(VNODE_TO_S5FS(dir), dir_inode);
        s5_dirty_inode(VNODE_TO_S5FS(dir), new_dir_inode); */
        /* dirty the disk block */
        
        kmutex_unlock(&dir->vn_mutex);
        vput(new_dir_vnode);
        return 0;
}

/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * When this function returns, the inode refcount on the parent should be
 * decremented (since ".." in the removed directory no longer references
 * it). Remember that the directory must be empty (except for "." and
 * "..").
 *
 * You probably want to use s5_find_dirent() and s5_remove_dirent().
 */
static int
s5fs_rmdir(vnode_t *parent, const char *name, size_t namelen)
{
        KASSERT(parent != NULL);
        kmutex_lock(&parent -> vn_mutex);
        s5_inode_t* parent_inode = VNODE_TO_S5INODE(parent);
        fs_t* fs = parent -> vn_fs;
        
        /* confirm it has */
        int target_ino = s5_find_dirent(parent, name, namelen);
        if(target_ino < 0){
            kmutex_unlock(&parent -> vn_mutex);
            return -ENOENT;
        }

        /* actully remove..*/
        vnode_t* target_vnode = vget(fs, target_ino);
        KASSERT(target_vnode != NULL);

        /*  some tests */
        if(target_vnode -> vn_mode != S_IFDIR){
            vput(target_vnode);
            kmutex_unlock(&parent->vn_mutex);
            return -ENOTDIR;
        }
        
        if(target_vnode -> vn_len != 64){
            vput(target_vnode);
            kmutex_unlock(&parent -> vn_mutex);
            return -ENOTEMPTY;
        }

        int res = s5_remove_dirent(parent, name , namelen);
        if(res < 0){
            kmutex_unlock(&parent -> vn_mutex);
            return res;
        }
        
        s5_inode_t* target_inode = VNODE_TO_S5INODE(target_vnode);
        KASSERT(target_vnode -> vn_len == 2 * sizeof(s5_dirent_t));
        vput(target_vnode);
        parent_inode -> s5_linkcount --;
        kmutex_unlock(&parent -> vn_mutex);
        return 0;
}


/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * Here you need to use s5_read_file() to read a s5_dirent_t from a directory
 * and copy that data into the given dirent. The value of d_off is dependent on
 * your implementation and may or may not b e necessary.  Finally, return the
 * number of bytes read.
 */
static int
s5fs_readdir(vnode_t *vnode, off_t offset, struct dirent *d)
{
        KASSERT(vnode != NULL);
        KASSERT(d != NULL);
        
        kmutex_lock( &vnode -> vn_mutex);

        if(offset == vnode -> vn_len){
            kmutex_unlock(&vnode -> vn_mutex);
            return 0;
        }
        
        KASSERT(vnode -> vn_mode == S_IFDIR);
        KASSERT(offset % sizeof(s5_dirent_t) == 0);
        
        s5_dirent_t read_dirent;
        int res = s5_read_file(vnode, offset,(char*) &read_dirent, sizeof(s5_dirent_t));
        if(res != sizeof(s5_dirent_t)){
            kmutex_unlock(&vnode -> vn_mutex);
            return -1;
        }
        
        strcpy(d->d_name, read_dirent.s5d_name);
        d-> d_ino = read_dirent. s5d_inode;
        d->d_off = offset;

        kmutex_unlock(&vnode -> vn_mutex);
    
        return sizeof(s5_dirent_t);
}


/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * Don't worry if you don't know what some of the fields in struct stat
 * mean. The ones you should be sure to set are st_mode, st_ino,
 * st_nlink, st_size, st_blksize, and st_blocks.
 *
 * You probably want to use s5_inode_blocks().
 */
static int
s5fs_stat(vnode_t *vnode, struct stat *ss)
{
        KASSERT(vnode != NULL);
        KASSERT(ss != NULL);
        kmutex_lock(&vnode -> vn_mutex);
        s5_inode_t* inode = VNODE_TO_S5INODE( vnode );
        ss -> st_mode = vnode -> vn_mode;
        ss -> st_ino = inode -> s5_number;
        ss -> st_nlink = inode -> s5_linkcount;
        ss -> st_size = inode -> s5_size;
        ss -> st_blksize = S5_BLOCK_SIZE;
        ss -> st_blocks = s5_inode_blocks(vnode);
        kmutex_unlock(&vnode -> vn_mutex);
        return 0;
}


/*
 * See the comment in vnode.h for what is expected of this function.
 *
 * You'll probably want to use s5_seek_to_block and the device's
 * read_block function.
 */
static int
s5fs_fillpage(vnode_t *vnode, off_t offset, void *pagebuf)
{
    KASSERT(vnode != NULL);
    int block_num = s5_seek_to_block(vnode, offset, 0);
    int res = 0;
    if(block_num < 0){
        return block_num;
    }
    else if(block_num == 0){
        memset(pagebuf, 0, PAGE_SIZE);
    }
    else{
        s5fs_t *s5 = VNODE_TO_S5FS(vnode);
        blockdev_t* blockdev = s5 -> s5f_bdev;
        res = blockdev -> bd_ops -> read_block(blockdev, pagebuf, block_num, 1);
    }
    return res;
}


/*
 * if this offset is NOT within a sparse region of the file
 *     return 0;
 *
 * attempt to make the region containing this offset no longer
 * sparse
 *     - attempt to allocate a free block
 *     - if no free blocks available, return -ENOSPC
 *     - associate this block with the inode; alter the inode as
 *       appropriate
 *         - dirty the page containing this inode
 *
 * Much of this can be done with s5_seek_to_block()
 */
static int
s5fs_dirtypage(vnode_t *vnode, off_t offset)
{
        KASSERT(vnode != NULL);
        int res = s5_seek_to_block(vnode, offset, 0);
        if(res < 0) return res;

        if(res == 0){
            /* sparse block */
            /*  attempt to allocate a free block */
            res = s5_seek_to_block(vnode, offset , 1);
            if(res < 0){
                return -ENOSPC;
            }
            s5_dirty_inode(VNODE_TO_S5FS(vnode), VNODE_TO_S5INODE(vnode));
        }
        else{
            /*  What makes me suffer!!! */
            /*  
            s5fs_t* s5 = VNODE_TO_S5FS(vnode);
            struct mmobj *s5obj = S5FS_TO_VMOBJ(s5); 
            pframe_t* page_frame;
            res = pframe_get(s5obj, res, &page_frame);
            if(res < 0){
                return res;
            }
            pframe_dirty(page_frame);
            */
            return 0;
        }
        return res;
}

/*
 * Like fillpage, but for writing.
 */
static int
s5fs_cleanpage(vnode_t *vnode, off_t offset, void *pagebuf)
{
    KASSERT(vnode != NULL);
    int block_num = s5_seek_to_block(vnode, offset, 0);
    if(block_num < 0){
        return block_num;
    }
    
    s5fs_t *s5 = VNODE_TO_S5FS(vnode);
    blockdev_t* blockdev = s5 -> s5f_bdev;
    int res = blockdev -> bd_ops -> write_block(blockdev, pagebuf, block_num, 1);
    return res;
}

/* Diagnostic/Utility: */

/*
 * verify the superblock.
 * returns -1 if the superblock is corrupt, 0 if it is OK.
 */
static int
s5_check_super(s5_super_t *super)
{
        if (!(super->s5s_magic == S5_MAGIC
              && (super->s5s_free_inode < super->s5s_num_inodes
                  || super->s5s_free_inode == (uint32_t) - 1)
              && super->s5s_root_inode < super->s5s_num_inodes))
                return -1;
        if (super->s5s_version != S5_CURRENT_VERSION) {
                dbg(DBG_PRINT, "Filesystem is version %d; "
                    "only version %d is supported.\n",
                    super->s5s_version, S5_CURRENT_VERSION);
                return -1;
        }
        return 0;
}

static void
calculate_refcounts(int *counts, vnode_t *vnode)
{
        int ret;

        counts[vnode->vn_vno]++;
        dbg(DBG_S5FS, "calculate_refcounts: Incrementing count of inode %d to"
            " %d\n", vnode->vn_vno, counts[vnode->vn_vno]);
        /*
         * We only consider the children of this directory if this is the
         * first time we have seen it.  Otherwise, we would recurse forever.
         */
        if (counts[vnode->vn_vno] == 1 && S_ISDIR(vnode->vn_mode)) {
                int offset = 0;
                struct dirent d;
                vnode_t *child;

                while (0 < (ret = s5fs_readdir(vnode, offset, &d))) {
                        /*
                         * We don't count '.', because we don't increment the
                         * refcount for this (an empty directory only has a
                         * link count of 1).
                         */
                        if(offset == 160){
                            int a = 1;
                        }
                        if (0 != strcmp(d.d_name, ".")) {
                                child = vget(vnode->vn_fs, d.d_ino);
                                calculate_refcounts(counts, child);
                                vput(child);
                        }
                        offset += ret;
                }

                KASSERT(ret == 0);
        }
}

/*
 * This will check the refcounts for the filesystem.  It will ensure that that
 * the expected number of refcounts will equal the actual number.  To do this,
 * we have to create a data structure to hold the counts of all the expected
 * refcounts, and then walk the fs to calculate them.
 */
int
s5fs_check_refcounts(fs_t *fs)
{
        s5fs_t *s5fs = (s5fs_t *)fs->fs_i;
        int *refcounts;
        int ret = 0;
        uint32_t i;

        refcounts = kmalloc(s5fs->s5f_super->s5s_num_inodes * sizeof(int));
        KASSERT(refcounts);
        memset(refcounts, 0, s5fs->s5f_super->s5s_num_inodes * sizeof(int));

        calculate_refcounts(refcounts, fs->fs_root);
        --refcounts[fs->fs_root->vn_vno]; /* the call on the preceding line
                                           * caused this to be incremented
                                           * not because another fs link to
                                           * it was discovered */

        dbg(DBG_PRINT, "Checking refcounts of s5fs filesystem on block "
            "device with major %d, minor %d\n",
            MAJOR(s5fs->s5f_bdev->bd_id), MINOR(s5fs->s5f_bdev->bd_id));

        for (i = 0; i < s5fs->s5f_super->s5s_num_inodes; i++) {
                vnode_t *vn;

                if (!refcounts[i]) continue;

                vn = vget(fs, i);
                KASSERT(vn);

                if (refcounts[i] != VNODE_TO_S5INODE(vn)->s5_linkcount - 1) {
                        dbg(DBG_PRINT, "   Inode %d, expecting %d, found %d\n", i,
                            refcounts[i], VNODE_TO_S5INODE(vn)->s5_linkcount - 1);
                        ret = -1;
                }
                vput(vn);
        }

        dbg(DBG_PRINT, "Refcount check of s5fs filesystem on block "
            "device with major %d, minor %d completed %s.\n",
            MAJOR(s5fs->s5f_bdev->bd_id), MINOR(s5fs->s5f_bdev->bd_id),
            (ret ? "UNSUCCESSFULLY" : "successfully"));

        kfree(refcounts);
        return ret;
}
