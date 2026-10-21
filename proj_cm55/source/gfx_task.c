/*******************************************************************************
* File Name:   gfx_task.c
*
* Description: This file contains the task definition for initializing the
*              graphics subsystem, allocate memory for frame buffers, draw 
*              and decode images and display them on the screen.
* 
* Related Document: See README.md
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
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "retarget_io_init.h"
#include "cy_graphics.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos_hal_impl.h"
#include "image_database_file.h"
#include "vg_lite.h"
#include "vg_lite_platform.h"
#include "mtb_hal_syspm.h"
#include "cybsp.h"
#include "cy_gpio.h"
#include "cy_sysclk.h"
#include "cy_sysint.h"
#include "gfx_task.h"
#include "resolution_overlay.h"
#include "jpeg_image_decoder.h"
#include "motion_task.h"
#include "smif_psram_control.h"
#include "cy_syspm.h"
#include "cy_syspm_ppu.h"
#include "cycfg_peripheral_clocks.h"
#include "cycfg_peripherals.h"
#include "cycfg_protection.h"
#include "pd1_protocol.h"

/*******************************************************************************
* Macros
*******************************************************************************/

/* RGB565_BPP (bytes per pixel for RGB565 format) is provided by
 * jpeg_image_decoder.h. */

/* Max buffer size for decoded images (in RGB565 format: 2 bytes per pixel) */
#define MAX_IMAGE_BUFFER_SIZE   (MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * RGB565_BPP)

/* Stride alignment mask for 64-byte boundary (64 - 1) */
#define STRIDE_ALIGN_MASK       (63U)

/* Temporary display buffer sizing: 64-byte aligned stride */
#define ALIGNED_STRIDE_DISPLAY  (((MAX_IMAGE_WIDTH * RGB565_BPP) + STRIDE_ALIGN_MASK) & ~STRIDE_ALIGN_MASK)
#define MAX_DISPLAY_BUFFER_SIZE (ALIGNED_STRIDE_DISPLAY * MAX_IMAGE_HEIGHT)


#if defined(__ARMCC_VERSION)
CY_ALIGN(64) __attribute__((section(".bss.cy_xip"), used)) uint8_t img_background_png_data[MAX_IMAGE_BUFFER_SIZE];
CY_ALIGN(64) __attribute__((section(".bss.cy_xip"), used)) uint8_t display_temp_buffer[MAX_DISPLAY_BUFFER_SIZE];
#elif defined(__ICCARM__)
#pragma location = ".cy_xip"
__no_init CY_ALIGN(64) uint8_t img_background_png_data[MAX_IMAGE_BUFFER_SIZE];
#pragma location = ".cy_xip"
__no_init CY_ALIGN(64) uint8_t display_temp_buffer[MAX_DISPLAY_BUFFER_SIZE];
#else
CY_ALIGN(64) CY_SECTION(".cy_xip") __attribute__((used)) uint8_t img_background_png_data[MAX_IMAGE_BUFFER_SIZE];
CY_ALIGN(64) CY_SECTION(".cy_xip") __attribute__((used)) uint8_t display_temp_buffer[MAX_DISPLAY_BUFFER_SIZE];
#endif

unsigned char* image = img_background_png_data;  // Buffer for decoded JPEG images

static bool retained_image_restored = false;

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
#define RETAINED_IMAGE_MAGIC   (0x50535233UL)
#define RETAINED_IMAGE_VERSION (2UL)

static uint32_t retained_frame_stride;
static uint32_t retained_frame_bytes;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_bytes;
    uint32_t pixel_checksum;
    uint32_t source_checksum;
    uint32_t frame_stride;
    uint32_t frame_bytes;
    uint32_t frame_checksum;
    uint32_t metadata_checksum;
} retained_image_t;

#if defined(__ARMCC_VERSION)
CY_ALIGN(64) __attribute__((section(".bss.cy_xip"), used)) static volatile retained_image_t retained_image;
#elif defined(__ICCARM__)
#pragma location = ".cy_xip"
__no_init CY_ALIGN(64) static volatile retained_image_t retained_image;
#else
CY_ALIGN(64) CY_SECTION(".cy_xip") __attribute__((used)) static volatile retained_image_t retained_image;
#endif
#endif

#define VG_PARAMS_POS                       (0UL)

#define I2C_CONTROLLER_IRQ_PRIORITY         (2UL)

#ifdef USE_KIT_PSE84_AI
#define DISPLAY_I2C_CONTROLLER_IRQ          (CYBSP_I2C_DISPLAY_CONTROLLER_IRQ)
#define DISPLAY_I2C_CONTROLLER_HW           (CYBSP_I2C_DISPLAY_CONTROLLER_HW)
#define DISPLAY_I2C_CONTROLLER_config       (CYBSP_I2C_DISPLAY_CONTROLLER_config)
#else
#define DISPLAY_I2C_CONTROLLER_IRQ          (CYBSP_I2C_CONTROLLER_IRQ)
#define DISPLAY_I2C_CONTROLLER_HW           (CYBSP_I2C_CONTROLLER_HW)
#define DISPLAY_I2C_CONTROLLER_config       (CYBSP_I2C_CONTROLLER_config)
#endif

#define BUTTON_INTERRUPT_PRIORITY           (7U)
#define DEBOUNCE_DELAY_MS                   (300U)
#define BUFFER_SWAP_TIMEOUT_MS              (1000U)
#define SLEEP_ENTRY_RETRY_COUNT             (5U)
#define SLEEP_ENTRY_RETRY_DELAY_US          (1000U)

/* Deep Sleep panel/backlight controls (all scenarios) */
#define PANEL_BRIGHTNESS_OFF_PERCENT        (0U)
#define PANEL_BRIGHTNESS_ON_PERCENT         (100U)
#define WAKEUP_SETTLE_DELAY_MS              (50U)

/*******************************************************************************
* Global Variable(s)
*******************************************************************************/
/* Current image dimensions - updated based on which image is being decoded */
static int current_image_width = 0;
static int current_image_height = 0;
static GFXSS_Type* base = (GFXSS_Type*) GFXSS;
cy_stc_gfx_context_t gfx_context;

static vg_lite_buffer_t buffer0;
static vg_lite_buffer_t buffer1;
static vg_lite_matrix_t matrix;

/* Decoded image buffer - populated by decode_current_image(), reused by draw() */
static vg_lite_buffer_t image_buffer;


/* Heap memory for VGLite to allocate memory for buffers, command, and
 * tessellation buffers
 */
CY_SECTION(".cy_gpu_buf") uint8_t contiguous_mem[VGLITE_HEAP_SIZE];

volatile void *vglite_heap_base = &contiguous_mem;

/* Variable for storing character read from terminal */
uint32_t uart_read_value;

cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc = gfxss_interrupt_dc_IRQn,
    .intrPriority = DC_INT_PRIORITY
};

cy_stc_scb_i2c_context_t disp_i2c_controller_context;

