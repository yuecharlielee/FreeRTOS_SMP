//HJM_Securities FreeRTOS SMP Version
//Routines to compute various security prices using HJM framework (via Simulation).
//Adapted for FreeRTOS SMP multi-core execution
//Based on PARSEC swaptions benchmark

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// FreeRTOS SMP Configuration
#define CORE_NUM                configNUMBER_OF_CORES
#define WORKER_CORE_NUM         (CORE_NUM - 1)
#define TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 4)  // Larger stack for math computations
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 1)
#define COORDINATOR_CORE        0

// HJM Configuration
#define DEFAULT_NUM_TRIALS      1000
#define BLOCK_SIZE             256
#define MAX_FACTORS            3
#define MAX_N                  11

typedef float FTYPE;

// HJM Data Structures
typedef struct {
    int Id;
    int iN;
    int iFactors;
    FTYPE dYears;
    FTYPE dStrike;
    FTYPE dCompounding;
    FTYPE dMaturity;
    FTYPE dTenor;
    FTYPE dPaymentInterval;
    FTYPE *pdYield;
    FTYPE **ppdFactors;
    FTYPE dSimSwaptionMeanPrice;
    FTYPE dSimSwaptionStdError;
} parm;

// Global Variables
volatile uint32_t g_ulStartRunFlag = 0;
volatile uint32_t g_ulWorkersDoneMask = 0;
volatile uint32_t g_ulRunCounter = 0;

// HJM Parameters
int NUM_TRIALS = DEFAULT_NUM_TRIALS;
int nSwaptions = 4;  // One per core for simplicity
int iN = 11;
int iFactors = 3;
parm *swaptions;
FTYPE **factors;
long seed = 1979;
long swaption_seed;

extern void freertos_risc_v_trap_handler(void);
extern void xPortStartSchedulerOncore(void);

// Print synchronization
static inline void lock_print() {
    uint32_t ulHartId = rtos_core_id_get() + 1;
    uint32_t ulPrevVal;
    do {
        __asm__ volatile("amoswap.w.aqrl %0, %2, %1" : "=r"(ulPrevVal), "+A"(*PRINT_LOCK_ADDR) : "r"(ulHartId) : "memory");
    } while (ulPrevVal != 0);
}

static inline void unlock_print() {
    __asm__ volatile("amoswap.w.rl zero, zero, %0" : : "A"(*PRINT_LOCK_ADDR) : "memory");
}

static void atomic_or(volatile uint32_t *addr, int val) {
    __asm__ volatile("amoor.w.aqrl zero, %1, %0" : "+A"(*addr) : "r"(val) : "memory");
}

// Simple random number generator (since we don't have nr_routines.h)
static FTYPE RanUnif(long *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return (FTYPE)(*seed) / 2147483647.0f;
}

// Matrix allocation functions (replacing dmatrix/dvector from nr_routines.h)
static FTYPE **dmatrix(int nrl, int nrh, int ncl, int nch) {
    int i;
    FTYPE **m = (FTYPE **)pvPortMalloc((nrh-nrl+1)*sizeof(FTYPE*));
    if (!m) return NULL;
    
    m -= nrl;
    
    for (i = nrl; i <= nrh; i++) {
        m[i] = (FTYPE *)pvPortMalloc((nch-ncl+1)*sizeof(FTYPE));
        if (!m[i]) return NULL;
        m[i] -= ncl;
    }
    return m;
}

static FTYPE *dvector(int nl, int nh) {
    FTYPE *v = (FTYPE *)pvPortMalloc((nh-nl+1)*sizeof(FTYPE));
    if (!v) return NULL;
    return v-nl;
}

static void free_dmatrix(FTYPE **m, int nrl, int nrh, int ncl, int nch) {
    int i;
    for (i = nrh; i >= nrl; i--) {
        vPortFree((void*)(m[i]+ncl));
    }
    vPortFree((void*)(m+nrl));
}

static void free_dvector(FTYPE *v, int nl, int nh) {
    vPortFree((void*)(v+nl));
}

