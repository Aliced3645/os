/*
 *   FILE: s5fs_subr.c
 * AUTHOR: afenn
 *  DESCR:
 *  $Id: s5fs_subr.c,v 1.1.2.1 2006/06/04 01:02:15 afenn Exp $
 */

#include "kernel.h"
#include "util/debug.h"
#include "mm/kmalloc.h"
#include "globals.h"
#include "proc/sched.h"
#include "proc/kmutex.h"
#include "errno.h"
#include "util/string.h"
#include "util/printf.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/s5fs/s5fs_subr.h"
#include "fs/s5fs/s5fs.h"
#include "mm/mm.h"
#include "mm/page.h"

#define dprintf(...) dbg(DBG_S5FS, __VA_ARGS__)

#define s5_dirty_super(fs)                                           \
        do {                                                         \
                pframe_t *p;                                         \
                int err;                                             \
                pframe_get(S5FS_TO_VMOBJ(fs), S5_SUPER_BLOCK, &p);   \
                KASSERT(p);                                          \
                err = pframe_dirty(p);                               \
                KASSERT(!err                                         \
                        && "shouldn\'t fail for a page belonging "   \
                        "to a block device");                        \
        } while (0)


static void s5_free_block(s5fs_t *fs, int block);
static int s5_alloc_block(s5fs_t *);


/* helper function, given a vnode and a block index, return the block
 * number */
/* if the indirect block has not been allocated while the index is in the
 * indirect block, just return 0 
 */
 /*  return -1 if exceeds the maximum allowed blocks */
uint32_t get_block_by_index(vnode_t* vnode, int index){
    
    KASSERT(vnode != NULL);

    s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
    s5fs_t* s5 = VNODE_TO_S5FS(vnode);
    struct mmobj *s5obj = S5FS_TO_VMOBJ(s5); 

    if(index < S5_NDIRECT_BLOCKS)
        return inode -> s5_direct_blocks[index];

    uint32_t indirect_block = inode -> s5_indirect_block;
    if(indirect_block == 0)
        return 0;

    else{
        pframe_t* indirect_block_frame;
        int res = pframe_get(s5obj, indirect_block, &indirect_block_frame);
        if(res < 0){
            return 0;
        }
        uint32_t index_in_indirect = index - S5_NDIRECT_BLOCKS;
        uint32_t* block_array = (uint32_t*)indirect_block_frame->pf_addr;
        return block_array[index_in_indirect];
    }
}

/*
 * Return the disk-block number for the given seek pointer (aka file
 * position).
 *
 * If the seek pointer refers to a sparse block, and alloc is false,
 * then return 0. If the seek pointer refers to a sparse block, and
 * alloc is true, then allocate a new disk block (and make the inode
 * point to it) and return it.
 *
 * Be sure to handle indirect blocks!
 *
 * If there is an error, return -errno.
 *
 * You probably want to use pframe_get, pframe_pin, pframe_unpin, pframe_dirty.
 */
