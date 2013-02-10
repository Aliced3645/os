#define _POSIX_C_SOURCE 200112L // nanosleep(2)

#include "db.h"
#include "window.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/*
 * TODO (Part 2): This function should be implemented and called by client
 * threads to wait until progress is permitted. Ensure that the mutex is not
 * left locked if the thread is cancelled while waiting on a condition
 * variable.
 */

//a global flag indicating whether clients can handle input..
int clientBlock = 0;//1 if block
pthread_mutex_t clientBlockLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t clientBlockCond = PTHREAD_COND_INITIALIZER;

static void
ClientControl_wait(void) {
    /* TODO (Part 2): Not yet implemented. */
    pthread_mutex_lock(&clientBlockLock);
    while(clientBlock == 1){
        //here we should waiting
        pthread_cond_wait(&clientBlockCond, &clientBlockLock);
    }
    pthread_mutex_unlock(&clientBlockLock);
}

/*
 * TODO (Part 2): This function should be implemented and called by the main
 * thread to stop the client threads.
 */
static void
ClientControl_stop(void) {
	/* TODO (Part 2): Not yet implemented. */
     //just set the bit to 1
     pthread_mutex_lock(&clientBlockLock);
     clientBlock = 1;
     pthread_mutex_unlock(&clientBlockLock);
}

/*
 * TODO (Part 2): This function should be implemented and called by the main
 * thread to resume client threads.
 */
static void
ClientControl_release(void) {
	/* TODO (Part 2): Not yet implemented. */
    pthread_mutex_lock(&clientBlockLock);
    clientBlock = 0;
    pthread_mutex_unlock(&clientBlockLock);
    //notify all
    pthread_cond_broadcast(&clientBlockCond);
}

/*
 * the encapsulation of a client thread, i.e., the thread that handles commands
 * from clients
 */
typedef struct Client {
#ifdef _PTHREAD_H
	pthread_t thread;
#endif
	window_t *win;
	/*
	 * The thread list (Part 3):
	 * Client threads put themselves in the list and take themselves out,
	 * thus guaranteeing that a client's being in the list means that its
	 * thread is active and thus may be safely cancelled.
	 *
	 * See also ThreadListHead and ThreadListMutex, below.
	 */
	struct Client *prev;
	struct Client *next;
} Client_t;

Client_t *ThreadListHead = NULL;
Client_t *ThreadListTail = NULL;

#ifdef _PTHREAD_H
pthread_mutex_t ThreadListMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void *RunClient(void *);
static int handle_command(const char *, char *, size_t);
static void ThreadCleanup(void *);
static void DeleteAll(void);
static void Client_destructor(Client_t* client);


//print all clients
void PrintAllThreads(){
    Client_t* traverser = ThreadListHead;
    while(traverser != NULL){
        printf("%d\n", (int)traverser -> thread);
        traverser = traverser -> next;
    }
}
//helper function, add to linkedlist of Client_t
void AddThreadList(Client_t* client){

    if(ThreadListHead == NULL){
        ThreadListHead = ThreadListTail = client;
        return;
    }
    else{
        ThreadListTail -> next = client;
        client->prev = ThreadListTail;
        ThreadListTail = client;
    }
    return;
}

//return 0 if success
//return -1 if target not found
int DeleteThreadFromList(Client_t* client){

    //special case happens in the head
    if(ThreadListHead == NULL)
        return -1;
    else if(ThreadListHead == ThreadListTail){
        if(ThreadListTail == client){
            Client_destructor(client);
            ThreadListTail = ThreadListHead = NULL;
            return 0;
        }
    }
    else{
        Client_t* slow = ThreadListHead;
        Client_t* fast = ThreadListHead -> next;
        //take care of the first
        if(slow == client){
            fast ->prev = NULL;
            ThreadListHead = fast;
            Client_destructor(client);
            return 0;
        }

        while(fast != NULL){
            if(fast == client){
                slow->next = fast->next;
                if(fast != ThreadListTail){
                    fast->next->prev = slow;
                }
                else{
                    ThreadListTail = slow;
                }

                Client_destructor(client);
                return 0;
            }
        }
    }

    return -1;
}

