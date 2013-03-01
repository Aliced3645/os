/*
 *   FILE: uthread.c
 * AUTHOR: peter demoreuille
 *  DESCR: userland threads
 *   DATE: Sun Sep 30 23:45:00 EDT 2001
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "uthread.h"
#include "uthread_private.h"
#include "uthread_queue.h"
#include "uthread_bool.h"

/* ---------- created by me */
bool uthread_id_bitmap[UTH_MAX_UTHREADS];

/* ---------- globals -- */

uthread_t	*ut_curthr = NULL;		/* current running thread */
uthread_t	uthreads[UTH_MAX_UTHREADS];	/* threads on the system */

static list_t		reap_queue;		/* dead threads */
static uthread_id_t	reaper_thr_id;		/* to wake reaper */


/* ---------- prototypes -- */

static void create_first_thr(void);

static uthread_id_t uthread_alloc(void);
static void uthread_destroy(uthread_t *thread);

static char *alloc_stack(void);
static void free_stack(char *stack);

static void reaper_init(void);
static void reaper(long a0, void *a1);
static void make_reapable(uthread_t *uth);



/* ---------- public code -- */

/*
 * uthread_init
 *
 * Called exactly once when the user process (for which you will be scheduling
 * threads) is started up. Perform all of your global data structure 
 * initializations and other goodies here.  It should go before all the
 * provided code.
 */
void
uthread_init(void)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_init");
        //initialize uthread_id_bitmap
        memset(uthread_id_bitmap, 0, UTH_MAX_UTHREADS * sizeof(int));
        memset(uthreads, 0, UTH_MAX_UTHREADS * sizeof(uthread_t));
        /* these should go last, and in this order */

	uthread_sched_init();
	reaper_init();
	create_first_thr();
}



/*
 * uthread_create
 *
 * Create a uthread to execute the specified function <func> with argument
 * <arg> and initial priority <prio>. To do this, you should first find a
 * valid (unused) id for the thread using uthread_alloc (failing this, return
 * an error).  Next, create a context for the thread to execute on using
 * uthread_makecontext(), set up a uthread_t struct, make the thread runnable
 * and return the aforementioned thread id in <uidp>.  Return 0 on success, -1
 * on error.
 */
//TODO: not sure how to add to runqueue.
int
uthread_create(uthread_id_t *uidp, uthread_func_t func,
	       long arg1, void *arg2, int prio)
{
        assert(uidp != NULL);
        
        *uidp = uthread_alloc();                     
        if(*uidp == -1){
            //retirm an error
            errno = EAGAIN;
            return -1;
        }
        //create a context for the thread
        uthread_t new_thread = uthreads[*uidp];
        
        new_thread.ut_id = *uidp;
        new_thread.ut_state = UT_RUNNABLE;
        new_thread.ut_prio = prio;
        new_thread.ut_errno = 0;
        new_thread.ut_has_exited = false;
        new_thread.ut_detached = false;
        new_thread.ut_waiter = NULL;
        
        char* stack = alloc_stack();
        uthread_makecontext(&new_thread.ut_ctx, stack, UTH_STACK_SIZE, func, arg1, arg2);
        uthread_add_to_runnable_queue(&new_thread);
	
        return 0;

}

/*
 * uthread_exit
 *
 * Terminate the current thread.  Should set all the related flags and
 * such in the uthread_t. 
 *
 * If this is not a detached thread, and there is someone
 * waiting to join with it, you should wake up that thread.
 *
 * If the thread is detached, it should be put onto the reaper's dead
 * thread queue and wakeup the reaper thread by calling make_reapable().
 */
//TODO: uncertain still about uthread_exit
void
uthread_exit(int status)
{

        ut_curthr->ut_state = UT_ZOMBIE;
        ut_curthr->ut_exit = status;
        ut_curthr->ut_has_exited = true;
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_exit");
        if(ut_curthr -> ut_detached == false){
            //if there is a waiter?
            if(ut_curthr-> ut_waiter == NULL){
                //no waiter.
                //still in the runnable queue but ignore it
                //will be reallocated if someone calls uthread_detach()
                return;
            }
            else{
                //wake up the waiter
                uthread_wake(ut_curthr-> ut_waiter);
                //inside the join(), re-allocate the thread
            }
        }
        else if(ut_curthr->ut_detached == true){
            //put onto the reaper's dead thread queue
            make_reapable(ut_curthr);
        }
        
	uthread_switch();
	PANIC("returned to a dead thread");
}


