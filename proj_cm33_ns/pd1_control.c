/*******************************************************************************
 * File Name:   pd1_control.c
 *
 * Description: Retained CM33 control of PD1 shutdown and CM55 restart.
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

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cybsp.h"
#include "cycfg_qspi_memslot.h"
#include "cy_syspm_ppu.h"
#include "cy_syspm_pdcm.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mtb_hal_syspm.h"
#include "pd1_protocol.h"

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)

/*******************************************************************************
* Macros
*******************************************************************************/
#ifndef APP_PD1_INTERNAL_EXECUTION
#define APP_PD1_INTERNAL_EXECUTION (0)
#endif

#define PD1_TIMEOUT_US (100000UL)
#define PD1_BOOT_WAIT_US (1000UL)
#define PD1_APPSS_PWR_UP_US (12UL)

#if !defined(CYBSP_USER_BTN2_ENABLED)
#error "PD1-off requires SW4 on a MAIN-domain GPIO accessible to CM33"
#endif

/*******************************************************************************
* Global Variables
*******************************************************************************/
static TaskHandle_t coordinator_task;
static uint32_t cm55_boot_address;
static cy_stc_smif_context_t flash_context;
static cy_stc_smif_config_t flash_config;
static struct
{
    uint32_t select0;
    uint32_t select1;
    uint32_t output;
    uint32_t config;
    uint32_t input_config;
    uint32_t output_config;
    uint32_t sio_config;
    uint32_t output_config2;
    uint32_t slew_extension;
    uint32_t drive_extension0;
    uint32_t drive_extension1;
    uint32_t output_config3;
} flash_pins[2][3];
static uint32_t app_slave_control[CY_PERI_GROUP_NR];
static uint32_t app_slave_reset[CY_PERI_GROUP_NR];
static struct
{
    uint32_t data0;
    uint32_t data1;
} ipc1_state[APPCPUSS_IPC_NR];
static uint32_t ipc1_interrupt_masks[APPCPUSS_IPC_IRQ_NR];
volatile uint32_t app_pd1_last_error;
volatile uint32_t app_pd1_cycle_count;
static volatile struct
{
    uint32_t ppu;
    uint32_t pwpr;
    uint32_t pwsr;
    uint32_t disr;
    uint32_t misr;
    uint32_t stsr;
    uint32_t aisr;
} app_pd1_power_timeout;

#if APP_PD1_INTERNAL_EXECUTION
/*******************************************************************************
* Function Name: pd0_address
********************************************************************************
* Summary:
*  Checks whether an address belongs to internal SRAM or RRAM.
*
* Parameters:
*  address: Code or data address to inspect.
*
* Return:
*  true if the address is in a memory range retained across PD1-off.
*
*******************************************************************************/
static bool pd0_address(uintptr_t address)
{
    return ((address >= CY_SRAM_BASE) && (address - CY_SRAM_BASE < CY_SRAM_SIZE)) ||
           ((address >= CY_SRAM_CBUS_BASE) && (address - CY_SRAM_CBUS_BASE < CY_SRAM_SIZE)) ||
           ((address >= CY_RRAM_BASE) && (address - CY_RRAM_BASE < CY_RRAM_SIZE)) ||
           ((address >= CY_RRAM_CBUS_BASE) && (address - CY_RRAM_CBUS_BASE < CY_RRAM_SIZE));
}
#endif

/*******************************************************************************
* Function Name: wait_power
********************************************************************************
* Summary:
*  Polls a power policy unit with a one-microsecond delay between checks.
*  Saves the PPU address and status registers in app_pd1_power_timeout on timeout
*  for debugger inspection before recovery changes the power configuration.
*
* Parameters:
*  ppu: Power policy unit to inspect.
*  mode: Required power state.
*
* Return:
*  true if the state is observed within PD1_TIMEOUT_US polling attempts.
*
*******************************************************************************/
static bool wait_power(struct ppu_v1_reg *ppu, enum ppu_v1_mode mode)
{
    for (uint32_t remaining = PD1_TIMEOUT_US; remaining != 0U; --remaining)
    {
        if (cy_pd_ppu_get_power_mode(ppu) == mode)
        {
            return true;
        }
        Cy_SysLib_DelayUs(1U);
    }
    app_pd1_power_timeout.ppu = (uint32_t)ppu;
    app_pd1_power_timeout.pwpr = ppu->PWPR;
    app_pd1_power_timeout.pwsr = ppu->PWSR;
    app_pd1_power_timeout.disr = ppu->DISR;
    app_pd1_power_timeout.misr = ppu->MISR;
    app_pd1_power_timeout.stsr = ppu->STSR;
    app_pd1_power_timeout.aisr = ppu->AISR;
    return false;
}

