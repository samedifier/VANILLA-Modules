#include "main.h"
#include "vanilla_window_threading.h"
#include "vanilla_analyzer.h"

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

pthread_t* VANILLA_workers = NULL;
uint16_t* VANILLA_workerIndices = NULL;
uint16_t VANILLA_workerCount = 1;

static bool* VANILLA_workerReady = NULL;
static uint16_t VANILLA_createdWorkerCount = 0;

static bool didconfigthreading = false;
static bool VANILLA_threadingRunning = false;

static uint16_t VANILLA_activeWorkerCount = 0;
static uint16_t VANILLA_completedWorkerCount = 0;

static pthread_mutex_t VANILLA_threadMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t VANILLA_workCond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t VANILLA_doneCond = PTHREAD_COND_INITIALIZER;

static void* VANILLA_workerMain(void* arg) {
    uint16_t workerIndex = *(uint16_t*)arg;
    pthread_mutex_lock(&VANILLA_threadMutex);

    while (VANILLA_threadingRunning) {
        while (!VANILLA_workerReady[workerIndex] && VANILLA_threadingRunning) {
            pthread_cond_wait(&VANILLA_workCond, &VANILLA_threadMutex);
        }

        if (!VANILLA_threadingRunning) { break; }

        VANILLA_workerReady[workerIndex] = false;
        uint16_t activeWorkers = VANILLA_activeWorkerCount;

        pthread_mutex_unlock(&VANILLA_threadMutex);

        VANILLA_analyzeTransformWorker(workerIndex, activeWorkers);

        pthread_mutex_lock(&VANILLA_threadMutex);

        VANILLA_completedWorkerCount++;

        if (VANILLA_completedWorkerCount >= VANILLA_activeWorkerCount) {
            pthread_cond_signal(&VANILLA_doneCond);
        }
    }

    pthread_mutex_unlock(&VANILLA_threadMutex);
    return NULL;
}

int VANILLA_ThreadingInit(void) {
    size_t workerBytes = (size_t)VANILLA_workerCount * sizeof(pthread_t);
    size_t indexBytes = (size_t)VANILLA_workerCount * sizeof(uint16_t);
    size_t readyBytes  = (size_t)VANILLA_workerCount * sizeof(bool);

    VANILLA_workers = VANILLA_mmap(workerBytes);
    VANILLA_workerIndices = VANILLA_mmap(indexBytes);
    VANILLA_workerReady = VANILLA_mmap(readyBytes);
    
    if (VANILLA_workers == NULL || VANILLA_workerReady == NULL || VANILLA_workerIndices == NULL) {
        if (VANILLA_workers != NULL) {
            VANILLA_munmap(VANILLA_workers, workerBytes);
            VANILLA_workers = NULL;
        }
        if (VANILLA_workerIndices != NULL) {
            VANILLA_munmap(VANILLA_workerIndices, indexBytes);
            VANILLA_workerIndices = NULL;
        }
        if (VANILLA_workerReady != NULL) {
            VANILLA_munmap(VANILLA_workerReady, readyBytes);
            VANILLA_workerReady = NULL;
        }        

        return 1;
    }

    for (uint16_t i = 0; i < VANILLA_workerCount; i++) { VANILLA_workerReady[i] = false; VANILLA_workerIndices[i] = i; }

    VANILLA_threadingRunning = true;
    for (uint16_t i = 0; i < VANILLA_workerCount; i++) {
        if (pthread_create(&VANILLA_workers[i], NULL, VANILLA_workerMain, &VANILLA_workerIndices[i]) != 0) {
            pthread_mutex_lock(&VANILLA_threadMutex);
            VANILLA_threadingRunning = false;
            pthread_cond_broadcast(&VANILLA_workCond);
            pthread_mutex_unlock(&VANILLA_threadMutex);

            for (uint16_t j = 0; j < VANILLA_createdWorkerCount; j++) { pthread_join(VANILLA_workers[j], NULL); }
            VANILLA_createdWorkerCount = 0;

            VANILLA_munmap(VANILLA_workers, workerBytes);
            VANILLA_munmap(VANILLA_workerReady, readyBytes);

            VANILLA_workers = NULL;
            VANILLA_workerReady = NULL;

            return 1;
        }
        VANILLA_createdWorkerCount++;
    }

    return 0;
}

void VANILLA_ThreadingStartTransforms(uint32_t dirtyChunkCount) {
    uint16_t activeWorkers = VANILLA_workerCount;
    if (dirtyChunkCount < activeWorkers) { activeWorkers = (uint16_t)dirtyChunkCount; }

    if (activeWorkers == 0) {
        VANILLA_activeWorkerCount = 0;
        return;
    }

    pthread_mutex_lock(&VANILLA_threadMutex);

    VANILLA_activeWorkerCount = activeWorkers;
    VANILLA_completedWorkerCount = 0;

    for (uint16_t i = 0; i < activeWorkers; i++) { VANILLA_workerReady[i] = true; }
    pthread_cond_broadcast(&VANILLA_workCond);

    pthread_mutex_unlock(&VANILLA_threadMutex);
}

void VANILLA_ThreadingWaitTransforms(void) {
    pthread_mutex_lock(&VANILLA_threadMutex);

    while (VANILLA_completedWorkerCount < VANILLA_activeWorkerCount) {
        pthread_cond_wait(&VANILLA_doneCond, &VANILLA_threadMutex);
    }
    VANILLA_activeWorkerCount = 0;

    pthread_mutex_unlock(&VANILLA_threadMutex);
}

void VANILLA_ThreadingShutdown(void) {
    pthread_mutex_lock(&VANILLA_threadMutex);

    VANILLA_threadingRunning = false;
    pthread_cond_broadcast(&VANILLA_workCond);

    pthread_mutex_unlock(&VANILLA_threadMutex);

    for (uint16_t i = 0; i < VANILLA_createdWorkerCount; i++) { pthread_join(VANILLA_workers[i], NULL); }
    VANILLA_createdWorkerCount = 0;

    if (VANILLA_workers != NULL) {
        VANILLA_munmap(VANILLA_workers, (size_t)VANILLA_workerCount * sizeof(pthread_t));
        VANILLA_workers = NULL;
    }
    if (VANILLA_workerIndices != NULL) {
        VANILLA_munmap(VANILLA_workerIndices, (size_t)VANILLA_workerCount * sizeof(uint16_t));
        VANILLA_workerIndices = NULL;
    }
    if (VANILLA_workerReady != NULL) {
        VANILLA_munmap(VANILLA_workerReady, (size_t)VANILLA_workerCount * sizeof(bool));
        VANILLA_workerReady = NULL;
    }    
}

VANILLA_EXPORT void VANILLA_setWorkerCount(uint16_t workerCount) {
    if (didconfigthreading == true) { return; }

    if (workerCount == 0) { workerCount = 1; }
    VANILLA_workerCount = workerCount;

    didconfigthreading = true;
}