cy_stc_sysint_t disp_i2c_controller_irq_cfg =
{
    .intrSrc      = DISPLAY_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

cy_stc_sysint_t user_button_irq_cfg =
{
   .intrSrc      = CYBSP_USER_BTN_IRQ,
   .intrPriority = BUTTON_INTERRUPT_PRIORITY,
};


static __IO int32_t  active_buffer  = RESET_VAL;
static __IO int32_t  pending_buffer = INVALID_VAL;

/* Button state variables */
static volatile bool button_pressed = false;
static volatile uint8_t current_image = 0;
static uint32_t debounce_ticks;

/* Deep Sleep state shared by all scenarios.
 * SW4 (USER_BTN2) requests entering Deep Sleep and also wakes the CPU from it.
 * Only scenario 1 retains the SoCMEM frame buffers; all scenarios require
 * external PSRAM to remain powered. */
static volatile bool sw4_low_power_event = false;
static volatile bool deep_sleep_enabled  = false;
static bool          mipi_data_ulpm_entered = false;

/* Array of pointers to frame buffers */
static vg_lite_buffer_t *buffers[APP_BUFFER_COUNT] = { &buffer0, &buffer1 };

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
/*******************************************************************************
* Function Name: retention_checksum
********************************************************************************
* Summary:
*  Computes an FNV-1a checksum to detect accidental corruption of retained data.
*
* Parameters:
*  data - Bytes to checksum.
*  length - Number of bytes.
*
* Return:
*  32-bit checksum.
*
*******************************************************************************/
static uint32_t retention_checksum(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t checksum = 2166136261UL;

    for (size_t index = 0U; index < length; ++index)
    {
        checksum = (checksum ^ bytes[index]) * 16777619UL;
    }
    return checksum;
}

/*******************************************************************************
* Function Name: invalidate_retained_image
********************************************************************************
* Summary:
*  Consumes the retained record so it cannot be reused by a later reset.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void invalidate_retained_image(void)
{
    retained_image.magic = 0U;
    SCB_CleanDCache();
    __DSB();
}

/*******************************************************************************
* Function Name: save_retained_image
********************************************************************************
* Summary:
*  Saves image metadata and checksums in PSRAM, committing the valid marker last.
*
* Parameters:
*  void
*
* Return:
*  true if a bounded, database-matching image was saved; false otherwise.
*
*******************************************************************************/
static bool save_retained_image(void)
{
    invalidate_retained_image();
    if ((current_image >= NUM_IMAGES) ||
        (current_image_width <= 0) || (current_image_width > MAX_IMAGE_WIDTH) ||
        (current_image_height <= 0) || (current_image_height > MAX_IMAGE_HEIGHT))
    {
        return false;
    }

    const image_info_t *source = &image_database[current_image];
    if ((source->jpeg_data == NULL) || (source->jpeg_size == 0U) ||
        (source->width != current_image_width) ||
        (source->height != current_image_height))
    {
        return false;
    }

    uint32_t pixel_bytes = (uint32_t)current_image_width *
                           (uint32_t)current_image_height * RGB565_BPP;
    const vg_lite_buffer_t *frame = buffers[active_buffer];
    if ((frame->memory == NULL) || (frame->stride <= 0) ||
        (frame->height != DISPLAY_H) || (frame->width != DISPLAY_W) ||
        ((uint32_t)frame->stride > sizeof(display_temp_buffer) / DISPLAY_H))
    {
        return false;
    }
    uint32_t frame_bytes = (uint32_t)frame->stride * DISPLAY_H;
    memcpy(display_temp_buffer, frame->memory, frame_bytes);
    retained_image_t record =
    {
        .magic = RETAINED_IMAGE_MAGIC,
        .version = RETAINED_IMAGE_VERSION,
        .image_index = current_image,
        .width = (uint32_t)current_image_width,
        .height = (uint32_t)current_image_height,
        .pixel_bytes = pixel_bytes,
        .pixel_checksum = retention_checksum(img_background_png_data, pixel_bytes),
        .source_checksum = retention_checksum(source->jpeg_data, source->jpeg_size),
        .frame_stride = (uint32_t)frame->stride,
        .frame_bytes = frame_bytes,
        .frame_checksum = retention_checksum(display_temp_buffer, frame_bytes)
    };
    record.metadata_checksum = retention_checksum(&record,
                                      offsetof(retained_image_t, metadata_checksum));

    record.magic = 0U;
    retained_image = record;
    SCB_CleanDCache();
    __DSB();
    retained_image.magic = RETAINED_IMAGE_MAGIC;
    SCB_CleanDCache();
    __DSB();
    return true;
}
#endif

/*******************************************************************************
* Function Name: gfx_restore_retained_image
********************************************************************************
* Summary:
*  Validates and consumes retained PSRAM pixels after SMIF/XIP initialization.
*  Must run before the graphics task clears or decodes the image buffers.
*
* Parameters:
*  pd1_restart - true only for a CM33-coordinated PD1 restart indication.
*
* Return:
*  true if the retained image is valid and ready for descriptor reconstruction.
*
*******************************************************************************/
bool gfx_restore_retained_image(bool pd1_restart)
{
    retained_image_restored = false;
#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    retained_image_t record = retained_image;
    invalidate_retained_image();

    if (!pd1_restart || (record.magic != RETAINED_IMAGE_MAGIC) ||
        (record.version != RETAINED_IMAGE_VERSION) ||
        (record.metadata_checksum != retention_checksum(&record,
                                     offsetof(retained_image_t, metadata_checksum))) ||
        (record.image_index >= NUM_IMAGES) ||
        (record.width == 0U) || (record.width > MAX_IMAGE_WIDTH) ||
        (record.height == 0U) || (record.height > MAX_IMAGE_HEIGHT) ||
        (record.pixel_bytes != record.width * record.height * RGB565_BPP) ||
        (record.pixel_bytes > sizeof(img_background_png_data)) ||
        (record.frame_stride < DISPLAY_W * RGB565_BPP) ||
        (record.frame_stride > sizeof(display_temp_buffer) / DISPLAY_H) ||
        (record.frame_bytes != record.frame_stride * DISPLAY_H))
    {
        return false;
    }

    init_image_database();
    const image_info_t *source = &image_database[record.image_index];
    if ((source->jpeg_data == NULL) || (source->jpeg_size == 0U) ||
        (record.width != (uint32_t)source->width) ||
        (record.height != (uint32_t)source->height) ||
        (record.source_checksum != retention_checksum(source->jpeg_data, source->jpeg_size)) ||
        (record.pixel_checksum != retention_checksum(img_background_png_data, record.pixel_bytes)) ||
        (record.frame_checksum != retention_checksum(display_temp_buffer, record.frame_bytes)))
    {
        return false;
    }

    current_image = (uint8_t)record.image_index;
    current_image_width = (int)record.width;
    current_image_height = (int)record.height;
    retained_frame_stride = record.frame_stride;
    retained_frame_bytes = record.frame_bytes;
    retained_image_restored = true;
#else
    CY_UNUSED_PARAMETER(pd1_restart);
#endif
    return retained_image_restored;
}

/*******************************************************************************
* Function Name: wait_for_buffer_swap
********************************************************************************
* Summary:
*  This function waits for a buffer swap to complete within a specified timeout.
*
* Parameters:
*  timeout_ms - The timeout period in milliseconds
*
* Return:
*  true if the buffer swap completed within the timeout, false otherwise
*
*******************************************************************************/
static bool wait_for_buffer_swap(uint32_t timeout_ms)
{
    TickType_t start_ticks = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (pending_buffer >= RESET_VAL)
    {
        if ((xTaskGetTickCount() - start_ticks) >= timeout_ticks)
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return true;
}

/* Forward declaration — defined after render_image() */
static void decode_current_image(void);
static void prepare_image_buffer(void);

/* Deep Sleep helpers (all scenarios) */
static void enter_deep_sleep_mode(void);
static void exit_deep_sleep_mode(void);
static void reinitialize_graphics_pipeline(void);

/*******************************************************************************
* Function Name: disable_sw2_interrupt
********************************************************************************
* Summary:
*  Masks image-navigation interrupts during the low-power transition.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void disable_sw2_interrupt(void)
{
    button_pressed = false;
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    Cy_GPIO_SetInterruptMask(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN, 0UL);
}

/*******************************************************************************
* Function Name: enable_sw2_interrupt
********************************************************************************
* Summary:
*  Clears stale button events and enables image navigation after recovery.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void enable_sw2_interrupt(void)
{
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    Cy_GPIO_SetInterruptMask(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN, 1UL);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
}

/*******************************************************************************
* Function Name: render_image
********************************************************************************
* Summary:
*  This function renders the current image to the display, handling buffer
*  swaps and user input for image navigation.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void render_image()
{

    if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
    {
        printf("WARNING: Timeout waiting for startup frame swap\r\n");
        pending_buffer = INVALID_VAL;
    }

    /* Discard stale button events from panel init window */
    button_pressed = false;
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
#if defined(CYBSP_USER_BTN2_ENABLED)
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
#endif

    /* Prepare retained pixels or decode the first image, then display it. */
    int32_t draw_buffer = (active_buffer + 1) % APP_BUFFER_COUNT;
    if (retained_image_restored)
    {
#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
        if ((uint32_t)buffers[draw_buffer]->stride != retained_frame_stride)
        {
            printf("ERROR: Retained framebuffer layout changed.\r\n");
            handle_app_error();
        }
        memcpy(buffers[draw_buffer]->memory, display_temp_buffer, retained_frame_bytes);
        SCB_CleanDCache();
        __DSB();
#endif
        prepare_image_buffer();
        retained_image_restored = false;
        printf("Retained PSRAM framebuffer restored without JPEG decoding.\r\n");
    }
    else
    {
        decode_current_image();
        draw(buffers[draw_buffer]);
    }
    pending_buffer = draw_buffer;
    Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(buffers[pending_buffer]->address), &gfx_context);
    if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
    {
        printf("WARNING: Timeout waiting for first image swap\r\n");
        pending_buffer = INVALID_VAL;
#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
        app_pd1_set_state(APP_PD1_FAILED);
        handle_app_error();
#endif
    }
#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    enable_sw2_interrupt();
    app_pd1_set_state(APP_PD1_READY);
#endif

    printf("\r\nPress USER button (SW2) to decode and display next image...\r\n");
    printf("Press USER button (SW4) to enter Deep Sleep...\r\n");

#ifdef AUTO_ROTATION
    /* Track the rotation state at the time of last draw to detect changes */
    bool last_rotate_180 = (current_orientation == ORIENTATION_BOTTOM_EDGE);
#endif

    while (true)
    {
        bool advance_image = false;

        /* Wait for a button press, a low-power request, or a rotation change */
        for (;;)
        {
            if (sw4_low_power_event)
            {
                /* SW4 is handled below, outside the image-advance path. */
                break;
            }
            if (button_pressed)
            {
                button_pressed = false;
                advance_image = true;
                break;
            }
#ifdef AUTO_ROTATION
            bool curr_rotate_180 = (current_orientation == ORIENTATION_BOTTOM_EDGE);
            if (curr_rotate_180 != last_rotate_180)
            {
                last_rotate_180 = curr_rotate_180;
                break;
            }
#endif
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        /* SW4 requests the selected low-power and display-retention scenario. */
        if (sw4_low_power_event)
        {
            sw4_low_power_event = false;
            enter_deep_sleep_mode();  /* Blocks in Deep Sleep; returns after wake + restore */

#if (RETENTION_SCENARIO != SOCMEM_FRAMEBUFFER)
            /* Re-blit the last decoded image (retained in PSRAM) into a clean
             * back buffer and present it. */
            draw_buffer = (active_buffer + 1) % APP_BUFFER_COUNT;
            draw(buffers[draw_buffer]);
            pending_buffer = draw_buffer;
            Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(buffers[pending_buffer]->address), &gfx_context);
            if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
            {
                printf("WARNING: Timeout waiting for wakeup restore swap\r\n");
                pending_buffer = INVALID_VAL;
            }
#endif
            printf("\r\nPress USER button (SW2) to decode and display next image...\r\n");
            printf("Press USER button (SW4) to enter Deep Sleep...\r\n");
            continue;
        }

        if (advance_image)
        {
            current_image = (current_image + 1) % NUM_IMAGES;
            /* New image selected: decode it before blitting */
            decode_current_image();
        }
        /* else: same image, orientation changed — reuse decoded buffer, just re-blit */

        /* Draw into the back buffer (not currently displayed) */
        draw_buffer = (active_buffer + 1) % APP_BUFFER_COUNT;
        draw(buffers[draw_buffer]);
        pending_buffer = draw_buffer;
        Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(buffers[pending_buffer]->address), &gfx_context);
        if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
        {
            printf("WARNING: Timeout waiting for display buffer swap\r\n");
            pending_buffer = INVALID_VAL;
        }

        if (advance_image)
        {
            printf("\r\nPress USER button (SW2) to decode and display next image...\r\n");
        }
    }
}


