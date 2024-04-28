#include <stdio.h>
#include <string.h>  // For memset()
#include <stdlib.h>
#include <fcntl.h>           /* For O_* constants */
#include <semaphore.h>
#include <unistd.h> 
#include <sys/stat.h>        /* For mode constants */
#include <sys/mman.h>
#include <sys/shm.h>
#include <time.h>

// TODOs:

typedef struct {
    sem_t *mutex;          // Semaphore acting as a mutex
    sem_t *bus;            // Semaphore to signal the bus
    sem_t *allAboard;
    sem_t *multiplex;      // Semaphore for all riders boarded
    sem_t *printMutex;
    sem_t **stopMutexes;
    sem_t *busCapacity;
    int *skiers_at_stop;
    int idZ;               // 
    int Z;
    int K;
    int TL;
    int TB;
    int idP;
    int A;
    int arrived; 
    int skiers_in_bus;
} SharedResources;

SharedResources* init_shared_resources(int z, int k, int tl, int tb) {
    size_t size = sizeof(SharedResources) + sizeof(int) * z;
    // Create a shared memory area
    SharedResources *shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    // Allocate space for stop mutexes
    shared->stopMutexes = mmap(NULL, sizeof(sem_t*) * z, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared->stopMutexes == MAP_FAILED) {
        perror("mmap for stopMutexes failed");
        exit(EXIT_FAILURE);
    }

    // Initialize each semaphore and unlink previous instances
    for (int i = 0; i < z; i++) {
        char sem_name[20];
        sprintf(sem_name, "/stopMutexSem%d", i);
        sem_unlink(sem_name);
        shared->stopMutexes[i] = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
        if (shared->stopMutexes[i] == SEM_FAILED) {
            perror("Semaphore initialization failed for stop mutex");
            exit(EXIT_FAILURE);
        }
    }

    sem_unlink("/mutexSem");
    sem_unlink("/busSem");
    sem_unlink("/allAboardSem");
    sem_unlink("/multiplexSem");
    sem_unlink("/printMutexSem");
    sem_unlink("/busCapacitySem");


    shared->printMutex = sem_open("/printMutexSem", O_CREAT | O_EXCL, 0644, 1);
    shared->mutex = sem_open("/mutexSem", O_CREAT | O_EXCL, 0644, 1);
    shared->bus = sem_open("/busSem", O_CREAT | O_EXCL, 0644, 0);
    shared->allAboard = sem_open("/allAboardSem", O_CREAT | O_EXCL, 0644, 0);
    shared->multiplex = sem_open("/multiplexSem", O_CREAT | O_EXCL, 0644, 5000);
    shared->busCapacity = sem_open("/busCapacitySem", O_CREAT | O_EXCL, 0644, 1);

    if (shared->mutex == SEM_FAILED || shared->bus == SEM_FAILED || shared->allAboard == SEM_FAILED || shared->multiplex == SEM_FAILED) {
        perror("Semaphore initialization failed");
        // Cleanup semaphores if any was opened before failure
        if (shared->mutex != SEM_FAILED) sem_close(shared->mutex);
        if (shared->bus != SEM_FAILED) sem_close(shared->bus);
        if (shared->allAboard != SEM_FAILED) sem_close(shared->allAboard);
        if (shared->multiplex != SEM_FAILED) sem_close(shared->multiplex);
        if (shared->printMutex != SEM_FAILED) sem_close(shared->printMutex);
        if (shared->busCapacity != SEM_FAILED) sem_close(shared->busCapacity);
        exit(EXIT_FAILURE);
    }

    shared->skiers_at_stop = (int *)(shared + 1);

    // Initialize the array of skiers
    memset(shared->skiers_at_stop, 0, sizeof(int) * z);

    // Acquire the mutex before accessing shared data
    sem_wait(shared->mutex);
    
    // printf("Riders at each stop:\n");
    // for (int i = 0; i < z; i++) {
    //     shared->skiers_at_stop[i] = 0;
    //     printf("Stop %d: %d riders\n", i + 1, shared->skiers_at_stop[i]);
    // }
    shared->Z = z;
    shared->K = k;
    shared->TL = tl;
    shared->TB = tb;
    shared->A = 1;
    shared->arrived = 0;
    shared->skiers_in_bus = 0;
    // Release the mutex after accessing shared data
    sem_post(shared->mutex);



    return shared;
}

void cleanup_shared_resources(SharedResources *shared) {
    if (!shared) return; // Early exit if shared is NULL

    for (int i = 0; i < shared->Z; i++) {
        sem_close(shared->stopMutexes[i]);
        char sem_name[20];
        sprintf(sem_name, "/stopMutexSem%d", i);
        sem_unlink(sem_name);
    }

    munmap(shared->stopMutexes, sizeof(sem_t*) * shared->Z);

    // Close and unlink semaphores
    sem_close(shared->mutex);
    sem_unlink("/mutexSem");
    sem_close(shared->bus);
    sem_unlink("/busSem");
    sem_close(shared->allAboard);
    sem_unlink("/allAboardSem");
    sem_close(shared->multiplex);
    sem_unlink("/multiplexSem"); 
    sem_close(shared->printMutex);
    sem_unlink("/printMutexSem");
    sem_close(shared->busCapacity);
    sem_unlink("/busCapacitySem");

    // Unmap shared memory
    if (munmap(shared, sizeof(SharedResources) + sizeof(int) * shared->Z) == -1) {
        perror("munmap failed");
        exit(EXIT_FAILURE);
    }
}

