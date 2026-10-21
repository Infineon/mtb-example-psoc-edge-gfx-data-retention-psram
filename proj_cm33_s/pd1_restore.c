/*******************************************************************************
 * File Name:   pd1_restore.c
 *
 * Description: Secure protection and memory restoration after PD1 power loss.
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
#include "cycfg_system.h"
#include "cy_syspm_ppu.h"
#include "pd1_protocol.h"

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
#include "pd1_ram.h"
#include <arm_cmse.h>

#if defined(__ICCARM__)
/*******************************************************************************
* Global Variables
*******************************************************************************/
static volatile struct
{
    uint32_t exception;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t sfsr;
    uint32_t sfar;
} app_pd1_secure_fault;

/*******************************************************************************
* Function Name: app_pd1_secure_fault_handler
********************************************************************************
* Summary:
*  Records secure exception status in retained SRAM and stops without flash access.
*  The stacked register frame is not captured.
*
* Parameters:
*  void
*
* Return:
*  Does not return.
*
*******************************************************************************/
static void app_pd1_secure_fault_handler(void)
{
    __disable_irq();
    app_pd1_secure_fault.exception = __get_IPSR();
    app_pd1_secure_fault.cfsr = SCB->CFSR;
    app_pd1_secure_fault.hfsr = SCB->HFSR;
    app_pd1_secure_fault.mmfar = SCB->MMFAR;
    app_pd1_secure_fault.bfar = SCB->BFAR;
    app_pd1_secure_fault.sfsr = SAU->SFSR;
    app_pd1_secure_fault.sfar = SAU->SFAR;
    __DSB();
    for (;;)
    {
        __NOP();
    }
}

/*******************************************************************************
* Function Name: app_pd1_secure_vectors_init
********************************************************************************
* Summary:
*  Installs the retained handler in nonzero core exception entries after startup.
*  Preserves reset, reserved entries, and the incoming interrupt mask.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void app_pd1_secure_vectors_init(void)
{
    uint32_t interrupt_state = Cy_SysLib_EnterCriticalSection();
    volatile uint32_t *vectors = (volatile uint32_t *)SCB->VTOR;
    for (uint32_t vector = 2U; vector < 16U; ++vector)
    {
        if (vectors[vector] != 0U)
        {
            vectors[vector] = (uint32_t)app_pd1_secure_fault_handler;
        }
    }
    __DSB();
    __ISB();
    Cy_SysLib_ExitCriticalSection(interrupt_state);
}
#endif

/*******************************************************************************
* Function Name: pd1_mpc
********************************************************************************
* Summary:
*  Identifies memory protection controllers that lose state with PD1.
*
* Parameters:
*  base: Memory protection controller to inspect.
*
* Return:
*  true for a PD1 memory protection controller.
*
*******************************************************************************/
static bool APP_PD1_RAM pd1_mpc(const MPC_Type *base)
{
    return (base == (MPC_Type *)SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0) ||
           (base == (MPC_Type *)SMIF0_CORE_AXI_MPC0) ||
           (base == (MPC_Type *)SMIF1_CACHE_BLOCK_CACHEBLK_AHB_MPC0) ||
           (base == (MPC_Type *)SMIF1_CORE_AXI_MPC0) ||
           (base == (MPC_Type *)SOCMEM_SRAM_MPC0);
}

/*******************************************************************************
* Function Name: app_pd1_restore_protection
********************************************************************************
* Summary:
*  Restores the generated PD1 peripheral protection configuration.
*
* Parameters:
*  void
*
* Return:
*  PDL configuration status.
*
*******************************************************************************/
static uint32_t APP_PD1_RAM app_pd1_restore_protection(void)
{
    return init_cycfg_ppc1();
}

