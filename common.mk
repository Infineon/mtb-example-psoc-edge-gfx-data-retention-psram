
################################################################################
# \file common.mk
# \version 1.0
#
# \brief
# Settings shared across all projects.
#
################################################################################
# \copyright
# (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG.  SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

MTB_TYPE=PROJECT

# Target board/hardware (BSP).
# To change the target, it is recommended to use the Library manager
# ('make library-manager' from command line), which will also update 
# Eclipse IDE launch configurations.
TARGET=KIT_PSE84_EVAL_EPC2

# Name of toolchain to use. Options include:
#
# GCC_ARM 	-- GCC is available as part of ModusToolbox Setup program
# ARM     	-- ARM Compiler (must be installed separately)
# IAR     	-- IAR Compiler (must be installed separately)
# LLVM_ARM	-- LLVM Embedded Toolchain (must be installed separately)
#
# See also: CY_COMPILER_PATH below
TOOLCHAIN=GCC_ARM

# Default build configuration. Options include:
#
# Debug -- build with minimal optimizations, focus on debugging.
# Release -- build with full optimizations
# Custom -- build with custom configuration, set the optimization flag in CFLAGS
# 
# If CONFIG is manually edited, ensure to update or regenerate 
# launch configurations for your IDE.
CONFIG=Debug

# Option to scale the image to fit the display.
#
# ENABLED   - Smaller images are scaled up to fill the display; larger images
#             are scaled down to fit, which may crop the edges.
# DISABLED  - Images are shown at native size; larger images are cropped to the
#             display size.
#
IMAGE_SCALING=ENABLED

ifeq ($(IMAGE_SCALING),ENABLED)
DEFINES+=SCALING_IMAGE
endif

# Display-data-retention scenario exercised by the Deep Sleep demo.
#
# 1 - Data retained in SoCMEM: the frame buffer stays powered across Deep Sleep;
#     on wake the display path is re-initialised and the retained frame is shown.
# 2 - Data retained in PSRAM: SoCMEM is powered down in Deep Sleep; on wake the
#     SMIF/XIP mapping to PSRAM is restored and the image is re-blitted into a
#     freshly allocated SoCMEM frame buffer.
# 3 - Data retained in PSRAM with PD1-off: the whole PD1 power domain (CM55,
#     GFXSS and SMIF) is switched off by retained CM33. SW4 causes CM33 to
#     restore flash XIP and restart only CM55. CM55 restores the saved display
#     framebuffer from PSRAM into newly allocated SoCMEM. MAIN is not reset.
#     CM33 waits for SW4 in ordinary Deep Sleep, retaining its execution state.
#
RETENTION_SCENARIO?=PSRAM_IMAGE_PD1_OFF

# All three scenarios use the standard per-core BSP linker filenames.
# The shared CM33 SRAM layout enables execution while PD1 is off in scenario 3.
# Scenarios 1 and 2 also incur this code-SRAM usage and the startup copy cost.
PD1_INTERNAL_EXECUTION?=1
PD1_IPC_CHANNEL?=15
NSC_VENEER=../proj_cm33_s/build/$(TARGET)/$(CONFIG)/$(TOOLCHAIN)/$(RETENTION_SCENARIO)/nsc_veneer.o
DEFINES+=APP_PD1_INTERNAL_EXECUTION=$(PD1_INTERNAL_EXECUTION)
DEFINES+=APP_PD1_IPC_CHANNEL=$(PD1_IPC_CHANNEL)

DEFINES+=SOCMEM_FRAMEBUFFER=1
DEFINES+=PSRAM_IMAGE_SOCMEM_OFF=2
DEFINES+=PSRAM_IMAGE_PD1_OFF=3
DEFINES+=RETENTION_SCENARIO=$(RETENTION_SCENARIO)

# Option to automatically rotate the image based on the IMU (BMI270 motion
# sensor) orientation.
#
# ENABLED   - image is rotated 180 degrees automatically when the board is
#             flipped, using the on-board IMU. The motion sensor task is started.
# DISABLED  - image orientation is fixed; the IMU is not used for rotation and
#             the motion sensor task is not started.
#
AUTO_ROTATION=DISABLED

ifeq ($(AUTO_ROTATION),ENABLED)
DEFINES+=AUTO_ROTATION
endif

# Option to enable extra logs.
#
# ENABLED   - enable extra logs
# DISABLED  - disable extra logs
#
DETAILED_LOG=DISABLED

ifeq ($(DETAILED_LOG),ENABLED)
DEFINES+=DISPLAY_LOG
endif

# JPEG decode quality/performance mode.
#
# TIME_PERFORMANCE - Use fast integer DCT (JDCT_IFAST) and enable ARM Helium
#                    (MVE) optimised paths. This provides best performance.
# HIGH_QUALITY     - Use the library default DCT method (JDCT_ISLOW) for
#                    highest decode accuracy. No MVE overrides applied. This is
#                    the default mode.
#
JPEG_DECODE_MODE?=HIGH_QUALITY

ifeq ($(JPEG_DECODE_MODE),TIME_PERFORMANCE)
DEFINES+=JDCT_DEFAULT=JDCT_IFAST
DEFINES+=USE_HELIUM_MVE
else ifeq ($(JPEG_DECODE_MODE),HIGH_QUALITY)
DEFINES+=JDCT_DEFAULT=JDCT_ISLOW
endif

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS+=GFXSS

# Config file for postbuild sign and merge operations.
# NOTE: Check the JSON file for the command parameters
COMBINE_SIGN_JSON?=configs/boot_with_extended_boot.json

ifneq ($(filter proj_cm33_ns,$(notdir $(CURDIR))),)
ifneq ($(filter $(TOOLCHAIN),ARM GCC_ARM LLVM_ARM),)
CFLAGS+=-fno-builtin
endif
ifeq ($(TOOLCHAIN),GCC_ARM)
CFLAGS+=-fno-tree-loop-distribute-patterns
endif
endif

include ../common_app.mk
