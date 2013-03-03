/*
 *   FILE: uthread_sched.c 
 * AUTHOR: Peter Demoreuille
 *  DESCR: scheduling wack for uthreads
 *   DATE: Mon Oct  1 00:19:51 2001
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "uthread.h"
#include "uthread_private.h"
#include "uthread_ctx.h"
#include "uthread_queue.h"
#include "uthread_bool.h"


/* ---------- globals -- */

/* Remove __attribute__((unused)) when you use this variable. */

static utqueue_t runq_table[UTH_MAXPRIO + 1];	/* priority runqueues */

/* ----------- public code -- */
void uthread_add_to_runnable_queue(uthread_t* thread){
    int prio = thread->ut_prio;
    utqueue_enqueue(&runq_table[prio], thread);
}

/*
 * uthread_yield
 *
 * Causes the currently running thread to yield use of the processor to
 * another thread. The thread is still runnable however, so it should
 * be in the UT_RUNNABLE state and schedulable by the scheduler. When this
 * function returns, the thread should be executing again. A bit more clearly,
 * when this function is called, the current thread stops executing for some
 * period of time (allowing another thread to execute). Then, when the time
 * is right (ie when a call to uthread_switch() results in this thread
 * being swapped in), the function returns.
 */
void
uthread_yield(void)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_yield");
        ut_curthr-> ut_state = UT_RUNNABLE;
        uthread_switch();
        return;
}


/*
 * uthread_block
 *
 * Put the current thread to sleep, pending an appropriate call to 
 * uthread_wake().
 */
void
uthread_block(void) 
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_block");
        ut_curthr -> ut_state = UT_WAIT;
        uthread_switch();
        return;
}


/*
 * uthread_wake
 *
 * Wakes up the supplied thread (schedules it to be run again).  The
 * thread may already be runnable or (well, if uthreads allowed for
 * multiple cpus) already on cpu, so make sure to only mess with it if
 * it is actually in a wait state.
 */
void
uthread_wake(uthread_t *uthr)
{
    int state = uthr -> ut_state;
    
    if( state == UT_WAIT){

        uthr -> ut_state = UT_ON_CPU;   
        uthread_t* old_thr = ut_curthr;
        ut_curthr = uthr;
        if(old_thr -> ut_state == UT_ON_CPU){
            old_thr -> ut_state = UT_RUNNABLE;
            uthread_add_to_runnable_queue(old_thr);       
        }
        
        uthread_swapcontext( & old_thr -> ut_ctx, &ut_curthr -> ut_ctx);
    }
    
}


/*
 * uthread_setprio
 *
 * Changes the priority of the indicated thread.  Note that if the thread
 * is in the UT_RUNNABLE state (it's runnable but not on cpu) you should
 * change the list it's waiting on so the effect of this call is
 * immediate.
 */
void
uthread_setprio(uthread_id_t id, int prio)
{
    uthread_t* thread_to_change = &uthreads[id];
    if(thread_to_change->ut_state == UT_RUNNABLE){
        int previous_prio = thread_to_change->ut_prio;
        thread_to_change->ut_prio = prio;
        utqueue_remove(&runq_table[previous_prio], thread_to_change);
        utqueue_enqueue(&runq_table[prio], thread_to_change);
    }
    else{
        //change directly
        thread_to_change->ut_prio = prio;
    }
    return;
}



/* ----------- private code -- */


/*
 * uthread_switch()
 *
 * This is where all the magic is.  Wait until there is a runnable thread, and
 * then switch to it using uthread_swapcontext().  Make sure you pick the
 * highest priority runnable thread to switch to. Also don't forget to take
 * care of setting the ON_CPU thread state and the current thread. Note that
 * it is okay to switch back to the calling thread if it is the highest
 * priority runnable thread.
 *
 * Every time uthread_switch() is called, uthread_idle() should be called at
 * least once.  In addition, when there are no runnable threads, you should
 * repeatedly call uthread_idle() until there are runnable threads.  Threads
 * with numerically higher priorities run first. For example, a thread with
 * priority 8 will run before one with priority 3.
 * */
void
uthread_switch(void)
{

        //we change the location of 
        //the first thread: it might be put in runnable queue
        //or it might be just switched
        uthread_t * old_thr = ut_curthr;
            //check the state
        if( old_thr -> ut_state == UT_RUNNABLE){
                //called by uthread_yield
                uthread_add_to_runnable_queue(old_thr);
        }
        else{
                //wont add to the runnable queue
        }
        while(1){
        //get the next runnable thread.
        //from highest priority queue to low priority queue
        int i = UTH_MAXPRIO;
        uthread_t* next_thread = NULL;
        for(; i >= 0; i --){
            if( !utqueue_empty(&runq_table[i])){
                printf("%d:%d\n", i, runq_table[i].tq_size);
                next_thread = utqueue_dequeue(&runq_table[i]);
                break;
            }
        }

        if(next_thread != NULL){
            //switch!
            next_thread->ut_state = UT_ON_CPU;
            ut_curthr = next_thread;
            uthread_swapcontext(& old_thr -> ut_ctx, &ut_curthr -> ut_ctx);
            return;
        }
        //no runnable threads
        //TODO: Rewrtie uthread_idle
        else{
            uthread_idle();
        }
     }
}



/*
 * uthread_sched_init
 *
 * Setup the scheduler. This is called once from uthread_init().
 */
void
uthread_sched_init(void)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_sched_init");
        int i = 0;
        for(; i < UTH_MAXPRIO + 1; i ++){
            utqueue_init(&runq_table[i]);
        }

}
