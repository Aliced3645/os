/*
 *   FILE: uthread_mtx.c 
 * AUTHOR: Peter Demoreuille
 *  DESCR: userland mutexes
 *   DATE: Sat Sep  8 12:40:00 2001
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "list.h"
#include "uthread.h"
#include "uthread_mtx.h"



/*
 * uthread_mtx_init
 *
 * Initialize the fields of the specified mutex.
 */
void
uthread_mtx_init(uthread_mtx_t *mtx)
{
        //NOT_YET_IMPLEMENTED("UTHREADS: uthread_mtx_init");
        assert(mtx != NULL);
        memset(mtx, 0, sizeof(uthread_mtx_t));
        utqueue_init(&mtx->m_waiters);
        mtx->m_owner = NULL;
}


/*
 * uthread_mtx_lock
 *
 * Lock the mutex.  This call will block if it's already locked.  When the
 * thread wakes up from waiting, it should own the mutex (see _unlock()).
 */
void
uthread_mtx_lock(uthread_mtx_t *mtx)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_mtx_lock");
        assert(mtx != NULL);
        if(mtx -> m_owner == NULL)
            mtx -> m_owner = ut_curthr;
        else{
            assert(mtx->m_owner != ut_curthr);
            //put current thread in the waiters queue
            utqueue_enqueue(&mtx->m_waiters, ut_curthr);
            //block current thread
            uthread_block();
        }
}


/*
 * uthread_mtx_trylock
 *
 * Try to lock the mutex, return 1 if we get the lock, 0 otherwise.
 * This call should not block.
 */
int
uthread_mtx_trylock(uthread_mtx_t *mtx)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_mtx_trylock");
        assert(mtx != NULL);
        if(mtx -> m_owner == NULL){
            mtx-> m_owner = ut_curthr;
            return 1;
        }
        else{
            assert(mtx->m_owner != ut_curthr);
            return 0;
        }
}


/*
 * uthread_mtx_unlock
 *
 * Unlock the mutex.  If there are people waiting to get this mutex,
 * explicitly hand off the ownership of the lock to a waiting thread and
 * then wake that thread.
 */
void
uthread_mtx_unlock(uthread_mtx_t *mtx)
{
    assert(mtx != NULL);
    assert(mtx->m_owner == ut_curthr);
    if(utqueue_empty(&mtx->m_waiters)){
        mtx->m_owner = NULL;
        return;
    }
    else{
        //dequeue a thread
        //set it as the owner
        //change it to runnable
        //and put back to runnable queue
        uthread_t* dequeued_thread = utqueue_dequeue(&mtx->m_waiters);
        mtx->m_owner = dequeued_thread;
        dequeued_thread->ut_state = UT_RUNNABLE;
        uthread_add_to_runnable_queue(dequeued_thread);
        return;
    }
}