/*******************************************************************************
* Function Name: decode_current_image
********************************************************************************
* Summary:
*  Decodes the current JPEG image into the raw pixel buffer and prepares the
*  image_buffer descriptor (stride alignment / cropping). Must be called once
*  before draw(), and again whenever the displayed image changes.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void decode_current_image(void)
{
    /* Decode the current image into img_background_png_data */
    if (!decode_image(current_image, image, &current_image_width, &current_image_height))
    {
        handle_app_error();
        return;
    }
    prepare_image_buffer();
}

/*******************************************************************************
* Function Name: prepare_image_buffer
********************************************************************************
* Summary:
*  Builds the image descriptor and aligned/cropped pixels without JPEG decoding.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void prepare_image_buffer(void)
{
    /* Calculate stride for RGB565 (2 bytes per pixel) */
    int base_stride = current_image_width * RGB565_BPP;

    /* Reset image_buffer descriptor */
    memset(&image_buffer, 0, sizeof(image_buffer));

    /* Determine if we need to crop/scale and copy to aligned buffer */
    uint8_t* image_data_ptr = img_background_png_data;
    int display_width = current_image_width;
    int display_height = current_image_height;
    int display_stride = base_stride;

#ifdef SCALING_IMAGE
    /* Scaling mode: keep full decoded image, only enforce aligned stride */
    int aligned_stride = (base_stride + STRIDE_ALIGN_MASK) & ~STRIDE_ALIGN_MASK;
    if (aligned_stride != base_stride)
    {
        display_stride = aligned_stride;
#ifdef DISPLAY_LOG
        printf("Re-aligning image stride from %d to %d bytes...\r\n", base_stride, aligned_stride);
#endif
        uint8_t* src_line = img_background_png_data;
        uint8_t* dst_line = display_temp_buffer;

        for (int y = 0; y < display_height; y++)
        {
            memcpy(dst_line, src_line, display_width * RGB565_BPP);
            src_line += base_stride;
            dst_line += display_stride;
        }

        image_data_ptr = display_temp_buffer;
    }
#else
    /* Non-scaling mode: crop oversized image to display bounds, then align stride */
    if (current_image_height > DISPLAY_H || current_image_width > DISPLAY_W)
    {
        int crop_x = 0;
        int crop_y = 0;

        if (current_image_height > DISPLAY_H)
        {
            crop_y = (current_image_height - DISPLAY_H) / 2;
            display_height = DISPLAY_H;
        }

        if (current_image_width > DISPLAY_W)
        {
            crop_x = (current_image_width - DISPLAY_W) / 2;
            display_width = DISPLAY_W;
        }

        display_stride = ALIGNED_STRIDE_DISPLAY;
        uint8_t* src_line = img_background_png_data + (crop_y * base_stride) + (crop_x * RGB565_BPP);
        uint8_t* dst_line = display_temp_buffer;

        for (int y = 0; y < display_height; y++) {
            memcpy(dst_line, src_line, display_width * RGB565_BPP);
            src_line += base_stride;
            dst_line += display_stride;
        }

        image_data_ptr = display_temp_buffer;
    }
    else
    {
        int aligned_stride = (base_stride + STRIDE_ALIGN_MASK) & ~STRIDE_ALIGN_MASK;
        if (aligned_stride != base_stride)
        {
            display_stride = aligned_stride;
            uint8_t* src_line = img_background_png_data;
            uint8_t* dst_line = display_temp_buffer;

            for (int y = 0; y < display_height; y++)
            {
                memcpy(dst_line, src_line, display_width * RGB565_BPP);
                src_line += base_stride;
                dst_line += display_stride;
            }

            image_data_ptr = display_temp_buffer;
        }
    }
#endif

    /* Populate image_buffer descriptor for use by draw() */
    image_buffer.width = display_width;
    image_buffer.height = display_height;
    image_buffer.stride = display_stride;
    image_buffer.format = VG_LITE_BGR565;
    image_buffer.handle = NULL;
    image_buffer.memory = (void*)image_data_ptr;
    image_buffer.address = (uint32_t)image_data_ptr;
    image_buffer.tiled = VG_LITE_LINEAR;
    image_buffer.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    image_buffer.transparency_mode = VG_LITE_IMAGE_OPAQUE;
}

