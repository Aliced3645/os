/*
 *   FILE: uthread_cond.c 
 * AUTHOR: Peter Demoreuille
 *  DESCR: uthreads condition variables
 *   DATE: Mon Oct  1 01:59:37 2001
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "uthread.h"
#include "uthread_mtx.h"
#include "uthread_cond.h"
#include "uthread_queue.h"


/*
 * uthread_cond_init
 *
 * initialize the given condition variable
 */
void
uthread_cond_init(uthread_cond_t *cond)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_cond_init");
        assert(cond != NULL);
        utqueue_init(&cond->uc_waiters);
}


/*
 * uthread_cond_wait
 *
 * Should behave just like a stripped down version of pthread_cond_wait.
 * Block on the given condition variable.  The caller should lock the
 * mutex and it should be locked again after the broadcast.
 */
void
uthread_cond_wait(uthread_cond_t *cond, uthread_mtx_t *mtx)
{
        assert(cond != NULL);
        assert(mtx != NULL);
        assert(mtx->m_owner == ut_curthr);
        //unlock the mutex..
        uthread_mtx_unlock(mtx);
        //lock on the cond
        utqueue_enqueue(&cond->uc_waiters, ut_curthr);
        uthread_block();
        //woke up by uthread_browadcast
        uthread_mtx_lock(mtx);
        return;
}

 
/*
 * uthread_cond_broadcast
 *
 * Wakeup all the threads waiting on this condition variable.
 * Note there may be no threads waiting.
 */
void
uthread_cond_broadcast(uthread_cond_t *cond)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_cond_broadcast");
        assert(cond != NULL);
        while(!utqueue_empty(&cond->uc_waiters)){
            uthread_t* thread = utqueue_dequeue(&cond->uc_waiters);
            //put back to mutex queue
            thread->ut_state = UT_RUNNABLE;
            //put it onto runnable queue so it could be resumed to
            //uthread_cond_wait
            uthread_add_to_runnable_queue(thread);
        }
}



/*
 * uthread_cond_signal
 *
 * wakeup just one thread waiting on the condition variable.
 * Note there may be no threads waiting.
 */
void
uthread_cond_signal(uthread_cond_t *cond)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_cond_signal");
        assert(cond != NULL);
        if(!utqueue_empty(&cond->uc_waiters)){
            uthread_t* thread = utqueue_dequeue(&cond->uc_waiters);
            //put back to mutex queue
            thread->ut_state = UT_RUNNABLE;
            //put it onto runnable queue so it could be resumed to
            //uthread_cond_wait
            uthread_add_to_runnable_queue(thread);
        }
}
