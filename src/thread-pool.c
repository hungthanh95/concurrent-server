#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/prctl.h>
#include <signal.h>
#include <time.h>

#include "thread-pool.h"

#ifdef THPOOL_DEBUG
#define THPOOL_DEBUG 1
#else
#define THPOOL_DEBUG 0
#endif

#if !defined(DISABLE_PRINT) || defined(THPOOL_DEBUG)
#define err(str) fprintf(stderr, str)
#else
#define err(str)
#endif

/**************************** LOCAL VARIABLES ********************************/
static volatile int threads_keep_alive;
static volatile int threads_on_hold;

/**************************** LOCAL FUNCTIONS ********************************/
// Thread functions
static int thread_init(threadpool_* thpool_p, thread** thread_p, int id);
static void* thread_do(struct thread* thread_p);
static void thread_hold(int sig_id);
static void thread_destroy(thread* thread_p);

// Job queue functions
static int jobqueue_init(jobqueue * jobqueue_p);
static job* jobqueue_pull(jobqueue* jobqueue_p);
static void jobqueue_push(jobqueue* jobqueue_p, struct job* newjob);
static void jobqueue_destroy(jobqueue* jobqueue_p);
static void jobqueue_clear(jobqueue* jobqueue_p);

// Semaphore functions
static void bsem_init(bsem* bsem_p, int value);
static void bsem_reset(bsem* bsem_p);
static void bsem_wait(bsem* bsem_p);
static void bsem_post(bsem* bsem_p);
static void bsem_post_all(bsem *bsem_p);

/**************************** GLOBAL FUNCTIONS ********************************/
/**
 * @brief Initializes a threadpool
 * 
 * @param num_threads       Number of threads to be created in the threadpool
 * 
 * @return threadpool_*     return created threadpool on success
 *                          NULL on error     
 */
threadpool_* threadpool_init(int num_threads) 
{
    threads_on_hold = 0;
    threads_keep_alive = 1;

    if (num_threads < 0) {
        num_threads = 0;
    }

    /* Make new thread pool */
    threadpool_* l_thpool_p;
    l_thpool_p = (threadpool_*)malloc(sizeof(threadpool_));
    if (l_thpool_p == NULL) {
        err("threadpool_init(): Could not allocate memory for thread pool\n");
        return NULL;
    }
    l_thpool_p->num_threads_alive = 0;
    l_thpool_p->num_threads_working = 0;

    /* Init job queue */
    if(jobqueue_init(&l_thpool_p->jobqueue) == -1) {
        err("threadpool_init(): Could not allocate memory for job queue\n");
        free(l_thpool_p);
        return NULL;
    }

    /* make threads in pool */
    l_thpool_p->threads = (struct thread**)malloc(num_threads * sizeof(struct thread *));
    if (l_thpool_p->threads == NULL) {
        err("threadpool_init(): Could not allocate memory for threads\n");
        jobqueue_destroy(&l_thpool_p->jobqueue);
        free(l_thpool_p);
        return NULL;
    }

    /* Initialize mutex */
    pthread_mutex_init(&(l_thpool_p->count_lock), NULL);
    pthread_cond_init(&(l_thpool_p->threads_all_idle), NULL);

    /* Thread init */
    for (int n = 0; n < num_threads; n++) {
        thread_init(l_thpool_p, &l_thpool_p->threads[n], n);
        printf("threadpool_init(): Created thread %d in pool\n", n);
    }

    /* wait for thread initialized */
    while (l_thpool_p->num_threads_alive != num_threads) {};
    
    
    return l_thpool_p;
}


/**
 * @brief Take an action and its argument and adds it to the threadpool's job queue.
 * 
 * @param pool_p            Threadpool to which the work will be added
 * @param function_p        Pointer to function to add as work
 * @param arg_p             Pointer to an argument of function as work
 * 
 * @return int              0 on success, -1 otherwise
 */
int threadpool_add_work(threadpool_* thpool_p, void (*function_p)(void*), void* arg_p)
{
    job* newjob;

    newjob = (struct job*)malloc(sizeof(struct job));
    if (newjob == NULL) {
        err("threadpool_add_work(): Could not allocate memory for new job\n");
        return -1;
    }

    /* add function and argument */
    newjob->function = function_p;
    newjob->arg = arg_p;

    /* add job to queue */
    jobqueue_push(&thpool_p->jobqueue, newjob);
    
    return 0;
}


/**
 * @brief Construct a new threadpool wait object.
 *        Wait for all queued jobs to finish
 * 
 * @param pool_p            The threadpool to wait for
 * 
 * @return                  Nothing
 */
void threadpool_wait(threadpool_* thpool_p)
{
    pthread_mutex_lock(&thpool_p->count_lock);
    while (thpool_p->jobqueue.len || thpool_p->num_threads_working) {
        pthread_cond_wait(&thpool_p->threads_all_idle, &thpool_p->count_lock);
    }
    pthread_mutex_unlock(&thpool_p->count_lock);
}


