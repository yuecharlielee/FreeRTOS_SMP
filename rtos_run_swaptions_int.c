//HJM_Securities FreeRTOS SMP Version (Simplified)
//Routines to compute various security prices using HJM framework (via Simulation).
//Adapted for FreeRTOS SMP multi-core execution - NO FLOATING POINT VERSION
//Based on PARSEC swaptions benchmark

#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FreeRTOS SMP Configuration
#define CORE_NUM                configNUMBER_OF_CORES
#define WORKER_CORE_NUM         (CORE_NUM - 1)
#define TASK_STACK_SIZE         (configMINIMAL_STACK_SIZE * 4)  // Larger stack for computations
#define TASK_PRIORITY           (tskIDLE_PRIORITY + 1)
#define COORDINATOR_CORE        0

// HJM Configuration
#define DEFAULT_NUM_TRIALS      1000
#define BLOCK_SIZE             256
#define MAX_FACTORS            3
#define MAX_N                  11
#define FIXED_POINT_SCALE      10000  // For fixed-point arithmetic

// Using integers for fixed-point arithmetic instead of float
typedef int32_t FTYPE_INT;

// HJM Data Structures (using integers)
typedef struct {
    int Id;
    int iN;
    int iFactors;
    FTYPE_INT dYears;           // Fixed point * FIXED_POINT_SCALE
    FTYPE_INT dStrike;          // Fixed point * FIXED_POINT_SCALE
    FTYPE_INT dCompounding;
    FTYPE_INT dMaturity;
    FTYPE_INT dTenor;
    FTYPE_INT dPaymentInterval;
    FTYPE_INT *pdYield;
    FTYPE_INT **ppdFactors;
    FTYPE_INT dSimSwaptionMeanPrice;
    FTYPE_INT dSimSwaptionStdError;
} parm_int;

// Global Variables
volatile uint32_t g_ulStartRunFlag = 0;
volatile uint32_t g_ulWorkersDoneMask = 0;
volatile uint32_t g_ulRunCounter = 0;

// HJM Parameters
int NUM_TRIALS = DEFAULT_NUM_TRIALS;
int nSwaptions = 4;  // One per core for simplicity
int iN = 11;
int iFactors = 3;
parm_int *swaptions;
FTYPE_INT **factors;
uint32_t seed = 1979;
uint32_t swaption_seed;

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

// Fixed-point helper functions
static void print_fixed_point(FTYPE_INT val, const char* name) {
    int whole = val / FIXED_POINT_SCALE;
    int frac = (val % FIXED_POINT_SCALE);
    if (frac < 0) frac = -frac;
    printf("%s: %d.%04d", name, whole, frac);
}

// Simple random number generator (Linear Congruential Generator)
static uint32_t RanUnif(uint32_t *seed) {
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return *seed;
}

// Fixed-point math helper: simple square root approximation
static FTYPE_INT fixed_sqrt(FTYPE_INT x) {
    if (x <= 0) return 0;
    
    FTYPE_INT guess = x / 2;
    for (int i = 0; i < 10; i++) {  // Newton's method iterations
        if (guess == 0) break;
        FTYPE_INT new_guess = (guess + x / guess) / 2;
        if (new_guess == guess) break;
        guess = new_guess;
    }
    return guess;
}

// Matrix allocation functions
static FTYPE_INT **dmatrix_int(int nrl, int nrh, int ncl, int nch) {
    int i;
    FTYPE_INT **m = (FTYPE_INT **)pvPortMalloc((nrh-nrl+1)*sizeof(FTYPE_INT*));
    if (!m) return NULL;
    
    m -= nrl;
    
    for (i = nrl; i <= nrh; i++) {
        m[i] = (FTYPE_INT *)pvPortMalloc((nch-ncl+1)*sizeof(FTYPE_INT));
        if (!m[i]) return NULL;
        m[i] -= ncl;
    }
    return m;
}

static FTYPE_INT *dvector_int(int nl, int nh) {
    FTYPE_INT *v = (FTYPE_INT *)pvPortMalloc((nh-nl+1)*sizeof(FTYPE_INT));
    if (!v) return NULL;
    return v-nl;
}

