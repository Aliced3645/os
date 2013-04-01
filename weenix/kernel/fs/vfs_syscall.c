/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.9.2.2 2006/06/04 01:02:32 afenn Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/* To read a file:
 *      o fget(fd)
 *      o call its virtual read f_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
        if(fd < 0 || fd >= NFILES){
            return -EBADF;
        }
        file_t* file = fget(fd);
        if(file == NULL)
            return -EBADF;

        else if( (file->f_mode &  FMODE_READ) == 0){
            fput(file);
            return -EBADF;
        }

        else if((file->f_vnode->vn_mode & S_IFDIR) != 0){
            fput(file);
            return -EISDIR;
        }

        vnode_t* file_vnode = file->f_vnode;
        int res = file_vnode->vn_ops->read(file_vnode, file->f_pos, buf, nbytes);
        file->f_pos += res;
        fput(file);

        return res;
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * f_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
        if( fd < 0 || fd >= NFILES ){
            return -EBADF;
        }

        file_t* file = fget(fd);
        
        if(file == NULL || file->f_mode == FMODE_READ){
            if(file != NULL)
                fput(file);
            return -EBADF;
        }

        int res;
        if( (file->f_mode & FMODE_APPEND) != 0){
            res = do_lseek( fd, 0, SEEK_END);
            if(res < 0) {
                fput(file);
                return res;
            }
        }
        
        vnode_t* file_vnode = file->f_vnode;
        res = file_vnode->vn_ops->write(file_vnode, file->f_pos, buf, nbytes);
        file->f_pos += res;
        fput(file);
        
        return res;
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{

       
        if(fd < 0 || fd >= NFILES){
            return -EBADF;
        }

        if(curproc->p_files[fd] == NULL)
            return -EBADF;
        
        file_t* file = curproc->p_files[fd];
        
        if(fd == 0 && file->f_vnode->vn_vno == 10){
            int i = 0;
        }

        curproc->p_files[fd] = NULL;
        fput(file);
        
        return 0;
}

/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{

        if(fd < 0 || fd >= NFILES){
            return -EBADF;
        }

        file_t* file = fget(fd);
        if(file == NULL){
            return -EBADF;
        }

        int new_fd = get_empty_fd( curproc );
        if(new_fd < 0){
            fput(file);
            return -EMFILE;
        }

        curproc->p_files[new_fd] = file;

        return new_fd;
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
        if(ofd < 0 || ofd >= NFILES || nfd >= NFILES || nfd < 0){
            return -EBADF;
        }
        file_t* file = fget(ofd);

        if(file == NULL){
            return -EBADF;
        }
        
        if(curproc -> p_files[nfd] != NULL){
            if(nfd != ofd){
                do_close(nfd);
                curproc -> p_files[nfd] = file;
            }
            else{
                fput(file);
            }
        }

        else{
            curproc -> p_files[nfd] = file;
        }
          
        return nfd;
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
        if( (mode != S_IFCHR) && (mode != S_IFBLK) )
            return -EINVAL;

        char cname[STR_MAX];
        const char* name = cname;
        size_t namelen;
        vnode_t* dir_vnode = NULL, *res_vnode = NULL;
        int res = dir_namev(path, &namelen, &name, curproc->p_cwd, &dir_vnode);
        if(res < 0){
            vput(dir_vnode);
            return res;
        }
        
        /*  check if already exists */
        res = lookup(dir_vnode, name, namelen, &res_vnode);
        if(res == 0){
            vput(dir_vnode);
            vput(res_vnode);
            return -EEXIST;
        }
        
        if(res == -ENOENT)
        /*  make the node */
            res = dir_vnode -> vn_ops -> mknod(dir_vnode, name, namelen, mode, devid);

        vput(dir_vnode);
        if(res_vnode){
            vput(res_vnode);
        }
        return res;
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
        char cname[STR_MAX];
        const char* name = cname;
        size_t namelen;
        vnode_t* dir_vnode = NULL, *res_vnode = NULL;
        int res = dir_namev(path, &namelen, &name, curproc->p_cwd, &dir_vnode);
        if(res < 0){
            if(dir_vnode != NULL) 
                vput(dir_vnode);
            return res;
        }

        res = lookup(dir_vnode, name, namelen, &res_vnode);
        if(res == 0 || res != -ENOENT){
            if(dir_vnode != NULL)
                vput(dir_vnode);
            if(res_vnode != NULL)
                vput(res_vnode);
            if(res == 0) {
                return -EEXIST;
            }
            else return res;
        }

        if(res == -ENOENT)
            res = dir_vnode -> vn_ops -> mkdir(dir_vnode, name, namelen);
        
        vput(dir_vnode);
        if(res_vnode != NULL)
            vput(res_vnode);
        return res;
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
        char cname[STR_MAX];
        const char* name = cname;
        size_t namelen;
        vnode_t* dir_vnode = NULL;
        int res = dir_namev(path, &namelen, &name, curproc->p_cwd, &dir_vnode);
        if(res < 0){
            if(dir_vnode)
                vput(dir_vnode);
            return res;
        }
        /* check the final component */
        if(strcmp(name, ".") == 0){
            vput(dir_vnode);
            return -EINVAL;
        }
        else if(strcmp(name, "..") == 0){
            vput(dir_vnode);
            return -ENOTEMPTY;
        }

        res = dir_vnode->vn_ops->rmdir(dir_vnode, name, namelen);
        vput(dir_vnode);
        return res;
}

