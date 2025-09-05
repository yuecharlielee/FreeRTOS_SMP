#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

// Precision to use for calculations
#define fptype float

/* --- Test Parameters --- */
#define NUM_RUNS                1
#define CORE_NUM                configNUMBER_OF_CORES
#define COORDINATOR_CORE        0
#define TASK_STACK_SIZE         ( configMINIMAL_STACK_SIZE * 4 ) // HJM needs a larger stack
#define TASK_PRIORITY           ( tskIDLE_PRIORITY + 1 )

// HJM Simulation Parameters
#define N_FACTORS               3
#define N_SAMPLES               128 // Reduced for faster simulation on FPGA
#define SEED                    1979

/* --- Global Variables --- */
static int      g_iN;
static fptype  *g_pdYield;
static fptype **g_ppdFactors;
static fptype  *g_pdTotalDrift;

// Multi-core synchronization flag
volatile uint32_t g_ulWorkersDoneMask = 0;

// External function prototypes
extern void xPortStartSchedulerOncore(void);

/* --- Utility Functions --- */
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

/* --- HJM Core Calculation Functions (Unchanged) --- */
fptype RanUnif(long* s) {
    long i = (long)(*s);
    i = 1664525L * i + 1013904223L;
    *s = i;
    return (fptype)((fptype)i / (fptype)4294967296.0);
}

fptype CumNormalInv(fptype u)
{
    int i;
    fptype x, w, y, r;
    const fptype a[4] = {2.50662823884, -18.61500062529, 41.39119773534, -25.44106049637};
    const fptype b[4] = {-8.47351093090, 23.08336743743, -21.06224101826, 3.13082909833};
    const fptype c[9] = {0.3374754822726147, 0.9761690190917186, 0.1607979714918209, 0.0276438810333863, 0.0038405729373609, 0.0003951896511919, 0.0000321767881768, 0.0000002888167364, 0.0000003960315187};

    if( u < 0.5 ) r = u; else r = 1.0 - u;
    if( r <= 0.0 ) { x = 0.0; }
    else {
        y = -log(r);
        if( y <= 1.0) {
            w = 1.0; for(i=0; i<4; i++) { w = w*y + b[i]; }
            x = (((a[3]*y + a[2])*y + a[1])*y + a[0])/w;
        } else {
            y = y-2.0; w = 1.0; for( i=0; i<9; i++) { w = w*y + c[i]; }
            x = w;
        }
    }
    if( u < 0.5 ) x = -x;
    return x;
}

void HJM_SimPath_Forward_Blocking(fptype **ppdHJMPath, int iN, int iFactors, fptype dYears, fptype *pdForward, fptype *pdTotalDrift, fptype **ppdFactors, long *lRndSeed)
{
  int i,j,k;
  fptype dTotalShock;
  fptype *pdZ = (fptype*) pvPortMalloc(iFactors*sizeof(fptype));

  for (j=1; j<iN; ++j) {
    for (i=0; i<iFactors; ++i) {
      pdZ[i] = RanUnif(lRndSeed);
      pdZ[i] = CumNormalInv(pdZ[i]);
    }
    for (k=0; k<iN-j; ++k) {
      dTotalShock = 0;
      for (i=0; i<iFactors; ++i) {
        dTotalShock += ppdFactors[i][k]*pdZ[i];
      }
      ppdHJMPath[k][j] = ppdHJMPath[k][j-1] + pdTotalDrift[k]*dYears + dTotalShock*sqrt(dYears);
    }
  }
  vPortFree(pdZ);
}