/*******************************************************************************
* Function Name: draw
********************************************************************************
* Summary:
*  This function performs GPU aided drawing of the already-decoded image into
*  the memory buffer passed as argument.  decode_current_image() must have been
*  called at least once before this function.
*
* Parameters:
*  vg_lite_buffer_t *target: Pointer to a vglite struct.
*
* Return:
*  void
*
*******************************************************************************/
void draw(vg_lite_buffer_t *target)
{
    vg_lite_error_t error = VG_LITE_SUCCESS;

    int display_width  = image_buffer.width;
    int display_height = image_buffer.height;

    /* Loads an identity matrix */
    vg_lite_identity(&matrix);

    /* Determine whether 180-degree rotation is needed */
#ifdef AUTO_ROTATION
    bool rotate_180 = (current_orientation == ORIENTATION_BOTTOM_EDGE);
#else
    bool rotate_180 = false;
#endif

    int x_offset = 0;
    int y_offset = 0;

#ifdef SCALING_IMAGE
    /* Scaling mode: Scale image to fit full display area */
    float scale_x = (float)DISPLAY_W / display_width;
    float scale_y = (float)DISPLAY_H / display_height;

#ifdef AUTO_ROTATION
    if (rotate_180)
    {
        /* 180-degree rotation: translate to bottom-right corner, scale, rotate */
        vg_lite_translate((vg_lite_float_t)DISPLAY_W, (vg_lite_float_t)DISPLAY_H, &matrix);
        vg_lite_scale(scale_x, scale_y, &matrix);
        vg_lite_rotate(180.0f, &matrix);
    }
    else
#endif
    {
        vg_lite_scale(scale_x, scale_y, &matrix);
        x_offset = 0;
        y_offset = 0;
        vg_lite_translate(x_offset, y_offset, &matrix);
    }

#ifdef DISPLAY_LOG
    printf("Scaling image to fit display (sx=%.3f, sy=%.3f): %dx%d -> %dx%d\r\n",
           scale_x, scale_y, display_width, display_height, DISPLAY_W, DISPLAY_H);
#endif

#else
    /* Cropping mode: Center the cropped/aligned image on display */
    x_offset = (DISPLAY_W - display_width) / 2;
    y_offset = (DISPLAY_H - display_height) / 2;
#ifdef DISPLAY_LOG
    printf("Positioning image at offset (%d,%d), size %dx%d\r\n",
           x_offset, y_offset, display_width, display_height);
#endif

#ifdef AUTO_ROTATION
    if (rotate_180)
    {
        /* 180-degree rotation: translate to far corner of image extent, then rotate */
        vg_lite_translate((vg_lite_float_t)(x_offset + display_width),
                          (vg_lite_float_t)(y_offset + display_height), &matrix);
        vg_lite_rotate(180.0f, &matrix);
    }
    else
#endif
    {
        vg_lite_translate(x_offset, y_offset, &matrix);
    }

#endif

    /* Clear the target frame buffer */
    error = vg_lite_clear(target, NULL, BLACK_COLOR);
    if (error)
    {
        printf("Clear failed: vg_lite_clear() returned error %d\n", error);
        handle_app_error();
    }

#ifdef DISPLAY_LOG
    printf("Blitting image: buffer=%dx%d, target=%dx%d, offset=(%d,%d)\r\n",
           image_buffer.width, image_buffer.height,
           target->width, target->height, x_offset, y_offset);
#endif

#ifdef SCALING_IMAGE
    /* Use bilinear filtering for better quality when scaling */
    vg_lite_filter_t filter = VG_LITE_FILTER_BI_LINEAR;
#else
    vg_lite_filter_t filter = VG_LITE_FILTER_POINT;
#endif

    error = vg_lite_blit(target, &image_buffer, &matrix, VG_LITE_BLEND_SRC_OVER, 0, filter);

#ifdef SCALING_IMAGE
    if (error)
    {
        printf("WARNING: Bilinear blit failed with error %d, retrying with point filter\r\n", error);
        filter = VG_LITE_FILTER_POINT;
        error = vg_lite_blit(target, &image_buffer, &matrix, VG_LITE_BLEND_SRC_OVER, 0, filter);
    }
#endif

    if (error)
    {
        printf("ERROR: vg_lite_blit() returned error %d\n", error);
        printf("  Image buffer: %dx%d, stride=%d, format=%d\r\n",
               image_buffer.width, image_buffer.height, image_buffer.stride, image_buffer.format);
        printf("  Target buffer: %dx%d, stride=%d, format=%d\r\n",
               target->width, target->height, target->stride, target->format);
        handle_app_error();
    }

    draw_resolution_overlay(target, current_image_width, current_image_height, rotate_180);
    vg_lite_finish();
}


#if !_BAREMETAL
extern void vg_lite_IRQHandler(void);

/*******************************************************************************
* Function Name: gpu_irq_handler
********************************************************************************
* Summary:
*  Dispatches the VGLite interrupt and clears the GFXSS GPU interrupt source.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void gpu_irq_handler(void)
{
    vg_lite_IRQHandler();
    Cy_GFXSS_Clear_GPU_Interrupt(base, &gfx_context);
    __DSB();
}

/*******************************************************************************
* Function Name: initialize_gpu_interrupt
********************************************************************************
* Summary:
*  Configures and enables the GPU interrupt used by the FreeRTOS VGLite driver.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void initialize_gpu_interrupt(void)
{
    const cy_stc_sysint_t gpu_irq_cfg =
    {
        .intrSrc = gfxss_interrupt_gpu_IRQn,
        .intrPriority = configMAX_SYSCALL_INTERRUPT_PRIORITY >> (8U - __NVIC_PRIO_BITS)
    };

    if (CY_SYSINT_SUCCESS != Cy_SysInt_Init(&gpu_irq_cfg, gpu_irq_handler))
    {
        printf("ERROR: GPU interrupt initialization failed\r\n");
        handle_app_error();
    }
    Cy_GFXSS_Clear_GPU_Interrupt(base, &gfx_context);
    NVIC_ClearPendingIRQ(gfxss_interrupt_gpu_IRQn);
    Cy_GFXSS_Enable_GPU_Interrupt(base);
    NVIC_EnableIRQ(gfxss_interrupt_gpu_IRQn);
}
#endif

/*******************************************************************************
* Function Name: dc_irq_handler
********************************************************************************
* Summary:
*  Records completion of the pending framebuffer swap and clears the DC interrupt.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void dc_irq_handler(void)
{
    if (pending_buffer >= RESET_VAL)
    {
        active_buffer = pending_buffer;
        pending_buffer = INVALID_VAL;
    }

    /* Clear the DC interrupt */
    Cy_GFXSS_Clear_DC_Interrupt(base, &gfx_context);
}


