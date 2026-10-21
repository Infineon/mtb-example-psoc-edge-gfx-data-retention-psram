/*******************************************************************************
* File Name:   resolution_overlay.c
*
* Description: This file contains the functions for drawing resolution overlay
*              on the display. It includes functions for drawing rectangles, 
*              characters, and strings using a 5x7 font.
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
#include <stdio.h>
#include <string.h>

#include "resolution_overlay.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define RES_TEXT_MARGIN_X          (8)
#define RES_TEXT_MARGIN_Y          (8)
#define RES_TEXT_MARGIN_X_ROTATED  (40)
#define RES_TEXT_CHAR_W            (5)
#define RES_TEXT_CHAR_H            (7)
#define RES_TEXT_CHAR_SPACING      (1)
#define RES_TEXT_SCALE             (3)
#define RES_TEXT_BOX_PADDING       (2)

/*******************************************************************************
 * Variable(s)
 ******************************************************************************/
/* 5x7 pixel font bitmap data for digit characters 0-9 */
static const uint8_t font_5x7_digits[10][RES_TEXT_CHAR_H] =
{
    {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU},
    {0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
    {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU},
    {0x1EU, 0x01U, 0x01U, 0x06U, 0x01U, 0x01U, 0x1EU},
    {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U},
    {0x1FU, 0x10U, 0x1EU, 0x01U, 0x01U, 0x11U, 0x0EU},
    {0x06U, 0x08U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU},
    {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
    {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU},
    {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x02U, 0x0CU}
};

/* 5x7 pixel font bitmap data for the 'x' separator character */
static const uint8_t font_5x7_x[RES_TEXT_CHAR_H] =
{
    0x11U,
    0x11U,
    0x0AU,
    0x04U,
    0x0AU,
    0x11U,
    0x11U
};

/*******************************************************************************
* Function Name: draw_rect_gpu
********************************************************************************
* Summary:
*  This function draws a rectangle on the GPU target buffer.
*
* Parameters:
*  target - The GPU target buffer
*  x - The x-coordinate of the rectangle's top-left corner
*  y - The y-coordinate of the rectangle's top-left corner
*  width - The width of the rectangle
*  height - The height of the rectangle
*  color - The color of the rectangle
*
* Return:
*  VG_LITE_SUCCESS if the rectangle was drawn successfully, error code otherwise
*
*******************************************************************************/
static vg_lite_error_t draw_rect_gpu(vg_lite_buffer_t *target, int x, int y, int width, int height, vg_lite_color_t color)
{
    if ((target == NULL) || (width <= 0) || (height <= 0))
    {
        return VG_LITE_SUCCESS;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + width;
    int y1 = y + height;

    if (x0 < 0)
    {
        x0 = 0;
    }
    if (y0 < 0)
    {
        y0 = 0;
    }
    if (x1 > (int)target->width)
    {
        x1 = (int)target->width;
    }
    if (y1 > (int)target->height)
    {
        y1 = (int)target->height;
    }

    if ((x1 <= x0) || (y1 <= y0))
    {
        return VG_LITE_SUCCESS;
    }

    vg_lite_rectangle_t rect;
    rect.x = x0;
    rect.y = y0;
    rect.width = x1 - x0;
    rect.height = y1 - y0;

    return vg_lite_clear(target, &rect, color);
}

/*******************************************************************************
* Function Name: draw_char_5x7_gpu
********************************************************************************
* Summary:
*  This function draws a character on the GPU target buffer using a 5x7 pixel font.
*
* Parameters:
*  target - The GPU target buffer
*  x - The x-coordinate of the character's top-left corner
*  y - The y-coordinate of the character's top-left corner
*  c - The character to draw
*  color - The color of the character
*
* Return:
*  VG_LITE_SUCCESS if the character was drawn successfully, error code otherwise
*
*******************************************************************************/
static vg_lite_error_t draw_char_5x7_gpu(vg_lite_buffer_t *target, int x, int y, char c, vg_lite_color_t color, bool rotate_180)
{
    const uint8_t *glyph = NULL;

    if ((c >= '0') && (c <= '9'))
    {
        glyph = font_5x7_digits[(int)(c - '0')];
    }
    else if ((c == 'x') || (c == 'X'))
    {
        glyph = font_5x7_x;
    }
    else
    {
        return VG_LITE_SUCCESS;
    }

    for (int row = 0; row < RES_TEXT_CHAR_H; row++)
    {
        uint8_t bits = glyph[row];
        for (int col = 0; col < RES_TEXT_CHAR_W; col++)
        {
            if (((bits >> (RES_TEXT_CHAR_W - 1 - col)) & 0x01U) != 0U)
            {
                int draw_col = rotate_180 ? (RES_TEXT_CHAR_W  - 1 - col) : col;
                int draw_row = rotate_180 ? (RES_TEXT_CHAR_H - 1 - row) : row;
                vg_lite_error_t error = draw_rect_gpu(target,
                                                     x + (draw_col * RES_TEXT_SCALE),
                                                     y + (draw_row * RES_TEXT_SCALE),
                                                     RES_TEXT_SCALE,
                                                     RES_TEXT_SCALE,
                                                     color);
                if (error)
                {
                    return error;
                }
            }
        }
    }

    return VG_LITE_SUCCESS;
}

/*******************************************************************************
* Function Name: draw_text_5x7_gpu
********************************************************************************
* Summary:
*  This function draws a string of text on the GPU target buffer using a 5x7 pixel font.
*  Each character is drawn using the draw_char_5x7_gpu function.
*
* Parameters:
*  target - The GPU target buffer
*  x - The x-coordinate of the text's top-left corner
*  y - The y-coordinate of the text's top-left corner
*  text - The string of text to draw
*  color - The color of the text
*
* Return:
*  VG_LITE_SUCCESS if the text was drawn successfully, error code otherwise
*
*******************************************************************************/
static vg_lite_error_t draw_text_5x7_gpu(vg_lite_buffer_t *target, int x, int y, const char *text, vg_lite_color_t color, bool rotate_180)
{
    if (text == NULL)
    {
        return VG_LITE_SUCCESS;
    }

    int char_advance = (RES_TEXT_CHAR_W + RES_TEXT_CHAR_SPACING) * RES_TEXT_SCALE;
    int text_len = (int)strlen(text);
    int cursor_x = x;

    for (int i = 0; i < text_len; i++)
    {
        /* When rotated 180 degrees, draw characters in reverse order so the
         * string reads correctly when the display is viewed upside-down. */
        char c = rotate_180 ? text[text_len - 1 - i] : text[i];
        vg_lite_error_t error = draw_char_5x7_gpu(target, cursor_x, y, c, color, rotate_180);
        if (error)
        {
            return error;
        }
        cursor_x += char_advance;
    }

    return VG_LITE_SUCCESS;
}

/*******************************************************************************
* Function Name: draw_resolution_overlay
********************************************************************************
* Summary:
*  This function draws a resolution overlay on the GPU target buffer.
*
* Parameters:
*  target - The GPU target buffer
*  width - The width of the resolution
*  height - The height of the resolution
*
* Return:
*  void
*
*******************************************************************************/
void draw_resolution_overlay(vg_lite_buffer_t *target, int width, int height, bool rotate_180)
{
    char resolution_text[20];
    (void)snprintf(resolution_text, sizeof(resolution_text), "%dx%d", width, height);

    int text_len = (int)strlen(resolution_text);
    if (text_len <= 0)
    {
        return;
    }

    int text_width_px = ((text_len * RES_TEXT_CHAR_W) + ((text_len - 1) * RES_TEXT_CHAR_SPACING)) * RES_TEXT_SCALE;
    int text_height_px = RES_TEXT_CHAR_H * RES_TEXT_SCALE;
    int box_padding_px = RES_TEXT_BOX_PADDING * RES_TEXT_SCALE;
    int text_x;
    int text_y;

    if (rotate_180)
    {
        /* Image is rotated 180 degrees: place overlay at the top-right corner
         * (the logical bottom-left of the rotated image).
         * Account for box padding so the full box stays within the display. */
        text_x = (int)target->width - RES_TEXT_MARGIN_X_ROTATED - text_width_px - box_padding_px;
        text_y = RES_TEXT_MARGIN_Y + box_padding_px;

        /* Clamp in case text is wider than the display */
        if (text_x < RES_TEXT_MARGIN_X_ROTATED + box_padding_px)
        {
            text_x = RES_TEXT_MARGIN_X_ROTATED + box_padding_px;
        }
    }
    else
    {
        text_x = RES_TEXT_MARGIN_X + box_padding_px;
        text_y = (int)target->height - RES_TEXT_MARGIN_Y - text_height_px - box_padding_px;
    }

    if (text_y < 0)
    {
        text_y = 0;
    }

    vg_lite_error_t error = draw_rect_gpu(target,
                                          text_x - box_padding_px,
                                          text_y - box_padding_px,
                                          text_width_px + (2 * box_padding_px),
                                          text_height_px + (2 * box_padding_px),
                                          0xFF000000U);
    if (error)
    {
        printf("Resolution background draw failed: %d\r\n", error);
        return;
    }

    error = draw_text_5x7_gpu(target, text_x, text_y, resolution_text, 0xFFFFFFFFU, rotate_180);
    if (error)
    {
        printf("Resolution text draw failed: %d\r\n", error);
    }
}
/* [] END OF FILE */