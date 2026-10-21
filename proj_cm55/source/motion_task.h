/******************************************************************************
* File Name:   motion_task.h
*
* Description: This file is the public interface of motion_task.c. This file 
*              also contains the BMI270 motion sensor configuration parameters.
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

#ifndef MOTION_TASK_H_
#define MOTION_TASK_H_

/*******************************************************************************
* Header Files
*******************************************************************************/

#include "FreeRTOS.h"

/* Task priority and stack size for the Motion sensor task */
#define TASK_MOTION_SENSOR_PRIORITY     (configMAX_PRIORITIES - 2)
#define TASK_MOTION_SENSOR_STACK_SIZE   (1024U)

/*******************************************************************************
* Orientation Types
********************************************************************************/
typedef enum {
    ORIENTATION_UNKNOWN = 0,
    ORIENTATION_UP,
    ORIENTATION_DOWN,
    ORIENTATION_TOP_EDGE,
    ORIENTATION_BOTTOM_EDGE,
    ORIENTATION_LEFT_EDGE,
    ORIENTATION_RIGHT_EDGE,
} motion_orientation_t;

/* Current board orientation, updated by the motion sensor task */
extern volatile motion_orientation_t current_orientation;

/*******************************************************************************
* Function Prototypes
********************************************************************************/
void task_motion(void* pvParameters);

#endif /* MOTION_TASK_H_ */
/* [] END OF FILE */
