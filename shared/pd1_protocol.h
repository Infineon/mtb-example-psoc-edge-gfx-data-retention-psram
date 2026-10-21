/*******************************************************************************
 * File Name:   pd1_protocol.h
 *
 * Description: Shared CM33/CM55 IPC states and secure services for PD1 control.
 *
 * Related Document: See README.md
 *
 *******************************************************************************
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

#ifndef PD1_PROTOCOL_H
#define PD1_PROTOCOL_H

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cy_pdl.h"

/*******************************************************************************
* Type Definitions
*******************************************************************************/
typedef enum
{
    APP_PD1_ERROR_NONE = 0U,
    APP_PD1_ERROR_LAYOUT = 1U,
    APP_PD1_ERROR_CM55_OFF = 2U,
    APP_PD1_ERROR_SMIF_BUSY = 3U,
    APP_PD1_ERROR_POWER_OFF = 4U,
    APP_PD1_ERROR_POWER_ON = 5U,
    APP_PD1_ERROR_SOCMEM_ON = 6U,
    APP_PD1_ERROR_FLASH_RESTORE = 7U,
    APP_PD1_ERROR_BOOT_VECTOR = 8U,
    APP_PD1_ERROR_CM55_READY = 10U,
    APP_PD1_ERROR_PROTECTION = 11U,
    APP_PD1_ERROR_DEPENDENCY = 12U,
    APP_PD1_ERROR_CM55_SLEEP = 13U,
    APP_PD1_ERROR_MEMORY = 14U,
    APP_PD1_ERROR_IPC_BUSY = 15U,
    APP_PD1_ERROR_SECURE_STAGE = 16U,
    APP_PD1_ERROR_SOCMEM_OFF = 17U
} app_pd1_error_t;

typedef enum
{
    APP_PD1_SECURE_PREPARE,
    APP_PD1_SECURE_PROTECTION,
    APP_PD1_SECURE_MEMORY,
    APP_PD1_SECURE_COMPLETE,
    APP_PD1_SECURE_SLEEP
} app_pd1_secure_stage_t;

/*******************************************************************************
* Macros
*******************************************************************************/
#if !defined(RETENTION_SCENARIO) || !defined(SOCMEM_FRAMEBUFFER) || \
    !defined(PSRAM_IMAGE_SOCMEM_OFF) || !defined(PSRAM_IMAGE_PD1_OFF)
#error "All projects must use the retention scenario definitions from common.mk"
#endif

#if (RETENTION_SCENARIO != SOCMEM_FRAMEBUFFER) && \
    (RETENTION_SCENARIO != PSRAM_IMAGE_SOCMEM_OFF) && \
    (RETENTION_SCENARIO != PSRAM_IMAGE_PD1_OFF)
#error "RETENTION_SCENARIO must select a supported retention scenario"
#endif

/* Application reservation: retained IPC0 channel 15 and interrupt output 3.
 * CM33 acquires the channel lock in
 * app_pd1_cm33_init() and keeps it for the protocol lifetime. BSP/SRF uses IPC1;
 * any override must select an otherwise unused IPC0 channel. */
#ifndef APP_PD1_IPC_CHANNEL
#define APP_PD1_IPC_CHANNEL (15U)
#endif

#define APP_PD1_IPC_INTERRUPT (3U)
#define APP_PD1_IPC_IRQ ((IRQn_Type)CY_IPC0_INTR_MUX(APP_PD1_IPC_INTERRUPT))

#if (APP_PD1_IPC_CHANNEL < 0) || (APP_PD1_IPC_CHANNEL >= 16U)
#error "APP_PD1_IPC_CHANNEL must select an exclusively reserved IPC0 channel"
#endif

typedef enum
{
    APP_PD1_COLD = 0x50443100UL,
    APP_PD1_READY,
    APP_PD1_REQUEST,
    APP_PD1_ACCEPT,
    APP_PD1_PARKED,
    APP_PD1_OFF,
    APP_PD1_RESTORE,
    APP_PD1_RESTART_ABORT,
    APP_PD1_REJECT,
    APP_PD1_FAILED,
    APP_PD1_REJECT_DISABLED,
    APP_PD1_REJECT_MEMORY,
    APP_PD1_REJECT_DEPENDENCY,
    APP_PD1_CPU_OFF_ARMED
} app_pd1_state_t;

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
uint32_t app_pd1_secure_config(app_pd1_secure_stage_t stage);
bool app_pd1_cm33_init(uint32_t boot_address);
void app_pd1_cm33_wait(void);
void app_pd1_cm33_poll(void);
void app_pd1_start_cm55(uint32_t boot_address);

/*******************************************************************************
* Function Name: app_pd1_state
********************************************************************************
* Summary:
*  Reads the retained inter-core protocol state.
*
* Parameters:
*  void
*
* Return:
*  Current app_pd1_state_t value stored in IPC0.
*
*******************************************************************************/
__STATIC_INLINE uint32_t app_pd1_state(void)
{
    return Cy_IPC_Drv_ReadDataValue(Cy_IPC_Drv_GetIpcBaseAddress(APP_PD1_IPC_CHANNEL));
}

/*******************************************************************************
* Function Name: app_pd1_set_state
********************************************************************************
* Summary:
*  Publishes a protocol state after completing preceding memory accesses.
*
* Parameters:
*  state: Next inter-core protocol state.
*
* Return:
*  void
*
*******************************************************************************/
__STATIC_INLINE void app_pd1_set_state(app_pd1_state_t state)
{
    __DMB();
    Cy_IPC_Drv_WriteDataValue(Cy_IPC_Drv_GetIpcBaseAddress(APP_PD1_IPC_CHANNEL), (uint32_t)state);
    __DSB();
#if CY_CPU_CORTEX_M55
    Cy_IPC_Drv_AcquireNotify(Cy_IPC_Drv_GetIpcBaseAddress(APP_PD1_IPC_CHANNEL),
                           1UL << APP_PD1_IPC_INTERRUPT);
#endif
}

#endif