void skier(int idL, int idZ, SharedResources *shared) {
    setbuf(stdout, NULL);
    sem_wait(shared->printMutex);
        printf("%d: L %d: started\n",shared->A, idL);
        shared->A += 1;   
    sem_post(shared->printMutex);
    // Simulate having breakfast
    usleep((rand() % shared->TL) * 1);  // Wait for a random time in microseconds within TL
    sem_wait(shared->printMutex);
        printf("%d: L %d: arrived to %d\n", shared->A, idL, idZ + 1);
        shared->A += 1;   
    sem_post(shared->printMutex);

    sem_wait(shared->stopMutexes[idZ]);
        shared->skiers_at_stop[idZ] += 1;
    sem_post(shared->stopMutexes[idZ]);

    sem_wait(shared->bus);
    sem_wait(shared->printMutex);
    printf("%d: L %d: boarding\n", shared->A, idL);
    shared->A += 1;   
    sem_post(shared->printMutex);
    sem_post(shared->allAboard);
    // Simulate the trip to the ski area
    sem_wait(shared->printMutex);
    printf("%d: L %d: going to ski\n",shared->A, idL);
    shared->A += 1;   
    sem_post(shared->printMutex);
}

void skibus(SharedResources *shared, int l) {
    setbuf(stdout, NULL);
    sem_wait(shared->printMutex);
    printf("%d: BUS: started\n", shared->A);
    shared->A += 1;   
    sem_post(shared->printMutex);
    shared->idZ = 0; // Start from the first stop
    int counter = 0;
    while (1) {
        // Simulate travel to the next stop
        int travelTime = rand() % shared->TB;
        usleep(travelTime * 10000);

        sem_wait(shared->printMutex);
        printf("%d: BUS: arrived to %d\n",shared->A, shared->idZ + 1);
        shared->A += 1;   
        sem_post(shared->printMutex);
        printf("Riders at each stop before boarding:\n");
        for (int i = 0; i < shared->Z; i++) {
            printf("%d, ", shared->skiers_at_stop[i]);
        }
        printf("\n");

        // Ensure exclusive access to this stop's skier count
        sem_wait(shared->stopMutexes[shared->idZ]);

        // Display boarding information and reset the counter
        printf("Boarding %d skiers at stop %d.\n", shared->skiers_at_stop[shared->idZ], shared->idZ + 1);
        int n = shared->skiers_at_stop[shared->idZ]; // number of skiers to board
        if (n + shared->skiers_in_bus > shared->K) {
            n = shared->K - shared->skiers_in_bus;
        }
        for (int i = 0; i < n; i++){
            sem_post(shared->bus);
            sem_wait(shared->allAboard);
        }
            shared->skiers_at_stop[shared->idZ] -= n;
            shared->skiers_in_bus += n;


        sem_post(shared->stopMutexes[shared->idZ]);

        sem_wait(shared->printMutex);
        printf("%d: BUS: leaving %d\n",shared->A, shared->idZ + 1);
        shared->A += 1;   
        sem_post(shared->printMutex);

        if (shared->idZ < shared->Z - 1) {
            shared->idZ++;  // Move to the next stop
        } else {
            // Travel to the final destination
            sem_wait(shared->printMutex);
            printf("%d: BUS: arrived to final\n", shared->A);
            shared->A += 1;   
            sem_post(shared->printMutex);
            // Assuming all skiers disembark
            printf("Disembarking all skiers.\n");
            sem_wait(shared->printMutex);
            printf("%d: BUS: leaving final\n", shared->A);
            shared->A += 1;   
            sem_post(shared->printMutex);

            shared->arrived += shared->skiers_in_bus;
            shared->skiers_in_bus = 0;
            if (shared->arrived == l) {
                sem_wait(shared->printMutex);
                printf("%d: BUS: finish\n", shared->A);
                shared->A += 1;   
                sem_post(shared->printMutex);   
                break;  // No skiers left to service, finish the loop
            }

            // Reset to start for a new round if skiers are still waiting
            shared->idZ = 0;
            counter++;
        }
    }
}


int main(int argc, char *argv[]){
    setbuf(stdout, NULL);
    if (argc != 6) {
        fprintf(stderr, "Usage: %s L Z K TL TB\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int L = atoi(argv[1]); // Number of skiers
    int Z = atoi(argv[2]); // Number of stops
    int K = atoi(argv[3]); // Bus capacity
    int TL = atoi(argv[4]); // Max wait time for skiers
    int TB = atoi(argv[5]); // Max travel time between stops

    if (L < 1 || L > 20000 || Z < 1 || Z > 10 || K < 10 || K > 100 || TL < 0 || TL > 10000 || TB < 0 || TB > 1000) {
        fprintf(stderr, "Invalid input parameters\n");
        exit(EXIT_FAILURE);
    }

    printf("L: %d, Z: %d, K: %d, TL: %d, TB: %d\n", L, Z, K, TL, TB);

    // Initialize the array with values and print them

    SharedResources *shared = init_shared_resources(Z, K, TL, TB);
    
    pid_t pid;
    // Creating processes
    pid = fork();
    if (pid == 0) {
        // This is the bus process
        skibus(shared, L);
        exit(0);
    } else if (pid < 0) {
        perror("Failed to fork bus process");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < L; i++) {
        pid = fork();
        if (pid == 0) {
            // This is a skier process
            srand((unsigned) time(NULL) ^ (getpid() << 16));
            int stop_id = rand() % 2;
            skier(i, stop_id, shared);
            exit(0);
        } else if (pid < 0) {
            perror("Failed to fork skier process");
            exit(EXIT_FAILURE);
        }
    }



    wait(NULL);  // Wait for child process to finish

    cleanup_shared_resources(shared);

    return 0;
}