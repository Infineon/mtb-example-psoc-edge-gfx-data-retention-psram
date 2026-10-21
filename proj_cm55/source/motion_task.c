/******************************************************************************
 * File Name:   motion_task.c
 *
 * Description: This file contains the task that initializes and configures the
 *              BMI270 Motion Sensor.
 *
 * Related Document: See README.md
 *
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
#include <stdlib.h>

#include "mtb_bmi270.h"
#include "cybsp.h"
#include "cy_result.h"
#include "motion_task.h"
#include "retarget_io_init.h"

/*******************************************************************************
 * Global Variables
 ********************************************************************************/

static mtb_hal_i2c_t CYBSP_I2C_CONTROLLER_hal_obj;
static cy_stc_scb_i2c_context_t CYBSP_I2C_CONTROLLER_context;

/* Instance of BMI270 sensor structure */
static mtb_bmi270_t bmi270;
static mtb_bmi270_data_t bmi270_data;

/* Current board orientation, updated by motion_sensor_update_orientation() */
volatile motion_orientation_t current_orientation = ORIENTATION_UNKNOWN;

/******************************************************************************
 * Macros
 ******************************************************************************/
#define DELAY_MS (1000U)

/*******************************************************************************
 * Function Name: motion_sensor_init
 ********************************************************************************
 * Summary:
 *  Function that configures the I2C controller interface and then initializes
 *  the motion sensor.
 *
 * Parameters:
 *  None
 *
 * Return:
 * result
 *
 *******************************************************************************/
static cy_rslt_t motion_sensor_init(void)
{
    cy_rslt_t result;

    /* Initialize the I2C controller interface for BMI270 motion sensor */
    result = Cy_SCB_I2C_Init(CYBSP_I2C_CONTROLLER_HW,
            &CYBSP_I2C_CONTROLLER_config,
            &CYBSP_I2C_CONTROLLER_context);

    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : I2C initialization failed !!\r\n");
        handle_app_error();
    }
    Cy_SCB_I2C_Enable(CYBSP_I2C_CONTROLLER_HW);

    /* Configure the I2C controller interface with the desired clock frequency */
    result = mtb_hal_i2c_setup(&CYBSP_I2C_CONTROLLER_hal_obj,
            &CYBSP_I2C_CONTROLLER_hal_config,
            &CYBSP_I2C_CONTROLLER_context,
            NULL);

    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : I2C setup failed !!\r\n");
        handle_app_error();
    }

    /* Initialize the BMI270 motion sensor */
    result = mtb_bmi270_init_i2c(&bmi270, 
                                &CYBSP_I2C_CONTROLLER_hal_obj, 
                                MTB_BMI270_ADDRESS_DEFAULT);
    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : IMU sensor init failed !!\r\n");
        handle_app_error();
    }

    result = mtb_bmi270_config_default(&bmi270);
    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : IMU sensor config failed !!\r\n");
        handle_app_error();
    }

    return result;
}

/*******************************************************************************
 * Function Name: motion_sensor_update_orientation
 ********************************************************************************
 * Summary:
 *  Function that updates the orientation status to one of the 6 types,
 *  'ORIENTATION_UP, ORIENTATION_DOWN, TOP_EDGE, BOTTOM_EDGE,
 *  LEFT_EDGE, and RIGHT_EDGE'. This functions detects the axis that is most perpendicular
 *  to the ground based on the absolute value of acceleration in that axis.
 *  The sign of the acceleration signifies whether the axis is facing the ground
 *  or the opposite.
 *
 * Return:
 *  CY_RSLT_SUCCESS upon successful orientation update, else a non-zero value
 *  that indicates the error.
 *
 *******************************************************************************/
static cy_rslt_t motion_sensor_update_orientation(void)
{
    /* Status variable */
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t abs_x;
    int32_t abs_y;
    int32_t abs_z;

    /* Read x, y, z components of acceleration */
    result = mtb_bmi270_read(&bmi270, &bmi270_data);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("read data failed\r\n");
        return result;
    }
    abs_x = abs(bmi270_data.sensor_data.acc.x);
    abs_y = abs(bmi270_data.sensor_data.acc.y);
    abs_z = abs(bmi270_data.sensor_data.acc.z);

    if ((abs_z > abs_x) && (abs_z > abs_y))
    {
        if (bmi270_data.sensor_data.acc.z < 0)
        {
            /* Kit faces down (towards the ground) */
            current_orientation = ORIENTATION_DOWN;
        }
        else
        {
            /* Kit faces up (towards the sky/ceiling) */
            current_orientation = ORIENTATION_UP;
        }
    }
    /* Y axis (parallel with shorter edge of board) is most aligned with
     * gravity.
     */
    else if ((abs_y > abs_x) && (abs_y > abs_z))
    {
        if (bmi270_data.sensor_data.acc.y > 0)
        {
            /* Kit has an inverted landscape orientation */
            current_orientation = ORIENTATION_BOTTOM_EDGE;
        }
        else
        {
            /* Kit has landscape orientation */
            current_orientation = ORIENTATION_TOP_EDGE;
        }
    }
    /* X axis (parallel with longer edge of board) is most aligned with
     * gravity.
     */
    else
    {
        if (bmi270_data.sensor_data.acc.x < 0)
        {
            /* Kit has an inverted portrait orientation */
            current_orientation = ORIENTATION_RIGHT_EDGE;
        }
        else
        {
            /* Kit has portrait orientation */
            current_orientation = ORIENTATION_LEFT_EDGE;
        }
    }
    return result;
}

/*******************************************************************************
 * Function Name: task_motion
 ********************************************************************************
 * Summary:
 *  Task that configures the Motion Sensor and processes the sensor data to
 *  identify the sensor orientation.
 *
 * Parameters:
 *  void *pvParameters : Task parameter defined during task creation (unused)
 *
 * Return:
 *  None
 *
 *******************************************************************************/
void task_motion(void* pvParameters)
{
    /* Status variable to indicate the result of various operations */
    cy_rslt_t result;

    /* Remove warning for unused parameter */
    CY_UNUSED_PARAMETER(pvParameters);

    /* Initialize BMI270 motion sensor and suspend the task upon failure */
    result = motion_sensor_init();
    if(CY_RSLT_SUCCESS != result)
    {
        printf(" Error : Motion Sensor initialization failed !!\n Check hardware connection\r\n");
        handle_app_error();
    }

    for(;;)
    {
        /* Get current orientation */
        motion_sensor_update_orientation();
        vTaskDelay(pdMS_TO_TICKS(DELAY_MS));
    }
}


/* [] END OF FILE */