/*******************************************************************************
* Function Name: disp_i2c_controller_interrupt
********************************************************************************
* Summary:
*  I2C controller ISR which invokes Cy_SCB_I2C_Interrupt to perform I2C transfer
*  as controller.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void disp_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(DISPLAY_I2C_CONTROLLER_HW, &disp_i2c_controller_context);
}


/*******************************************************************************
* Function Name: reinitialize_graphics_pipeline
********************************************************************************
* Summary:
*  Restores GFXSS after sleep. Scenario 1 reuses its retained VGLite context and
*  active framebuffer without clearing or reallocating. Other scenarios rebuild
*  VGLite after pre-sleep teardown and allocate fresh framebuffers.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void reinitialize_graphics_pipeline(void)
{
    cy_en_gfx_status_t status = CY_GFX_SUCCESS;
#if (RETENTION_SCENARIO != SOCMEM_FRAMEBUFFER)
    vg_lite_error_t error = VG_LITE_SUCCESS;
#endif

#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    GFXSS_graphics_layer.buffer_address = (gctADDRESS*)buffers[active_buffer]->address;
#else
    GFXSS_graphics_layer.buffer_address = (gctADDRESS*)contiguous_mem;
#endif

    status = Cy_GFXSS_Init(base, &GFXSS_config, &gfx_context);
    if (CY_GFX_SUCCESS != status)
    {
        printf("ERROR: GFXSS re-init failed after wakeup (status=%d)\r\n", status);
        handle_app_error();
    }

    NVIC_ClearPendingIRQ(GFXSS_DC_IRQ);
    NVIC_EnableIRQ(GFXSS_DC_IRQ);
#if !_BAREMETAL
    initialize_gpu_interrupt();
#endif

#if (RETENTION_SCENARIO != SOCMEM_FRAMEBUFFER)
    vg_module_parameters_t vg_params = {0};
    vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
    vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
    vg_params.contiguous_mem_base[VG_PARAMS_POS] = (volatile void *) vglite_heap_base;
    vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;
    vg_lite_init_mem(&vg_params);

    error = vg_lite_init(DISPLAY_W, DISPLAY_H);
    if (VG_LITE_SUCCESS != error)
    {
        printf("ERROR: VGLite re-init failed after wakeup (error=%d)\r\n", error);
        handle_app_error();
    }

    buffer0.width  = DISPLAY_W;
    buffer0.height = DISPLAY_H;
    buffer0.format = VG_LITE_BGR565;
    error = vg_lite_allocate(&buffer0);
    if (VG_LITE_SUCCESS != error)
    {
        printf("ERROR: Buffer0 re-allocation failed after wakeup (error=%d)\r\n", error);
        handle_app_error();
    }

    buffer1.width  = DISPLAY_W;
    buffer1.height = DISPLAY_H;
    buffer1.format = VG_LITE_BGR565;
    error = vg_lite_allocate(&buffer1);
    if (VG_LITE_SUCCESS != error)
    {
        printf("ERROR: Buffer1 re-allocation failed after wakeup (error=%d)\r\n", error);
        handle_app_error();
    }

    if (vg_lite_clear(&buffer0, NULL, BLACK_COLOR) != VG_LITE_SUCCESS)
    {
        printf("ERROR: Failed to clear buffer0 during wakeup re-init\r\n");
        handle_app_error();
    }
    if (vg_lite_clear(&buffer1, NULL, BLACK_COLOR) != VG_LITE_SUCCESS)
    {
        printf("ERROR: Failed to clear buffer1 during wakeup re-init\r\n");
        handle_app_error();
    }
    if (vg_lite_finish() != VG_LITE_SUCCESS)
    {
        printf("ERROR: Failed to finish framebuffer initialization\r\n");
        handle_app_error();
    }

    active_buffer  = 0;
#endif
    pending_buffer = active_buffer;
    Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(buffers[active_buffer]->address), &gfx_context);
    if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
    {
        printf("WARNING: Timeout waiting for wakeup re-init frame swap\r\n");
        pending_buffer = INVALID_VAL;
    }
}


#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
/*******************************************************************************
* Function Name: request_pd1_shutdown
********************************************************************************
* Summary:
*  Requests CM33-coordinated PD1 shutdown and waits for acceptance or rejection.
*
* Parameters:
*  void
*
* Return:
*  true if CM33 accepts the request; false on rejection or timeout.
*
*******************************************************************************/
static bool request_pd1_shutdown(void)
{
    if (app_pd1_state() != APP_PD1_READY)
    {
        printf("PD1: IPC coordinator state=0x%08lx, expected READY.\r\n",
               (unsigned long)app_pd1_state());
        return false;
    }
    app_pd1_set_state(APP_PD1_REQUEST);
    printf("PD1: shutdown request published, state=0x%08lx.\r\n",
           (unsigned long)app_pd1_state());
    for (uint32_t attempt = 0U; attempt < 100U; ++attempt)
    {
        uint32_t state = app_pd1_state();
        if (state == APP_PD1_ACCEPT)
        {
            printf("PD1: CM33 accepted shutdown; internal-memory checks passed.\r\n");
            return true;
        }
        if (state == APP_PD1_REJECT_DISABLED)
        {
            printf("PD1: CM33 was built with PD1_INTERNAL_EXECUTION=0.\r\n");
            break;
        }
        if (state == APP_PD1_REJECT_MEMORY)
        {
            printf("PD1: CM33 code, data, stack or vector table is outside internal memory.\r\n");
            break;
        }
        if (state == APP_PD1_REJECT_DEPENDENCY)
        {
            printf("PD1: APP/SoCMEM dependencies are not configurable from this setup.\r\n");
            break;
        }
        if ((state == APP_PD1_REJECT) || (state == APP_PD1_FAILED))
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    printf("PD1: shutdown not accepted, final state=0x%08lx.\r\n",
           (unsigned long)app_pd1_state());
    app_pd1_set_state(APP_PD1_READY);
    return false;
}

/*******************************************************************************
* Function Name: park_cm55_for_pd1_shutdown
********************************************************************************
* Summary:
*  Parks CM55 in internal RAM while CM33 removes PD1 power. Successful shutdown
*  restarts CM55 through reset; a cancelled transition restores the saved state.
*
* Parameters:
*  void
*
* Return:
*  void; returns only if shutdown is cancelled before PD1 powers off.
*
*******************************************************************************/
CY_SECTION(".cy_ramfunc") __attribute__((noinline))
static void park_cm55_for_pd1_shutdown(void)
{
    uint32_t interrupt_state = Cy_SysLib_EnterCriticalSection();
    uint32_t systick_control = SysTick->CTRL;
    uint32_t sleep_control = SCB->SCR;
    uint32_t coprocessor_power = SCS_CPPWR;
    uint32_t coprocessor_access = SCB->CPACR;
    uint32_t memory_control = MEM_CTL_MSCR;
    uint32_t wake_control = EWIC_EWCI_ASCR;
    uint32_t interrupt_masks[16];
    uint32_t interrupt_banks = ((ICB->ICTR & ICB_ICTR_INTLINESNUM_Msk) >>
                                ICB_ICTR_INTLINESNUM_Pos) + 1U;

    SysTick->CTRL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    for (uint32_t bank = 0U; bank < interrupt_banks; ++bank)
    {
        interrupt_masks[bank] = NVIC->ISER[bank];
        NVIC->ICER[bank] = 0xFFFFFFFFUL;
        NVIC->ICPR[bank] = 0xFFFFFFFFUL;
    }
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    MEM_CTL_MSCR &= ~(MEMSYSCTL_MSCR_ICACTIVE_Msk | MEMSYSCTL_MSCR_DCACTIVE_Msk);
    EWIC_EWCI_ASCR &= ~EWIC_EWIC_ASCR_ASPU_Msk;
    SCS_CPPWR |= SCS_ENABLE_CPPWR_SU10_SU11;
    SCB->CPACR &= ~SCB_ENABLE_CPACR_CP10_CP11;
    __DSB();
    __ISB();
    app_pd1_set_state(APP_PD1_PARKED);
    while (app_pd1_state() == APP_PD1_PARKED)
    {
        __NOP();
    }
    while (app_pd1_state() == APP_PD1_CPU_OFF_ARMED)
    {
        SCB->SCR = sleep_control | SCB_SCR_SLEEPDEEP_Msk;
        __DSB();
        __WFI();
        __ISB();
    }
    SCB->SCR = sleep_control;
    MEM_CTL_MSCR = memory_control;
    EWIC_EWCI_ASCR = wake_control;
    SCS_CPPWR = coprocessor_power;
    SCB->CPACR = coprocessor_access;
    __DSB();
    __ISB();
    for (uint32_t bank = 0U; bank < interrupt_banks; ++bank)
    {
        NVIC->ISER[bank] = interrupt_masks[bank];
    }
    SysTick->CTRL = systick_control;
    Cy_SysLib_ExitCriticalSection(interrupt_state);
}
#endif

#if (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
/*******************************************************************************
* Function Name: request_socmem_power
********************************************************************************
* Summary:
*  Requests a SoCMEM power mode and waits for the power controller to confirm it.
*
* Parameters:
*  mode - Requested PPU power mode.
*
* Return:
*  true if the requested mode is reached; false on failure or timeout.
*
*******************************************************************************/
static bool request_socmem_power(uint32_t mode)
{
    struct ppu_v1_reg *const power = (struct ppu_v1_reg *)CY_PPU_SOCMEM_BASE;
    if (cy_pd_ppu_set_power_mode(power, mode) != CY_RSLT_SUCCESS)
    {
        return false;
    }
    for (uint32_t remaining = 1000U; remaining != 0U; --remaining)
    {
        if (cy_pd_ppu_get_power_mode(power) == mode)
        {
            return true;
        }
        Cy_SysLib_DelayUs(1U);
    }
    return false;
}
#endif

/*******************************************************************************
* Function Name: enter_deep_sleep_mode
********************************************************************************
* Summary:
*  Prepares the panel and graphics runtime before arming the selected policy.
*  Scenarios 1 and 2 return on SW4 wake. Successful scenario 3 restarts CM55;
*  a returning park operation is handled as an aborted transition. All returning
*  paths restore ordinary Deep Sleep policies before resuming task scheduling.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void enter_deep_sleep_mode(void)
{
    cy_en_scb_i2c_status_t panel_i2c_status = CY_SCB_I2C_SUCCESS;
    cy_en_mipidsi_status_t mipi_status = CY_MIPIDSI_SUCCESS;

    printf("\r\n--- Entering Deep Sleep Mode ---\r\n");
#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    printf("Retention: last frame kept in SoCMEM (SoCMEM + PSRAM stay powered).\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    printf("Retention: decoded image kept in PSRAM; SoCMEM will be powered OFF.\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    printf("Retention: display snapshot kept in PSRAM; CM33 will power off PD1 on CM55's request.\r\n");
#endif
    printf("Display will sleep. Press USER button (SW4) to wake up...\r\n");

    /* Make sure any in-flight frame swap has completed. */
    if (pending_buffer >= RESET_VAL)
    {
        if (!wait_for_buffer_swap(BUFFER_SWAP_TIMEOUT_MS))
        {
            printf("ERROR: Pending frame swap prevents sleep\r\n");
            handle_app_error();
        }
    }

    if (vg_lite_finish() != VG_LITE_SUCCESS)
    {
        printf("ERROR: GPU work prevents sleep\r\n");
        handle_app_error();
    }

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    if (!request_pd1_shutdown())
    {
        printf("PD1: entry not accepted; no power-off requested.\r\n");
        return;
    }
    if (!save_retained_image())
    {
        app_pd1_set_state(APP_PD1_READY);
        printf("ERROR: Could not snapshot the displayed framebuffer.\r\n");
        return;
    }
#endif

    /* Turn the panel backlight off, then power the panel controller down. */
    panel_i2c_status = mtb_disp_waveshare_4p3_set_brightness(DISPLAY_I2C_CONTROLLER_HW,
                                                             &disp_i2c_controller_context,
                                                             PANEL_BRIGHTNESS_OFF_PERCENT);
    if (CY_SCB_I2C_SUCCESS != panel_i2c_status)
    {
        printf("WARNING: Failed to set panel brightness to 0 (status=%d)\r\n", panel_i2c_status);
    }

    panel_i2c_status = mtb_disp_waveshare_4p3_deinit(DISPLAY_I2C_CONTROLLER_HW,
                                                     &disp_i2c_controller_context);
    if (CY_SCB_I2C_SUCCESS != panel_i2c_status)
    {
        printf("WARNING: Failed to deinit panel before Deep Sleep (status=%d)\r\n", panel_i2c_status);
    }

    /* Put the DPHY data lane into ULPM to reduce MIPI power during Deep Sleep. */
    mipi_status = Cy_MIPIDSI_EnterULPM_Data(GFXSS_GFXSS_MIPIDSI);
    if (CY_MIPIDSI_SUCCESS != mipi_status)
    {
        mipi_data_ulpm_entered = false;
        printf("WARNING: Failed to enter DPHY Data-lane ULPM (status=%u)\r\n", (unsigned int)mipi_status);
    }
    else
    {
        mipi_data_ulpm_entered = true;
    }

    /* Assert panel reset to fully switch the panel off. */
    Cy_GPIO_Clr(CYBSP_DISP_RST_PORT, CYBSP_DISP_RST_PIN);
    Cy_SysLib_Delay(5U);

    deep_sleep_enabled = true;
    sw4_low_power_event = false;

    /* Ignore SW2 while sleeping; SW4 remains enabled on the shared IRQ line. */
    disable_sw2_interrupt();
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
#if defined(CYBSP_USER_BTN2_ENABLED)
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);

    /* Wait for the entry press to be released before arming the wake edge. */
    while (0U == Cy_GPIO_Read(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN))
    {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
#endif

    NVIC_DisableIRQ(GFXSS_DC_IRQ);
    pending_buffer = INVALID_VAL;
#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    base->GFXSS_DC.MXDC.INTR_MASK = 0U;
    Cy_GFXSS_Clear_DC_Interrupt(base, &gfx_context);
    base->GFXSS_DC.MXDC.CTL &= ~GFXSS_DC_MXDC_CTL_ENABLED_Msk;
    NVIC_ClearPendingIRQ(GFXSS_DC_IRQ);
    __DSB();
#else
    if ((vg_lite_free(&buffer0) != VG_LITE_SUCCESS) ||
        (vg_lite_free(&buffer1) != VG_LITE_SUCCESS) ||
        (vg_lite_close() != VG_LITE_SUCCESS))
    {
        printf("ERROR: Graphics teardown failed before sleep\r\n");
        handle_app_error();
    }
    memset(&buffer0, 0, sizeof(buffer0));
    memset(&buffer1, 0, sizeof(buffer1));
    if (Cy_GFXSS_DeInit(base, &gfx_context) != CY_GFX_SUCCESS)
    {
        printf("ERROR: GFXSS shutdown failed before sleep\r\n");
        handle_app_error();
    }
    mipi_data_ulpm_entered = false;
#endif

#if (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    vTaskSuspendAll();
#if !_BAREMETAL
    mtb_hal_syspm_unlock_deepsleep();
#endif
    uint32_t dcache_enabled = SCB->CCR & SCB_CCR_DC_Msk;
    SCB_DisableDCache();
    __DSB();
    Cy_SysPm_DeepSleepUpdate();
    if ((app_pd1_state() == APP_PD1_ACCEPT) &&
        (Cy_SysPm_GetDeepSleepMode() == CY_SYSPM_MODE_DEEPSLEEP))
    {
        park_cm55_for_pd1_shutdown();
    }
    app_pd1_set_state(APP_PD1_READY);
    invalidate_retained_image();
    if (dcache_enabled != 0U)
    {
        SCB_EnableDCache();
    }
#if !_BAREMETAL
    mtb_hal_syspm_lock_deepsleep();
#endif
    (void)xTaskResumeAll();
    prepare_image_buffer();
    printf("PD1: aborted before PD1-off; CM55 resumed without reset.\r\n");
#else
    cy_en_syspm_status_t sleep_status = CY_SYSPM_SUCCESS;
    const cy_en_syspm_deep_sleep_mode_t requested_mode = CY_SYSPM_MODE_DEEPSLEEP;

    cy_en_syspm_status_t app_policy_status = CY_SYSPM_FAIL;
    cy_en_syspm_status_t socmem_policy_status = CY_SYSPM_FAIL;
    vTaskSuspendAll();
#if !_BAREMETAL
    mtb_hal_syspm_unlock_deepsleep();
#endif
    uint32_t dcache_enabled = SCB->CCR & SCB_CCR_DC_Msk;
    SCB_DisableDCache();
    __DSB();
    if (sleep_status == CY_SYSPM_SUCCESS)
    {
        app_policy_status = Cy_SysPm_SetAppDeepSleepMode(requested_mode);
        sleep_status = app_policy_status;
    }
    if (sleep_status == CY_SYSPM_SUCCESS)
    {
#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    socmem_policy_status = Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
#else
    socmem_policy_status = Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP_OFF);
#endif
    sleep_status = socmem_policy_status;
    }
    Cy_SysPm_DeepSleepUpdate();
    const cy_en_syspm_deep_sleep_mode_t effective_mode = Cy_SysPm_GetDeepSleepMode();
    if ((sleep_status == CY_SYSPM_SUCCESS) && (effective_mode != requested_mode))
    {
        sleep_status = CY_SYSPM_FAIL;
    }