// Simplified HJM_Swaption_Blocking function (core pricing logic)
static int HJM_Swaption_Blocking(FTYPE *pdSwaptionPrice, 
                                 FTYPE dStrike, FTYPE dCompounding, FTYPE dMaturity,
                                 FTYPE dTenor, FTYPE dPaymentInterval,
                                 int iN, int iFactors, FTYPE dYears,
                                 FTYPE *pdYield, FTYPE **ppdFactors,
                                 long lRndSeed, int iNumPaths, int iBlockSize, int tid) {
    
    // Simplified Monte Carlo simulation for demonstration
    FTYPE dSumPrice = 0.0f;
    FTYPE dSumSquare = 0.0f;
    long localSeed = lRndSeed;
    
    lock_print();
    printf("[Core %ld] Computing swaption with %d paths, strike=%.4f, maturity=%.2f\n", 
           rtos_core_id_get(), iNumPaths, dStrike, dMaturity);
    unlock_print();
    
    // Simple Monte Carlo loop
    for (int path = 0; path < iNumPaths; path++) {
        FTYPE dPrice = 0.0f;
        
        // Simplified path generation using basic random walk
        FTYPE dRate = pdYield[0];
        for (int i = 1; i < iN; i++) {
            // Basic random walk with volatility from factors
            FTYPE dRand = RanUnif(&localSeed) - 0.5f;
            FTYPE dVolatility = ppdFactors[0][i-1];  // Use first factor
            dRate += dRand * dVolatility * 0.01f;  // Small step size
        }
        
        // Simple payoff calculation: max(rate - strike, 0)
        dPrice = (dRate > dStrike) ? (dRate - dStrike) * dTenor : 0.0f;
        
        dSumPrice += dPrice;
        dSumSquare += dPrice * dPrice;
    }
    
    // Calculate mean and standard error
    FTYPE dMean = dSumPrice / iNumPaths;
    FTYPE dVariance = (dSumSquare / iNumPaths) - (dMean * dMean);
    FTYPE dStdError = sqrtf(dVariance / iNumPaths);
    
    pdSwaptionPrice[0] = dMean;
    pdSwaptionPrice[1] = dStdError;
    
    return 1;  // Success
}