/**
 * @brief Destroy the threadpool
 *        They will wait for the currently active threads to finish and then kill
 *        whole threadpool to free up memory.
 * 
 * @param pool_p            The threadpool to destroy
 * 
 * @return                  Nothing
 */
void threadpool_destroy(threadpool_* thpool_p) {
    /* No need to destroy if it's NULL */
    if (thpool_p == NULL) return;

    volatile int threads_total = thpool_p->num_threads_alive;

    /* End each thread's to kill idle threads */
    threads_keep_alive = 0;

    /* Give one second to kill idle threads */
    double TIMEOUT = 1.0;
    time_t start, end;

    double tpassed = 0.0;
    time(&start);

    while (tpassed < TIMEOUT && thpool_p->num_threads_alive) {
        bsem_post_all(thpool_p->jobqueue.has_jobs);
        time(&end);
        tpassed = difftime(end, start);
    }

    /* Polling remaining threads */
    while (thpool_p->num_threads_alive) {
        bsem_post_all(thpool_p->jobqueue.has_jobs);
        sleep(1);
    }

    /* job queue cleanup */
    jobqueue_destroy(&thpool_p->jobqueue);
    
    /* deadllocs */
    for (int n = 0; n < threads_total; n++) {
        thread_destroy(thpool_p->threads[n]);
    }
    
    free(thpool_p->threads);
    free(thpool_p);
}


/**
 * @brief Pause all threads immediately
 *        The threads return to their previous states once thread_resume is called
 *        While the thread is being paused, new work can be added.
 * 
 * @param pool_p            The threadpool which should be paused
 * 
 * @return                  Nothing
 */
void threadpool_pause(threadpool_* thpool_p)
{
    for (int n = 0; n < thpool_p->num_threads_alive; n++) {
        pthread_kill(thpool_p->threads[n]->pthread, SIGUSR1);
    }
}


/**
 * @brief Unpauses all threads if they are paused
 * 
 * @param pool_p            The threadpool which should be unpaused
 * 
 * @return                  Nothing
 */
void threadpool_resume(threadpool_* thpool_p)
{
    (void)thpool_p;
    threads_on_hold = 0;
}


/**
 * @brief Show currently working threads
 * 
 * @param pool_p            The threadpool that will being show
 * 
 * @return int              Number of threads working
 */
int threadpool_threads_working(threadpool_* thpool_p)
{
    return thpool_p->num_threads_working;
}


/**************************** LOCAL FUNCTIONS ********************************/
/*------------- THREAD FUNCTIONS ------------*/
/* Initialize thread in thread pool */
static int thread_init(threadpool_* thpool_p, thread** thread_p, int id)
{
    *thread_p = (struct thread*)malloc(sizeof(struct thread));
    if (*thread_p == NULL) {
        err("thread_init(): Could not allocate memory for thread\n");
        return -1;
    }

    (*thread_p)->thpool_p = thpool_p;
    (*thread_p)->id       = id;

    pthread_create(&(*thread_p)->pthread, NULL, (void * (*)(void *))thread_do, (*thread_p));
    pthread_detach((*thread_p)->pthread);
    return 0;
}


/* What thread is doing */
static void* thread_do(struct thread* thread_p)
{
    char thread_name[16] = {0};
    snprintf(thread_name, 16, "threadpool-%d", thread_p->id);

    /* use prctl instead to prevent using _GNU_SOURCE flag implicit declaration */
    prctl(PR_SET_NAME, thread_name);

    /* assure all threads have been created before starting serving */
    threadpool_* l_thpool_p = thread_p->thpool_p;

    /* Register signal handler */
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = thread_hold;
    if (sigaction(SIGUSR1, &act, NULL) == -1) {
        err("thread_do(); cannot handle SIGUSER1");
    }

    /* mark thread as alive (initialized )*/
    pthread_mutex_lock(&l_thpool_p->count_lock);
    l_thpool_p->num_threads_alive++;
    pthread_mutex_unlock(&l_thpool_p->count_lock);

    while (threads_keep_alive) {
        /* waiting until has job */
        bsem_wait(l_thpool_p->jobqueue.has_jobs);
        
        if (threads_keep_alive) {
            pthread_mutex_lock(&l_thpool_p->count_lock);
            l_thpool_p->num_threads_working++;
            pthread_mutex_unlock(&l_thpool_p->count_lock);

            /* Read job from queue and execute it */
            void (*func_buff)(void *);
            void* arg_buff;
            job* job_p = jobqueue_pull(&l_thpool_p->jobqueue);
            if (job_p) {
                func_buff = job_p->function;
                arg_buff = job_p->arg;
                /* execute job */
                func_buff(arg_buff);
                free(job_p);
            }

            pthread_mutex_lock(&l_thpool_p->count_lock);
            l_thpool_p->num_threads_working--;

            if (!l_thpool_p->num_threads_working) {
                pthread_cond_signal(&l_thpool_p->threads_all_idle);
            }
            pthread_mutex_unlock(&l_thpool_p->count_lock);
        }
    }
    pthread_mutex_lock(&l_thpool_p->count_lock);
    l_thpool_p->num_threads_alive--;
    pthread_mutex_unlock(&l_thpool_p->count_lock);

    return NULL;
}


