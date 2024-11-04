#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <pthread.h>

#define SHARED_MEM_SIZE (5ULL * 1024 * 1024 * 1024) // 5GB
#define SHM_KEY 12345
#define NUM_THREADS 15 // 스레드 수

volatile int *shared_mem;
volatile int *access_flag;

void *access_memory(void *arg) {
    size_t j;
    size_t random_index;
    srand(time(NULL) ^ pthread_self());

    while(1)
    {
        random_index = rand() % (SHARED_MEM_SIZE / sizeof(int));
        shared_mem[random_index]++;
        *access_flag = 1;
    }
    return NULL;
}

int main() {
    // Shared memory 접근
    int shmid = shmget(SHM_KEY, SHARED_MEM_SIZE + sizeof(int), 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *)shmat(shmid, NULL, 0);
    if (shared_mem == (int *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    access_flag = shared_mem + (SHARED_MEM_SIZE / sizeof(int));

    // 스레드 생성 및 실행
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, access_memory, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    // 스레드가 완료될 때까지 기다림
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // 공유 메모리 분리
    if (shmdt((const void*)shared_mem) < 0) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }

    return 0;
}