/*******************************************************************************
* Function Name: app_pd1_restore_memory
********************************************************************************
* Summary:
*  Restores PD1 memory protection and closes temporary secure access windows.
*
* Parameters:
*  void
*
* Return:
*  First failing PDL status, or CY_RSLT_SUCCESS.
*
*******************************************************************************/
static uint32_t APP_PD1_RAM app_pd1_restore_memory(void)
{
    static const cy_en_prot_region_t mpc_registers[] APP_PD1_RAM_DATA =
    {
        PROT_PERI1_SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_MAIN,
        PROT_PERI1_SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_PC,
        PROT_PERI1_SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_ROT,
        PROT_PERI1_SMIF1_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_MAIN,
        PROT_PERI1_SMIF1_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_PC,
        PROT_PERI1_SMIF1_CACHE_BLOCK_CACHEBLK_AHB_MPC0_PPC_MPC_ROT,
        PROT_PERI1_SMIF0_CORE_AXI_MPC0_PPC_MPC_MAIN,
        PROT_PERI1_SMIF0_CORE_AXI_MPC0_PPC_MPC_PC,
        PROT_PERI1_SMIF0_CORE_AXI_MPC0_PPC_MPC_ROT,
        PROT_PERI1_SMIF1_CORE_AXI_MPC0_PPC_MPC_MAIN,
        PROT_PERI1_SMIF1_CORE_AXI_MPC0_PPC_MPC_PC,
        PROT_PERI1_SMIF1_CORE_AXI_MPC0_PPC_MPC_ROT,
        PROT_PERI1_SOCMEM_SRAM_MPC0_PPC_MPC_MAIN,
        PROT_PERI1_SOCMEM_SRAM_MPC0_PPC_MPC_PC,
        PROT_PERI1_SOCMEM_SRAM_MPC0_PPC_MPC_ROT
    };
    cy_stc_ppc_attribute_t secure_access = cycfg_unused_ppc_cfg;
    secure_access.secAttribute = CY_PPC_SECURE;
    static const cy_stc_smif_config_t config APP_PD1_RAM_DATA =
    {
        .mode = CY_SMIF_NORMAL,
        .deselectDelay = 7U,
        .blockEvent = CY_SMIF_BUS_ERROR,
        .enable_internal_dll = false
    };
    cy_stc_smif_context_t context;
    static SMIF_Type *const controllers[] APP_PD1_RAM_DATA =
    {
        (SMIF_Type *)GET_NSALIAS_ADDRESS(SMIF0_CORE),
        (SMIF_Type *)GET_NSALIAS_ADDRESS(SMIF1_CORE)
    };
    uint32_t result = CY_RSLT_SUCCESS;
    for (size_t controller = 0U; controller < 2U; ++controller)
    {
        result = Cy_SMIF_Init(controllers[controller], &config, 1000U, &context);
        if (result != CY_SMIF_SUCCESS)
        {
            break;
        }
        Cy_SMIF_Enable(controllers[controller], &context);
    }
    for (size_t window = 0U;
         (window < sizeof(mpc_registers) / sizeof(mpc_registers[0])) && (result == CY_RSLT_SUCCESS);
         ++window)
    {
        result = Cy_Ppc_ConfigAttrib(PPC1, mpc_registers[window], &secure_access);
    }
    if (result == CY_RSLT_SUCCESS)
    {
        for (size_t response = 0U; response < cy_response_mpcs_count; ++response)
        {
            if (pd1_mpc(cy_response_mpcs[response].base))
            {
                Cy_Mpc_SetViolationResponse(cy_response_mpcs[response].base,
                                           cy_response_mpcs[response].response);
            }
        }
        for (size_t domain_index = 0U;
             (domain_index < unified_mpc_domains_count) && (result == CY_RSLT_SUCCESS);
             ++domain_index)
        {
            const cy_stc_mpc_unified_t *domain = &unified_mpc_domains[domain_index];
            for (uint32_t region_index = 0U;
                 (region_index < domain->region_count) && (result == CY_RSLT_SUCCESS);
                 ++region_index)
            {
                const cy_stc_mpc_regions_t *region = &domain->regions[region_index];
                if (!pd1_mpc(region->base))
                {
                    continue;
                }
                for (uint32_t config_index = 0U; config_index < domain->cfg_count; ++config_index)
                {
                    result = Cy_Mpc_ConfigRotMpcStruct(region->base, region->offset,
                                                      region->size, &domain->cfg[config_index]);
                    if (result != CY_RSLT_SUCCESS)
                    {
                        break;
                    }
                }
            }
        }
    }
    Cy_SMIF_Disable(controllers[1]);
    Cy_SMIF_Disable(controllers[0]);
    for (size_t window = 0U; window < sizeof(mpc_registers) / sizeof(mpc_registers[0]); ++window)
    {
        uint32_t restore_result = Cy_Ppc_ConfigAttrib(PPC1, mpc_registers[window], &cycfg_unused_ppc_cfg);
        if (result == CY_RSLT_SUCCESS)
        {
            result = restore_result;
        }
    }
    __DSB();
    return result;
}