/*******************************************************************************
* Function Name: pd1_fail
********************************************************************************
* Summary:
*  Publishes a fatal diagnostic and stops unsafe execution.
*
* Parameters:
*  error: app_pd1_error_t diagnostic value.
*
* Return:
*  Does not return.
*
*******************************************************************************/
static void pd1_fail(uint32_t error)
{
    app_pd1_last_error = error;
    app_pd1_set_state(APP_PD1_FAILED);
    __disable_irq();
    for (;;)
    {
        __NOP();
    }
}

/*******************************************************************************
* Function Name: app_pd1_start_cm55
********************************************************************************
* Summary:
*  Starts CM55 from its reset vector after external flash access is restored.
*
* Parameters:
*  boot_address: CM55 vector table address.
*
* Return:
*  void
*
*******************************************************************************/
void app_pd1_start_cm55(uint32_t boot_address)
{
    Cy_SysEnableCM55(MXCM55, boot_address, PD1_BOOT_WAIT_US);
}

/*******************************************************************************
* Function Name: flash_restore
********************************************************************************
* Summary:
*  Restores saved SMIF pins and reestablishes the external flash mapping.
*
* Parameters:
*  void
*
* Return:
*  true if flash controller and memory initialization succeed.
*
*******************************************************************************/
static bool flash_restore(void)
{
    SMIF_Type *const controllers[] = {CYBSP_SMIF_CORE_0_XSPI_FLASH_HW, CYBSP_SMIF_CORE_1_PSRAM_HW};
    for (uint32_t controller = 0U; controller < 2U; ++controller)
    {
        SMIF_Type *flash = controllers[controller];
        for (uint32_t port = 0U; port < 3U; ++port)
        {
            flash->SMIF_GPIO.SMIF_PRT[port].OUT = flash_pins[controller][port].output;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_IN = flash_pins[controller][port].input_config;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT = flash_pins[controller][port].output_config;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_SIO = flash_pins[controller][port].sio_config;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT2 = flash_pins[controller][port].output_config2;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_SLEW_EXT = flash_pins[controller][port].slew_extension;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_DRIVE_EXT0 = flash_pins[controller][port].drive_extension0;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_DRIVE_EXT1 = flash_pins[controller][port].drive_extension1;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT3 = flash_pins[controller][port].output_config3;
            flash->SMIF_HSIOM.SMIF_PRT[port].PORT_SEL0 = flash_pins[controller][port].select0;
            flash->SMIF_HSIOM.SMIF_PRT[port].PORT_SEL1 = flash_pins[controller][port].select1;
            flash->SMIF_GPIO.SMIF_PRT[port].CFG = flash_pins[controller][port].config;
        }
    }
    __DSB();
    flash_config.mode = CY_SMIF_NORMAL;
    if (Cy_SMIF_Init(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW, &flash_config,
                     PD1_TIMEOUT_US, &flash_context) != CY_SMIF_SUCCESS)
    {
        return false;
    }
    Cy_SMIF_Enable(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW, &flash_context);
    if (Cy_SMIF_MemInit(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW,
                       &smif0BlockConfig, &flash_context) != CY_SMIF_SUCCESS)
    {
        return false;
    }
    Cy_SMIF_SetMode(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW, CY_SMIF_MEMORY);
    __DSB();
    __ISB();
    return true;
}

