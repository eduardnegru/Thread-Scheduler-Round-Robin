/*
 * Threads scheduler header
 *
 * 2017, Operating Systems
 */

#ifndef SO_SCHEDULER_H_
#define SO_SCHEDULER_H_

/* OS dependent stuff */
#ifdef __linux__
#include <pthread.h>

#define DECL_PREFIX

typedef pthread_t tid_t;
#elif defined(_WIN32)
#include <windows.h>

#ifdef DLL_IMPORTS
#define DECL_PREFIX __declspec(dllimport)
#else
#define DECL_PREFIX __declspec(dllexport)
#endif

typedef DWORD tid_t;
#else
#error "Unknown platform"
#endif

/*
 * the maximum priority that can be assigned to a thread
 */
#define SO_MAX_PRIO 5
/*
 * the maximum number of events
 */
#define SO_MAX_NUM_EVENTS 256

/*
 * return value of failed tasks
 */
#define INVALID_TID ((tid_t)0)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * handler prototype
 */
typedef void (so_handler)(unsigned int);

#define NEW 0
#define READY 1
#define RUNNING 2
#define WAITING 3
#define TERMINATED 4

typedef struct thread
{
    unsigned int priority;
    unsigned int state;
    unsigned int event;
    unsigned int receivedSignal;
    unsigned int time_quantum;
    pthread_t tid;
    pthread_cond_t cond_event;
    pthread_cond_t cond_ready_running_state;
    pthread_cond_t cond_start_thread_called;
    pthread_mutex_t mutex;
    struct thread *next;
    struct thread* parent;
}thread;

typedef struct thread_scheduler
{
    thread *head;
    pthread_mutex_t lock;
    unsigned int events_max_count;
    unsigned int threads_current_size;
    unsigned int time_quantum;
    unsigned int isInitialized;
    pthread_cond_t cond_all_threads_finished;
    pthread_mutex_t finished_lock;
    pthread_mutex_t condition_lock;
}thread_scheduler;


typedef struct function_param
{
    unsigned int priority;
    so_handler *function;
}function_param;

thread_scheduler scheduler;


/*
 * creates and initializes scheduler
 * + time quantum for each thread
 * + number of IO devices supported
 * returns: 0 on success or negative on error
 */
DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io);

/*
 * creates a new so_task_t and runs it according to the scheduler
 * + handler function
 * + priority
 * returns: tid of the new task if successful or INVALID_TID
 */
DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority);

/*
 * waits for an IO device
 * + device index
 * returns: -1 if the device does not exist or 0 on success
 */
DECL_PREFIX int so_wait(unsigned int io);

/*
 * signals an IO device
 * + device index
 * return the number of tasks woke or -1 on error
 */
DECL_PREFIX int so_signal(unsigned int io);

/*
 * does whatever operation
 */
DECL_PREFIX void so_exec(void);

/*
 * destroys a scheduler
 */
DECL_PREFIX void so_end(void);

#ifdef __cplusplus
}
#endif

#endif /* SO_SCHEDULER_H_ */