/*******************************************************************************
* Function Name: pd1_sram_range
********************************************************************************
* Summary:
*  Checks that a complete range fits in either retained SRAM address view.
*
* Parameters:
*  address: Start of the range.
*  size: Number of bytes required.
*
* Return:
*  true if the complete range is in internal SRAM.
*
*******************************************************************************/
static bool APP_PD1_RAM pd1_sram_range(uintptr_t address, uint32_t size)
{
    return (size <= CY_SRAM_SIZE) &&
           (((address >= CY_SRAM_BASE) && (address - CY_SRAM_BASE <= CY_SRAM_SIZE - size)) ||
            ((address >= CY_SRAM_CBUS_BASE) && (address - CY_SRAM_CBUS_BASE <= CY_SRAM_SIZE - size)));
}

/*******************************************************************************
* Function Name: app_pd1_secure_interrupts_ready
********************************************************************************
* Summary:
*  Requires retained secure exception vectors and no enabled secure IRQ sources.
*
* Parameters:
*  void
*
* Return:
*  true when restoring secure PRIMASK cannot dispatch a PD1-dependent handler.
*
*******************************************************************************/
static bool APP_PD1_RAM app_pd1_secure_interrupts_ready(void)
{
    if (!pd1_sram_range(SCB->VTOR, 16U * sizeof(uint32_t)) ||
        ((SysTick->CTRL & (SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk)) != 0U) ||
        ((SCB->ICSR & (SCB_ICSR_PENDSVSET_Msk | SCB_ICSR_PENDSTSET_Msk)) != 0U))
    {
        return false;
    }
    const uint32_t *vectors = (const uint32_t *)SCB->VTOR;
    for (uint32_t vector = 2U; vector < 16U; ++vector)
    {
        uint32_t handler = vectors[vector] & ~1UL;
        if ((handler != 0U) && !pd1_sram_range(handler, 1U))
        {
            return false;
        }
    }
    uint32_t banks = (SCnSCB->ICTR & SCnSCB_ICTR_INTLINESNUM_Msk) + 1U;
    for (uint32_t bank = 0U; bank < banks; ++bank)
    {
        if ((NVIC->ISER[bank] & ~NVIC->ITNS[bank]) != 0U)
        {
            return false;
        }
    }
    return true;
}

/*******************************************************************************
* Function Name: app_pd1_secure_sleep_ready
********************************************************************************
* Summary:
*  Checks MAIN power policy and readiness through their nonsecure aliases.
*
* Parameters:
*  void
*
* Return:
*  true when ordinary Deep Sleep is configured and low-power entry is ready.
*
*******************************************************************************/
static bool APP_PD1_RAM app_pd1_secure_sleep_ready(void)
{
    const volatile uint32_t *main_policy =
        (const volatile uint32_t *)GET_NSALIAS_ADDRESS(CY_PPU_MAIN_BASE);
    const SRSS_Type *srss = (const SRSS_Type *)GET_NSALIAS_ADDRESS(SRSS_BASE);
    return ((*main_policy & 0xFUL) == CY_SYSTEM_MAIN_PPU_DEEPSLEEP_MODE) &&
           ((srss->PWR_CTL & SRSS_PWR_CTL_LPM_READY_Msk) != 0U);
}

