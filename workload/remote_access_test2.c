#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <numa.h>
#include <sched.h>
#include <sys/wait.h>

#define SHM_KEY 12345

volatile int *shared_mem;
volatile int *access_flag;
size_t mem_size = 5ULL * 1024 * 1024 * 1024; // Default 5GB
size_t access_iterations;
int sequential_access = 0;
int num_threads = 15; // Default number of threads

void *access_memory(void *arg) {
    size_t index = 0;
    srand(time(NULL) ^ pthread_self());

    for (size_t i = 0; i < access_iterations; i++) {
        if (sequential_access) {
            shared_mem[index]++;
            index = (index + 1) % (mem_size / sizeof(int));
        } else {
            size_t random_index = rand() % (mem_size / sizeof(int));
            shared_mem[random_index]++;
        }
        *access_flag = 1;
    }
    return NULL;
}

void parse_arguments(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"size", required_argument, 0, 's'},
        {"access-rnd", no_argument, 0, 'r'},
        {"access-seq", no_argument, 0, 'q'},
        {"thread-Number", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int option_index = 0;
    int opt;

    while ((opt = getopt_long(argc, argv, "s:rqt:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                if (strcmp(optarg, "1G") == 0) {
                    mem_size = 1ULL * 1024 * 1024 * 1024;
                } else if (strcmp(optarg, "5G") == 0) {
                    mem_size = 5ULL * 1024 * 1024 * 1024;
                } else if (strcmp(optarg, "10G") == 0) {
                    mem_size = 10ULL * 1024 * 1024 * 1024;
                } else if (strcmp(optarg, "50G") == 0) {
                    mem_size = 50ULL * 1024 * 1024 * 1024;
                } else if (strcmp(optarg, "100G") == 0) {
                    mem_size = 100ULL * 1024 * 1024 * 1024;
                } else {
                    fprintf(stderr, "Error: Invalid size. Choose from 1G, 5G, 10G, 50G, 100G.\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'r':
                sequential_access = 0; // Random access
                break;

            case 'q':
                sequential_access = 1; // Sequential access
                break;

            case 't':
                if (strcmp(optarg, "1") == 0) {
                    num_threads = 1;
                } else if (strcmp(optarg, "10") == 0) {
                    num_threads = 10;
                } else if (strcmp(optarg, "100") == 0) {
                    num_threads = 100;
                } else {
                    fprintf(stderr, "Error: Invalid thread number. Choose from 1, 10, 100.\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'h':
                printf("Usage: %s [OPTIONS]\n\n", argv[0]);
                printf("Options:\n");
                printf("  --size=[1G|5G|10G|50G|100G]  Set the shared memory size. Default is 5G.\n");
                printf("  --access-rnd                 Access memory randomly.\n");
                printf("  --access-seq                 Access memory sequentially.\n");
                printf("  --thread-Number=[1|10|100]   Set the number of threads. Default is 15.\n");
                printf("  --help                       Show this help message and exit.\n");
                exit(EXIT_SUCCESS);

            default:
                fprintf(stderr, "Usage: %s --size=[1G|5G|10G|50G|100G] [--access-rnd | --access-seq] [--thread-Number=1|10|100] [--help]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    access_iterations = mem_size / sizeof(int);
}

void set_memory_and_cpu_bind(int node) {
    if (numa_available() == -1) {
        fprintf(stderr, "NUMA not available\n");
        exit(EXIT_FAILURE);
    }
    struct bitmask *mask = numa_allocate_nodemask();
    numa_bitmask_setbit(mask, node);
    numa_bind(mask);
    numa_free_nodemask(mask);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(node, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);

    int shmid = shmget(SHM_KEY, mem_size + sizeof(int), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    shared_mem = (int *)shmat(shmid, NULL, 0);
    if (shared_mem == (int *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    access_flag = shared_mem + (mem_size / sizeof(int));

    struct timeval start, end;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        set_memory_and_cpu_bind(1);
        gettimeofday(&start, NULL);

        pthread_t threads[num_threads];
        for (int i = 0; i < num_threads; i++) {
            if (pthread_create(&threads[i], NULL, access_memory, NULL) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
        }
        for (int i = 0; i < num_threads; i++) {
            pthread_join(threads[i], NULL);
        }

        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
        printf("Child process execution time: %.2f seconds\n", elapsed);
        _exit(0);
    } else {
        set_memory_and_cpu_bind(0);
        gettimeofday(&start, NULL);

        pthread_t threads[num_threads];
        for (int i = 0; i < num_threads; i++) {
            if (pthread_create(&threads[i], NULL, access_memory, NULL) != 0) {
                perror("pthread_create");
                exit(EXIT_FAILURE);
            }
        }
        for (int i = 0; i < num_threads; i++) {
            pthread_join(threads[i], NULL);
        }

        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
        printf("Parent process execution time: %.2f seconds\n", elapsed);

        wait(NULL);

        if (shmdt((const void*)shared_mem) < 0) {
            perror("shmdt");
            exit(EXIT_FAILURE);
        }
        shmctl(shmid, IPC_RMID, NULL);
    }

    return 0;
}
