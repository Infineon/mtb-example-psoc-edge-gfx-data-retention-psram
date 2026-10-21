/*******************************************************************************
* File Name: smif_psram_control.h
*
* Description:
*  SMIF/PSRAM/XIP restore helpers implemented in main.c.
*  Scenario 2 resumes with retained runtime objects. Successful PD1-off wake
*  restarts main() and initializes SMIF before validating retained image data.
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

#ifndef SMIF_PSRAM_CONTROL_H
#define SMIF_PSRAM_CONTROL_H

#include "cy_result.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Restore the SMIF/PSRAM XIP mapping after waking from Deep Sleep.
 * Tries a fast XIP re-enable first and falls back to a full SMIF/PSRAM
 * re-initialisation if the fast path fails. Used by PSRAM_IMAGE_SOCMEM_OFF. */
cy_rslt_t app_smif_psram_restore_after_wakeup(void);

#if defined(__cplusplus)
}
#endif

#endif /* SMIF_PSRAM_CONTROL_H */
