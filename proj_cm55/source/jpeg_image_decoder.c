/*******************************************************************************
* File Name:   jpeg_image_decoder.c
*
* Description: This file contains functions to decode JPEG images from memory
*              into RGB565 format and retrieve image data from the image database 
*              for display on the screen.
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
#include <stdint.h>
#include <stdio.h>

#include "cy_result.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mtb_jpeg_image_codec.h"
#include "image_database_file.h"
#include "jpeg_image_decoder.h"


/*******************************************************************************
* Macros
*******************************************************************************/

#define MAX_IMAGE_BUFFER_SIZE   (MAX_IMAGE_WIDTH * MAX_IMAGE_HEIGHT * RGB565_BPP)

/*******************************************************************************
* Function Name: decode_image
********************************************************************************
* Summary:
*  This function decodes a JPEG image from memory and stores the result in the
*  provided output buffer. Dimensions are published only on success; the output
*  buffer must not be used after a failure because it may be partially written.
*
* Parameters:
*  image_num - The index of the image to decode
*  output_buf - Buffer of at least MAX_IMAGE_BUFFER_SIZE bytes
*  current_width - Pointer to store the width of the decoded image
*  current_height - Pointer to store the height of the decoded image
*
* Return:
*  true on successful decoding with matching dimensions, otherwise false
*
*******************************************************************************/
bool decode_image(uint8_t image_num, unsigned char *output_buf, int *current_width, int *current_height)
{
    if ((output_buf == NULL) || (current_width == NULL) || (current_height == NULL))
    {
        printf("ERROR: decode_image invalid argument(s)\r\n");
        return false;
    }

    if (image_num >= NUM_IMAGES)
    {
        printf("ERROR: Invalid image number %d (max: %d)\r\n", image_num, NUM_IMAGES - 1);
        return false;
    }

    const image_info_t* img = &image_database[image_num];
    const unsigned char* jBuf = img->jpeg_data;
    unsigned long jSize = img->jpeg_size;
    const char* image_name = img->name;

    if ((img->width <= 0) || (img->width > MAX_IMAGE_WIDTH) ||
        (img->height <= 0) || (img->height > MAX_IMAGE_HEIGHT))
    {
        printf("ERROR: Invalid image dimensions\r\n");
        return false;
    }

    printf("\r\n=== Decoding %s ===\r\n", image_name);
    printf("JPEG buffer size: %lu bytes\r\n", jSize);

    if (jBuf == NULL || jSize == 0)
    {
        printf("ERROR: JPEG data invalid\r\n");
        return false;
    }

    if (jSize >= 2 && jBuf[0] == 0xFF && jBuf[1] == 0xD8)
    {
        printf("JPEG header validation: PASS\r\n");
    }
    else
    {
        printf("ERROR: Invalid JPEG header\r\n");
        return false;
    }

    int expected_buffer_size = img->width * img->height * RGB565_BPP;
    printf("Expected output buffer size: %d bytes\r\n", expected_buffer_size);
    printf("Max buffer size: %d bytes\r\n", MAX_IMAGE_BUFFER_SIZE);

    if (expected_buffer_size > MAX_IMAGE_BUFFER_SIZE)
    {
        printf("ERROR: Image too large for buffer!\r\n");
        return false;
    }

    int decoded_width = 0;
    int decoded_height = 0;

    printf("Starting JPEG decoding...\r\n");

    TickType_t decode_start_time = xTaskGetTickCount();

    /* Decode the JPEG image from memory */
    cy_rslt_t decode_result = mtb_decode_jpeg_from_memory(jBuf, jSize, output_buf, MAX_IMAGE_BUFFER_SIZE, &decoded_width, &decoded_height);
    
    if (decode_result != CY_RSLT_SUCCESS)
    {
        printf("ERROR: JPEG decoding failed\r\n");
        return false;
    }

    TickType_t decode_end_time = xTaskGetTickCount();
    TickType_t decode_duration_ticks = decode_end_time - decode_start_time;
    uint32_t decode_duration_ms = (decode_duration_ticks * 1000) / configTICK_RATE_HZ;

    printf("JPEG Decoding Time: %u ms\r\n", (unsigned int)decode_duration_ms);
    printf("Decoded dimensions: %dx%d\r\n", decoded_width, decoded_height);

    if (decoded_width != img->width || decoded_height != img->height)
    {
        printf("ERROR: Decoded dimensions (%dx%d) differ from expected (%dx%d)\r\n",
               decoded_width, decoded_height, img->width, img->height);
        return false;
    }

    *current_width = decoded_width;
    *current_height = decoded_height;
    printf("=== Decoding Complete ===\r\n\r\n");
    return true;
}
/* [] END OF FILE */