/*******************************************************************************
* Function Name: pd1_wait_for_sw4
********************************************************************************
* Summary:
*  Sleeps until SW4 is pressed and released, with only its GPIO IRQ enabled.
*  Both interrupt masks and GPIO edge configuration are restored before return.
*
* Parameters:
*  void
*
* Return:
*  void; stops if secure Deep Sleep entry is rejected.
*
*******************************************************************************/
static void pd1_wait_for_sw4(void)
{
    uint32_t saved_interrupts[sizeof(NVIC->ISER) / sizeof(NVIC->ISER[0])];
    uint32_t banks = (SCnSCB->ICTR & SCnSCB_ICTR_INTLINESNUM_Msk) + 1U;
    uint32_t saved_edge = Cy_GPIO_GetInterruptEdge(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    uint32_t saved_gpio_mask = CYBSP_USER_BTN2_PORT->INTR_MASK;
    for (uint32_t bank = 0U; bank < banks; ++bank)
    {
        saved_interrupts[bank] = NVIC->ISER[bank];
        NVIC->ICER[bank] = saved_interrupts[bank];
    }
    Cy_GPIO_SetInterruptEdge(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN, CY_GPIO_INTR_BOTH);
    CYBSP_USER_BTN2_PORT->INTR_MASK = 1UL << CYBSP_USER_BTN2_PIN;
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
    NVIC_EnableIRQ(CYBSP_USER_BTN2_IRQ);
    while ((Cy_GPIO_Read(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN) != 0U) &&
           (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN) == 0U))
    {
        uint32_t result = app_pd1_secure_config(APP_PD1_SECURE_SLEEP);
        if (result != APP_PD1_ERROR_NONE)
        {
            pd1_fail(result);
        }
    }
    for (;;)
    {
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
        (void)Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
        if (Cy_GPIO_Read(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN) != 0U)
        {
            break;
        }
        uint32_t result = app_pd1_secure_config(APP_PD1_SECURE_SLEEP);
        if (result != APP_PD1_ERROR_NONE)
        {
            pd1_fail(result);
        }
    }
    NVIC_DisableIRQ(CYBSP_USER_BTN2_IRQ);
    Cy_GPIO_SetInterruptEdge(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN, saved_edge);
    CYBSP_USER_BTN2_PORT->INTR_MASK = saved_gpio_mask;
    for (uint32_t bank = 0U; bank < banks; ++bank)
    {
        NVIC->ISER[bank] = saved_interrupts[bank];
    }
}