int
s5_seek_to_block(vnode_t *vnode, off_t seekptr, int alloc)
{
        KASSERT(vnode != NULL);

        s5fs_t* s5 = VNODE_TO_S5FS(vnode);
        struct mmobj *s5obj = S5FS_TO_VMOBJ(s5); 
        s5_inode_t* inode = VNODE_TO_S5INODE(vnode);

        int nth_block = S5_DATA_BLOCK(seekptr);
        uint32_t file_size =  inode -> s5_size;
        int block_num = 0;
        /* check whether in the indirect block */
        if(nth_block < S5_NDIRECT_BLOCKS){
            block_num = inode -> s5_direct_blocks[nth_block];
            if(block_num == 0){
                if(alloc != 0){
                    /* sparse block */
                    block_num = s5_alloc_block(s5);
                    if(block_num < 0){
                        /* allocation fails */
                        return -ENOSPC;
                    }
                    inode -> s5_direct_blocks[nth_block] = block_num;           
                    /*  the content of inode is modified */
                    s5_dirty_inode(s5, inode);
                }
            }
            return block_num;
        }

        else{
            
            /*  get content or modify the indirect block */
            uint32_t index_in_indirect = nth_block - S5_NDIRECT_BLOCKS;
            if(index_in_indirect >= S5_NIDIRECT_BLOCKS){
                return -ENOSPC;
            }

            /*  indirect blocks */
            int indirect_block = inode-> s5_indirect_block;
            if(indirect_block == 0){
                /* allocate the indirect block first */
                indirect_block = s5_alloc_block(s5);
                if(indirect_block < 0){
                    return -ENOSPC;
                }
                inode -> s5_indirect_block = indirect_block;
                s5_dirty_inode(s5, inode);
            }

            /*  get the contents of indirect block */
            pframe_t* indirect_block_frame;
            int res = pframe_get(s5obj, indirect_block, &indirect_block_frame);
            if(res < 0){
                return res;
            }
            pframe_pin(indirect_block_frame);
            /*  get the block number in indirect block */
            uint32_t* block_array = (uint32_t*)indirect_block_frame->pf_addr;
            block_num = block_array[index_in_indirect];
            if(block_num == 0){
                if(alloc != 0){
                    /*  allocate a new block */
                    block_num = s5_alloc_block(s5);
                    if(block_num < 0){
                        /* allocation fails */
                        return -ENOSPC;
                    }
                    block_array[index_in_indirect] = block_num;           
                    pframe_dirty(indirect_block_frame);
                }
            }
            pframe_unpin(indirect_block_frame);
            return block_num;           
        }
        
}


/*
 * Locks the mutex for the whole file system
 */
static void
lock_s5(s5fs_t *fs)
{
        kmutex_lock(&fs->s5f_mutex);
}

/*
 * Unlocks the mutex for the whole file system
 */
static void
unlock_s5(s5fs_t *fs)
{
        kmutex_unlock(&fs->s5f_mutex);
}


/*
 * Write len bytes to the given inode, starting at seek bytes from the
 * beginning of the inode. On success, return the number of bytes
 * actually written (which should be 'len', unless there's only enough
 * room for a partial write); on failure, return -errno.
 *
 * This function should allow writing to files or directories, treating
 * them identically.
 *
 * Writing to a sparse block of the file should cause that block to be
 * allocated.  Writing past the end of the file should increase the size
 * of the file. Blocks between the end and where you start writing will
 * be sparse.
 *
 * Do not call s5_seek_to_block() directly from this function.  You will
 * use the vnode's pframe functions, which will eventually result in a
 * call to s5_seek_to_block().
 *
 * You will need pframe_dirty(), pframe_get(), memcpy().
 */

int
s5_write_file(vnode_t *vnode, off_t seek, const char *bytes, size_t len)
{
        /*  get the block, we need to write */

        KASSERT(vnode != NULL);
        s5fs_t* s5 = VNODE_TO_S5FS(vnode);

        /*  get the block frame to write to */
        void* block_frame = NULL;
        int res = vnode -> vn_ops -> fillpage(vnode, seek, block_frame);
        if(res < 0) return res;

        /*  get the start location (offset) in the block */
        uint32_t offset = S5_DATA_OFFSET(seek);
        uint32_t remaining = S5_BLOCK_SIZE - offset;
        int written = 0;
        /* look to see if remaining space coudl be written */
        uint32_t to_write = len;
        while(to_write > 0){
            if(to_write <= remaining){
                memcpy( (char*) block_frame + offset, bytes + written, to_write);
                res = vnode -> vn_ops -> dirtypage(vnode, seek + written );
                if(res < 0) return res;
                written += to_write;
                to_write = 0;
                offset = 0;
            }
            else{
                memcpy( (char*) block_frame + offset, bytes + written, remaining);
                res = vnode -> vn_ops -> dirtypage(vnode, seek + written );
                if(res < 0) return res;
                to_write -= remaining;
                written += remaining;
                offset = 0;
            }
            /*see whether we need to go to next block*/
            if(to_write > 0){
                int res = vnode -> vn_ops -> fillpage(vnode, seek + written, block_frame);
                /*  exceeded the capacity */
                if(res < 0){
                    return written;
                }
                remaining = S5_BLOCK_SIZE;
            }
        }

        /*  update the frame of vnode */
        if(seek + written > vnode -> vn_len)
            vnode->vn_len = seek + written;

        s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
        s5_dirty_inode(s5, inode);

        return written;
}

