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

#include "mm/kmalloc.h"


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
        if(dir == NULL || name == NULL)
            return -1;
        
        if(strcmp(name, ".") == 0){
            *result = dir;
            vref(dir);
            return 0;
        }
        
        if(dir -> vn_ops -> lookup == NULL)
            return -ENOTDIR;
        
        /*  call the lookup function in vnode */
        if(dir -> vn_ops -> lookup(dir, name, len, result) == -ENOENT){
            return -ENOENT;
        }
        /*  
        if(*result != NULL){
            vref(*result);    
        }
        */

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
        
        if(pathname == NULL)
            return -1;

        /* check the base first */
        vnode_t* prev_v_node = NULL;
        vnode_t* next_v_node = NULL;
        
        int slash_counter = 0;
        if(pathname[0] == '/'){
            prev_v_node = vfs_root_vn;
            while(pathname[slash_counter] == '/')
                slash_counter ++;
        }

        else{
            if(base == NULL){
                prev_v_node = curproc->p_cwd;
            }
            else{
                prev_v_node = base;
            }
        }
            
        vref(prev_v_node);

        int start_index = (pathname[0] == '/' ? slash_counter : 0);
        int end_index = 0;
        int terminate = 0;
        slash_counter = 0;
        /*  do the finding */
        while(1){

            int i = start_index;
            
            while(i < (int) strlen(pathname)){
                if(pathname[i] == '/'){
                    end_index = i - 1;
                    slash_counter = i;
                    while(pathname[slash_counter] == '/')
                        slash_counter ++;
                    slash_counter = slash_counter - i;
                    break;
                }
                i ++;
            }

            if(i == (int)strlen(pathname)){
                /*t is the end..*/
                end_index = (int)strlen(pathname)  - 1;
                terminate = 1;
            }
              
            else if( i + slash_counter == (int)strlen(pathname)){
                end_index = i - 1;
                terminate = 1;
            }
            
            int component_length = end_index - start_index + 2;
            if(component_length > STR_MAX){
                vput(prev_v_node);
                return -ENAMETOOLONG;
            }

            char* next_name = (char*)kmalloc(component_length);
            /*  char next_name[end_index - start_index + 2]; */
            
            strncpy(next_name, pathname + start_index, end_index - start_index + 1);
            next_name[end_index - start_index + 1] = '\0';
            if(terminate == 1){
                /* check if dir */
                if(prev_v_node -> vn_mode != S_IFDIR){
                    vput(prev_v_node);
                    kfree(next_name);
                    return -ENOTDIR;
                }
                *namelen = strlen(next_name);
                *name = next_name;
                /*strncpy(*name, *next_name, *namelen);*/
                *res_vnode = prev_v_node;
                break;
            }
            int res;
            if( (res = lookup(prev_v_node, next_name, strlen(next_name), &next_v_node)) != 0){
                /* no entry or not dir */
                vput(prev_v_node);
                kfree(next_name);
                return res;
            }
            
            /* decrement the reference count */
            vput(prev_v_node);
            start_index = end_index + slash_counter + 1;
            prev_v_node = next_v_node;
            kfree(next_name);
            slash_counter = 0;
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

        /*  flag:
         *  O_CREAT
         *  O_TRUNC
         *  O_APPEND
         */

        if(pathname == NULL)
            return -1;
        size_t namelen;
        const char* name;
        vnode_t* dir_vnode = NULL;
        int res;
        if( (res = dir_namev(pathname, &namelen, &name, base, &dir_vnode))  != 0){
            if(dir_vnode != NULL)
                vput(dir_vnode);
            return res;
        }

        /* Now we get the vnode of parent directory and the name of the target
         * file*/
        vnode_t* result = NULL;
        res = lookup(dir_vnode, name, namelen, &result) ;
        if( res == -ENOENT){
            if((flag & O_CREAT) != 0){
                if( (res = dir_vnode -> vn_ops -> create(dir_vnode, name, namelen, &result)) < 0){ 
                    vput(dir_vnode);
                    return res;
                }

            }
            else{
                vput(dir_vnode);
                return -ENOENT;
            }
        }
        vput(dir_vnode);
        *res_vnode = result;
        return 0;
}

/* #ifdef __GETCWD__*/

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
        if(dir == NULL || entry == NULL)
            return -1;

        int offset = 0;
        struct dirent* dent = NULL;
        while(offset != dir -> vn_len){
            offset = dir -> vn_ops -> readdir(dir, offset, dent);
            /*check whether the ent is the target*/
            ino_t inode_number = dent -> d_ino;
            if(inode_number == entry -> vn_vno){
                /* found the target */
                size_t name_length = strlen(dent -> d_name);
                if(size < name_length + 1){
                    strncpy(buf, dent->d_name, size - 1);
                    buf[size] = '\0';
                    return -ERANGE;
                }
                else{
                    strncpy( buf, dent->d_name, name_length);
                    return 0;
                }

            }
        }
        /* at the end, not found.. */
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

/*  helper struct  */
typedef struct name_list{
    list_link_t name_link;
    char* name;
}name_list_t;

ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        if(dir == NULL)
            return -ENOENT;
        if((osize == 0 && buf != NULL) || (buf == NULL))
            return -EINVAL;
        
        /* always getting the previous */

        vnode_t* up_vnode = NULL;
        vnode_t* low_vnode = dir;
        lookup(dir, "..", 3, &up_vnode);
        list_t names_list;
        list_init(&names_list);
        size_t total_length = 0;

        while(up_vnode != low_vnode){
            char* namebuf = kmalloc(STR_MAX);
            memset(namebuf, 0, STR_MAX);
            if( ( lookup_name(up_vnode, low_vnode, namebuf + 1, STR_MAX - 1)) != 0){
                return -1;
            }
            namebuf[0] = '/';
            total_length += strlen(namebuf);
            /* add this portion to list */
            name_list_t* name_node = kmalloc(sizeof(name_list_t));
            memset(name_node, 0, sizeof(name_list_t));
            name_node -> name = namebuf;
            list_insert_head(&names_list, &name_node->name_link);
            low_vnode = up_vnode;
            lookup(low_vnode, "..", 3, &up_vnode);
        }
        total_length += 1; /* for null terminator */
        if(total_length > osize)
            return -ERANGE;

        /* recover the full */
        size_t offset = 0;
        size_t exceed_size = 0;
        name_list_t* traverser;
        list_iterate_begin(&names_list, traverser, name_list_t, name_link)
        {
            if(offset < osize - 1){
                size_t required_length = strlen(traverser -> name);
                size_t remaining_length = osize - offset;
                if(remaining_length - 1 >= required_length){
                    memcpy(buf + offset, traverser -> name, required_length);
                    offset += required_length;
                }
                else{
                    memcpy(buf + offset, traverser -> name, remaining_length - 1);
                    buf[osize - 1] = '\0';
                    offset = osize - 1;
                }
            }
            list_remove(&traverser->name_link);
            kfree(traverser->name);
            traverser -> name = NULL;
            kfree(traverser);
        }
        list_iterate_end();
        
        if(offset < osize - 1)
            buf[offset + 1] = '\0';
        
        return 0;
}
 /* #endif  __GETCWD__ */