void DeleteAllClients(){
    //delete all threads
    Client_t* traverser = ThreadListHead;
    while(traverser != NULL){
        Client_t* todelete = traverser;
        traverser = traverser -> next;
        Client_destructor(todelete);
    }
    ThreadListHead = ThreadListTail = NULL;
    return;
}


/*
 * TODO (Part 1): Modify this function such that a new thread is created and
 * detached that runs RunClient.
 *
 * TODO (Part 3): Note that window_constructor must be executed atomically:
 * cancellation must be disabled within it.
 *
 * TODO (Part 3): See the comment in RunClient about a barrier.
 */
int client_counter = 0;
static Client_t *
Client_constructor()
{

        char title[16];
	Client_t *new_Client = malloc(sizeof (Client_t));
	if (new_Client == NULL)
		return (NULL);
	sprintf(title, "Client %d", client_counter);
                
	/*
	 * This constructor creates a window and sets up a communication
	 * channel with it.
	 */
	new_Client->win = window_constructor(title);

	//return (new_Client);
        int res = pthread_create(&new_Client->thread, NULL, RunClient, new_Client);
        if(res != 0){
            printf("Error creating client thread: %s\n", strerror(errno));
            //clean the client structure
            Client_destructor(new_Client);
            return NULL;
        }
        client_counter ++;
        //also add to the link list

        return new_Client;
        
}

static void
Client_destructor(Client_t *client)
{
	/* The destructor removes the window. */
	window_destructor(client->win);
	free(client);
}

/*
 * TODO (Part 3): This function cancels every thread in the list (see
 * ThreadListHead). Each thread must remove itself from the thread list.
 */
static void
DeleteAll()
{
	/* TODO (Part 3): Not yet implemented. */
}

/*
 * This is the cleanup routine for client threads, called on cancel and exit.
 *
 * TODO (Part 1): This function should free the client instance by calling
 * Client_destructor.
 *
 * TODO (Part 3): This function pulls the client thread from the thread list
 * (see ThreadListHead) and deletes it. Thus the client thread must be in the
 * thread list before the thread acts on a cancel.
 */
static void
ThreadCleanup(void *arg)
{
	/*
	 * TODO (Part 1): After implementing this function, remove
	 * "__attribute__((unused))" below.
	 */
	Client_t *client __attribute__((unused)) = arg;

	/* TODO (Part 1): Not yet implemented. */
}

/*
 * This struct helps keep track of how long it's been since a command has been
 * received by a client thread. The client thread is cancelled if it's been too
 * long. For each client thread an associated "watchdog thread" is created
 * which loops, sleeping wait_secs seconds since the last time the client
 * thread received input. At each wakeup, it checks to see if the client thread
 * has received input since the watchdog went to sleep. If not, the watchdog
 * cancels the client thread, then terminates itself. Otherwise it loops again,
 * sleeping wait_secs seconds since the last time the client thread received
 * input ...
 */
typedef struct Timeout {
	/* time when client thread "times out" */
	struct timeval timeout;

	/* set when client thread receives input */
	/* reset each time watchdog thread wakes up */
	int active;

	Client_t *client;

#ifdef _PTHREAD_H
	pthread_mutex_t timeout_lock;
	pthread_t WatchDogThread;
#endif
} Timeout_t;

static time_t Timeout_wait_secs = 10; /* timeout interval */

static void Timeout_reset(Timeout_t *);
static void *Timeout_WatchDog(void *); /* watchdog thread */

/*
 * TODO (Part 3): Allocate a new timeout instance (described by a Timeout_t)
 * and initialize its members. Set the initial timeout to Timeout_wait_secs
 * from the current time; see gettimeofday(2) and friends. Finally, create a
 * watchdog thread for this timeout.
 */
static Timeout_t *
Timeout_constructor(Client_t *a_client)
{
	/* TODO (Part 3): Not yet implemented. */
	return (NULL);
}