/*******************************************************************************
* Function Name: disable_pd1_and_wait_for_sw4
********************************************************************************
* Summary:
*  Powers down CM55, SoCMEM and PD1, then sleeps until SW4 press and release.
*
* Parameters:
*  void
*
* Return:
*  true if PD1 OFF was confirmed; stops on CM55 or SMIF timeout.
*
*******************************************************************************/
static bool disable_pd1_and_wait_for_sw4(void)
{
    Cy_SysCM55Disable();
    if (!wait_power((struct ppu_v1_reg *)CY_PPU_APPCPU_BASE, PPU_V1_MODE_OFF))
    {
        pd1_fail(APP_PD1_ERROR_CM55_OFF);
    }
    for (uint32_t remaining = PD1_TIMEOUT_US;; --remaining)
    {
        if (!Cy_SMIF_BusyCheck(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW) &&
            !Cy_SMIF_BusyCheck(CYBSP_SMIF_CORE_1_PSRAM_HW))
        {
            break;
        }
        if (remaining == 0U)
        {
            pd1_fail(APP_PD1_ERROR_SMIF_BUSY);
        }
        Cy_SysLib_DelayUs(1U);
    }
    Cy_SMIF_Disable(CYBSP_SMIF_CORE_1_PSRAM_HW);
    Cy_SMIF_Disable(CYBSP_SMIF_CORE_0_XSPI_FLASH_HW);
    Cy_SysEnableSOCMEM(false);
    bool powered_off = false;
    if (wait_power((struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE, PPU_V1_MODE_OFF))
    {
        Cy_System_DisablePD1();
        powered_off = wait_power((struct ppu_v1_reg *)CY_PPU_PD1_BASE, PPU_V1_MODE_OFF);
        if (!powered_off)
        {
            app_pd1_last_error = APP_PD1_ERROR_POWER_OFF;
        }
    }
    else
    {
        app_pd1_last_error = APP_PD1_ERROR_SOCMEM_OFF;
    }
    if (powered_off)
    {
        ++app_pd1_cycle_count;
        app_pd1_set_state(APP_PD1_OFF);
        pd1_wait_for_sw4();
    }
    return powered_off;
}

/*******************************************************************************
* Function Name: restore_secure_config
********************************************************************************
* Summary:
*  Calls one secure restoration stage and stops if the service reports failure.
*
* Parameters:
*  stage: Requested app_pd1_secure_stage_t stage.
*
* Return:
*  void
*
*******************************************************************************/
static void restore_secure_config(app_pd1_secure_stage_t stage)
{
    uint32_t result = app_pd1_secure_config(stage);
    if (result != APP_PD1_ERROR_NONE)
    {
        pd1_fail(result);
    }
}

/*******************************************************************************
* Function Name: enable_pd1_after_sw4
********************************************************************************
* Summary:
*  Restores PD1 power, protection, memory, IPC1 and external flash access.
*
* Parameters:
*  void
*
* Return:
*  void; stops on a failed restoration step.
*
*******************************************************************************/
static void enable_pd1_after_sw4(void)
{
    (void)cy_pd_ppu_set_power_mode((struct ppu_v1_reg *)CY_PPU_PD1_BASE, PPU_V1_MODE_ON);
    if (!wait_power((struct ppu_v1_reg *)CY_PPU_PD1_BASE, PPU_V1_MODE_ON))
    {
        pd1_fail(APP_PD1_ERROR_POWER_ON);
    }
    if (cy_pd_pdcm_set_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU) != CY_RSLT_SUCCESS)
    {
        pd1_fail(APP_PD1_ERROR_DEPENDENCY);
    }
    Cy_SysLib_DelayUs(PD1_APPSS_PWR_UP_US);
    restore_secure_config(APP_PD1_SECURE_PROTECTION);
    for (uint32_t group = 0U; group < CY_PERI_GROUP_NR; ++group)
    {
        PERI_GR_SL_CTL2(1U, group) = app_slave_reset[group];
        PERI_GR_SL_CTL(1U, group) = app_slave_control[group];
    }
    __DSB();
    Cy_System_EnablePD1();
    Cy_SysEnableSOCMEM(true);
    if (!wait_power((struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE, PPU_V1_MODE_ON))
    {
        pd1_fail(APP_PD1_ERROR_SOCMEM_ON);
    }
    restore_secure_config(APP_PD1_SECURE_MEMORY);
    for (uint32_t channel = 0U; channel < APPCPUSS_IPC_NR; ++channel)
    {
        IPC1->STRUCT[channel].DATA0 = ipc1_state[channel].data0;
        IPC1->STRUCT[channel].DATA1 = ipc1_state[channel].data1;
    }
    for (uint32_t interrupt = 0U; interrupt < APPCPUSS_IPC_IRQ_NR; ++interrupt)
    {
        IPC1->INTR_STRUCT[interrupt].INTR_MASK = ipc1_interrupt_masks[interrupt];
    }
    __DSB();
    if (!flash_restore())
    {
        pd1_fail(APP_PD1_ERROR_FLASH_RESTORE);
    }
    restore_secure_config(APP_PD1_SECURE_COMPLETE);
}

/*******************************************************************************
* Function Name: pd1_cycle
********************************************************************************
* Summary:
*  Saves controller state, cycles PD1 and restarts CM55 without resetting MAIN.
*  Scheduling and interrupts remain suspended until flash is restored.
*
* Parameters:
*  void
*
* Return:
*  void; stops if power restoration or CM55 startup fails.
*
*******************************************************************************/
static void pd1_cycle(void)
{
    uint32_t vector_words[2];
    uint32_t saved_app_dependency = CY_PDCM_PD_SENSE(CY_PD_PDCM_APPCPUSS);
    uint32_t saved_socmem_dependency = CY_PDCM_PD_SENSE(CY_PD_PDCM_SOCMEM);
    uint32_t saved_pd1_policy = cy_pd_ppu_get_programmed_power_mode((struct ppu_v1_reg *)CY_PPU_PD1_BASE);
    uint32_t saved_app_policy = cy_pd_ppu_get_programmed_power_mode((struct ppu_v1_reg *)CY_PPU_APPCPUSS_BASE);
    uint32_t saved_socmem_policy = cy_pd_ppu_get_programmed_power_mode((struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE);
    flash_config = *CYBSP_SMIF_CORE_0_XSPI_FLASH_hal_config.config;
    SMIF_Type *const controllers[] = {CYBSP_SMIF_CORE_0_XSPI_FLASH_HW, CYBSP_SMIF_CORE_1_PSRAM_HW};
    for (uint32_t controller = 0U; controller < 2U; ++controller)
    {
        SMIF_Type *flash = controllers[controller];
        for (uint32_t port = 0U; port < 3U; ++port)
        {
            flash_pins[controller][port].select0 = flash->SMIF_HSIOM.SMIF_PRT[port].PORT_SEL0;
            flash_pins[controller][port].select1 = flash->SMIF_HSIOM.SMIF_PRT[port].PORT_SEL1;
            flash_pins[controller][port].output = flash->SMIF_GPIO.SMIF_PRT[port].OUT;
            flash_pins[controller][port].config = flash->SMIF_GPIO.SMIF_PRT[port].CFG;
            flash_pins[controller][port].input_config = flash->SMIF_GPIO.SMIF_PRT[port].CFG_IN;
            flash_pins[controller][port].output_config = flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT;
            flash_pins[controller][port].sio_config = flash->SMIF_GPIO.SMIF_PRT[port].CFG_SIO;
            flash_pins[controller][port].output_config2 = flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT2;
            flash_pins[controller][port].slew_extension = flash->SMIF_GPIO.SMIF_PRT[port].CFG_SLEW_EXT;
            flash_pins[controller][port].drive_extension0 = flash->SMIF_GPIO.SMIF_PRT[port].CFG_DRIVE_EXT0;
            flash_pins[controller][port].drive_extension1 = flash->SMIF_GPIO.SMIF_PRT[port].CFG_DRIVE_EXT1;
            flash_pins[controller][port].output_config3 = flash->SMIF_GPIO.SMIF_PRT[port].CFG_OUT3;
        }
    }
    for (uint32_t group = 0U; group < CY_PERI_GROUP_NR; ++group)
    {
        app_slave_control[group] = PERI_GR_SL_CTL(1U, group);
        app_slave_reset[group] = PERI_GR_SL_CTL2(1U, group);
    }
    vector_words[0] = ((volatile uint32_t *)cm55_boot_address)[0];
    vector_words[1] = ((volatile uint32_t *)cm55_boot_address)[1];

    vTaskSuspendAll();
    uint32_t interrupt_state = Cy_SysLib_EnterCriticalSection();
    uint32_t systick_control = SysTick->CTRL;
    SysTick->CTRL = 0U;
    uint32_t pending_exceptions = SCB->ICSR & (SCB_ICSR_PENDSTSET_Msk | SCB_ICSR_PENDSVSET_Msk);
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    for (uint32_t channel = 0U; channel < APPCPUSS_IPC_NR; ++channel)
    {
        if (Cy_IPC_Drv_IsLockAcquired(&IPC1->STRUCT[channel]))
        {
            pd1_fail(APP_PD1_ERROR_IPC_BUSY);
        }
        ipc1_state[channel].data0 = IPC1->STRUCT[channel].DATA0;
        ipc1_state[channel].data1 = IPC1->STRUCT[channel].DATA1;
    }
    for (uint32_t interrupt = 0U; interrupt < APPCPUSS_IPC_IRQ_NR; ++interrupt)
    {
        ipc1_interrupt_masks[interrupt] = IPC1->INTR_STRUCT[interrupt].INTR_MASK;
    }
    __DSB();
    restore_secure_config(APP_PD1_SECURE_PREPARE);
    bool powered_off = disable_pd1_and_wait_for_sw4();
    enable_pd1_after_sw4();
    if ((((volatile uint32_t *)cm55_boot_address)[0] != vector_words[0]) ||
        (((volatile uint32_t *)cm55_boot_address)[1] != vector_words[1]))
    {
        pd1_fail(APP_PD1_ERROR_BOOT_VECTOR);
    }
    CY_PDCM_PD_SENSE(CY_PD_PDCM_APPCPUSS) = saved_app_dependency;
    CY_PDCM_PD_SENSE(CY_PD_PDCM_SOCMEM) = saved_socmem_dependency;
    (void)cy_pd_ppu_set_power_mode((struct ppu_v1_reg *)CY_PPU_PD1_BASE, saved_pd1_policy);
    (void)cy_pd_ppu_set_power_mode((struct ppu_v1_reg *)CY_PPU_APPCPUSS_BASE, saved_app_policy);
    (void)cy_pd_ppu_set_power_mode((struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE, saved_socmem_policy);
    app_pd1_set_state(powered_off ? APP_PD1_RESTORE : APP_PD1_RESTART_ABORT);
    SCB->ICSR = pending_exceptions;
    SysTick->CTRL = systick_control;
    Cy_SysLib_ExitCriticalSection(interrupt_state);
    (void)xTaskResumeAll();
    app_pd1_start_cm55(cm55_boot_address);
    for (uint32_t attempt = 0U; attempt < 3000U; ++attempt)
    {
        if (app_pd1_state() == APP_PD1_READY)
        {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    pd1_fail(APP_PD1_ERROR_CM55_READY);
}

/*******************************************************************************
* Function Name: pd1_layout_status
********************************************************************************
* Summary:
*  Checks retained code/data addresses and the required power dependencies.
*
* Parameters:
*  void
*
* Return:
*  APP_PD1_ACCEPT or a protocol rejection reason.
*
*******************************************************************************/
static app_pd1_state_t pd1_layout_status(void)
{
#if APP_PD1_INTERNAL_EXECUTION
    uintptr_t stack_address = (uintptr_t)&stack_address;
    bool internal_memory = pd0_address(stack_address) &&
           pd0_address((uintptr_t)pd1_cycle) &&
           pd0_address((uintptr_t)disable_pd1_and_wait_for_sw4) &&
           pd0_address((uintptr_t)enable_pd1_after_sw4) &&
           pd0_address((uintptr_t)restore_secure_config) &&
           pd0_address((uintptr_t)wait_power) &&
           pd0_address((uintptr_t)pd1_fail) &&
           pd0_address((uintptr_t)flash_restore) &&
           pd0_address((uintptr_t)Cy_System_DisablePD1) &&
           pd0_address((uintptr_t)Cy_System_EnablePD1) &&
           pd0_address((uintptr_t)Cy_SysEnableSOCMEM) &&
           pd0_address((uintptr_t)cy_pd_ppu_get_power_mode) &&
           pd0_address((uintptr_t)cy_pd_ppu_set_power_mode) &&
           pd0_address((uintptr_t)Cy_SysLib_DelayUs) &&
           pd0_address((uintptr_t)Cy_SMIF_Init) &&
           pd0_address((uintptr_t)Cy_SMIF_MemInit) &&
           pd0_address((uintptr_t)&flash_context) &&
           pd0_address((uintptr_t)&flash_config) &&
           pd0_address((uintptr_t)flash_pins) &&
           pd0_address((uintptr_t)ipc1_state) &&
           pd0_address((uintptr_t)ipc1_interrupt_masks) &&
           pd0_address((uintptr_t)app_slave_control) &&
           pd0_address((uintptr_t)app_slave_reset) &&
           pd0_address((uintptr_t)&smif0BlockConfig) &&
           pd0_address((uintptr_t)SCB->VTOR);
    if (!internal_memory)
    {
        return APP_PD1_REJECT_MEMORY;
    }
    if ((cy_pd_pdcm_get_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU) != CY_PD_PDCM_DEP_CONFIG) ||
        (cy_pd_pdcm_get_dependency(CY_PD_PDCM_SOCMEM, CY_PD_PDCM_SYSCPU) != CY_PD_PDCM_DEP_CONFIG))
    {
        return APP_PD1_REJECT_DEPENDENCY;
    }
    return APP_PD1_ACCEPT;
#else
    return APP_PD1_REJECT_DISABLED;
#endif
}

/*******************************************************************************
* Function Name: pd1_ipc_interrupt
********************************************************************************
* Summary:
*  Acknowledges CM55 state changes and wakes the coordinator task.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void pd1_ipc_interrupt(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    Cy_IPC_Drv_ClearInterrupt(Cy_IPC_Drv_GetIntrBaseAddr(APP_PD1_IPC_INTERRUPT),
                             0U, 1UL << APP_PD1_IPC_CHANNEL);
    if (coordinator_task != NULL)
    {
        vTaskNotifyGiveFromISR(coordinator_task, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/*******************************************************************************
* Function Name: app_pd1_cm33_wait
********************************************************************************
* Summary:
*  Blocks until CM55 changes state, allowing Sleep between requests.
*  Holds a Deep Sleep lock while waiting so that CM55 requests are serviced
*  promptly; the lock is released before the PD1-off sleep cycle.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_pd1_cm33_wait(void)
{
    coordinator_task = xTaskGetCurrentTaskHandle();
    if (app_pd1_state() != APP_PD1_REQUEST)
    {
        mtb_hal_syspm_lock_deepsleep();
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        mtb_hal_syspm_unlock_deepsleep();
    }
}

/*******************************************************************************
* Function Name: app_pd1_cm33_init
********************************************************************************
* Summary:
*  Saves the CM55 boot address and reserves IPC0 for the protocol lifetime.
*
* Parameters:
*  boot_address: CM55 vector table address.
*
* Return:
*  true if the IPC channel and notification interrupt were reserved.
*
*******************************************************************************/
bool app_pd1_cm33_init(uint32_t boot_address)
{
    cm55_boot_address = boot_address;
    if (Cy_IPC_Drv_GetInterruptMask(Cy_IPC_Drv_GetIntrBaseAddr(APP_PD1_IPC_INTERRUPT)) != 0U)
    {
        return false;
    }
    if (Cy_IPC_Drv_LockAcquire(Cy_IPC_Drv_GetIpcBaseAddress(APP_PD1_IPC_CHANNEL)) != CY_IPC_DRV_SUCCESS)
    {
        return false;
    }
    app_pd1_set_state(APP_PD1_COLD);
    const cy_stc_sysint_t ipc_interrupt =
    {
        .intrSrc = APP_PD1_IPC_IRQ,
        .intrPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS)
    };
    if (Cy_SysInt_Init(&ipc_interrupt, pd1_ipc_interrupt) != CY_SYSINT_SUCCESS)
    {
        return false;
    }
    Cy_IPC_Drv_ClearInterrupt(Cy_IPC_Drv_GetIntrBaseAddr(APP_PD1_IPC_INTERRUPT),
                             0U, 1UL << APP_PD1_IPC_CHANNEL);
    Cy_IPC_Drv_SetInterruptMask(Cy_IPC_Drv_GetIntrBaseAddr(APP_PD1_IPC_INTERRUPT),
                               0U, 1UL << APP_PD1_IPC_CHANNEL);
    NVIC_ClearPendingIRQ(APP_PD1_IPC_IRQ);
    NVIC_EnableIRQ(APP_PD1_IPC_IRQ);
    return true;
}

/*******************************************************************************
* Function Name: app_pd1_cm33_poll
********************************************************************************
* Summary:
*  Handles a shutdown request and waits for CM55 to park or cancel it.
*  A parked CM55 is powered down and restarted through pd1_cycle().
*
* Parameters:
*  void
*
* Return:
*  void; stops if a parked CM55 does not enter its sleep state before timeout.
*
*******************************************************************************/
void app_pd1_cm33_poll(void)
{
    if (app_pd1_state() != APP_PD1_REQUEST)
    {
        return;
    }
    app_pd1_state_t layout_status = pd1_layout_status();
    if (layout_status != APP_PD1_ACCEPT)
    {
        app_pd1_last_error = APP_PD1_ERROR_LAYOUT;
        app_pd1_set_state(layout_status);
        return;
    }
    app_pd1_last_error = APP_PD1_ERROR_NONE;
    app_pd1_set_state(APP_PD1_ACCEPT);
    for (;;)
    {
        uint32_t state = app_pd1_state();
        if (state == APP_PD1_PARKED)
        {
            Cy_SysCM55SetDbgPort(APPCPUSS_DBG_DISABLE);
            app_pd1_set_state(APP_PD1_CPU_OFF_ARMED);
            uint32_t sleep_mask = MXCM55_CM55_STATUS_SLEEPING_Msk | MXCM55_CM55_STATUS_SLEEPDEEP_Msk;
            for (uint32_t remaining = PD1_TIMEOUT_US;
                 (MXCM55->CM55_STATUS & sleep_mask) != sleep_mask; --remaining)
            {
                if (remaining == 0U)
                {
                    pd1_fail(APP_PD1_ERROR_CM55_SLEEP);
                }
                Cy_SysLib_DelayUs(1U);
            }
            pd1_cycle();
            return;
        }
        else if (state != APP_PD1_ACCEPT)
        {
            return;
        }
        mtb_hal_syspm_lock_deepsleep();
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        mtb_hal_syspm_unlock_deepsleep();
    }
}

#endif