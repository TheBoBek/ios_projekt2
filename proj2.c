#include <stdio.h>
#include <string.h>  // For memset()
#include <stdlib.h>
#include <fcntl.h>           /* For O_* constants */
#include <semaphore.h>
#include <unistd.h> 
#include <sys/mman.h>
#include <time.h>
#include <stdarg.h>

// TODOs:

typedef struct {
    sem_t *mutex;          // Semaphore acting as a mutex
    sem_t *bus;            // Semaphore to signal the bus
    sem_t *allAboard;
    sem_t *isFinalStop;      // Semaphore for all riders boarded
    sem_t *printMutex;
    sem_t **stopMutexes;
    sem_t **busSems;
    sem_t **allAboardSems;
    sem_t *allDisembarked;
    int *skiers_at_stop;
    int busC;
    int allAboardC;
    int isFinalStopC;
    int busCapacityC;
    int allDisembarkedC;
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
    SharedResources *shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    shared->stopMutexes = mmap(NULL, sizeof(sem_t*) * z, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    shared->busSems = mmap(NULL, sizeof(sem_t*) * z, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    shared->allAboardSems = mmap(NULL, sizeof(sem_t*) * z, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shared->stopMutexes == MAP_FAILED || shared->busSems == MAP_FAILED || shared->allAboardSems == MAP_FAILED) {
        perror("Failed to mmap semaphore arrays");
        exit(EXIT_FAILURE);
    }

    char sem_name[64];
    for (int i = 0; i < z; i++) {
        // Initialize stopMutexes
        sprintf(sem_name, "/stopMutexSem%d", i);
        sem_unlink(sem_name);
        shared->stopMutexes[i] = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);

        // Initialize busSems
        sprintf(sem_name, "/busSem%d", i);
        sem_unlink(sem_name);
        shared->busSems[i] = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 0);

        // Initialize allAboardSems
        sprintf(sem_name, "/allAboardSem%d", i);
        sem_unlink(sem_name);
        shared->allAboardSems[i] = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 0);

        if (shared->stopMutexes[i] == SEM_FAILED || shared->busSems[i] == SEM_FAILED || shared->allAboardSems[i] == SEM_FAILED) {
            perror("Failed to initialize a semaphore");
            exit(EXIT_FAILURE);
        }
    }

    sem_unlink("/mutexSem");
    sem_unlink("/busSem");
    sem_unlink("/allAboardSem");
    sem_unlink("/isFinalStopSem");
    sem_unlink("/printMutexSem");
    sem_unlink("/allDisembarkedSem");

    shared->printMutex = sem_open("/printMutexSem", O_CREAT | O_EXCL, 0644, 1);
    shared->mutex = sem_open("/mutexSem", O_CREAT | O_EXCL, 0644, 1);
    shared->bus = sem_open("/busSem", O_CREAT | O_EXCL, 0644, 0);
    shared->allAboard = sem_open("/allAboardSem", O_CREAT | O_EXCL, 0644, 0);
    shared->isFinalStop = sem_open("/isFinalStopSem", O_CREAT | O_EXCL, 0644, 0);
    shared->allDisembarked = sem_open("/allDisembarkedSem", O_CREAT | O_EXCL, 0644, 0);

    if (shared->mutex == SEM_FAILED || shared->bus == SEM_FAILED || shared->allAboard == SEM_FAILED || shared->isFinalStop == SEM_FAILED) {
        perror("Semaphore initialization failed");
        // Cleanup semaphores if any was opened before failure
        if (shared->mutex != SEM_FAILED) sem_close(shared->mutex);
        if (shared->bus != SEM_FAILED) sem_close(shared->bus);
        if (shared->allAboard != SEM_FAILED) sem_close(shared->allAboard);
        if (shared->isFinalStop != SEM_FAILED) sem_close(shared->isFinalStop);
        if (shared->printMutex != SEM_FAILED) sem_close(shared->printMutex);
        if (shared->allDisembarked != SEM_FAILED) sem_close(shared->allDisembarked);
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
    shared->busC = 1;
    shared->allAboardC = 0;
    shared->isFinalStopC = 0;
    shared->busCapacityC = 0;
    shared->allDisembarkedC = 0;
    // Release the mutex after accessing shared data
    sem_post(shared->mutex);



    return shared;
}

void cleanup_shared_resources(SharedResources *shared) {
    if (!shared) return;
    char sem_name[64]; // Ensure the buffer is large enough for the semaphore name.
    for (int i = 0; i < shared->Z; i++) {
        // Close and unlink stopMutexes semaphores
        sprintf(sem_name, "/stopMutexSem%d", i);
        sem_close(shared->stopMutexes[i]);
        sem_unlink(sem_name);

        // Close and unlink busSems semaphores
        sprintf(sem_name, "/busSem%d", i);
        sem_close(shared->busSems[i]);
        sem_unlink(sem_name);

        // Close and unlink allAboardSems semaphores
        sprintf(sem_name, "/allAboardSem%d", i);
        sem_close(shared->allAboardSems[i]);
        sem_unlink(sem_name);
    }
    munmap(shared->stopMutexes, sizeof(sem_t*) * shared->Z);
    munmap(shared->busSems, sizeof(sem_t*) * shared->Z);
    munmap(shared->allAboardSems, sizeof(sem_t*) * shared->Z);

    // Close and unlink semaphores
    sem_close(shared->mutex);
    sem_unlink("/mutexSem");
    sem_close(shared->bus);
    sem_unlink("/busSem");
    sem_close(shared->allAboard);
    sem_unlink("/allAboardSem");
    sem_close(shared->isFinalStop);
    sem_unlink("/isFinalStopSem"); 
    sem_close(shared->printMutex);
    sem_unlink("/printMutexSem");

    // Unmap shared memory
    if (munmap(shared, sizeof(SharedResources) + sizeof(int) * shared->Z) == -1) {
        perror("munmap failed");
        exit(EXIT_FAILURE);
    }
}