/*
 * TODO (Part 3): Destroy this timeout instance. Be sure to cancel the watchdog
 * thread and join with it.
 */
static void
Timeout_destructor(Timeout_t *timeout)
{
	/* TODO (Part 3): Not yet implemented. */
}

/*
 * This function contains code executed by the client.
 *
 * TODO (Part 1): Make sure that the client instance is properly freed when the
 * thread terminates by calling ThreadCleanup. We strongly suggest you use
 * pthread_cleanup_push(3) and pthread_cleanup_pop(3) for this purpose.
 *
 * TODO (Part 3): Make sure that the timeout instance (and its watchdog thread)
 * is properly freed when the thread terminates by calling Timeout_destructor.
 * We strongly suggest you use pthread_cleanup_push(3) and
 * pthread_cleanup_pop(3) for this purpose.
 */
static void *
RunClient(void *arg)
{
	Client_t *client = arg;

	/*
	 * Establish our watchdog thread to deal with timeouts. Timeout's
	 * destructor cancels the watchdog thread and doesn't return until
	 * after joining with it.
	 */
	Timeout_t *timeout = Timeout_constructor(client);

	/*
	 * TODO (Part 3): Add the client thread to the thread list (see
	 * ThreadListHead).
	 */

	/* The client thread is now fully started. */

	/*
	 * TODO (Part 3): Other threads waiting for this thread need to be
	 * notified somehow. (Hint: Consider the watchdog thread, which
	 * shouldn't worry about timeouts till now, and the main thread, which
	 * must not call DeleteAll until this thread is fully started. A
	 * barrier might be useful.)
	 */

	/*
	 * main loop of the client: fetch commands from window, interpret and
	 * handle them, return results to window
	 */
        while(1)
	{

                char command[256];
		/* response must be NULL for the first call to serve */
		char response[256] = {0};

		serve(client->win, response, command);
                
                /* we've received input: reset timer */
		Timeout_reset(timeout);

		while (handle_command(command, response, sizeof (response))) {
			/* we've processed a command: reset timer */
                        ClientControl_wait();

			Timeout_reset(timeout);

			serve(client->win, response, command);

			/* we've received input: reset timer */
			Timeout_reset(timeout);
		}
                

	}
        
	return (NULL);
}

static int
handle_command(const char *command, char *response, size_t len)
{
	if (command[0] == EOF) {
		strncpy(response, "all done", len - 1);
		return (0);
	} else {
		interpret_command(command, response, len);
	}
	return (1);
}

/*
 * TODO (Part 3): This function is called by client threads each time they
 * receive a command. It stores the current time + Timeout_wait_secs in
 * timeout; see gettimeofday(2)_and friends. This function also indicates that
 * new input has been received. If the timeout expires and no further input has
 * been received, then the watchdog thread cancels us. Note: be sure to lock
 * the appropriate mutex!
 */
void
Timeout_reset(Timeout_t *timeout)
{
	/* TODO (Part 3): Not yet implemented. */
}

/*
 * There is one watchdog thread per client thread. It cancels the client if it
 * hasn't received a command within the specified time period (stored in
 * wait_secs).
 */
void *
Timeout_WatchDog(void *arg)
{
	Timeout_t *timeout = arg;
	struct timespec to;

	/*
	 * TODO (Part 3): Make sure that the timeout clock doesn't start till
	 * the client is fully started, and thus that the thread won't time out
	 * until after it has fully started. A barrier may be useful here.
	 */

	to.tv_sec = Timeout_wait_secs;
	to.tv_nsec = 0;

	while (1) {
		struct timeval now;
                /*
		 * sleep for the current timeout interval
		 * (since thread was started or last input)
		 */
		if (nanosleep(&to, NULL) == -1) {
			perror("nanosleep");
			exit(EXIT_FAILURE);
		}

		if (timeout->active == 0) {
			/*
			 * TODO (Part 3): Our client thread hasn't received
			 * input in a while, so cancel it (and self-destruct).
			 */
		} else {
			timeout->active = 0;
		}

		/*
		 * Set the next timeout to be the requested number of seconds
		 * since the last client input; convert an absolute timeout to
		 * an interval.
		 */
#ifdef _PTHREAD_H
		pthread_mutex_lock(&timeout->timeout_lock);
#endif
		gettimeofday(&now, NULL);
		to.tv_sec = timeout->timeout.tv_sec - now.tv_sec;
		to.tv_nsec = 1000 * (timeout->timeout.tv_usec - now.tv_usec);
		if (to.tv_nsec < 0) {
			/* "borrow" from seconds */
			to.tv_sec--;
			to.tv_nsec += 1000000000;
		}
#ifdef _PTHREAD_H
		pthread_mutex_unlock(&timeout->timeout_lock);
#endif
	}

	return (NULL);
}

