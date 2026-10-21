[Click here](../README.md) to view the README.

## Design and implementation

This project uses a [4.3-inch Capacitive Touch IPS Display for Raspberry Pi, DSI Interface, 800×480 pixel](https://www.waveshare.com/4.3inch-dsi-lcd.htm) from Waveshare. The LCD houses a Chipone ICN6211 display controller and uses the MIPI DSI interface.

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

 Project | Description
 --------|------------------------
 *proj_cm33_s* | Project for CM33 secure processing environment (SPE)
 *proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
 *proj_cm55* | CM55 project – JPEG decode, graphics rendering, and UI

<br>

At device reset, the secure boot process starts from ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After the secure configuration, non-secure CM33 initializes the platform resources and starts CM55. In scenario 3, a CM33 task waits for IPC notifications and coordinates PD1 shutdown and recovery from retained internal memory. CM33 is not reset during a successful PD1-off cycle. CM55 runs the display application.

The CM55 application decodes JPEG images from an in-firmware database and renders them on the LCD using the PSOC&trade; Edge graphics subsystem. The graphics subsystem houses an independent 2.5D graphics processing unit (GPU), a display controller (DC), and a MIPI DSI host controller with MIPI D-PHY physical layer interface. VGLite driver APIs are used for color conversion, blit, and display operations. After the GPU renders a frame, it is transferred to the MIPI DSI host controller through the display controller (DC) and then shown on the LCD.

The application also demonstrates display-data retention during Deep Sleep. The decoded RGB565 image is stored in externally powered PSRAM, while the display frame buffers are allocated in SoCMEM. Pressing `SW4` enters the selected low-power state and pressing it again wakes the device. On wake-up, the graphics pipeline is reinitialized and the retained image is rendered again when the selected scenario has powered down SoCMEM or the graphics subsystem.

The `RETENTION_SCENARIO` make variable selects the power and recovery behavior:

**Table 2. Display-data retention scenarios**

 Scenario | Data and power behavior | Wake-up recovery
 ---------|-------------------------|-----------------
 `SOCMEM_FRAMEBUFFER` | The decoded image remains in PSRAM and the frame buffers remain in SoCMEM. The application uses the regular Deep Sleep path. | Repoint the display controller at the retained active framebuffer without clearing, reallocating, or re-blitting it.
 `PSRAM_IMAGE_SOCMEM_OFF` | The decoded image remains in PSRAM while SoCMEM, including the frame buffers, is powered off. The SMIF context remains powered. | Restore the power and peripheral configuration, restore the PSRAM XIP mapping (with full reinitialization as a fallback), allocate new frame buffers, and render the image.
 `PSRAM_IMAGE_PD1_OFF` | Decoded pixels, a framebuffer snapshot, and a checksummed record remain in uninitialized external PSRAM. CM33 powers off PD1 and SoCMEM; CM55, its DTCM, GFXSS, and SMIF lose state. MAIN/CM33 remain powered. | CM33 restores flash access and secure configuration, sets `APP_PD1_RESTORE`, and restarts CM55. Before BSP initialization, `main()` reads the CM33 IPC restart reason. After PSRAM/XIP initialization it validates retention, then creates fresh tasks and framebuffers. Valid retained data restores the framebuffer without JPEG decoding. This is the default scenario.

The retained record contains the image index, dimensions, byte counts, format version, framebuffer stride, and checksums of the source JPEG, decoded pixels, framebuffer snapshot, and metadata. The valid marker is written last and the cache is cleaned before sleep. Startup validation consumes the marker so stale records cannot be reused. A cold boot decodes normally. Invalid data with `APP_PD1_RESTORE` sets `APP_PD1_FAILED` and stops the application rather than silently reporting a successful retention recovery. `APP_PD1_RESTART_ABORT` reports that PD1-off was not observed and can reuse a valid snapshot, but does not count as successful PD1-off. `SW2` remains available after recovery.

Before SoCMEM power-off, both VGLite framebuffers are freed and the VGLite context is closed while its memory is still available. Power-off is armed only after panel shutdown and button release, with task scheduling suspended. Scenarios 1 and 2 restore ordinary Deep Sleep policy before scheduling resumes. Scenario 3 keeps that policy and parks CM55 for CM33-controlled shutdown; a returning park operation invalidates the record and recovers the display without claiming PD1-off success.

Scenarios 1 and 2 use ordinary Deep Sleep with the UART power-management callback. In scenario 3, CM33 powers off PD1 and enters ordinary Deep Sleep from retained secure code, waking on SW4 without resetting MAIN; this path does not use the PDL Deep Sleep OFF entry API.

### PD1 coordination and memory placement

CM55 requests shutdown and parks after CM33 accepts; CM33 powers off PD1 and waits for SW4. On wake, CM33 restores the power domains, protection, memory configuration, and flash access before restarting CM55, without resetting MAIN or resuming the old CM55 tasks.

The application reserves retained IPC0 channel 15 by default (`PD1_IPC_CHANNEL`) and IPC0 interrupt output 3 for notifications; a custom BSP must keep both resources exclusive and route the interrupt to nonsecure CM33. While waiting for notifications, CM33 uses Sleep rather than tickless Deep Sleep for reliable request delivery, separately from the explicit PD1-off Deep Sleep cycle.

The handshake is declared in [shared/pd1_protocol.h](../shared/pd1_protocol.h), [proj_cm33_ns/pd1_control.c](../proj_cm33_ns/pd1_control.c) implements the coordinator, and [proj_cm33_s/pd1_restore.c](../proj_cm33_s/pd1_restore.c) supplies the secure service using [proj_cm33_s/pd1_ram.h](../proj_cm33_s/pd1_ram.h) for retained placement. The supplied secure project is required for this service.

A custom secure project must retain the service, vector table, exception handlers, and all code and data needed while PD1 is off, with no enabled secure peripheral interrupts or secure SysTick and with ordinary Deep Sleep configured. For IAR, applications needing functional core exception handlers must provide retained implementations instead of the example's fail-stop handler.

All scenarios share the CM33 SRAM layout, including nonsecure runtime code and secure veneers, so the coordinator can run while external flash is unavailable. This consumes code SRAM and startup-copy time even in scenarios 1 and 2; data, stacks, and heaps must also fit within their configured internal-memory regions.

Both kit templates place the CM55 C heap in DTCM, outside SoCMEM, for all supported toolchains. Scenario 2 therefore preserves allocator metadata while rebuilding the VGLite heap in SoCMEM; scenario 3 loses DTCM and restarts the C runtime.

### Retention integrity and latency

Scenario 3 computes byte-wise FNV-1a checksums over the complete decoded pixels (up to 3,000,000 bytes), the framebuffer snapshot, the source JPEG, and metadata at sleep entry and again during restart validation. This gives full-data accidental-corruption detection, not cryptographic authenticity. It adds linear work and memory traffic before both power-off and display recovery; sleep entry also copies the framebuffer and cleans the cache. It is not a constant-time or zero-copy wake path.

Recovery latency depends on image size, compiler settings, power restoration, CM55 startup, integrity checks, graphics initialization, and UART output. The complete decoded image and framebuffer are validated before the display is restored.

The following are the important files in this code example:

- **proj_cm55/source/jpeg_image_decoder.c/h:** Implements JPEG decoding using libjpeg. The `decode_image()` function (which calls `mtb_decode_jpeg_from_memory()` from the LibJPEG middleware) decompresses a raw JPEG byte array from the image database into an RGB565 pixel buffer ready for display

- **proj_cm55/source/gfx_task.c/h:** Contains the `cm55_ns_gfx_task` FreeRTOS task. It initializes the graphics subsystem and LCD display driver, decodes and displays the initial JPEG, and handles button events. On each USER button (`SW2`) press, it decodes the next JPEG from the image database and blits the resulting RGB565 frame to the display. Images larger than the display are center-cropped; images smaller than the display are centered with a black background. When the `IMAGE_SCALING` build option is enabled, smaller images are scaled up to fill the display, while larger images are scaled down to fit, which may crop the edges. When the `AUTO_ROTATION` build option is enabled, the rendered image is rotated 180 degrees automatically when the board is flipped, based on the orientation reported by the on-board BMI270 motion sensor (IMU). `SW4` starts and ends the Deep Sleep retention demonstration; the task performs the scenario-specific display and memory recovery after wake-up.

- **proj_cm55/source/motion_task.c/h:** Contains the `task_motion` FreeRTOS task. It initializes the BMI270 motion sensor over I2C and periodically reads the accelerometer to determine the board orientation, which is published through the `current_orientation` variable consumed by `gfx_task.c`. This task is only created when the `AUTO_ROTATION` build option is enabled

- **proj_cm55/source/resolution_overlay.c/h:** Renders an on-screen text overlay showing the decoded image name and resolution using VGLite font APIs

- **proj_cm55/source/smif_psram_control.h:** Declares the SMIF/PSRAM restore helpers implemented in **proj_cm55/source/main.c**. Scenario 2 uses the fast XIP restore with a full rebuild fallback. Successful PD1-off recovery uses the normal SMIF initialization in `main()` after reset, not the old task's runtime objects

- **proj_cm55/source/data_base/image_database_file.h/c:** Auto-generated by the converter script. Declares and initializes the `image_database[]` array with metadata (name, byte pointer, size, width, and height) for every image. The database is ordered from largest to smallest by pixel area

- **proj_cm55/source/jpeg_image_array/\*.h:** Auto-generated per-image headers, each containing the raw JPEG byte array and dimension macros (`JPEG_<NAME>_WIDTH`, `JPEG_<NAME>_HEIGHT`)

- **proj_cm55/source/main.c:** Entry point for the CM55 application. Initializes the serial memory interface, CLIB support, retarget-io, and the FreeRTOS runtime. It creates the `cm55_ns_gfx_task` graphics task and, when `AUTO_ROTATION` is enabled, the `task_motion` motion sensor task before starting the scheduler

## Compile-time configuration options

The application behavior can be customized at build time through the make variables defined in `common.mk`. After changing any option, rebuild and reprogram the application.

**Table 3. Build configuration options**

 Make variable | Values | Default | Description
 -------------- | ------ | ------- | -----------
 `IMAGE_SCALING` | `ENABLED` / `DISABLED` | `ENABLED` | When `ENABLED`, smaller images are scaled up to fill the display, while larger images are scaled down to fit (which may crop the edges). When `DISABLED`, images are shown at native size; larger images are cropped to the display size and smaller images are centered on a black background. Defines the `SCALING_IMAGE` macro
 `AUTO_ROTATION` | `ENABLED` / `DISABLED` | `DISABLED` | When `ENABLED`, the displayed image is rotated 180 degrees automatically when the board is flipped, using the on-board BMI270 motion sensor (IMU); the `task_motion` motion sensor task is started. When `DISABLED`, the image orientation is fixed, the IMU is not used, and the motion sensor task is not created. Defines the `AUTO_ROTATION` macro
 `DETAILED_LOG` | `ENABLED` / `DISABLED` | `DISABLED` | When `ENABLED`, additional diagnostic logs (scaling factors, blit details, and positioning information) are printed to the UART terminal. Defines the `DISPLAY_LOG` macro
 `JPEG_DECODE_MODE` | `HIGH_QUALITY` / `TIME_PERFORMANCE` | `HIGH_QUALITY` | Selects the JPEG decode quality/performance trade-off. `HIGH_QUALITY` uses the library default accurate DCT (`JDCT_ISLOW`) for best decode accuracy. `TIME_PERFORMANCE` uses the fast integer DCT (`JDCT_IFAST`) and enables the ARM Helium (MVE) optimized IDCT paths for fastest decode time. Defines `JDCT_DEFAULT=JDCT_ISLOW` in `HIGH_QUALITY` mode, and `JDCT_DEFAULT=JDCT_IFAST` plus `USE_HELIUM_MVE` in `TIME_PERFORMANCE` mode
 `RETENTION_SCENARIO` | `SOCMEM_FRAMEBUFFER` / `PSRAM_IMAGE_SOCMEM_OFF` / `PSRAM_IMAGE_PD1_OFF` | `PSRAM_IMAGE_PD1_OFF` | Selects which memory and power domains remain available during Deep Sleep. The selected scenario controls the wake-up restore path for the display frame buffers and PSRAM/XIP interface.

<br>

## Adding images to the database

The image database is generated automatically from JPEG files placed in the `proj_cm55/jpeg_images/` folder. The Python script `proj_cm55/script/jpeg_to_array.py` converts each JPEG into a C byte-array header and regenerates the database source files. Follow these steps to add a new image:

1. Copy one or more JPEG files (`.jpg`, `.jpeg`, `.jpe`, or `.jfif`) into the `proj_cm55/jpeg_images/` folder

   > **Note:** The script derives the C identifier from the filename. Use lowercase filenames with no spaces; special characters are automatically replaced with underscores. For example, `my_photo.jpg` generates the symbol `jpeg_my_photo_data`. Output filenames (case-insensitive), symbols, and include guards must be unique across incoming images and retained headers. Collisions are rejected before files are written; an exact-name header can be regenerated.

2. Open a command shell in the `proj_cm55/script/` directory and run the converter script:

   ```
   python jpeg_to_array.py
   ```

   For each JPEG found in `proj_cm55/jpeg_images/`, the script:

   - Generates a header file in `proj_cm55/source/jpeg_image_array/` (for example, `my_photo.h`) containing the raw byte array and dimension macros
   - Updates `proj_cm55/source/data_base/image_database_file.h` with the `NUM_IMAGES`, `MAX_IMAGE_WIDTH`, and `MAX_IMAGE_HEIGHT` macros
   - Updates `proj_cm55/source/data_base/image_database_file.c` with the `init_image_database()` function that registers all images. Images are ordered by decreasing resolution

   Typical script output:

   ```
   Found 1 image(s) in: ...\proj_cm55\jpeg_images

   OK my_photo.jpg -> my_photo.h
     - bytes: 45312 (44.25 KB)
     - symbols: jpeg_my_photo_data, jpeg_my_photo_data_len

     - dimensions: JPEG_MY_PHOTO_WIDTH=1024, JPEG_MY_PHOTO_HEIGHT=768

   Detected 8 header file(s) in: ...\proj_cm55\source\jpeg_image_array
   Updated database header: ...\proj_cm55\source\data_base\image_database_file.h
   Updated database source: ...\proj_cm55\source\data_base\image_database_file.c
   ```

   > **Note:** Running the script replaces any previously auto-generated database files. Do not manually edit `image_database_file.h` or `image_database_file.c`; always regenerate them using the script

3. Rebuild and reprogram the application. The new image will be included in the cycle and can be displayed by pressing the USER button (`SW2`)