/*
 * =====================================================================================
 *
 *       Filename:  test.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  03/03/13 20:37:00
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "uthread.h"
#include "uthread_mtx.h"
#include "uthread_cond.h"

#define	NUM_THREADS 3

#define SBUFSZ 256

uthread_id_t	thr[NUM_THREADS];
uthread_mtx_t	mtx1;
uthread_mtx_t   mtx2;
uthread_mtx_t   mtx3;
uthread_cond_t	cond;
 
void pprintf(const char* string){
    char pbuffer[SBUFSZ];
    sprintf(pbuffer, string);  
    write(STDOUT_FILENO, pbuffer, strlen(pbuffer));

}

static void tester1(long a0, void* a1){
    int	i = 0, ret;
    char pbuffer[SBUFSZ];
    
    //uthread_mtx_lock(&mtx);
    while (i < 1)
    {
        i ++;
        uthread_mtx_lock(&mtx1);
        pprintf("thread1 about to block\n");
        uthread_cond_wait(&cond, &mtx1);
        //uthread_mtx_lock(&mtx2);

        sprintf(pbuffer, "thread 1: out of wait\n");  
        ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
        if (ret < 0) 
        {
            perror("uthreads_test");
            /* XXX: we should really cleanup here */
            exit(1);
        }
        
        uthread_mtx_unlock(&mtx1);
        //uthread_mtx_unlock(&mtx2);
               
    }

    sprintf(pbuffer, "thread 1 exiting.\n");  
    ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
    if (ret < 0) 
    {
        perror("uthreads_test");
        exit(1);
    }

    uthread_exit(a0);
}

static void tester2(long a0, void* a1){
    int	i = 0, ret;
    char pbuffer[SBUFSZ];
    
    //uthread_mtx_lock(&mtx);
    while (i < 1)
    {
        i ++;
        uthread_mtx_lock(&mtx1);
        pprintf("thread2 about to block\n");

        //uthread_mtx_lock(&mtx3);
        uthread_cond_wait(&cond,&mtx1);
        sprintf(pbuffer, "thread 2 out of wait\n");  
        ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
        if (ret < 0) 
        {
            perror("uthreads_test");
            /* XXX: we should really cleanup here */
            exit(1);
        }
        
        uthread_mtx_unlock(&mtx1);
        //uthread_mtx_unlock(&mtx3);
               
    }

    sprintf(pbuffer, "thread 2 exiting.\n");  
    ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
    if (ret < 0) 
    {
        perror("uthreads_test");
        /* XXX: we should really cleanup here */
        exit(1);
    }

    uthread_exit(a0);
}

static void tester3(long a0, void* a1){
    int	i = 0, ret;
    char pbuffer[SBUFSZ];
    pprintf("tester3 starts\n");
    
    while (i < 1)
    {
        i ++;
        /*
            uthread_mtx_lock(&mtx1);
            uthread_mtx_lock(&mtx3);
        */
        pprintf("test3 about to broadcast\n");
        uthread_cond_broadcast(&cond);

        sprintf(pbuffer, "thread 3: broadcast ends\n");  
        ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
        if (ret < 0) 
        {
            perror("uthreads_test");
            /* XXX: we should really cleanup here */
            exit(1);
        }
        
        /*  
        uthread_mtx_unlock(&mtx1);
        uthread_mtx_unlock(&mtx3);
        */       
    }

    sprintf(pbuffer, "thread 3 exiting.\n");  
    ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
    if (ret < 0) 
    {
        perror("uthreads_test");
        /* XXX: we should really cleanup here */
        exit(1);
    }

    uthread_exit(a0);
}

int
main(int ac, char **av)
{
    int	i;

    uthread_init();
    
    uthread_mtx_init(&mtx1);
    uthread_mtx_init(&mtx2);
    uthread_mtx_init(&mtx3);

    uthread_cond_init(&cond);
    
    void (*tester[3]) (long a0, void* a1);
    tester[0] = tester1;
    tester[1] = tester2;
    tester[2] = tester3;

    for (i = 0; i < NUM_THREADS; i++)
    {
        
        uthread_create(&thr[i], tester[i], i, NULL,  UTH_MAXPRIO - i % UTH_MAXPRIO
                                        //2
                                        );
    }

    //uthread_setprio(thr[0], 6);

    for (i = 0; i < NUM_THREADS; i++)
    {
        char pbuffer[SBUFSZ];
        int	tmp, ret;

        uthread_join(thr[i], &tmp);
    
        sprintf(pbuffer, "joined with thread %i, exited %i.\n", thr[i], tmp);  
        ret = write(STDOUT_FILENO, pbuffer, strlen(pbuffer));
        if (ret < 0) 
        {
            perror("uthreads_test");
            return EXIT_FAILURE;
        }   

        /* 
        uthread_mtx_lock(&mtx);
        uthread_cond_signal(&cond);
        uthread_mtx_unlock(&mtx);
        */
        
    }
    printf("Main ends\n");
    uthread_exit(0);

    return 0;

}

