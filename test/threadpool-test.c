#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "thread-pool.h"


void task(void *arg){
	printf("Thread #%u working on %d\n", (int)pthread_self(), (int)arg);
}


int main(int argc, char const *argv[])
{
	printf("Making threadpool with 2 threads\n");
	threadpool_* threadpool = threadpool_init(2);



	for (int i=0; i<1000; i++){
		threadpool_add_work(threadpool, task, (void*)(uintptr_t)i);
	};

	threadpool_wait(threadpool);

	for (int i=1000; i<2000; i++){
		threadpool_add_work(threadpool, task, (void*)(uintptr_t)i);
	};
	printf("Killing threadpool\n");
	threadpool_destroy(threadpool);

	return 0;
}