/* Set the calling thread on hold */
static void thread_hold(int sig_id)
{
    (void)sig_id;
    threads_on_hold = 1;
    while (threads_on_hold) {
        sleep(1);
    }
    
}


/* free a thread */
static void thread_destroy(thread* thread_p)
{
    free(thread_p);
}


/*------------- JOB QUEUE FUNCTIONS -----------*/
/* Initialize queue */
static int jobqueue_init(jobqueue * jobqueue_p)
{
    jobqueue_p->len = 0;
    jobqueue_p->front = NULL;
    jobqueue_p->rear = NULL;

    jobqueue_p->has_jobs = (struct bsem*)malloc(sizeof(struct bsem));
    if (jobqueue_p->has_jobs == NULL) {
        return -1;
    }

    pthread_mutex_init(&(jobqueue_p->mutex), NULL);
    bsem_init(jobqueue_p->has_jobs, 0);

    return 0;
}


/* Get first job from queue and remove it from queue */
static job* jobqueue_pull(jobqueue* jobqueue_p)
{
    pthread_mutex_lock(&jobqueue_p->mutex);
    job* l_job_p = jobqueue_p->front;

    switch (jobqueue_p->len) {
    case ZERO_JOB:
        /* do nothing */
        break;
    
    case ONE_JOB:
        jobqueue_p->front = NULL;
        jobqueue_p->rear = NULL;
        jobqueue_p->len = 0;
        break;
    
    default:
        /* jobs > 1 */
        jobqueue_p->front = l_job_p->next;
        jobqueue_p->len--;
        /* more than one job in queue -> post it */
        bsem_post(jobqueue_p->has_jobs);
        break;
    }

    pthread_mutex_unlock(&jobqueue_p->mutex);
    
    return l_job_p;
}


/* add job to queue */
static void jobqueue_push(jobqueue* jobqueue_p, struct job* newjob)
{
    pthread_mutex_lock(&jobqueue_p->mutex);
    newjob->next = NULL;

    switch (jobqueue_p->len) {
    case ZERO_JOB:
        jobqueue_p->front = newjob;
        jobqueue_p->rear = newjob;
        break;

    default:
        jobqueue_p->rear->next = newjob;
        jobqueue_p->rear = newjob;
        break;
    }
    jobqueue_p->len++;

    bsem_post(jobqueue_p->has_jobs);
    pthread_mutex_unlock(&jobqueue_p->mutex);
}


/* Free all queue resources */
static void jobqueue_destroy(jobqueue* jobqueue_p)
{
    jobqueue_clear(jobqueue_p);
    free(jobqueue_p->has_jobs);
}


/* Clear the queue */
static void jobqueue_clear(jobqueue* jobqueue_p)
{
    while (jobqueue_p->len) {
        free(jobqueue_pull(jobqueue_p));
    }

    jobqueue_p->front = NULL;
    jobqueue_p->rear = NULL;
    bsem_reset(jobqueue_p->has_jobs);
    jobqueue_p->len = 0;
}


/*------------- JOB QUEUE FUNCTIONS -----------*/


/* Initialize binary semaphore */
static void bsem_init(bsem* bsem_p, int value)
{
    if (value < 0 || value > 1) {
        err("bsem_init(): Binary semaphore can take only values 1 or 0");
        exit(1);
    }
    pthread_mutex_init(&(bsem_p->mutex), NULL);
    pthread_cond_init(&(bsem_p->cond), NULL);
    bsem_p->v = value;
}

/* Reset semaphore to 0 */
static void bsem_reset(bsem* bsem_p)
{
    bsem_init(bsem_p, 0);
}


/* wait on semaphore until semaphore has value 0 */
static void bsem_wait(bsem* bsem_p)
{
    pthread_mutex_lock(&bsem_p->mutex);
    while (bsem_p->v != 1) {
        pthread_cond_wait(&bsem_p->cond, &bsem_p->mutex);
    }
    bsem_p->v = 0;
    pthread_mutex_unlock(&bsem_p->mutex);   
}


 /* notify to at least one thread is waiting that has job */
static void bsem_post(bsem* bsem_p)
{
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->v = 1;
    pthread_cond_signal(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
} 


/* Post to all thread */
static void bsem_post_all(bsem *bsem_p)
{
    pthread_mutex_lock(&bsem_p->mutex);
    bsem_p->v = 1;
    pthread_cond_broadcast(&bsem_p->cond);
    pthread_mutex_unlock(&bsem_p->mutex);
}