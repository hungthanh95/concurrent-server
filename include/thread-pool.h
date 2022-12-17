#ifndef THREAD_POOL_H
#define THREAD_POOL_H


#define     ZERO_JOB         0
#define     ONE_JOB          1

/**************************** DEFINE STRUCTURES ******************************/
/* Binary semaphore */
typedef struct bsem {
    pthread_mutex_t   mutex;
    pthread_cond_t    cond;
    int v;
} bsem;


/* job */
typedef struct job {
    struct job*       next;                     /* pointer to previous job */
    void              (*function)(void* arg);   /* function pointer to job */
    void*             arg;                      /* job's argument */
} job;


/* job queue*/
typedef struct jobqueue {
    pthread_mutex_t   mutex;                    /* used for queue r/w access */
    job*              front;                    /* pointer to front of queue */
    job*              rear;                     /* pointer to rear of queue */
    bsem*             has_jobs;                 /* flag as binary semaphore */
    int               len;                      /* number of jobs in queue */
} jobqueue;



/* thread */
typedef struct thread {
    int               id;                       /* thread id */
    pthread_t         pthread;                  /* pointer to actual thread */
    struct threadpool_* thpool_p;                /* access to threadpool */
} thread;


/* threadpool */
typedef struct threadpool_ {
    thread**          threads;                  /* pointer to threads */
    volatile int      num_threads_alive;        /* threads currently alive */
    volatile int      num_threads_working;      /* threads currently working */
    pthread_mutex_t   count_lock;               /* used for thread count lock */
    pthread_cond_t    threads_all_idle;         /* signal to threadpool_wait */
    jobqueue          jobqueue;                 /* job queue */
} threadpool_;


/**************************** GLOBAL FUNCTIONS *******************************/
/**
 * @brief Initializes a threadpool
 * 
 * @param num_threads       Number of threads to be created in the threadpool
 * 
 * @return threadpool       return created threadpool on success
 *                          NULL on error     
 */
threadpool_* threadpool_init(int num_threads);


/**
 * @brief Take an action and its argument and adds it to the threadpool's job queue.
 * 
 * @param pool_p            Threadpool to which the work will be added
 * @param function_p        Pointer to function to add as work
 * @param arg_p             Pointer to an argument of function as work
 * 
 * @return int              0 on success, -1 otherwise
 */
int threadpool_add_work(threadpool_* pool_p, void (*function_p)(void*), void* arg_p);


/**
 * @brief Construct a new threadpool wait object.
 *        Wait for all queued jobs to finish
 * 
 * @param pool_p            The threadpool to wait for
 * 
 * @return                  Nothing
 */
void threadpool_wait(threadpool_* pool_p);


/**
 * @brief Pause all threads immediately
 *        The threads return to their previous states once thread_resume is called
 *        While the thread is being paused, new work can be added.
 * 
 * @param pool_p            The threadpool which should be paused
 * 
 * @return                  Nothing
 */
void threadpool_pause(threadpool_* pool_p);


/**
 * @brief Unpauses all threads if they are paused
 * 
 * @param pool_p            The threadpool which should be unpaused
 * 
 * @return                  Nothing
 */
void threadpool_resume(threadpool_* pool_p);


/**
 * @brief Destroy the threadpool
 *        They will wait for the currently active threads to finish and then kill
 *        whole threadpool to free up memory.
 * 
 * @param pool_p            The threadpool to destroy
 * 
 * @return                  Nothing
 */
void threadpool_destroy(threadpool_* pool_p);


/**
 * @brief Show currently working threads
 * 
 * @param pool_p            The threadpool that will being show
 * 
 * @return int              Number of threads working
 */
int threadpool_threads_working(threadpool_* pool_p);


#endif /* THREAD_POOL_H */