/*
 * Read up to len bytes from the given inode, starting at seek bytes
 * from the beginning of the inode. On success, return the number of
 * bytes actually read, or 0 if the end of the file has been reached; on
 * failure, return -errno.
 *
 * This function should allow reading from files or directories,
 * treating them identically.
 *
 * Reading from a sparse block of the file should act like reading
 * zeros; it should not cause the sparse blocks to be allocated.
 *
 * Similarly as in s5_write_file(), do not call s5_seek_to_block()
 * directly from this function.
 *
 * If the region to be read would extend past the end of the file, less
 * data will be read than was requested.
 *
 * You probably want to use pframe_get(), memcpy().
 */
int
s5_read_file(struct vnode *vnode, off_t seek, char *dest, size_t len)
{
        KASSERT(vnode != NULL);
        KASSERT(dest != NULL);

        /*  get the block frame to read from */
        s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
        s5fs_t* s5 = VNODE_TO_S5FS(vnode);
        struct mmobj *s5obj = S5FS_TO_VMOBJ(s5); 
        struct mmobj fileobj = vnode -> vn_mmobj;

        uint32_t block_index = S5_DATA_BLOCK(seek);
        if(block_index >= S5_MAX_FILE_BLOCKS)
            return 0;
        /*  get the start location (offset) in the block */
        uint32_t offset = S5_DATA_OFFSET(seek);
        uint32_t remaining = S5_BLOCK_SIZE - offset;
        int has_read = 0;
        uint32_t to_read = len;

        while(to_read > 0){
            if(to_read <= remaining){
                uint32_t block_num_to_read = get_block_by_index(vnode, block_index);
                if(block_num_to_read == 0){
                    /*  sparse block */
                    memcpy(dest + has_read, 0, to_read);
                }
                else{
                    /*  get the block content */
                    pframe_t* block = NULL;
                    pframe_pin(block);
                    int res = pframe_get(&fileobj, block_index, &block);
                    if(res < 0)
                        return res;
                    /*  do the reading */
                    memcpy((char*)dest + has_read, (char*) block->pf_addr + offset, to_read);    
                    pframe_unpin(block);
                }
                has_read += to_read;
                to_read = 0;
                offset = 0;
            }
            else{
                uint32_t block_num_to_read = get_block_by_index(vnode, block_index);
                if(block_num_to_read == 0){
                    /*  sparse block */
                    memcpy(dest + has_read, 0, remaining);
                }
                else{
                    /*  get the block content */
                    pframe_t* block = NULL;
                    pframe_pin(block);
                    int res = pframe_get(&fileobj, block_index, &block);
                    if(res < 0)
                        return res;
                    /*  do the reading */
                    memcpy((char*)dest + has_read, (char*) block->pf_addr + offset, remaining);    
                    pframe_unpin(block);
                }
                has_read += remaining;
                to_read -= remaining;
                offset = 0;
            }

            if(to_read > 0){
                /*  get next page.. */
                block_index ++;
                remaining = S5_BLOCK_SIZE;
                if(block_index == S5_MAX_FILE_BLOCKS)
                    break;
            }
        }

        return has_read;
}

