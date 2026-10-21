/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for the
*                    CE243610 display-data-retention application on the CM55 CPU.
*                    It initializes the serial memory (SMIF/QSPI), RTC, CLIB
*                    support, retarget I/O, and launches the FreeRTOS graphics
*                    task for decoding and displaying JPEG images on the DSI
*                    display.
*
* Related Document : See README.md
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cy_time.h"
#include "retarget_io_init.h"
#include "gfx_task.h"
#include "motion_task.h"
#include "cycfg_qspi_memslot.h"
#include "mtb_serial_memory.h"
#include "smif_psram_control.h"
#include "cy_syspm.h"
#include "pd1_protocol.h"
#include <string.h>

/*******************************************************************************
 * Macros
 ******************************************************************************/
/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles  
 * to come into effect. This wait time value will depend on the actual CLK_LF  
 * frequency set by the BSP.
 */
#define LPTIMER_1_WAIT_TIME_USEC            (62U)

/* Define the LPTimer interrupt priority number. '1' implies highest priority. */
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

#define SMIF_INIT_TIMEOUT_USEC              (1000u)  /* 1ms SMIF init timeout */
/***********************************************************************************
 * Global Variables
**********************************************************************************/
/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

static mtb_serial_memory_t serial_memory_obj;
static cy_stc_smif_mem_context_t smif_mem_context;
static cy_stc_smif_mem_info_t smif_mem_info;

void check_status(const char *message, uint32_t status);

/* Motion sensor task handle */
#ifdef AUTO_ROTATION
static TaskHandle_t motion_sensor_task_handle;
#endif
/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. This function configures and initializes the Real-Time Clock (RTC)).
*    2. It then initializes the RTC HAL object to enable CLIB support library 
*       to work with the provided Real-Time Clock (RTC) module.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_clib_support(void)
{
    /* RTC Initialization is done in CM33 non-secure project */

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}

/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for LPTimer instance. 
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}


/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
* 1. This function first configures and initializes an interrupt for LPTimer.
* 2. Then it initializes the LPTimer HAL object to be used in the RTOS 
*    tickless idle mode implementation to allow the device enter deep sleep 
*    when idle task runs. LPTIMER_1 instance is configured for CM55 CPU.
* 3. It then passes the LPTimer object to abstraction RTOS library that 
*    implements tickless idle mode
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = 
                                    Cy_SysInt_Init(&lptimer_intr_cfg, 
                                                    lptimer_interrupt_handler);
    
    /* LPTimer interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status = 
                                    Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW, 
                                                &CYBSP_CM55_LPTIMER_1_config);

    /* MCWDT initialization failed. Stop program execution. */
    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        handle_app_error();
    }
  
    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW,
                    CY_MCWDT_CTR_Msk, 
                    LPTIMER_1_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj, 
                                            &CYBSP_CM55_LPTIMER_1_hal_config);
    
    /* LPTimer setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements 
     * tickless idle mode 
     */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/*******************************************************************************
 * Function Name: smif_ospi_psram_init
 ********************************************************************************
 * Summary:
 *  Initialize SMIF for PSRAM (QSPI Core 1) and set-up serial memory interfeace
 *
 * Parameters:
 *  none
 *
 * Return:
 *  void
 *
 *******************************************************************************/
static void smif_ospi_psram_init(void)
{
     cy_rslt_t result;

        /* Disable SMIF Block for reconfiguration. */
        Cy_SMIF_Disable(CYBSP_SMIF_CORE_1_PSRAM_HW);

        /* Initialize SMIF-1 Peripheral. */
        result = Cy_SMIF_Init((CYBSP_SMIF_CORE_1_PSRAM_hal_config.base),
                               (CYBSP_SMIF_CORE_1_PSRAM_hal_config.config),
                               SMIF_INIT_TIMEOUT_USEC, &smif_mem_context.smif_context);

        check_status("Cy_SMIF_Init failed", result);

        /* Configure Data Select Option for SMIF-1 */
        Cy_SMIF_SetDataSelect(CYBSP_SMIF_CORE_1_PSRAM_hal_config.base,
                              smif1BlockConfig.memConfig[0]->slaveSelect,
                              smif1BlockConfig.memConfig[0]->dataSelect);

        /* Enable the SMIF_CORE_1 block. */
        Cy_SMIF_Enable(CYBSP_SMIF_CORE_1_PSRAM_hal_config.base, &smif_mem_context.smif_context);

        /* Set-up serial memory. */
        result = mtb_serial_memory_setup(&serial_memory_obj,
                                    MTB_SERIAL_MEMORY_CHIP_SELECT_2,
                                    CYBSP_SMIF_CORE_1_PSRAM_hal_config.base,
                                    CYBSP_SMIF_CORE_1_PSRAM_hal_config.clock,
                                    &smif_mem_context,
                                    &smif_mem_info,
                                    &smif1BlockConfig);

        check_status("serial memory setup failed", result);

}