void printVariables(int busC, int allAboardC, int isFinalStopC, int busCapacityC, int allDisembarkedC) {
    printf("busC: %d\n", busC);
    printf("allAboardC: %d\n", allAboardC);
    printf("isFinalStopC: %d\n", isFinalStopC);
    printf("busCapacityC: %d\n", busCapacityC);
    printf("allDisembarkedC: %d\n", allDisembarkedC);
}

void print_action(SharedResources *shared, const char *format, ...) {
    va_list args;
    va_start(args, format);

    FILE *file = fopen("proj2.out", "a");  // Open the file in append mode
    if (file == NULL) {
        perror("Failed to open file");
        va_end(args);
        return;
    }

    sem_wait(shared->printMutex);
    fprintf(file, "%d: ", shared->A++);  // Write to file instead of stdout
    vfprintf(file, format, args);        // Use vfprintf to write the formatted string
    fflush(file);                        // Flush immediately to ensure it writes to file
    sem_post(shared->printMutex);

    fclose(file);  // Close the file each time to ensure data is written
    va_end(args);
}

void skier(int idL, int idZ, SharedResources *shared) {    
    print_action(shared, "L %d: started\n", idL);
    // Simulate having breakfast
    if (shared->TL != 0) {
            usleep(rand() % shared->TB);
    }
    else {
        usleep(0);
    }


    sem_wait(shared->stopMutexes[idZ]);
        print_action(shared, "L %d: arrived to %d\n", idL, idZ + 1);
        shared->skiers_at_stop[idZ] += 1;
    sem_post(shared->stopMutexes[idZ]);

    // printVariables(shared->busC, shared->allAboardC, shared->isFinalStopC, shared->busCapacityC, shared->allDisembarkedC);
    sem_wait(shared->busSems[idZ]);
    print_action(shared, "L %d: boarding\n", idL);
    sem_post(shared->allAboardSems[idZ]);
    // Simulate the trip to the ski area
    sem_wait(shared->isFinalStop);    
    print_action(shared, "L %d: going to ski\n", idL);
    sem_post(shared->allDisembarked);
}

void skibus(SharedResources *shared, int l) {
    setbuf(stdout, NULL);
    print_action(shared, "BUS: started\n");
    shared->idZ = 0; // Start from the first stop
    int counter = 0;
    while (1) {
        // Simulate travel to the next stop
        if (shared->TB != 0) {
            usleep(rand() % shared->TB);
        }
        else {
            usleep(0);
        }
        print_action(shared, "BUS: arrived to %d\n", shared->idZ + 1);

        // sem_wait(shared->printMutex);
        // printf("Riders at each stop before boarding:\n");
        // for (int i = 0; i < shared->Z; i++) {
        //     printf("%d, ", shared->skiers_at_stop[i]);
        // }
        // printf("\n");
        // sem_post(shared->printMutex);
        // Ensure exclusive access to this stop's skier count
        sem_wait(shared->stopMutexes[shared->idZ]);

        // Display boarding information and reset the counter
        // printf("Boarding %d skiers at stop %d.\n", shared->skiers_at_stop[shared->idZ], shared->idZ + 1);
        // printVariables(shared->busC, shared->allAboardC, shared->isFinalStopC, shared->busCapacityC, shared->allDisembarkedC);
        int n = shared->skiers_at_stop[shared->idZ]; // number of skiers to board
        if (n + shared->skiers_in_bus > shared->K) {
            print_action(shared, "KAPACITA\n");
            print_action(shared, "Na zastavke %d, V buse %d\n", shared->skiers_at_stop[shared->idZ], shared->skiers_in_bus);
            n = shared->K - shared->skiers_in_bus;
        }
        for (int i = 0; i < n; i++){
            sem_post(shared->busSems[shared->idZ]);
            sem_wait(shared->allAboardSems[shared->idZ]);
        }
            shared->skiers_at_stop[shared->idZ] -= n;
            shared->skiers_in_bus += n;


        sem_post(shared->stopMutexes[shared->idZ]);

        print_action(shared, "BUS: leaving %d\n", shared->idZ + 1);

        if (shared->idZ < shared->Z - 1) {
            shared->idZ++;  // Move to the next stopa
        } else {
            // Travel to the final destination
            print_action(shared, "BUS: arrived to final\n");

            // Assuming all skiers disembark
            // print_action(shared, "Disembarking all skiers.\n");

            for (int h = 0; h < shared->skiers_in_bus; h++)
            {
                sem_post(shared->isFinalStop);
                sem_wait(shared->allDisembarked);
            }
            
            print_action(shared, "BUS: leaving final\n");
            shared->arrived += shared->skiers_in_bus;
            shared->skiers_in_bus = 0;

            if (shared->arrived == l) {
                print_action(shared, "BUS: finish\n"); 
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

    if (access("proj2.out", F_OK) == 0) {
        // If the file exists, delete it
        if (remove("proj2.out") != 0) {
            perror("Failed to remove existing file 'proj2.out'");
            exit(EXIT_FAILURE);
        }
    }

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
            int stop_id = rand() % shared->Z;
            skier(i + 1, stop_id, shared);
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