typedef struct SigHandler {
	sigset_t set;
#ifdef _PTHREAD_H
	pthread_t SigThread;
#endif
} SigHandler_t;

static void *SigMon(void *);

/*
 * TODO (Part 3): Allocate a new instance of a signal handler (SigHandler_t),
 * mask off Ctrl-C (SIGINT, see sigsetops(3)), and create the signal-handling
 * thread (SigMon).
 */
static SigHandler_t *
SigHandler_constructor()
{
	/* TODO (Part 3): Not yet implemented. */
	return (NULL);
}

/*
 * TODO (Part 3): Destroy an instance of a signal handler. Be sure to cancel
 * the signal-handling thread and join with it.
 */
static void
SigHandler_destructor(SigHandler_t *sighandler)
{
	/* TODO (Part 3): Not yet implemented. */
}

/*
 * This function contains the code for the signal-handling thread.
 *
 * TODO (Part 3): When a ctrl-C occurs, delete all current client
 * threads with DeleteAll.
 */
static void *
SigMon(void *arg) {
	/*
	 * TODO (Part 3): After implementing this function, remove
	 * "__attribute__((unused))" below.
	 */
	sigset_t *setp __attribute__((unused)) = arg;

	/* TODO (Part 3): Not yet implemented. */

	return (NULL);
}

/*
 * TODO (Part 1): Modify this function so that no window is created
 * automatically. Instead, every time you type enter (in the window in which
 * you're running server), a new window is created along with a new thread to
 * handle it.
 */
#define MAX_LENGTH 255

int
main(int argc, char *argv[])
{
	//Client_t *c;
	SigHandler_t *sig_handler;

	if (argc == 2) {
		Timeout_wait_secs = atoi(argv[1]);
	} else if (argc > 2) {
		fprintf(stderr, "Usage: server [timeout_secs]\n");
		exit(EXIT_FAILURE);
	}

	sig_handler = SigHandler_constructor();
        pthread_rwlock_init(&coarseDBLock, NULL);
        //initialize the lock for head
        pthread_rwlock_init(&head.lock, NULL);
        char* command = (char*) malloc(MAX_LENGTH);
        
        while(1){
            //capture user action(pressing enter)
            int byte_read = read(STDIN_FILENO, command, MAX_LENGTH);
            if(byte_read == 0){
                //CTRL+D
                printf("asdf\n");
                DeleteAllClients();
                exit(1);
            }
            else if(command[0] == 's' && command[1] == '\n'){
                printf("stopped\n");
                ClientControl_stop();
            }
            else if(command[0] == 'g' && command[1] == '\n'){
                printf("released\n");
                ClientControl_release();
            }
            else if(command[byte_read - 1 ] == 10){
                Client_t* new_client = Client_constructor();
                AddThreadList(new_client);
                pthread_detach(new_client->thread);
            }
            else{
                write(STDOUT_FILENO, "\n", 2);
            }
            memset(command, MAX_LENGTH, 0);
        }

        SigHandler_destructor(sig_handler);
        pthread_mutex_destroy(&clientBlockLock);
        pthread_cond_destroy(&clientBlockCond);
	pthread_rwlock_destroy(&coarseDBLock);

        DeleteAll();
	cleanup_db();
	return (EXIT_SUCCESS);
}