/* --- Worker Task: Executes its share of HJM simulation --- */
void vWorkerTask(void *pvParameters) {
    UBaseType_t uxCoreID = rtos_core_id_get();
    int i;
    long lRndSeed;
    fptype **ppdHJMPath;

    int iFactors = N_FACTORS;
    int iN = g_iN;
    
    // Distribute workload based on Core ID
    int chunk_size = N_SAMPLES / CORE_NUM;
    int start_i = uxCoreID * chunk_size;
    int end_i = (uxCoreID == CORE_NUM - 1) ? N_SAMPLES : start_i + chunk_size;

    lock_print();
    printf("[Core %u] Worker task started. Simulating from %d to %d.\n", (unsigned int)uxCoreID, start_i, end_i - 1);
    unlock_print();

    ppdHJMPath = (fptype **) pvPortMalloc(iN * sizeof(fptype*));
    for(i=0; i<iN; i++) {
      ppdHJMPath[i] = (fptype *) pvPortMalloc(iN * sizeof(fptype));
    }
    
    // Run simulation
    for (int j = start_i; j < end_i; j++) {
        lRndSeed = (long)(SEED + j);
        HJM_SimPath_Forward_Blocking(ppdHJMPath, iN, iFactors, 1.0, g_pdYield, g_pdTotalDrift, g_ppdFactors, &lRndSeed);
    }
    
    // Free local memory
    for(i=0; i<iN; i++) {
        vPortFree(ppdHJMPath[i]);
    }
    vPortFree(ppdHJMPath);

    lock_print();
    printf("[Core %u] Worker task finished.\n", (unsigned int)uxCoreID);
    unlock_print();
    
    // Signal completion to coordinator
    atomic_or(&g_ulWorkersDoneMask, (1 << uxCoreID));

    vTaskDelete(NULL); // Task is done, delete self.
}

/* --- Coordinator Task: Manages the benchmark --- */
void vCoordinatorTask(void *pvParameters) {
    (void)pvParameters;

    const uint32_t ulExpectedWorkerMask = (1 << CORE_NUM) - 1;

    // 1. Allocate global memory for HJM data
    g_iN = 11;
    g_pdYield = (fptype *) pvPortMalloc(g_iN * sizeof(fptype));
    g_pdYield[0] = .1;
    for(int j=1; j<g_iN; ++j) g_pdYield[j] = g_pdYield[j-1]+.005;

    g_ppdFactors = (fptype **) pvPortMalloc(N_FACTORS * sizeof(fptype*));
    for(int i=0; i<N_FACTORS; ++i) {
        g_ppdFactors[i] = (fptype *) pvPortMalloc(g_iN * sizeof(fptype));
        for(int j=0; j<g_iN; ++j) g_ppdFactors[i][j] = .2;
    }

    g_pdTotalDrift = (fptype *) pvPortMalloc(g_iN * sizeof(fptype));
    for(int i=0; i<g_iN; ++i) g_pdTotalDrift[i]=0.1;

    lock_print();
    printf("[Coordinator] Data initialized. Creating worker tasks on all %d cores...\n", CORE_NUM);
    unlock_print();

    // 2. Create worker tasks and pin them to each core
    for (int i = 0; i < CORE_NUM; i++) {
        xTaskCreateAffinitySet(vWorkerTask, "Worker", TASK_STACK_SIZE, NULL, TASK_PRIORITY, (1 << i), NULL);
    }

    // 3. Wait for all workers to finish
    while (g_ulWorkersDoneMask != ulExpectedWorkerMask) {
        __asm__ volatile("fence"); // Wait for the mask to be updated
    }

    // 4. Print results and clean up
    lock_print();
    printf("\n----------------------------------------\n");
    printf("[Coordinator] All HJM simulations finished.\n");
    printf("----------------------------------------\n");
    unlock_print();
    
    // Free global memory
    vPortFree(g_pdYield);
    for(int i=0; i<N_FACTORS; ++i) vPortFree(g_ppdFactors[i]);
    vPortFree(g_ppdFactors);
    vPortFree(g_pdTotalDrift);
    
    printf("\nBenchmark complete. System will now idle.\n");
    
    // The main task of the application is done. It can now delete itself.
    vTaskDelete(NULL); 
}


int main(void) {
    int core_id = rtos_core_id_get();

    if (core_id == COORDINATOR_CORE) {
        *(volatile uint32_t *)PRINT_LOCK_ADDR = 0u;
        *(volatile uint32_t *)MALLOC_LOCK_ADDR = 0u;

        lock_print();
        printf("Core 0: Starting HJM Securities benchmark on %d cores.\n", CORE_NUM);
        unlock_print();
        
        // Create the single coordinator task on core 0
        xTaskCreateAffinitySet(vCoordinatorTask, "Coordinator", TASK_STACK_SIZE, NULL, TASK_PRIORITY + 1, (1 << COORDINATOR_CORE), NULL);
        
        // Start the scheduler on the primary core
        vTaskStartScheduler();
    } else {
        // Secondary cores wait for the scheduler to start
        xPortStartSchedulerOncore();
    }

    // Should not reach here
    for (;;);
    return 0;
}


/* --- Hook Functions (Unchanged) --- */
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