/*******************************************************************************
 * Function Name: smif_ospi_psram_reinit
 ********************************************************************************
 * Summary:
 *  Rebuilds the SMIF/PSRAM runtime objects from a clean state. Used only by the
 *  Deep Sleep wake-up restore path (PSRAM_IMAGE_SOCMEM_OFF) when the fast XIP
 *  re-enable fails. Returns a status instead of halting on error.
 *
 * Parameters:
 *  none
 *
 * Return:
 *  cy_rslt_t
 *
 *******************************************************************************/
static cy_rslt_t smif_ospi_psram_reinit(void)
{
    cy_rslt_t result;

    /* Rebuild runtime objects from a clean state before re-initialising. */
    memset(&serial_memory_obj, 0, sizeof(serial_memory_obj));
    memset(&smif_mem_context, 0, sizeof(smif_mem_context));
    memset(&smif_mem_info, 0, sizeof(smif_mem_info));

    /* Disable SMIF Block for reconfiguration. */
    Cy_SMIF_Disable(CYBSP_SMIF_CORE_1_PSRAM_HW);

    /* Initialize SMIF-1 Peripheral. */
    result = Cy_SMIF_Init((CYBSP_SMIF_CORE_1_PSRAM_hal_config.base),
                          (CYBSP_SMIF_CORE_1_PSRAM_hal_config.config),
                          SMIF_INIT_TIMEOUT_USEC, &smif_mem_context.smif_context);
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    /* Configure Data Select Option for SMIF-1 */
    Cy_SMIF_SetDataSelect(CYBSP_SMIF_CORE_1_PSRAM_hal_config.base,
                          smif1BlockConfig.memConfig[0]->slaveSelect,
                          smif1BlockConfig.memConfig[0]->dataSelect);

    /* Enable the SMIF_CORE_1 block. */
    Cy_SMIF_Enable(CYBSP_SMIF_CORE_1_PSRAM_hal_config.base, &smif_mem_context.smif_context);

    /* Set-up serial memory. */
    result = mtb_serial_memory_setup(&serial_memory_obj,
                                MTB_SERIAL_MEMORY_CHIP_SELECT_2,
                                CYBSP_SMIF_CORE_1_PSRAM_hal_config.base,
                                CYBSP_SMIF_CORE_1_PSRAM_hal_config.clock,
                                &smif_mem_context,
                                &smif_mem_info,
                                &smif1BlockConfig);
    return result;
}

/*******************************************************************************
 * Function Name: app_smif_psram_restore_after_wakeup
 ********************************************************************************
 * Summary:
 *  Restores the SMIF/PSRAM XIP mapping after Deep Sleep wake-up. Attempts a
 *  fast XIP re-enable and falls back to a full SMIF/PSRAM re-initialisation if
 *  the fast path fails, then re-enables write access and reconciles the caches.
 *
 * Parameters:
 *  none
 *
 * Return:
 *  cy_rslt_t
 *
 *******************************************************************************/