/*******************************************************************************
* Function Name: app_pd1_secure_sleep
********************************************************************************
* Summary:
*  Enters ordinary Deep Sleep from retained code with PD1 already powered off.
*  The caller has stopped SysTick and armed the MAIN-domain SW4 wake interrupt.
*
* Parameters:
*  void
*
* Return:
*  APP_PD1_ERROR_NONE or APP_PD1_ERROR_LAYOUT for an unsafe sleep configuration.
*
*******************************************************************************/
static uint32_t APP_PD1_RAM app_pd1_secure_sleep(void)
{
    if ((__TZ_get_PRIMASK_NS() == 0U) ||
        !app_pd1_secure_sleep_ready())
    {
        return APP_PD1_ERROR_LAYOUT;
    }
    uint32_t saved_scr = SCB->SCR;
    SCB->SCR = (saved_scr | SCB_SCR_SLEEPDEEP_Msk) & ~SCB_SCR_SLEEPONEXIT_Msk;
    __DSB();
#if defined(CY_IP_MXS22SRSS_VERSION) && defined(CY_IP_MXS22SRSS_VERSION_MINOR) && \
    (CY_IP_MXS22SRSS_VERSION == 1) && (CY_IP_MXS22SRSS_VERSION_MINOR == 0)
    Cy_SysPm_SetRamTrimsPreDs();
#endif
    __WFI();
#if defined(CY_IP_MXS22SRSS_VERSION) && defined(CY_IP_MXS22SRSS_VERSION_MINOR) && \
    (CY_IP_MXS22SRSS_VERSION == 1) && (CY_IP_MXS22SRSS_VERSION_MINOR == 0)
    Cy_SysPm_SetRamTrimsPostDs();
#endif
    SCB->SCR = saved_scr;
    __ISB();
    return APP_PD1_ERROR_NONE;
}

/*******************************************************************************
* Function Name: app_pd1_secure_config
********************************************************************************
* Summary:
*  Executes one stage of the ordered secure restoration sequence.
*  Every invocation restores its incoming secure interrupt mask before return.
*  PREPARE checks interrupt safety; failed stages leave the sequence unchanged.
*
* Parameters:
*  stage: Requested app_pd1_secure_stage_t stage.
*
* Return:
*  APP_PD1_ERROR_NONE or an app_pd1_error_t failure value.
*
*******************************************************************************/
#if defined(__ICCARM__)
__cmse_nonsecure_entry
#else
__attribute__((cmse_nonsecure_entry))
#endif
uint32_t app_pd1_secure_config(app_pd1_secure_stage_t stage)
{
    static app_pd1_secure_stage_t expected_stage = APP_PD1_SECURE_PREPARE;
    uint32_t interrupt_state = Cy_SysLib_EnterCriticalSection();
    uint32_t result = APP_PD1_ERROR_NONE;
    if ((stage == APP_PD1_SECURE_SLEEP) && (expected_stage == APP_PD1_SECURE_PROTECTION))
    {
        result = app_pd1_secure_sleep();
    }
    else if (stage != expected_stage)
    {
        result = APP_PD1_ERROR_SECURE_STAGE;
    }
    else switch (stage)
    {
        case APP_PD1_SECURE_PREPARE:
            if (!app_pd1_secure_interrupts_ready() ||
                !app_pd1_secure_sleep_ready())
            {
                result = APP_PD1_ERROR_LAYOUT;
            }
            else
            {
                expected_stage = APP_PD1_SECURE_PROTECTION;
            }
            break;
        case APP_PD1_SECURE_PROTECTION:
            if (app_pd1_restore_protection() != CY_RSLT_SUCCESS)
            {
                result = APP_PD1_ERROR_PROTECTION;
            }
            else
            {
                expected_stage = APP_PD1_SECURE_MEMORY;
            }
            break;
        case APP_PD1_SECURE_MEMORY:
            if (app_pd1_restore_memory() != CY_RSLT_SUCCESS)
            {
                result = APP_PD1_ERROR_MEMORY;
            }
            else
            {
                expected_stage = APP_PD1_SECURE_COMPLETE;
            }
            break;
        case APP_PD1_SECURE_COMPLETE:
            expected_stage = APP_PD1_SECURE_PREPARE;
            break;
        default:
            result = APP_PD1_ERROR_SECURE_STAGE;
            break;
    }
    Cy_SysLib_ExitCriticalSection(interrupt_state);
    return result;
}
#endif