/*
 * Allocate a new disk-block off the block free list and return it. If
 * there are no free blocks, return -ENOSPC.
 *
 * This will not initialize the contents of an allocated block; these
 * contents are undefined.
 *
 * If the super block's s5s_nfree is 0, you need to refill 
 * s5s_free_blocks and reset s5s_nfree.  You need to read the contents 
 * of this page using the pframe system in order to obtain the next set of
 * free block numbers.
 *
 * Don't forget to dirty the appropriate blocks!
 *
 * You'll probably want to use lock_s5(), unlock_s5(), pframe_get(),
 * and s5_dirty_super()
 */
static int
s5_alloc_block(s5fs_t *fs)
{
        /*  get the super block frame */
        s5_super_t* super_block = fs -> s5f_super;
        KASSERT(S5_NBLKS_PER_FNODE > super_block->s5s_nfree);

        struct mmobj *s5obj =  S5FS_TO_VMOBJ(fs);
        lock_s5(fs);
        if( ((int)super_block->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] == -1) && (super_block->s5s_nfree == (uint32_t)0)){
            unlock_s5(fs);
            return -ENOSPC;
        }
        int to_return; 
        /*  get a free node */
        if(super_block -> s5s_nfree != 0){
            to_return = super_block->s5s_free_blocks[super_block->s5s_nfree -- ];
        }
        /*  move the next  */
        else{
            /*  copy the next here */
            int next_block_num = super_block -> s5s_free_blocks[S5_NBLKS_PER_FNODE - 1];
            pframe_t* next_block;
            int res = pframe_get(s5obj, next_block_num, &next_block);
            if(res < 0){
                unlock_s5(fs);
                return res;
            }

            /* copy the next free block list to the super block */
            memcpy((void*)(super_block -> s5s_free_blocks), next_block->pf_addr, S5_NBLKS_PER_FNODE);
            to_return = next_block_num;
        }
        /*dirty the super block*/
        s5_dirty_super(fs);       
        unlock_s5(fs);
        return to_return;
}


/*
 * Given a filesystem and a block number, frees the given block in the
 * filesystem.
 *
 * This function may potentially block.
 *
 * The caller is responsible for ensuring that the block being placed on
 * the free list is actually free and is not resident.
 */
static void
s5_free_block(s5fs_t *fs, int blockno)
{
        s5_super_t *s = fs->s5f_super;

        lock_s5(fs);

        KASSERT(S5_NBLKS_PER_FNODE > s->s5s_nfree);

        if ((S5_NBLKS_PER_FNODE - 1) == s->s5s_nfree) {
                /* get the pframe where we will store the free block nums */
                pframe_t *prev_free_blocks = NULL;
                KASSERT(fs->s5f_bdev);
                pframe_get(&fs->s5f_bdev->bd_mmobj, blockno, &prev_free_blocks);
                KASSERT(prev_free_blocks->pf_addr);

                /* copy from the superblock to the new block on disk */
                memcpy(prev_free_blocks->pf_addr, (void *)(s->s5s_free_blocks),
                       S5_NBLKS_PER_FNODE * sizeof(int));
                pframe_dirty(prev_free_blocks);

                /* reset s->s5s_nfree and s->s5s_free_blocks */
                s->s5s_nfree = 0;
                s->s5s_free_blocks[S5_NBLKS_PER_FNODE - 1] = blockno;
        } else {
                s->s5s_free_blocks[s->s5s_nfree++] = blockno;
        }

        s5_dirty_super(fs);

        unlock_s5(fs);
}

/*
 * Creates a new inode from the free list and initializes its fields.
 * Uses S5_INODE_BLOCK to get the page from which to create the inode
 *
 * This function may block.
 */
