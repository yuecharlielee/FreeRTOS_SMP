#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N                       64 
#define CORE_NUM                configNUMBER_OF_CORES
#define WORKER_CORE_NUM         (CORE_NUM - 1)

#define TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 2)
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 1)
#define COORDINATOR_CORE        0 

unsigned int *A, *B, *C, *ans;

volatile uint32_t g_ulRunCounter = 0;

// Semaphores for synchronization
SemaphoreHandle_t xStartSemaphore;
SemaphoreHandle_t xCompletionSemaphore;
SemaphoreHandle_t xPrintMutex;

extern void freertos_risc_v_trap_handler(void);
extern void xPortStartSchedulerOncore(void);

static inline void lock_print() {
    xSemaphoreTake(xPrintMutex, portMAX_DELAY);
}

static inline void unlock_print() {
    xSemaphoreGive(xPrintMutex);
}

void vWorkerTask() {
    lock_print();
    printf("[Worker] Task started on Core %ld. Starting run.\n", rtos_core_id_get());
    unlock_print();

    int core_idx = rtos_core_id_get() - 1;
    int rows_per_worker = N / WORKER_CORE_NUM;
    int remainder = N % WORKER_CORE_NUM;

    int start_row;
    int end_row;

    if (core_idx < remainder) {
        start_row = core_idx * (rows_per_worker + 1);
        end_row = start_row + (rows_per_worker + 1);
    } else {
        start_row = remainder * (rows_per_worker + 1) + (core_idx - remainder) * rows_per_worker;
        end_row = start_row + rows_per_worker;
    }
    
    // Wait for start signal
    xSemaphoreTake(xStartSemaphore, portMAX_DELAY);

    for (int r = start_row; r < end_row; r++) {
        for (int c = 0; c < N; c++) {
            C[r * N + c] = 0;
            for (int k = 0; k < N; k++) {
                C[r * N + c] += A[r * N + k] * B[k * N + c];
            }
        }
    }

    // Signal completion
    xSemaphoreGive(xCompletionSemaphore);
    
    lock_print();
    printf("[Worker %ld] Task finished.\n", rtos_core_id_get());
    unlock_print();

    for(;;){}
}

void vCoordinatorTask() {
    lock_print();
    printf("[Coordinator] Task started on Core 0. Starting run.\n");
    unlock_print();

    // Signal all workers to start
    for (int i = 0; i < WORKER_CORE_NUM; i++) {
        xSemaphoreGive(xStartSemaphore);
    }

    // Wait for all workers to complete
    for (int i = 0; i < WORKER_CORE_NUM; i++) {
        xSemaphoreTake(xCompletionSemaphore, portMAX_DELAY);
    }

    g_ulRunCounter++;
    int errors = 0;
    for (int i = 0; i < N * N; i++) {
        if (C[i] != ans[i]) {
            errors++;
        }
    }
    
    lock_print();
    printf("\n----------------------------------------\n");
    printf("[Coordinator] Run %lu finished. Found %d errors.\n", g_ulRunCounter, errors);
    printf("[Coordinator] Compute complete.\n");
    printf("----------------------------------------\n");
    unlock_print();

    for(;;){}
}

int main(void) {
    int core_id = rtos_core_id_get();

    if (core_id == COORDINATOR_CORE) {
        // Create semaphores
        xStartSemaphore = xSemaphoreCreateCounting(WORKER_CORE_NUM, 0);
        xCompletionSemaphore = xSemaphoreCreateCounting(WORKER_CORE_NUM, 0);
        xPrintMutex = xSemaphoreCreateMutex();

        lock_print();
        printf("Core 0: Initializing...\n");
        unlock_print();

        A = (unsigned int *)pvPortMalloc(N * N * sizeof(unsigned int));
        B = (unsigned int *)pvPortMalloc(N * N * sizeof(unsigned int));
        C = (unsigned int *)pvPortMalloc(N * N * sizeof(unsigned int));
        ans = (unsigned int *)pvPortMalloc(N * N * sizeof(unsigned int));
        if (!A || !B || !C || !ans) {}

        for (int i = 0; i < N * N; i++) { A[i] = i % 10; B[i] = i % 10; }

        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                ans[i * N + j] = 0;
                for (int k = 0; k < N; k++) {
                    ans[i * N + j] += A[i * N + k] * B[k * N + j];
                }
            }
        }
        
        lock_print();
        printf("Core 0: Creating tasks for a single run...\n");
        unlock_print();

        xTaskCreateAffinitySet(vCoordinatorTask, NULL, TASK_STACK_SIZE, NULL, TASK_PRIORITY, (1 << COORDINATOR_CORE), NULL);
        for (int i = 1; i < CORE_NUM; i++) {
            xTaskCreateAffinitySet(vWorkerTask, NULL, TASK_STACK_SIZE, NULL, TASK_PRIORITY, (1 << i), NULL);
        }
        vTaskStartScheduler();
    } else {
        xPortStartSchedulerOncore();
    }

    for (;;);
    return 0;
}

void vApplicationMallocFailedHook(void) {
    lock_print();
    printf("Malloc failed!\n");
    unlock_print();
    for(;;);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) { 
    (void)pxTask;
    lock_print();
    printf("Stack overflow in %s\n", pcTaskName);
    unlock_print();
    for(;;);
}

void vApplicationIdleHook(void) {}
void vApplicationTickHook(void) {}