#if (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    uint32_t socmem_dependency = CY_PDCM_PD_SENSE(CY_PD_PDCM_SOCMEM);
    if (sleep_status == CY_SYSPM_SUCCESS)
    {
        if ((cy_pd_pdcm_clear_dependency(CY_PD_PDCM_SOCMEM, CY_PD_PDCM_SYSCPU) != CY_RSLT_SUCCESS) ||
            (cy_pd_pdcm_clear_dependency(CY_PD_PDCM_SOCMEM, CY_PD_PDCM_APPCPU) != CY_RSLT_SUCCESS) ||
            !request_socmem_power(PPU_V1_MODE_OFF))
        {
            sleep_status = CY_SYSPM_FAIL;
        }
    }
#endif

    uint32_t tick_interrupt_state = Cy_SysLib_EnterCriticalSection();
    uint32_t systick_control = SysTick->CTRL;
    SysTick->CTRL = 0U;
    uint32_t pending_systick = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    __DSB();
    Cy_SysLib_ExitCriticalSection(tick_interrupt_state);
    uint32_t entry_failures = 0U;

    while ((sleep_status == CY_SYSPM_SUCCESS) && !sw4_low_power_event)
    {
        uint32_t interrupt_state = Cy_SysLib_EnterCriticalSection();
        if (!sw4_low_power_event)
        {
            sleep_status = Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
        }
        Cy_SysLib_ExitCriticalSection(interrupt_state);
        if (sleep_status == CY_SYSPM_SUCCESS)
        {
            entry_failures = 0U;
        }
        else if (!sw4_low_power_event && (++entry_failures < SLEEP_ENTRY_RETRY_COUNT))
        {
            Cy_SysLib_DelayUs(SLEEP_ENTRY_RETRY_DELAY_US);
            sleep_status = CY_SYSPM_SUCCESS;
        }
    }

