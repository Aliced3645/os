#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"



/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function, but you may want to special case
 * "." and/or ".." here depnding on your implementation.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{

        if(strcmp(name, ".") == 0)
            result = &dir;
        
        if(dir -> vn_ops -> lookup == NULL)
            return -ENOTDIR;
        
        /*  call the lookup function in vnode */
        dir -> vn_ops -> lookup(dir, name, len, result);
        if(*result != NULL){
            vref(*result);    
        }
        
        return 0;
}


/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
        
        /* check the base first */
        vnode_t* prev_v_node = NULL;
        vnode_t* next_v_node = NULL;

        if(pathname[0] == '/'){
            prev_v_node = vfs_root_vn;
        }

        if(base == NULL){
            prev_v_node = curproc->p_cwd;
        }
        else{
            prev_v_node = base;
        }
        vref(prev_v_node);

        int start_index = (pathname[0] == '/' ? 1 : 0);
        int end_index = 0;
        int terminate = 0;
        /*  do the finding */
        while(1){

            int i = start_index;
            while(i < (int) strlen(pathname)){
                if(pathname[i] == '/'){
                    end_index = i - 1;
                    break;
                }
            }
            if(i == (int)strlen(pathname)){
                /*t is the end..*/
                end_index = (int)strlen(pathname)  - 1;
                terminate = 1;
                break;
            }
            
            /*char* next_name = (char*)kmalloc(end_index - start_index + 2);*/
            char next_name[end_index - start_index + 2];

            strncpy(next_name, pathname + start_index, end_index - start_index + 1);
            next_name[end_index - start_index + 1] = '\0';
            if(terminate == 1){
                *namelen = strlen(next_name);
                /**name = next_name */
                strncpy(*name, next_name, *namelen);
                *res_vnode = prev_v_node;
                break;
            }

            if(lookup(prev_v_node, next_name, strlen(next_name), &next_v_node) != 0)
                return -1;
            
            /* decrement the reference count */
            vput(prev_v_node);
            start_index = end_index + 2;
            prev_v_node = next_v_node;
            kfree(next_name);

        }
        return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fnctl.h>.  If the O_CREAT flag is specified, and the file does
 * not exist call create() in the parent directory vnode.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */
int
open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
        size_t namelen;
        char name[256];
        vnode_t* dir_vnode = NULL;
        if(dir_namev(pathname, &namelen, &name, base, dir_vnode)  != 0)
            return -1;
        /* Now we get the vnode of parent directory and the name of the target
         * file*/
        vnode_t* result;
        if(lookup(dir_vnode, name, namelen, result) != 0)
            return -1;

        return 0;
}

#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