/*
 * uthread_join
 *
 * Wait for the given thread to finish executing. If the thread has not
 * finished executing, the calling thread needs to block until this event
 * happens.
 *
 * Error conditions include (but are not limited to):
 * o the thread described by <uid> does not exist
 * o two threads attempting to join the same thread, etc..
 * Return an appropriate error code (found in manpage for pthread_join) in 
 * these situations (and more).
 *
 * Note that if a thread finishes executing and is never uthread_join()'ed
 * (or uthread_detach()'ed) it remains in the state UT_ZOMBIE and is never 
 * cleaned up. 
 *
 * When you have successfully joined with the thread, set its ut_detached
 * flag to true, and then wake the reaper so it can cleanup the thread by
 * calling make_reapable
 */
int
uthread_join(uthread_id_t uid, int *return_value)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_join");
        if(uthread_id_bitmap[uid] == false){
            errno = ESRCH;
            return -1;
        }
        //get the thread
        uthread_t thread_to_join = uthreads[uid];
        //check for other error conditions
        if(thread_to_join.ut_detached == true){
            errno = EINVAL;
            return -1;
        }
        if(thread_to_join.ut_waiter  != NULL){
            if(thread_to_join.ut_waiter == ut_curthr){
                errno = EDEADLK;
                return -1;
            }
            else{
                errno = EINVAL;
                return -1;
            }
        }
        
        //set the thread_to_join's watier as the caller thread
        thread_to_join.ut_waiter = ut_curthr;
        uthread_block();
  
        //waken up 
        thread_to_join.ut_detached = true;  
        make_reapable(&thread_to_join);
	return 0;
}


/*
 * uthread_detach
 *
 * Detach the given thread. Thus, when this thread's function has finished
 * executing, no other thread need (or should) call uthread_join() to perform
 * the necessary cleanup.
 *
 * There is also the special case if the thread has already exited and then
 * is detached (i.e. was already in the state UT_ZOMBIE when uthread_deatch()
 * is called). In this case it is necessary to call make_reapable on the
 * appropriate thread.
 *
 * There are also some errors to check for, see the man page for
 * pthread_detach (basically just invalid threads, etc).
 * 
 */
int
uthread_detach(uthread_id_t uid)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_detach");
        if(uthread_id_bitmap[uid] == false){
            errno = ESRCH;
            return -1;
        }

        uthread_t thread_to_detach = uthreads[uid];
        //if the thread has already been ZOMBIE, call make_reapable
        if(thread_to_detach.ut_state == UT_ZOMBIE){
            make_reapable(&thread_to_detach);
            return 0;
        }

        //if already detached, return error
        //if someone is joining this thread, return directly with 0
        //otherwise, detach it and set corresponding flags
        if(thread_to_detach. ut_detached == true){
            errno = EINVAL;
            return -1;
        }
        else if(thread_to_detach.ut_detached == false){
            if(thread_to_detach.ut_waiter != NULL){
                return 0;
            }
            else{
                thread_to_detach.ut_detached = true;
                return 0;
            }
        }
                      

	return 0;
}


/*
 * uthread_self
 *
 * Returns the id of the currently running thread.
 */
uthread_id_t
uthread_self(void)
{
	assert(ut_curthr != NULL);
	return ut_curthr->ut_id;
}




/* ------------- private code -- */



/*
 * uthread_alloc
 *
 * find a free uthread_t, returns the id.
 * Remove __attribute__((unused)) when you call this function.
 */
static uthread_id_t
__attribute__((unused)) uthread_alloc(void)
{
	//NOT_YET_IMPLEMENTED("UTHREADS: uthread_alloc");
        //retrive the first index with 0
        int i = 0;
        for(; i < UTH_MAX_UTHREADS; i ++){
            if(uthread_id_bitmap[i] == false){
                uthread_id_bitmap[i] = true;
                return i;
            }
        }
	return -1;
}

/*
 * uthread_destroy
 *
 * Cleans up resources associated with a thread (since it's now finished
 * executing). This is called implicitly whenever a detached thread finishes 
 * executing or whenever non-detached thread is uthread_join()'d.
 */