#if (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    bool socmem_on = request_socmem_power(PPU_V1_MODE_ON);
    CY_PDCM_PD_SENSE(CY_PD_PDCM_SOCMEM) = socmem_dependency;
    __DSB();
    if (!socmem_on)
    {
        printf("ERROR: SoCMEM did not power ON after sleep\r\n");
        handle_app_error();
    }
#endif

    cy_en_syspm_status_t app_restore_status =
        Cy_SysPm_SetAppDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
    cy_en_syspm_status_t socmem_restore_status =
        Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);
    Cy_SysPm_DeepSleepUpdate();
    if (dcache_enabled != 0U)
    {
        SCB_EnableDCache();
    }
    if ((app_restore_status != CY_SYSPM_SUCCESS) ||
        (socmem_restore_status != CY_SYSPM_SUCCESS) ||
        (Cy_SysPm_GetDeepSleepMode() != CY_SYSPM_MODE_DEEPSLEEP))
    {
        handle_app_error();
    }
    tick_interrupt_state = Cy_SysLib_EnterCriticalSection();
    SCB->ICSR = pending_systick;
    SysTick->CTRL = systick_control;
    Cy_SysLib_ExitCriticalSection(tick_interrupt_state);
#if !_BAREMETAL
    mtb_hal_syspm_lock_deepsleep();
#endif
    (void)xTaskResumeAll();

    if (sleep_status != CY_SYSPM_SUCCESS)
    {
        printf("WARNING: Deep Sleep entry rejected (status=%u).\r\n",
               (unsigned int)sleep_status);
    }
#endif
    exit_deep_sleep_mode();
    enable_sw2_interrupt();
}


/*******************************************************************************
* Function Name: exit_deep_sleep_mode
********************************************************************************
* Summary:
*  Restores the display after a returning sleep call. Scenario 1 presents its
*  retained SoCMEM frame; scenario 2 rebuilds from retained PSRAM. For scenario 3
*  this is aborted-entry cleanup only; successful PD1-off recovery starts in
*  main() after reset and validates the retained record before creating tasks.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void exit_deep_sleep_mode(void)
{
    cy_en_scb_i2c_status_t panel_i2c_status = CY_SCB_I2C_SUCCESS;
    cy_en_mipidsi_status_t mipi_status = CY_MIPIDSI_SUCCESS;

    printf("\r\n--- Waking Up from Deep Sleep ---\r\n");

    /* Deep Sleep resumes in the previous active power mode; refresh clock state. */
    SystemCoreClockUpdate();

#if (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    /* Scenario 2: SoCMEM was powered down.
     * Restore the high-performance profile, generated peripheral/clock/MPU
     * config, and the SMIF/PSRAM interface before any retained PSRAM data is
     * accessed. */
    {
        cy_en_syspm_status_t pwr_status = Cy_SysPm_SystemEnterHp();
        if (CY_SYSPM_SUCCESS != pwr_status)
        {
            printf("WARNING: Failed to switch system back to HP mode (status=%d)\r\n", pwr_status);
        }

        SystemCoreClockUpdate();
        init_cycfg_peripheral_clocks();
        init_cycfg_peripherals();
        init_cycfg_protection();

        SCB_DisableDCache();
        SCB_DisableICache();
        __DSB();
        __ISB();

        /* Scenario 2: PD1 stayed powered, so the SMIF context is usually
         * retained; a fast XIP re-enable works, with a full rebuild fallback. */
        cy_rslt_t smif_restore_status = app_smif_psram_restore_after_wakeup();
        if (CY_RSLT_SUCCESS != smif_restore_status)
        {
            printf("ERROR: Failed to restore SMIF/PSRAM/XIP after wakeup (status=0x%08lx)\r\n",
                   (unsigned long)smif_restore_status);
            handle_app_error();
        }

        SCB_EnableICache();
        SCB_EnableDCache();
        __DSB();
        __ISB();
    }
#endif

    /* Bring the DPHY data lane out of ULPM before rebuilding the display path. */
    if (mipi_data_ulpm_entered)
    {
    #if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
        base->GFXSS_MIPIDSI.MXMIPIDSI.CLK_CTL |= GFXSS_MIPIDSI_MXMIPIDSI_CLK_CTL_CLK_CFG_Msk;
        Cy_MIPIDSI_Enable(GFXSS_GFXSS_MIPIDSI);
        __DSB();
    #endif
        mipi_status = Cy_MIPIDSI_ExitULPM_Data(GFXSS_GFXSS_MIPIDSI);
        if (CY_MIPIDSI_SUCCESS != mipi_status)
        {
            printf("WARNING: Failed to exit DPHY Data-lane ULPM (status=%u)\r\n", (unsigned int)mipi_status);
        }
        mipi_data_ulpm_entered = false;
    }

    /* Rebuild the graphics subsystem. Retained memory is not re-zeroed. */
    reinitialize_graphics_pipeline();

    /* Release panel reset and re-initialise the panel + backlight. */
    Cy_GPIO_Set(CYBSP_DISP_RST_PORT, CYBSP_DISP_RST_PIN);
    Cy_SysLib_Delay(WAKEUP_SETTLE_DELAY_MS);

    panel_i2c_status = mtb_disp_waveshare_4p3_init(DISPLAY_I2C_CONTROLLER_HW,
                                                   &disp_i2c_controller_context);
    if (CY_SCB_I2C_SUCCESS != panel_i2c_status)
    {
        printf("ERROR: Failed to re-init panel after wakeup (status=%d)\r\n", panel_i2c_status);
        handle_app_error();
    }

    panel_i2c_status = mtb_disp_waveshare_4p3_set_brightness(DISPLAY_I2C_CONTROLLER_HW,
                                                             &disp_i2c_controller_context,
                                                             PANEL_BRIGHTNESS_ON_PERCENT);
    if (CY_SCB_I2C_SUCCESS != panel_i2c_status)
    {
        printf("WARNING: Failed to restore panel brightness (status=%d)\r\n", panel_i2c_status);
    }

    deep_sleep_enabled = false;
    sw4_low_power_event = false;

#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    printf("Display restored from frame buffer retained in SoCMEM.\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    printf("Display initialized; rendering the retained PSRAM image next.\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    printf("Display initialized after aborted entry; no PD1-off recovery occurred.\r\n");
#else
    printf("Display restored from retained SoCMEM/PSRAM data.\r\n");
#endif
}


/*******************************************************************************
* Function Name: user_button_interrupt_handler
********************************************************************************
* Summary:
*  User button interrupt handler. Sets flag when button is pressed.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void user_button_interrupt_handler(void)
{
    static uint32_t last_button_ticks[2];
    static bool button_seen[2];
    const mtb_hal_lptimer_t *timer = cyabs_rtos_get_lptimer();
    uint32_t now = mtb_hal_lptimer_read(timer);

    if (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN))
    {
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
        if (!deep_sleep_enabled &&
            (!button_seen[0] || ((uint32_t)(now - last_button_ticks[0]) >= debounce_ticks)))
        {
            last_button_ticks[0] = now;
            button_seen[0] = true;
            button_pressed = true;
        }
    }

    /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port in
    * the PSOC™ Edge E84 evaluation kit and hence they share the same NVIC IRQ
    * line. Since both the buttons are configured for falling edge interrupt in
    * the BSP, pressing any button will trigger the execution of this ISR. Therefore,
    * we must clear the interrupt flag of the user button (CYBSP_USER_BTN2) to avoid
    * issues in case if user presses BTN2 by mistake.
    */
    #if defined(CYBSP_USER_BTN2_ENABLED)
        /* SW4 controls Deep Sleep enter/wake. The falling-edge interrupt status
         * latches a valid press, used both to enter Deep Sleep and to wake from it. */
        if (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN))
        {
            if (!button_seen[1] || ((uint32_t)(now - last_button_ticks[1]) >= debounce_ticks))
            {
                last_button_ticks[1] = now;
                button_seen[1] = true;
                sw4_low_power_event = true;
            }
        }
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
    #endif

}