static void free_dmatrix_int(FTYPE_INT **m, int nrl, int nrh, int ncl, int nch) {
    int i;
    for (i = nrh; i >= nrl; i--) {
        vPortFree((void*)(m[i]+ncl));
    }
    vPortFree((void*)(m+nrl));
}

static void free_dvector_int(FTYPE_INT *v, int nl, int nh) {
    vPortFree((void*)(v+nl));
}

// Simplified HJM_Swaption_Blocking function using fixed-point arithmetic
static int HJM_Swaption_Blocking_Int(FTYPE_INT *pdSwaptionPrice, 
                                     FTYPE_INT dStrike, FTYPE_INT dCompounding, FTYPE_INT dMaturity,
                                     FTYPE_INT dTenor, FTYPE_INT dPaymentInterval,
                                     int iN, int iFactors, FTYPE_INT dYears,
                                     FTYPE_INT *pdYield, FTYPE_INT **ppdFactors,
                                     uint32_t lRndSeed, int iNumPaths, int iBlockSize, int tid) {
    
    // Simplified Monte Carlo simulation for demonstration
    int64_t dSumPrice = 0;
    int64_t dSumSquare = 0;
    uint32_t localSeed = lRndSeed;
    
    lock_print();
    printf("[Core %ld] Computing swaption with %d paths, ", rtos_core_id_get(), iNumPaths);
    print_fixed_point(dStrike, "strike");
    printf(", ");
    print_fixed_point(dMaturity, "maturity");
    printf("\n");
    unlock_print();
    
    // Simple Monte Carlo loop
    for (int path = 0; path < iNumPaths; path++) {
        FTYPE_INT dPrice = 0;
        
        // Simplified path generation using basic random walk
        FTYPE_INT dRate = pdYield[0];
        for (int i = 1; i < iN; i++) {
            // Basic random walk with volatility from factors
            uint32_t randVal = RanUnif(&localSeed);
            FTYPE_INT dRand = (FTYPE_INT)(randVal % FIXED_POINT_SCALE) - FIXED_POINT_SCALE/2;
            FTYPE_INT dVolatility = ppdFactors[0][i-1];  // Use first factor
            dRate += (dRand * dVolatility) / (FIXED_POINT_SCALE * 100);  // Small step size
        }
        
        // Simple payoff calculation: max(rate - strike, 0)
        if (dRate > dStrike) {
            dPrice = ((dRate - dStrike) * dTenor) / FIXED_POINT_SCALE;
        } else {
            dPrice = 0;
        }
        
        dSumPrice += dPrice;
        dSumSquare += (int64_t)dPrice * dPrice;
    }
    
    // Calculate mean and standard error
    FTYPE_INT dMean = (FTYPE_INT)(dSumPrice / iNumPaths);
    int64_t dVariance = (dSumSquare / iNumPaths) - (int64_t)dMean * dMean;
    FTYPE_INT dStdError = fixed_sqrt((FTYPE_INT)(dVariance / iNumPaths));
    
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
        FTYPE_INT pdSwaptionPrice[2];
        int i = core_idx;  // Process swaption index equal to worker core index
        
        lock_print();
        printf("[Worker %d] Processing swaption %d\n", core_idx, i);
        unlock_print();
        
        int iSuccess = HJM_Swaption_Blocking_Int(pdSwaptionPrice, 
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
            printf("[Worker %d] Swaption %d completed: ", core_idx, i);
            print_fixed_point(pdSwaptionPrice[0], "Price");
            printf(", ");
            print_fixed_point(pdSwaptionPrice[1], "StdErr");
            printf("\n");
            unlock_print();
        } else {
            lock_print();
            printf("[Worker %d] Swaption %d failed\n", core_idx, i);
            unlock_print();
        }
    }

    // Signal completion
    atomic_or(&g_ulWorkersDoneMask, (1u << (core_idx + 1)));

    // Task completes naturally instead of infinite loop
    vTaskDelete(NULL);
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
    factors = dmatrix_int(0, iFactors-1, 0, iN-2);
    if (!factors) {
        lock_print();
        printf("[Coordinator] Failed to allocate factors matrix\n");
        unlock_print();
        for(;;);
    }

    // Initialize factor data (volatility data) - converted to fixed point
    for (int i = 0; i < iN-1; i++) {
        factors[0][i] = FIXED_POINT_SCALE / 100;  // 0.01 in fixed point
    }
    
    // Second factor data (converted from float to fixed point)
    int factor1_data[] = {90, 82, 74, 67, 61, 55, 50, 45, 41, 37}; // *0.001 of original values * 10000
    for (int i = 0; i < iN-1; i++) {
        factors[1][i] = factor1_data[i];
    }
    
    // Third factor data (converted from float to fixed point)  
    int factor2_data[] = {10, 8, 5, 3, 0, -3, -5, -8, -10, -13}; // *0.001 of original values * 10000
    for (int i = 0; i < iN-1; i++) {
        factors[2][i] = factor2_data[i];
    }

    // Initialize swaptions
    swaptions = (parm_int *)pvPortMalloc(sizeof(parm_int) * nSwaptions);
    if (!swaptions) {
        lock_print();
        printf("[Coordinator] Failed to allocate swaptions array\n");
        unlock_print();
        for(;;);
    }

    swaption_seed = RanUnif(&seed);
    
    for (int i = 0; i < nSwaptions; i++) {
        swaptions[i].Id = i;
        swaptions[i].iN = iN;
        swaptions[i].iFactors = iFactors;
        swaptions[i].dYears = (5 * FIXED_POINT_SCALE) + ((RanUnif(&seed) % 60) * FIXED_POINT_SCALE / 4); // 5 to 20 years
        swaptions[i].dStrike = (FIXED_POINT_SCALE / 10) + ((RanUnif(&seed) % 49) * FIXED_POINT_SCALE / 10); // 0.1 to 5.0
        swaptions[i].dCompounding = 0;
        swaptions[i].dMaturity = FIXED_POINT_SCALE;      // 1.0
        swaptions[i].dTenor = 2 * FIXED_POINT_SCALE;     // 2.0
        swaptions[i].dPaymentInterval = FIXED_POINT_SCALE; // 1.0

        // Initialize yield curve
        swaptions[i].pdYield = dvector_int(0, iN-1);
        if (!swaptions[i].pdYield) {
            lock_print();
            printf("[Coordinator] Failed to allocate yield vector for swaption %d\n", i);
            unlock_print();
            for(;;);
        }
        
        swaptions[i].pdYield[0] = FIXED_POINT_SCALE / 10;  // 0.1
        for (int j = 1; j < iN; j++) {
            swaptions[i].pdYield[j] = swaptions[i].pdYield[j-1] + (5 * FIXED_POINT_SCALE / 1000);  // +0.005
        }

        // Copy factors
        swaptions[i].ppdFactors = dmatrix_int(0, iFactors-1, 0, iN-2);
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
        printf("Swaption %d: [", i);
        print_fixed_point(swaptions[i].dSimSwaptionMeanPrice, "Price");
        printf(" ");
        print_fixed_point(swaptions[i].dSimSwaptionStdError, "StdErr");
        printf("] ");
        print_fixed_point(swaptions[i].dStrike, "Strike");
        printf(" ");
        print_fixed_point(swaptions[i].dYears, "Years");
        printf("\n");
    }
    printf("==============================================\n");
    unlock_print();

    // Cleanup
    for (int i = 0; i < nSwaptions; i++) {
        free_dvector_int(swaptions[i].pdYield, 0, iN-1);
        free_dmatrix_int(swaptions[i].ppdFactors, 0, iFactors-1, 0, iN-2);
    }
    vPortFree(swaptions);
    free_dmatrix_int(factors, 0, iFactors-1, 0, iN-2);

    // Task completes naturally instead of infinite loop
    vTaskDelete(NULL);
}

int main(void) {
    int core_id = rtos_core_id_get();

    if (core_id == COORDINATOR_CORE) {
        // Initialize synchronization
        *(volatile uint32_t *)PRINT_LOCK_ADDR = 0u;
        *(volatile uint32_t *)MALLOC_LOCK_ADDR = 0u;

        lock_print();
        printf("FreeRTOS SMP HJM Swaptions Benchmark (Fixed-Point)\n");
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