int
s5_alloc_inode(fs_t *fs, uint16_t type, devid_t devid)
{
        s5fs_t *s5fs = FS_TO_S5FS(fs);
        pframe_t *inodep;
        s5_inode_t *inode;
        int ret = -1;

        KASSERT((S5_TYPE_DATA == type)
                || (S5_TYPE_DIR == type)
                || (S5_TYPE_CHR == type)
                || (S5_TYPE_BLK == type));


        lock_s5(s5fs);

        if (s5fs->s5f_super->s5s_free_inode == (uint32_t) -1) {
                unlock_s5(s5fs);
                return -ENOSPC;
        }

        pframe_get(&s5fs->s5f_bdev->bd_mmobj,
                   S5_INODE_BLOCK(s5fs->s5f_super->s5s_free_inode),
                   &inodep);
        KASSERT(inodep);

        inode = (s5_inode_t *)(inodep->pf_addr)
                + S5_INODE_OFFSET(s5fs->s5f_super->s5s_free_inode);

        KASSERT(inode->s5_number == s5fs->s5f_super->s5s_free_inode);

        ret = inode->s5_number;

        /* reset s5s_free_inode; remove the inode from the inode free list: */
        s5fs->s5f_super->s5s_free_inode = inode->s5_next_free;
        pframe_pin(inodep);
        s5_dirty_super(s5fs);
        pframe_unpin(inodep);


        /* init the newly-allocated inode: */
        inode->s5_size = 0;
        inode->s5_type = type;
        inode->s5_linkcount = 0;
        memset(inode->s5_direct_blocks, 0, S5_NDIRECT_BLOCKS * sizeof(int));
        if ((S5_TYPE_CHR == type) || (S5_TYPE_BLK == type))
                inode->s5_indirect_block = devid;
        else
                inode->s5_indirect_block = 0;

        s5_dirty_inode(s5fs, inode);

        unlock_s5(s5fs);

        return ret;
}


/*
 * Free an inode by freeing its disk blocks and putting it back on the
 * inode free list.
 *
 * You should also reset the inode to an unused state (eg. zero-ing its
 * list of blocks and setting its type to S5_FREE_TYPE).
 *
 * Don't forget to free the indirect block if it exists.
 *
 * You probably want to use s5_free_block().
 */