/*******************************************************************************
* Function Name: print_retention_mode_banner
********************************************************************************
* Summary:
*  Prints a user-friendly banner on the UART describing the currently active
*  display-data-retention scenario (selected at build time via
*  RETENTION_SCENARIO), including where the image is retained, which low-power
*  mode is used, and what happens on wake.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void print_retention_mode_banner(void)
{
    printf("\r\n==================================================\r\n");
    printf(" Display Data Retention Demo (JPEG Decoder + Deep Sleep)\r\n");
    printf("==================================================\r\n");
#if (RETENTION_SCENARIO == SOCMEM_FRAMEBUFFER)
    printf(" Active mode : SCENARIO 1\r\n");
    printf(" Retention   : Frame buffer in SoCMEM (stays powered)\r\n");
    printf(" Low-power   : Deep Sleep (SoCMEM + PSRAM retained)\r\n");
    printf(" On wake     : Re-init display path and show retained frame\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_SOCMEM_OFF)
    printf(" Active mode : SCENARIO 2\r\n");
    printf(" Retention   : Decoded image in PSRAM (SoCMEM powered OFF)\r\n");
    printf(" Low-power   : Deep Sleep (PD1 on, SoCMEM off)\r\n");
    printf(" On wake     : Restore SMIF/PSRAM XIP, re-blit PSRAM -> SoCMEM\r\n");
#elif (RETENTION_SCENARIO == PSRAM_IMAGE_PD1_OFF)
    printf(" Active mode : SCENARIO 3\r\n");
    printf(" Retention   : Image and framebuffer snapshot in PSRAM\r\n");
    printf(" Low-power   : PD1/SoCMEM off; CM33 in Deep Sleep\r\n");
    printf(" On wake     : CM33 restarts CM55; validate PSRAM, restore frame\r\n");
#else
    printf(" Active mode : UNKNOWN (RETENTION_SCENARIO=%d)\r\n", (int)RETENTION_SCENARIO);
#endif
    printf(" Controls    : SW4 = Low-power / Resume, SW2 = Next image\r\n");
    printf("==================================================\r\n\r\n");
}

/*******************************************************************************
* Function Name: cm55_ns_gfx_task
********************************************************************************
* Summary:
*  FreeRTOS task that initializes the graphics pipeline and runs the display loop.
*
* Parameters:
*  arg - Unused task argument.
*
* Return:
*  void
*
*******************************************************************************/
void cm55_ns_gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

#if !_BAREMETAL
    mtb_hal_syspm_lock_deepsleep();
#endif

    /* Show the active retention scenario on the UART for clarity. */
    print_retention_mode_banner();

    cy_en_gfx_status_t status = CY_GFX_SUCCESS;
    vg_lite_error_t error = VG_LITE_SUCCESS;
    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    cy_rslt_t panel_status = CY_RSLT_SUCCESS;
    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;

    /* MIPI-DSI Display specific configs */
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;

    /* Zero-initialize the contiguous frame buffer memory */
    memset(contiguous_mem, 0, sizeof(contiguous_mem));

    /* Assign contiguous memory as the graphics layer frame buffer */
    GFXSS_graphics_layer.buffer_address = (gctADDRESS*)contiguous_mem;

    /* Initializes the graphics system according to the configuration */
    status = Cy_GFXSS_Init(base, &GFXSS_config, &gfx_context);

    if (CY_GFX_SUCCESS != status)
    {
        printf("Graphics subsystem initialization failed: Cy_GFXSS_Init() returned error %d\n", status);
        handle_app_error();
    }

    /* Initialize GFX DC interrupt */
    sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);
    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("Error in registering DC interrupt: %d\r\n", sysint_status);
        handle_app_error();
    }
    NVIC_EnableIRQ(GFXSS_DC_IRQ);

#if !_BAREMETAL
    initialize_gpu_interrupt();
#endif

    vg_module_parameters_t vg_params;
    vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
    vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
    vg_params.contiguous_mem_base[VG_PARAMS_POS] = (volatile void *) vglite_heap_base;
    vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;
    vg_lite_init_mem(&vg_params);

    error = vg_lite_init(DISPLAY_W, DISPLAY_H);
    if (VG_LITE_SUCCESS != error)
    {
        printf("vg_lite engine init failed: vg_lite_init() returned error %d\n", error);
        handle_app_error();
    }

    /* Allocate frame buffer 0 */
    buffer0.width  = DISPLAY_W;
    buffer0.height = DISPLAY_H;
    buffer0.format = VG_LITE_BGR565;
    error = vg_lite_allocate(&buffer0);
    if (VG_LITE_SUCCESS != error)
    {
        printf("Buffer0 allocation failed: vg_lite_allocate() returned error %d\n", error);
        handle_app_error();
    }

    /* Allocate frame buffer 1 */
    buffer1.width  = DISPLAY_W;
    buffer1.height = DISPLAY_H;
    buffer1.format = VG_LITE_BGR565;
    error = vg_lite_allocate(&buffer1);
    if (VG_LITE_SUCCESS != error)
    {
        printf("Buffer1 allocation failed: vg_lite_allocate() returned error %d\n", error);
        handle_app_error();
    }

    /* GPU-clear both buffers to black and block until done */
    if (vg_lite_clear(&buffer0, NULL, BLACK_COLOR) != VG_LITE_SUCCESS)
    {
        printf("ERROR: Failed to pre-clear frame buffer 0\r\n");
        handle_app_error();
    }
    if (vg_lite_clear(&buffer1, NULL, BLACK_COLOR) != VG_LITE_SUCCESS)
    {
        printf("ERROR: Failed to pre-clear frame buffer 1\r\n");
        handle_app_error();
    }
    vg_lite_finish();

    /* Arm the DC swap mechanism and point DC at black buffer0 */
    active_buffer  = 0;
    pending_buffer = active_buffer;
    Cy_GFXSS_Set_FrameBuffer(base, (uint32_t *)(buffers[active_buffer]->address), &gfx_context);

    i2c_result = Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW,
                &DISPLAY_I2C_CONTROLLER_config, &disp_i2c_controller_context);
    if (CY_SCB_I2C_SUCCESS != i2c_result)
    {
        printf("I2C controller initialization failed !!\n");
        handle_app_error();
    }

    sysint_status = Cy_SysInt_Init(&disp_i2c_controller_irq_cfg, &disp_i2c_controller_interrupt);
    if (CY_SYSINT_SUCCESS != sysint_status)
    {
        printf("I2C controller interrupt initialization failed\r\n");
        handle_app_error();
    }
    NVIC_EnableIRQ(disp_i2c_controller_irq_cfg.intrSrc);
    Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);

    /* Allow I2C to stabilise before panel init */
    Cy_SysLib_Delay(200);

    panel_status = mtb_disp_waveshare_4p3_init(DISPLAY_I2C_CONTROLLER_HW,
                                              &disp_i2c_controller_context);
    if (CY_RSLT_SUCCESS != panel_status)
    {
        printf("Waveshare 4.3-inch R-Pi display init failed with status = %u\r\n", (unsigned int) panel_status);
        handle_app_error();
    }


    /* Clear any stale button interrupt flags set during BSP init */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
#if defined(CYBSP_USER_BTN2_ENABLED)
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
#endif

    debounce_ticks = (uint32_t)(((uint64_t)Cy_SysClk_ClkLfGetFrequency() *
                                 DEBOUNCE_DELAY_MS) / 1000U);

    cy_en_sysint_status_t btn_interrupt_init_status = Cy_SysInt_Init(&user_button_irq_cfg,
                                                                     &user_button_interrupt_handler);
    if (CY_SYSINT_SUCCESS != btn_interrupt_init_status)
    {
        printf("Failed to initialize GPIO Interrupt\n");
        handle_app_error();
    }
    NVIC_EnableIRQ(user_button_irq_cfg.intrSrc);

    /* Initialise image database */
    init_image_database();
    if (!retained_image_restored)
    {
        memset(img_background_png_data, 0, sizeof(img_background_png_data));
        memset(display_temp_buffer, 0, sizeof(display_temp_buffer));
        current_image = 0U;
        current_image_width = image_database[0].width;
        current_image_height = image_database[0].height;
    }

    printf("Loaded %d images\r\n", NUM_IMAGES);
#ifdef SCALING_IMAGE
    printf("Display mode: SCALING (all images scaled to FILL display)\r\n");
#else
    printf("Display mode: CROPPING (oversized images cropped, small images centered)\r\n");
#endif
    printf("Display: %dx%d pixels\r\n", DISPLAY_W, DISPLAY_H);

    render_image();
}

/* [] END OF FILE */