cy_rslt_t app_smif_psram_restore_after_wakeup(void)
{
    cy_rslt_t result;

    /* Fast path: SMIF context is usually retained, so only restore XIP. */
    result = mtb_serial_memory_enable_xip(&serial_memory_obj, true);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("WARNING: Fast XIP restore failed, rebuilding SMIF/PSRAM (status=0x%08lx)\r\n",
               (unsigned long)result);

        result = smif_ospi_psram_reinit();
        if (CY_RSLT_SUCCESS != result)
        {
            printf("ERROR: SMIF/PSRAM rebuild failed (status=0x%08lx)\r\n", (unsigned long)result);
            return result;
        }

        result = mtb_serial_memory_enable_xip(&serial_memory_obj, true);
        if (CY_RSLT_SUCCESS != result)
        {
            printf("ERROR: XIP enable failed after rebuild (status=0x%08lx)\r\n", (unsigned long)result);
            return result;
        }
    }

    result = mtb_serial_memory_set_write_enable(&serial_memory_obj, true);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("ERROR: Failed to re-enable PSRAM write access after wakeup (status=0x%08lx)\r\n",
               (unsigned long)result);
        return result;
    }

    /* External memory contents may not match cache state after wake. */
    SCB_CleanInvalidateDCache();
    SCB_InvalidateICache();
    __DSB();
    __ISB();

    return CY_RSLT_SUCCESS;
}

 /*******************************************************************************
  * Function Name: check_status
  ********************************************************************************
  * Summary:
  *  Prints the message, indicates the non-zero status by turning the LED on, and
  *  asserts the non-zero status.
  *
  * Parameters:
  *  message - message to print if status is non-zero.
  *  status - status for evaluation.
  *
  * Return:
  *  void
  *
  *******************************************************************************/
    void check_status(const char *message, uint32_t status)
 {
     if (status)
     {
         printf("\n\r====================================================\n\r");
         printf("\n\rFAIL: %s\n\r", message);
         printf("Error Code: 0x%lx\n\r", (unsigned long)status);
         printf("\n\r====================================================\n\r");

         /* On failure, turn the LED ON */
         Cy_GPIO_Set(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
         while(true);
     }
 }



/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  This is the main function for CM55 application.
 *    1. It initializes the device and board peripherals.
 *    2. Initializes the SMIF/OSPI interface and PSRAM (serial memory).
 *    3. Enables XIP mode and write access on the PSRAM memory slot.
 *    4. Creates the cm55_ns_gfx_task FreeRTOS task which handles graphics
 *       initialization, JPEG decoding, and DSI display rendering.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    uint32_t restart_reason = app_pd1_state();
#endif
    cy_rslt_t result;
    BaseType_t gfx_task_return;
#ifdef AUTO_ROTATION
    BaseType_t imu_task_return;
#endif

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* If board init fails, stop the program execution */
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    init_retarget_io();
    printf("\x1b[2J\x1b[;H");
    printf("****************** PSOC Edge MCU: Display Data Retention ******************\r\n");

    /* Setup CLIB support library. */
    setup_clib_support();

    /* Setup the LPTimer instance for CM55*/
    setup_tickless_idle_timer();

   /* Enable global interrupts */
    __enable_irq();

    /* Initialize PSRAM and set-up serial memory */
    smif_ospi_psram_init();

    /* Enable XIP mode for the SMIF memory slot associated with the PSRAM. */
    result = mtb_serial_memory_enable_xip(&serial_memory_obj, true);
    check_status("mtb_serial_memory_enable_xip: failed", result);

    /* Enable write for the SMIF memory slot associated with the PSRAM. */
    result = mtb_serial_memory_set_write_enable(&serial_memory_obj, true);
    check_status("mtb_serial_memory_set_write_enable: failed", result);

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    bool image_restored = gfx_restore_retained_image(
        (restart_reason == APP_PD1_RESTORE) || (restart_reason == APP_PD1_RESTART_ABORT));
    printf("CM33 restart reason=0x%08lx, retained framebuffer=%s\r\n",
           (unsigned long)restart_reason, image_restored ? "valid" : "not restored");
    if ((restart_reason == APP_PD1_RESTORE) && !image_restored)
    {
        app_pd1_set_state(APP_PD1_FAILED);
        handle_app_error();
    }
    if (restart_reason == APP_PD1_RESTORE)
    {
        printf("CM33 observed PD1 OFF; MAIN/CM33 did not restart.\r\n");
    }
    else if (restart_reason == APP_PD1_RESTART_ABORT)
    {
        printf("WARNING: PD1 OFF was not observed; CM55 restarted after aborted shutdown.\r\n");
    }
#endif

    cy_en_syspm_status_t app_mode_status =
        Cy_SysPm_SetAppDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
    cy_en_syspm_status_t socmem_mode_status =
        Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
    Cy_SysPm_DeepSleepUpdate();
    if ((app_mode_status != CY_SYSPM_SUCCESS) ||
        (socmem_mode_status != CY_SYSPM_SUCCESS) ||
        (Cy_SysPm_GetDeepSleepMode() != CY_SYSPM_MODE_DEEPSLEEP))
    {
        printf("ERROR: Failed to initialize ordinary Deep Sleep policies\r\n");
        handle_app_error();
    }

    /* Create the FreeRTOS Task */
    gfx_task_return = xTaskCreate(cm55_ns_gfx_task, GFX_TASK_NAME,
            GFX_TASK_STACK_SIZE, NULL,
            GFX_TASK_PRIORITY, NULL);

#ifdef AUTO_ROTATION
    imu_task_return = xTaskCreate(task_motion, "Motion Sensor Task", TASK_MOTION_SENSOR_STACK_SIZE,
            NULL, TASK_MOTION_SENSOR_PRIORITY, &motion_sensor_task_handle);

    if ((pdPASS == gfx_task_return) && (pdPASS == imu_task_return))
#else
    if (pdPASS == gfx_task_return)
#endif
    {
        /* Start the RTOS Scheduler */
        vTaskStartScheduler();

        /* Should never get here! */
        /* Halt the CPU if scheduler exits */
        handle_app_error();
    }
    else
    {
        printf("Error: Failed to create cm55_ns_gfx_task.\r\n");
        handle_app_error();
    }
}

/* [] END OF FILE */
