/***************************************************************************//**
* \file pse84_ns_cm33.ld
* \version 1.0.0
*
* GNU linker layout for the nonsecure CM33 PD1 coordinator.
* All retention scenarios share this layout. Runtime code and constants occupy
* internal SRAM so scenario 3 can execute while external flash is powered off.
* Scenarios 1 and 2 also incur the SRAM usage and startup-copy cost.
*
********************************************************************************
* \copyright
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG.  SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)
ENTRY(Reset_Handler)

/* The C heap uses remaining data SRAM, with an overridable minimum size. */
PROVIDE(__StackSize = DEFINED(APP_MSP_STACK_SIZE) ? APP_MSP_STACK_SIZE : 0x1000);
PROVIDE(__HeapSize = DEFINED(HEAP_SIZE) ? HEAP_SIZE : 0x1000);

/* Use BSP-generated regions and the code-to-system-bus alias offset. */
INCLUDE cymem_gnu_CM33_0.ld
VECTORS_ALIGNMENT = 0x400;
MCUBOOT_HEADER_SIZE = 0x400;
m33_code_sel_Offset = ORIGIN(m33_code) - ORIGIN(m33_code_sel);

SECTIONS
{
    .mcu_boot_header (READONLY) : ALIGN(MCUBOOT_HEADER_SIZE)
    {
        . += MCUBOOT_HEADER_SIZE;
    } > m33_nvm_sel

    /* Early startup remains in flash until it copies the retained runtime. */
    .app_code_main (READONLY) : ALIGN(VECTORS_ALIGNMENT)
    {
        KEEP(*(.vectors))
        KEEP(*(.text.Reset_Handler .text.SystemInit .text.Cy_SystemInit))
        KEEP(*(.text.Cy_PDL_Init .text.Cy_RuntimeInit .text.__cmsis_start))
        *(.rodata.cy_device*)
        . = ALIGN(4);
    } > m33_nvm_sel

    /* Startup copies data, code, and unwind tables; code uses its writable alias. */
    .copy.table (READONLY) : ALIGN(4)
    {
        __copy_table_start__ = .;
        LONG(LOADADDR(.data))
        LONG(ADDR(.data))
        LONG(SIZEOF(.data) / 4)
        LONG(LOADADDR(.app_code_ram))
        LONG(ADDR(.app_code_ram) + m33_code_sel_Offset)
        LONG(SIZEOF(.app_code_ram) / 4)
        LONG(LOADADDR(.ARM.extab))
        LONG(ADDR(.ARM.extab) + m33_code_sel_Offset)
        LONG(SIZEOF(.ARM.extab) / 4)
        LONG(LOADADDR(.ARM.exidx))
        LONG(ADDR(.ARM.exidx) + m33_code_sel_Offset)
        LONG(SIZEOF(.ARM.exidx) / 4)
        __copy_table_end__ = .;
    } > m33_nvm_sel

    /* Only BSS is cleared; NOLOAD vectors and retained sections are separate. */
    .zero.table (READONLY) : ALIGN(4)
    {
        __zero_table_start__ = .;
        LONG(__bss_start__)
        LONG((__bss_end__ - __bss_start__) / 4)
        __zero_table_end__ = .;
    } > m33_nvm_sel

    /* Retain runtime code, constants, and constructor/destructor tables in SRAM. */
    .app_code_ram : ALIGN(4)
    {
        __pd1_ram_start__ = .;
        KEEP(*(.cy_sram_code .cy_ramfunc .text.cy_os_common))
        *(.text .text.* .gnu.linkonce.t.*)
        *(.rodata .rodata.* .constdata .constdata.* .conststring .conststring.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        *crtbegin.*(.ctors)
        *crtbegin?.*(.ctors)
        *(EXCLUDE_FILE(*crtend?.* *crtend.*) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        *crtbegin.*(.dtors)
        *crtbegin?.*(.dtors)
        *(EXCLUDE_FILE(*crtend?.* *crtend.*) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
        __pd1_ram_end__ = .;
    } > m33_code_sel AT > m33_nvm_sel

    /* Unwind metadata must remain accessible when flash is unavailable. */
    .ARM.extab (READONLY) : ALIGN(4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > m33_code_sel AT > m33_nvm_sel
    .ARM.exidx (READONLY) : ALIGN(4)
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > m33_code_sel AT > m33_nvm_sel

    /* BSP startup initializes the RAM vectors; data and BSS use the tables above. */
    .ram_vectors (NOLOAD) : ALIGN(VECTORS_ALIGNMENT)
    {
        KEEP(*(.ram_vectors))
    } > m33_data
    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data*)
        KEEP(*(.jcr*))
        . = ALIGN(4);
        __data_end__ = .;
    } > m33_data AT > m33_nvm_sel
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > m33_data
    .noinit (NOLOAD) : { KEEP(*(.noinit)) } > m33_data
    /* Reserve the main stack at the top of data SRAM and bound the C heap below it. */
    .heap (NOLOAD) : ALIGN(8)
    {
        __HeapBase = .;
        __end__ = .;
        end = .;
        KEEP(*(.heap*))
        . = ORIGIN(m33_data) + LENGTH(m33_data) - __StackSize;
        __HeapLimit = .;
    } > m33_data
    __StackTop = ORIGIN(m33_data) + LENGTH(m33_data);
    __StackLimit = __StackTop - __StackSize;
    PROVIDE(__stack = __StackTop);
    ASSERT(__HeapLimit > __HeapBase, "CM33 internal data/stack overflow")
    ASSERT((__HeapLimit - __HeapBase) >= __HeapSize, "CM33 minimum heap does not fit")

    /* Preserve BSP shared/GPU regions; PD1-off code must not depend on SoCMEM. */
    .cy_sharedmem (NOLOAD) : ALIGN(4)
    {
        KEEP(*(.cy_sharedmem))
        . = ALIGN(4);
    } > m33_allocatable_shared
    .cy_shared_socmem (NOLOAD) : ALIGN(4)
    {
        KEEP(*(.cy_shared_socmem))
        . = ALIGN(4);
    } > m33_m55_shared
    .cy_gpu_buf (NOLOAD) : { KEEP(*(.cy_gpu_buf)) } > gfx_mem
    .cy_trailer (NOLOAD) : { } > m33_trailer_sel
}
/* Reject static references from retained code/data to flash-only startup code. */
NOCROSSREFS_TO(.app_code_main .app_code_ram .data)