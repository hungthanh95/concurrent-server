#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include "thread-pool.h"


void task(void *arg){
	printf("Thread #%u working on %d\n", (int)pthread_self(), (int)arg);
}


int main(int argc, char const *argv[])
{
	printf("Making threadpool with 2 threads\n");
	threadpool_* threadpool = threadpool_init(5);



	for (int i=0; i<1000; i++){
		threadpool_add_work(threadpool, task, (void*)(uintptr_t)i);
		
		if (i == 500) {
			threadpool_pause(threadpool);
			sleep(10);
			threadpool_resume(threadpool);
		}
	};

	threadpool_wait(threadpool);

	printf("Killing threadpool\n");
	threadpool_destroy(threadpool);

	return 0;
}
