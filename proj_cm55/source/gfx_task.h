/*******************************************************************************
 * File Name:   gfx_task.h
 *
 * Description: This is the header file for gfx_task.c. It contains macros and
 *              function prototypes used by gfx_task.c.
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
#ifndef GFX_TASK_H_
#define GFX_TASK_H_

#include <stdbool.h>
#include "mtb_disp_dsi_waveshare_4p3.h"


#if (RETENTION_SCENARIO != SOCMEM_FRAMEBUFFER) && \
    (RETENTION_SCENARIO != PSRAM_IMAGE_SOCMEM_OFF) && \
    (RETENTION_SCENARIO != PSRAM_IMAGE_PD1_OFF)
#error "RETENTION_SCENARIO must select a supported retention scenario"
#endif

#define DC_INT_PRIORITY                     (3U)

#define COLOR_DEPTH                         (16U)   /* in bits */
#define BITS_PER_PIXEL                      (8U)
#define APP_BUFFER_COUNT                    (2U)
/* 64 KB */
#define DEFAULT_GPU_CMD_BUFFER_SIZE         ((64U) * (1024U))
#define DISPLAY_H                           (MTB_DISP_WAVESHARE_4P3_VER_RES)
#define DISPLAY_W                           (MTB_DISP_WAVESHARE_4P3_HOR_RES)
#define GPU_TESSELLATION_BUFFER_SIZE        ((DISPLAY_H) * 128U)
#define FRAME_BUFFER_SIZE                   ((DISPLAY_W) * (DISPLAY_H) * \
                                            ((COLOR_DEPTH) / (BITS_PER_PIXEL)))

#define VGLITE_HEAP_SIZE                    (((FRAME_BUFFER_SIZE) * \
                                            (APP_BUFFER_COUNT)) + \
                                            ((DEFAULT_GPU_CMD_BUFFER_SIZE) * \
                                            (APP_BUFFER_COUNT)) + \
                                            ((GPU_TESSELLATION_BUFFER_SIZE) * \
                                            (APP_BUFFER_COUNT)))

#define GPU_MEM_BASE                        (0x0U)
#define GFX_TASK_NAME                       ("CM55 Gfx Task")
/* stack size in words */
#define GFX_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 4)
#define GFX_TASK_PRIORITY                   (configMAX_PRIORITIES - 1)
#define BLACK_COLOR                         (0x00000000U)
#define IMAGE_DISPLAY_WAIT_MS               (2000U)
/* Display controller command commit mask */
#define ENTER_KEY_ASCII                     (0x0D)
#define BUF_ZERO_POS                        (0)
#define BUF_ONE_POS                         (1U)
#define NUM_OF_IMAGES                       (2U)
#define INVALID_VAL                         (-1)
#define RESET_VAL                           (0)
#define SET_VAL                             (1U)
#define UART_READ_TIMEOUT_MS                (1U)

/*******************************************************************************
 * Variable(s)
 ******************************************************************************/
/* Structure declaration for image parameters */
struct image_params {

    vg_lite_float_t x_translate;
    vg_lite_float_t y_translate;
    vg_lite_float_t x_scale;
    vg_lite_float_t y_scale;
    uint32_t path_count;
    vg_lite_path_t *path_data;
    uint32_t *color_data;
};

/******************************************************************************
 * Function Prototypes
 ******************************************************************************/
void cm55_ns_gfx_task(void *arg);
bool gfx_restore_retained_image(bool pd1_restart);
void draw(vg_lite_buffer_t *target);
void render_image();

#endif /* GFX_TASK_H_ */

/* [] END OF FILE */