void
s5_free_inode(vnode_t *vnode)
{
        uint32_t i;
        s5_inode_t *inode = VNODE_TO_S5INODE(vnode);
        s5fs_t *fs = VNODE_TO_S5FS(vnode);

        KASSERT((S5_TYPE_DATA == inode->s5_type)
                || (S5_TYPE_DIR == inode->s5_type)
                || (S5_TYPE_CHR == inode->s5_type)
                || (S5_TYPE_BLK == inode->s5_type));

        /* free any direct blocks */
        for (i = 0; i < S5_NDIRECT_BLOCKS; ++i) {
                if (inode->s5_direct_blocks[i]) {
                        dprintf("freeing block %d\n", inode->s5_direct_blocks[i]);
                        s5_free_block(fs, inode->s5_direct_blocks[i]);

                        s5_dirty_inode(fs, inode);
                        inode->s5_direct_blocks[i] = 0;
                }
        }

        if (((S5_TYPE_DATA == inode->s5_type)
             || (S5_TYPE_DIR == inode->s5_type))
            && inode->s5_indirect_block) {
                pframe_t *ibp;
                uint32_t *b;

                pframe_get(S5FS_TO_VMOBJ(fs),
                           (unsigned)inode->s5_indirect_block,
                           &ibp);
                KASSERT(ibp
                        && "because never fails for block_device "
                        "vm_objects");
                pframe_pin(ibp);

                b = (uint32_t *)(ibp->pf_addr);
                for (i = 0; i < S5_NIDIRECT_BLOCKS; ++i) {
                        KASSERT(b[i] != inode->s5_indirect_block);
                        if (b[i])
                                s5_free_block(fs, b[i]);
                }

                pframe_unpin(ibp);

                s5_free_block(fs, inode->s5_indirect_block);
        }

        inode->s5_indirect_block = 0;
        inode->s5_type = S5_TYPE_FREE;
        s5_dirty_inode(fs, inode);

        lock_s5(fs);
        inode->s5_next_free = fs->s5f_super->s5s_free_inode;
        fs->s5f_super->s5s_free_inode = inode->s5_number;
        unlock_s5(fs);

        s5_dirty_inode(fs, inode);
        s5_dirty_super(fs);
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and return its inode number. If there is no entry with the given
 * name, return -ENOENT.
 *
 * You'll probably want to use s5_read_file and name_match
 *
 * You can either read one dirent at a time or optimize and read more.
 * Either is fine.
 */
int
s5_find_dirent(vnode_t *vnode, const char *name, size_t namelen)
{

        KASSERT(vnode != NULL);
        KASSERT(name != NULL);
        KASSERT(vnode -> vn_mode == S_IFDIR);

        /*  read one by one.. */
        int res = 0;
        uint8_t buffer[sizeof(s5_dirent_t)];
        s5_dirent_t* entry = (s5_dirent_t*)buffer;
        int offset = 0;
        while(1){
            int length = s5_read_file(vnode, offset, (char*)entry, sizeof(s5_dirent_t));
            if(length != sizeof(s5_dirent_t))
                break;
            /* compare */
            if(name_match(name, entry -> s5d_name, namelen)){
                return entry -> s5d_inode;
            }
            offset += length;
        }
        return -ENOENT;
}

/*
 * Locate the directory entry in the given inode with the given name,
 * and delete it. If there is no entry with the given name, return
 * -ENOENT.
 *
 * In order to ensure that the directory entries are contiguous in the
 * directory file, you will need to move the last directory entry into
 * the remove dirent's place.
 *
 * When this function returns, the inode refcount on the removed file
 * should be decremented.
 *
 * It would be a nice extension to free blocks from the end of the
 * directory file which are no longer needed.
 *
 * Don't forget to dirty appropriate blocks!
 *
 * You probably want to use vget(), vput(), s5_read_file(),
 * s5_write_file(), and s5_dirty_inode().
 */
int
s5_remove_dirent(vnode_t *vnode, const char *name, size_t namelen)
{
        KASSERT(vnode != NULL);
        KASSERT(name != NULL);
        KASSERT(vnode -> vn_mode == S_IFDIR);
        s5fs_t *s5 = VNODE_TO_S5FS(vnode);

        /*  try to get the last entry.. */
        int target_ino_num = -1;
        uint8_t buffer[sizeof(s5_dirent_t)];
        s5_dirent_t* entry = (s5_dirent_t*)buffer;
        /*  the offset where should be replaced with the last entry */
        int offset = 0;
        while(1){
            int length = s5_read_file(vnode, offset, (char*)entry, sizeof(s5_dirent_t));
            if(length != sizeof(s5_dirent_t))
                break;
            /* compare */
            if(name_match(name, entry -> s5d_name, namelen)){
                target_ino_num = entry -> s5d_inode;
                break;
            }
            offset += length;
        }
        if(target_ino_num == -1)
            return -ENOENT;

        /*  decrement teh link count here */
        vnode_t* target_vno = vget(vnode->vn_fs, target_ino_num);
        s5_inode_t* target_ino = VNODE_TO_S5INODE(target_vno);
        target_ino->s5_linkcount --;
        /* update the block where the deleted entry locates in */
        s5_dirty_inode(s5, target_ino);
        vput(target_vno);
        
        /*  fetch the last */
        int length = s5_read_file(vnode, vnode->vn_len - sizeof(s5_dirent_t), (char*)entry, sizeof(s5_dirent_t));
        if(length != sizeof(s5_dirent_t))
            return -1;
        
        /* write to the hole */
        length = s5_write_file(vnode, offset, (char*)entry, sizeof(s5_dirent_t));
        

        /*  see if the last block could be freed */
        int last_offset = S5_DATA_OFFSET(vnode->vn_len - sizeof(s5_dirent_t));
        if(last_offset == 0){
            int last_index = S5_DATA_BLOCK(vnode->vn_len - sizeof(s5_dirent_t));
            uint32_t free_block_num = get_block_by_index(vnode, last_index);
            /*  free it  */
            s5_free_block(s5, free_block_num);
        }

        /*  update the directory vnode length */
        vnode->vn_len -= sizeof(s5_dirent_t);
        
        /*  update the vnode content on disk.. */
        s5_inode_t* dir_inode = VNODE_TO_S5INODE(vnode);
        s5_dirty_inode(s5, dir_inode);

        return 0;
}

/*
 * Create a new directory entry in directory 'parent' with the given name, which
 * refers to the same file as 'child'.
 *
 * When this function returns, the inode refcount on the file that was linked to
 * should be incremented.
 *
 * Remember to incrament the ref counts appropriately
 *
 * You probably want to use s5_find_dirent(), s5_write_file(), and s5_dirty_inode().
 */
int
s5_link(vnode_t *parent, vnode_t *child, const char *name, size_t namelen)
{
        KASSERT(parent != NULL);
        KASSERT(parent->vn_mode == S_IFDIR);
        KASSERT(child != NULL);
        KASSERT(name != NULL);
        s5fs_t *s5 = VNODE_TO_S5FS(parent);
        /*  if already exists? */
        if(s5_find_dirent(parent, name, namelen) >= 0){
            return -1;
        }

        /*  construct a dirent */
        s5_dirent_t new_entry;
        s5_inode_t* child_inode = VNODE_TO_S5INODE(child);
        new_entry.s5d_inode = child_inode -> s5_number;
        strncpy(new_entry.s5d_name, name, namelen);

        /* insert the new entry */
        int length = s5_write_file(parent, parent -> vn_len, (char*)& new_entry, namelen);
        if(length != sizeof(s5_dirent_t)){
            return length;
        }
    
        /*  increase the child refcount */
        child_inode->s5_linkcount ++;
        /* update the block where the deleted entry locates in */
        s5_dirty_inode(s5, child_inode);
        
        return 0;
}

/*
 * Return the number of blocks that this inode has allocated on disk.
 * This should include the indirect block, but not include sparse
 * blocks.
 *
 * This is only used by s5fs_stat().
 *
 * You'll probably want to use pframe_get().
 */
int
s5_inode_blocks(vnode_t *vnode)
{

        KASSERT(vnode != NULL);
        s5_inode_t* inode = VNODE_TO_S5INODE(vnode);
        s5fs_t* s5 = VNODE_TO_S5FS(vnode);
        struct mmobj *s5obj = S5FS_TO_VMOBJ(s5); 

        /* count */
        int file_size = vnode -> vn_len;
        /*  total possible number of blocks */
        int total_possible = S5_DATA_BLOCK(file_size);
        int block_count = 0;
        int indirect_block = inode-> s5_indirect_block;
        pframe_t* indirect_block_frame = NULL;
        int i =0;
        for(; i < total_possible; i ++){
            if( i < S5_NDIRECT_BLOCKS){
                if(inode->s5_direct_blocks[i] != 0)
                    block_count ++;
            }
            else{
                /*  get the frame of indirect block */
                if(indirect_block == 0){
                    /*  following are all sparse */
                    break;
                }
                else{
                    if(indirect_block_frame == NULL){
                        int res = pframe_get(s5obj, indirect_block, &indirect_block_frame);
                        if(res < 0){
                            return res;
                        }
                        pframe_pin(indirect_block_frame);
                    }
                    uint32_t* block_array = (uint32_t*)indirect_block_frame->pf_addr;
                    if( block_array[ i - S5_NDIRECT_BLOCKS] != 0)
                        block_count ++;
                }
            }
        }
        
        if(total_possible > S5_NDIRECT_BLOCKS && indirect_block != 0)
            pframe_unpin(indirect_block_frame);

        return block_count;
}