/*
 * Same as do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EISDIR
 *        path refers to a directory.
 *      o ENOENT
 *        A component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
        char cname[STR_MAX];
        const char* name = cname;

        size_t namelen;
        vnode_t* dir_vnode = NULL, *res_vnode = NULL;
        int res = dir_namev(path, &namelen, &name, curproc->p_cwd, &dir_vnode);
        if(res < 0){
            if(dir_vnode)
                vput(dir_vnode);
            return res;
        }
        /*  if it is  directory, then return EPERM*/
        res = lookup(dir_vnode, name, namelen, &res_vnode);
        if(res < 0){
            if(dir_vnode)
                vput(dir_vnode);
            if(res_vnode)
                vput(res_vnode);
            return res;
        }

        if(res_vnode -> vn_mode == S_IFDIR){
            if(dir_vnode)
                vput(dir_vnode);
            vput(res_vnode);
            return -EPERM;   
        }

        res = dir_vnode->vn_ops->unlink(dir_vnode, name, namelen);
        vput(dir_vnode);
        vput(res_vnode);
        return res;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 */
int
do_link(const char *from, const char *to)
{
    vnode_t* from_vnode;
    int res = open_namev(from, O_RDONLY, &from_vnode, curproc -> p_cwd);
    if(res < 0){
        vput(from_vnode);
        return res;
    }

    vnode_t* dir_vnode;
    char cname[STR_MAX];
    const char* name = cname;
    size_t namelen;

    res = dir_namev(to, &namelen, &name, curproc->p_cwd, &dir_vnode);
    if(res < 0){
        vput(dir_vnode);
        return res;
    }
    
    vnode_t* to_vnode;
    /*  test whether exists */
    res = lookup(dir_vnode, name, namelen, &to_vnode);
    if(res != -ENOENT){
        if(res == 0){
            vput(to_vnode);
            vput(from_vnode);
            return -EEXIST;
        }

        vput(from_vnode);
        return res;
    }
    
    res = dir_vnode->vn_ops->link(from_vnode, dir_vnode, name, namelen);
    vput(from_vnode);

    return res;
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
        int res = do_link(oldname, newname);
        if(res < 0)
            return res;

        res = do_unlink(oldname);
        return res;
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
        /* try to get the path first*/
        vnode_t* new_dir = NULL;
        vnode_t* old_dir = curproc -> p_cwd;
        int res = open_namev(path, O_RDONLY, &new_dir, old_dir);
        if(res < 0){
            if(new_dir != NULL)
                vput(new_dir);
            return res;
        }
        if(new_dir->vn_mode != S_IFDIR){
            vput(new_dir);
            return -ENOTDIR;
        }

        curproc -> p_cwd = new_dir;

        /*  down the refcount */
        vput(old_dir);
        return 0;
}

/* Call the readdir f_op on the given fd, filling in the given dirent_t*.
 * If the readdir f_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
int
do_getdent(int fd, struct dirent *dirp)
{
        if(fd == 0){
            int i = 1;
        }
        if(fd < 0 || fd >= NFILES){
            return -EBADF;
        }

        file_t* dir = fget(fd);
        if(dir == NULL){
            return -EBADF;
        }
        vnode_t* dir_vnode = dir->f_vnode;
        if(dir_vnode -> vn_mode != S_IFDIR){
            fput(dir);
            return -ENOTDIR;
        }

        /*  do the work */
        int offset = dir_vnode -> vn_ops -> readdir(dir->f_vnode, dir->f_pos, dirp);
        dir->f_pos += offset;
        /*  not sure whether to put */
        fput(dir);
        if(offset != 0)
            return sizeof(dirent_t);
        else 
            return 0;
}

/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
        if(fd < 0 || fd >= NFILES){
            return -EBADF;
        }

        if( (whence != SEEK_END) && (whence != SEEK_CUR) && (whence != SEEK_SET) ){
            return -EINVAL;
        }
        
        file_t* file = fget(fd);
        if(file == NULL){
            return -EBADF;
        }
        int length = file -> f_vnode -> vn_len;
        int res = 0;

        if(whence == SEEK_SET){
            if(offset >= 0){
                file -> f_pos = offset;
                res = offset;
            }
            else{
                fput(file);
                return  -EINVAL;
            }
        }
        else if(whence == SEEK_END){
            res = length + offset;
            if(res < 0){
                fput(file);
                return -EINVAL;
            }
            else 
                file -> f_pos = res;
        }
        else if(whence == SEEK_CUR){
            res = file -> f_pos + offset;
            if(res < 0){
                fput(file);
                return -EINVAL;
            }
            else 
                file -> f_pos = res;
        }

        fput(file);
        return res;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_stat(const char *path, struct stat *buf)
{
        
        if(strlen(path) == 0)
            return -ENOENT;

        vnode_t* res_vnode = NULL;
        int res = open_namev(path, O_RDWR, &res_vnode, curproc -> p_cwd);
        if(res < 0){
            if(res_vnode != NULL)
                vput(res_vnode);
            return res;
        }

        /*  do the work */
        res = res_vnode -> vn_ops -> stat(res_vnode, buf);
        vput(res_vnode);
        return res;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