void vWorkerTask() {
    lock_print();
    printf("[Worker] Task started on Core %ld. Waiting for work assignment.\n", rtos_core_id_get());
    unlock_print();

    int core_idx = rtos_core_id_get() - 1;  // Worker cores start from 1
    
    // Wait for coordinator to signal start
    while (!g_ulStartRunFlag) {
        __asm__ volatile("fence");
    }

    // Each worker processes one swaption (simplified distribution)
    if (core_idx < nSwaptions) {
        FTYPE pdSwaptionPrice[2];
        int i = core_idx;  // Process swaption index equal to worker core index
        
        lock_print();
        printf("[Worker %d] Processing swaption %d\n", core_idx, i);
        unlock_print();
        
        int iSuccess = HJM_Swaption_Blocking(pdSwaptionPrice, 
                                           swaptions[i].dStrike,
                                           swaptions[i].dCompounding, 
                                           swaptions[i].dMaturity,
                                           swaptions[i].dTenor, 
                                           swaptions[i].dPaymentInterval,
                                           swaptions[i].iN, 
                                           swaptions[i].iFactors, 
                                           swaptions[i].dYears,
                                           swaptions[i].pdYield, 
                                           swaptions[i].ppdFactors,
                                           swaption_seed + i, 
                                           NUM_TRIALS, 
                                           BLOCK_SIZE, 
                                           core_idx);
        
        if (iSuccess) {
            swaptions[i].dSimSwaptionMeanPrice = pdSwaptionPrice[0];
            swaptions[i].dSimSwaptionStdError = pdSwaptionPrice[1];
            
            lock_print();
            printf("[Worker %d] Swaption %d completed: Price=%.6f, StdErr=%.6f\n", 
                   core_idx, i, pdSwaptionPrice[0], pdSwaptionPrice[1]);
            unlock_print();
        } else {
            lock_print();
            printf("[Worker %d] Swaption %d failed\n", core_idx, i);
            unlock_print();
        }
    }

    // Signal completion
    atomic_or(&g_ulWorkersDoneMask, (1u << (core_idx + 1)));

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vCoordinatorTask() {
    uint32_t ulExpectedWorkerMask = ((1u << CORE_NUM) - 1) & ~(1u << COORDINATOR_CORE);
    
    lock_print();
    printf("[Coordinator] Task started on Core %ld. Expected worker mask: 0x%lx\n", 
           rtos_core_id_get(), ulExpectedWorkerMask);
    unlock_print();

    // Initialize HJM data
    lock_print();
    printf("[Coordinator] Initializing HJM swaptions data...\n");
    unlock_print();

    // Initialize factors matrix
    factors = dmatrix(0, iFactors-1, 0, iN-2);
    if (!factors) {
        lock_print();
        printf("[Coordinator] Failed to allocate factors matrix\n");
        unlock_print();
        for(;;);
    }

    // Initialize factor data (volatility data)
    for (int i = 0; i < iN-1; i++) {
        factors[0][i] = 0.01f;  // First factor: constant volatility
    }
    
    // Second factor data
    FTYPE factor1_data[] = {0.009048f, 0.008187f, 0.007408f, 0.006703f, 0.006065f,
                           0.005488f, 0.004966f, 0.004493f, 0.004066f, 0.003679f};
    for (int i = 0; i < iN-1; i++) {
        factors[1][i] = factor1_data[i];
    }
    
    // Third factor data
    FTYPE factor2_data[] = {0.001000f, 0.000750f, 0.000500f, 0.000250f, 0.000000f,
                           -0.000250f, -0.000500f, -0.000750f, -0.001000f, -0.001250f};
    for (int i = 0; i < iN-1; i++) {
        factors[2][i] = factor2_data[i];
    }

    // Initialize swaptions
    swaptions = (parm *)pvPortMalloc(sizeof(parm) * nSwaptions);
    if (!swaptions) {
        lock_print();
        printf("[Coordinator] Failed to allocate swaptions array\n");
        unlock_print();
        for(;;);
    }

    swaption_seed = (long)(2147483647L * RanUnif(&seed));
    
    for (int i = 0; i < nSwaptions; i++) {
        swaptions[i].Id = i;
        swaptions[i].iN = iN;
        swaptions[i].iFactors = iFactors;
        swaptions[i].dYears = 5.0f + ((int)(60*RanUnif(&seed)))*0.25f; // 5 to 20 years
        swaptions[i].dStrike = 0.1f + ((int)(49*RanUnif(&seed)))*0.1f; // 0.1 to 5.0
        swaptions[i].dCompounding = 0.0f;
        swaptions[i].dMaturity = 1.0f;
        swaptions[i].dTenor = 2.0f;
        swaptions[i].dPaymentInterval = 1.0f;

        // Initialize yield curve
        swaptions[i].pdYield = dvector(0, iN-1);
        if (!swaptions[i].pdYield) {
            lock_print();
            printf("[Coordinator] Failed to allocate yield vector for swaption %d\n", i);
            unlock_print();
            for(;;);
        }
        
        swaptions[i].pdYield[0] = 0.1f;
        for (int j = 1; j < iN; j++) {
            swaptions[i].pdYield[j] = swaptions[i].pdYield[j-1] + 0.005f;
        }

        // Copy factors
        swaptions[i].ppdFactors = dmatrix(0, iFactors-1, 0, iN-2);
        if (!swaptions[i].ppdFactors) {
            lock_print();
            printf("[Coordinator] Failed to allocate factors matrix for swaption %d\n", i);
            unlock_print();
            for(;;);
        }
        
        for (int k = 0; k < iFactors; k++) {
            for (int j = 0; j < iN-1; j++) {
                swaptions[i].ppdFactors[k][j] = factors[k][j];
            }
        }
    }

    lock_print();
    printf("[Coordinator] Starting swaption computation with %d trials, %d swaptions\n", 
           NUM_TRIALS, nSwaptions);
    unlock_print();

    // Signal workers to start
    __asm__ volatile("fence");
    g_ulStartRunFlag = 1;

    // Wait for all workers to complete
    while (g_ulWorkersDoneMask != ulExpectedWorkerMask) {
        __asm__ volatile("fence");
    }

    __asm__ volatile("fence");
    g_ulStartRunFlag = 0;

    lock_print();
    printf("\n========== SWAPTION PRICING RESULTS ==========\n");
    for (int i = 0; i < nSwaptions; i++) {
        printf("Swaption %d: [Price: %.10f StdError: %.10f] Strike: %.2f Years: %.2f\n", 
               i, swaptions[i].dSimSwaptionMeanPrice, swaptions[i].dSimSwaptionStdError,
               swaptions[i].dStrike, swaptions[i].dYears);
    }
    printf("==============================================\n");
    unlock_print();

    // Cleanup
    for (int i = 0; i < nSwaptions; i++) {
        free_dvector(swaptions[i].pdYield, 0, iN-1);
        free_dmatrix(swaptions[i].ppdFactors, 0, iFactors-1, 0, iN-2);
    }
    vPortFree(swaptions);
    free_dmatrix(factors, 0, iFactors-1, 0, iN-2);

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int main(void) {
    int core_id = rtos_core_id_get();

    if (core_id == COORDINATOR_CORE) {
        // Initialize synchronization
        *(volatile uint32_t *)PRINT_LOCK_ADDR = 0u;
        *(volatile uint32_t *)MALLOC_LOCK_ADDR = 0u;

        lock_print();
        printf("FreeRTOS SMP HJM Swaptions Benchmark\n");
        printf("Cores: %d, Trials: %d, Swaptions: %d\n", CORE_NUM, NUM_TRIALS, nSwaptions);
        unlock_print();

        // Create coordinator task
        xTaskCreateAffinitySet(vCoordinatorTask, NULL, TASK_STACK_SIZE, NULL, 
                              TASK_PRIORITY, (1 << COORDINATOR_CORE), NULL);
        
        // Create worker tasks
        for (int i = 1; i < CORE_NUM; i++) {
            xTaskCreateAffinitySet(vWorkerTask, NULL, TASK_STACK_SIZE, NULL, 
                                  TASK_PRIORITY, (1 << i), NULL);
        }
        
        vTaskStartScheduler();
    } else {
        xPortStartSchedulerOncore();
    }

    for (;;);
    return 0;
}

// FreeRTOS Hook Functions
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