static void
uthread_destroy(uthread_t *uth)
{
    //NOT_YET_IMPLEMENTED("UTHREADS: uthread_destroy");
    //free the thread id
    int id = uth->ut_id;
    uthread_id_bitmap[id] = false;
    free_stack(uth->ut_stack);
    memset(&uthreads[id], 0, sizeof(uthread_t));
    return;
}






/****************************************************************************
 * You do not have to modify any code below this line
 ****************************************************************************/


/*
 * reaper_init
 *
 * startup the reaper thread
 */
static void
reaper_init(void)
{
	list_init(&reap_queue);
	uthread_create(&reaper_thr_id, reaper, 0, NULL, UTH_MAXPRIO);
        
	assert(reaper_thr_id != -1);
}



/*
 * reaper
 *
 * this is responsible for going through all the threads on the dead
 * threads list (which should all be in the ZOMBIE state) and then
 * cleaning up all the threads that have been detached/joined with
 * already.
 *
 * in addition, when there are no more runnable threads (besides the
 * reaper itself) it will call exit() to stop the program.
 */
static void
reaper(long a0, void *a1)
{
	while(1)
	{
		uthread_t	*thread;
		int		th;

		/* block.  someone will wake me up when it is time */
		uthread_block();

		/* go through dead threads, find detached and
		 * call uthread_destroy() on them
		 */
		list_iterate_begin(&reap_queue, thread, uthread_t, ut_link)
		{
			assert(thread->ut_state == UT_ZOMBIE);

			list_remove(&thread->ut_link);
			uthread_destroy(thread);
		}
		list_iterate_end();

		/* check and see if there are still runnable threads */
		for (th = 0; th < UTH_MAX_UTHREADS; th++)
		{
			if (th != reaper_thr_id &&
			    uthreads[th].ut_state != UT_NO_STATE)
			{
				break;
			}
		}

		if (th == UTH_MAX_UTHREADS)
		{
			/* we leak the reaper's stack */
			fprintf(stderr, "uthreads: no more threads.\n");
			fprintf(stderr, "uthreads: bye!\n");
			exit(0);
		}
	}
}



/*
 * Turns the main context (the 'main' method that initialized
 * this process) into a regular uthread that can be switched
 * into and out of. Must be called from the main context (i.e.,
 * by uthread_init()).
 */
static void
create_first_thr(void)
{
	uthread_t	hack;
	uthread_id_t	main_thr;

	/*
	 * OK, this is a little bit of magic.  Right now, we are not
	 * really running in a uthread, but rather just this context that is
	 * the one that the program starts in.
	 *
	 * We would like to be running inside a real uthread, because then 
	 * we can block, reschedule, and so forth.
	 *
	 * So: First, allocate something to switch away from ("hack"), so 
	 *  the switch routine doesn't panic.
	 * Then, allocate the actual uthread we're switching into.
	 * Clone the current context into our new saved context.
	 * Switch into it.
	 * Return.
	 */

	memset(&hack, 0, sizeof(uthread_t));
	ut_curthr = &hack;
	ut_curthr->ut_state = UT_ON_CPU;

	uthread_create(&main_thr,
		       (uthread_func_t)0, 0, NULL,
			UTH_MAXPRIO);
	assert(main_thr != -1);
	uthread_detach(main_thr);

	/* grab the current context, so when we switch to the main
	 * thread, we start *right here*.  thus, we only want to call 
	 * uthread_switch() if the current thread is the hacky temporary
	 * thread, not a real one.
	 */
	uthread_getcontext(&uthreads[main_thr].ut_ctx);

	if (ut_curthr == &hack)
	{
		uthread_switch();
	}
	else
	{
		/* this should be the 'main_thr' */
		assert(uthread_self() == main_thr);
	}

}

/*
 * Adds the given thread to the reaper's queue, and wakes up the reaper.
 * Called when a thread is completely dead (is detached and exited).
 *
 * Remove __attribute__((unused)) when you call these functions.
 */

static void
make_reapable(uthread_t *uth)
{
	assert(uth->ut_detached);
	assert(uth->ut_state == UT_ZOMBIE);
	list_insert_tail(&reap_queue, &uth->ut_link);
	uthread_wake(&uthreads[reaper_thr_id]);
}



static char
__attribute__((unused)) *alloc_stack(void)
{
	return (char *)malloc(UTH_STACK_SIZE);
}

static void
__attribute__((unused)) free_stack(char *stack)
{
	free(stack);
}



