#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <numa.h>
#include <numaif.h>
#include <string.h>
#include <time.h>

#define SHM_KEY 12345

volatile int *shared_mem;
volatile int *access_flag;

size_t parse_size_option(const char *size_str) {
    if (strcmp(size_str, "1G") == 0) return 1ULL * 1024 * 1024 * 1024;
    if (strcmp(size_str, "5G") == 0) return 5ULL * 1024 * 1024 * 1024;
    if (strcmp(size_str, "10G") == 0) return 10ULL * 1024 * 1024 * 1024;
    if (strcmp(size_str, "50G") == 0) return 50ULL * 1024 * 1024 * 1024;
    if (strcmp(size_str, "100G") == 0) return 100ULL * 1024 * 1024 * 1024;

    fprintf(stderr, "Invalid size option. Available options: 1G, 5G, 10G, 50G, 100G\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int shmid;
    size_t shared_mem_size = 5ULL * 1024 * 1024 * 1024; // 기본값 5GB

    // --size 옵션 파싱
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            shared_mem_size = parse_size_option(argv[i] + 7);
        }
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);  // 시작 시간 기록

    // Shared memory 할당
    shmid = shmget(SHM_KEY, shared_mem_size + sizeof(int), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *)shmat(shmid, NULL, 0);
    if (shared_mem == (int *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    access_flag = shared_mem + (shared_mem_size / sizeof(int));

    // 공유 메모리를 초기화 (한 번만 실행)
    for (size_t i = 0; i < shared_mem_size / sizeof(int); i++) {
        shared_mem[i] = 0; // 초기값을 0으로 설정
    }
    *access_flag = 0;

    clock_gettime(CLOCK_MONOTONIC, &end);  // 종료 시간 기록

    double elapsed_time = (end.tv_sec - start.tv_sec) +
                          (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Shared memory allocated (%zu bytes) and initialized in %.6f seconds. PID: %d\n", shared_mem_size, elapsed_time, getpid());

    // 접근 감지 루프
    while (1) {
        if (*access_flag == 1) {
            printf("Shared memory accessed by another process! PID: %d\n", getpid());
            *access_flag = 0; // 플래그를 리셋
        }
    }

    // 메모리 분리 및 정리
    if (shmdt((const void *)shared_mem) < 0) {
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}
