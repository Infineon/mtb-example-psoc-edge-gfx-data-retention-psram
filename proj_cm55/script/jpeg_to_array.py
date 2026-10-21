#!/usr/bin/env python3
# File Name: jpeg_to_array.py
# (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG. All rights reserved.
# This software, associated documentation and materials ("Software") is
# owned by Infineon Technologies AG or one of its affiliates ("Infineon")
# and is protected by and subject to worldwide patent protection, worldwide
# copyright laws, and international treaty provisions. Therefore, you may use
# this Software only as provided in the license agreement accompanying the
# software package from which you obtained this Software. If no license
# agreement applies, then any use, reproduction, modification, translation, or
# compilation of this Software is prohibited without the express written
# permission of Infineon.
# 
# Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
# IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
# THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
# SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
# Infineon reserves the right to make changes to the Software without notice.
# You are responsible for properly designing, programming, and testing the
# functionality and safety of your intended application of the Software, as
# well as complying with any legal requirements related to its use. Infineon
# does not guarantee that the Software will be free from intrusion, data theft
# or loss, or other breaches ("Security Breaches"), and Infineon shall have
# no liability arising out of any Security Breaches. Unless otherwise
# explicitly approved by Infineon, the Software may not be used in any
# application where a failure of the Product or any consequences of the use
# thereof can reasonably be expected to result in personal injury.

"""
Image to C Header Converter

Generates one .h file per JPEG from proj_cm55/jpeg_images into source/jpeg_image_array,
and auto-updates source/data_base/image_database_file.h + image_database_file.c.
"""

from pathlib import Path
import re


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
SOURCE_DIR = PROJECT_DIR / "source"
JPEG_INPUT_DIR = PROJECT_DIR / "jpeg_images"
JPEG_OUTPUT_DIR = SOURCE_DIR / "jpeg_image_array"
DATABASE_DIR = SOURCE_DIR / "data_base"
DATABASE_HEADER_PATH = DATABASE_DIR / "image_database_file.h"
DATABASE_SOURCE_PATH = DATABASE_DIR / "image_database_file.c"

def jpeg_to_array(jpeg_filename):
    """
    Convert JPEG file to byte array

    Args:
        jpeg_filename: Path to the JPEG file

    Returns:
        tuple: (byte_array, array_size)
    """
    if not Path(jpeg_filename).exists():
        raise FileNotFoundError(f"File not found: {jpeg_filename}")

    # Read the JPEG file as binary
    with open(jpeg_filename, 'rb') as f:
        jpeg_data = f.read()

    array_size = len(jpeg_data)

    return jpeg_data, array_size


def sanitize_identifier(name):
    sanitized = re.sub(r'[^0-9a-zA-Z_]', '_', name)
    if sanitized and sanitized[0].isdigit():
        sanitized = f"_{sanitized}"
    return sanitized.lower()


def make_jpeg_symbol_base(stem):
    stem_id = sanitize_identifier(stem).lstrip('_')
    if stem_id.startswith("jpeg_"):
        return stem_id
    return f"jpeg_{stem_id}"


def make_include_guard(stem):
    return f"{make_jpeg_symbol_base(stem).upper()}_H"


def find_images_in_folder(folder):
    supported_suffixes = {".jpg", ".jpeg", ".jpe", ".jfif"}
    images = []
    for entry in folder.iterdir():
        if entry.is_file() and entry.suffix.lower() in supported_suffixes:
            images.append(entry)
    return sorted(images, key=lambda p: p.name.lower())


def find_headers_in_folder(folder):
    headers = [entry for entry in folder.iterdir() if entry.is_file() and entry.suffix.lower() == ".h"]
    return sorted(headers, key=lambda p: p.name.lower())


def get_image_dimensions(image_path):
    suffix = image_path.suffix.lower()
    with open(image_path, 'rb') as f:
        data = f.read()

    if suffix in {'.jpg', '.jpeg', '.jpe', '.jfif'}:
        return _jpeg_dimensions(data, image_path)

    raise ValueError(f"Only JPEG images are supported: {image_path.name}")


def _jpeg_dimensions(data, image_path):
    if len(data) < 4 or data[0] != 0xFF or data[1] != 0xD8:
        raise ValueError(f"Invalid JPEG file: {image_path.name}")

    sof_markers = {
        0xC0, 0xC1, 0xC2, 0xC3,
        0xC5, 0xC6, 0xC7,
        0xC9, 0xCA, 0xCB,
        0xCD, 0xCE, 0xCF,
    }

    i = 2
    data_len = len(data)
    while i + 1 < data_len:
        while i < data_len and data[i] == 0xFF:
            i += 1
        if i >= data_len:
            break

        marker = data[i]
        i += 1

        if marker in {0xD8, 0xD9}:
            continue

        if i + 1 >= data_len:
            break

        segment_length = (data[i] << 8) | data[i + 1]
        if segment_length < 2 or i + segment_length > data_len:
            break

        if marker in sof_markers:
            if segment_length < 7:
                break
            height = (data[i + 3] << 8) | data[i + 4]
            width = (data[i + 5] << 8) | data[i + 6]
            return width, height

        i += segment_length

    raise ValueError(f"Could not extract JPEG dimensions: {image_path.name}")



def format_c_array(data, array_name="image_data", bytes_per_line=12):
    """
    Format byte array as C-style array declaration

    Args:
        data: Bytes to format
        array_name: Name of the array variable
        bytes_per_line: Number of bytes per line in output

    Returns:
        str: Formatted C array string
    """
    output = f"const unsigned char {array_name}[] = {{\n"

    for i in range(0, len(data), bytes_per_line):
        line_data = data[i:i+bytes_per_line]
        hex_values = ', '.join(f'0x{b:02X}' for b in line_data)
        output += f"    {hex_values}"
        if i + bytes_per_line < len(data):
            output += ","
        output += "\n"

    output += "};\n"
    return output


def format_python_array(data, array_name="image_data", bytes_per_line=12):
    """
    Format byte array as Python list

    Args:
        data: Bytes to format
        array_name: Name of the array variable
        bytes_per_line: Number of bytes per line in output

    Returns:
        str: Formatted Python array string
    """
    output = f"{array_name} = [\n"

    for i in range(0, len(data), bytes_per_line):
        line_data = data[i:i+bytes_per_line]
        hex_values = ', '.join(f'0x{b:02X}' for b in line_data)
        output += f"    {hex_values}"
        if i + bytes_per_line < len(data):
            output += ","
        output += "\n"

    output += "]\n"
    return output


def write_header_for_image(image_path, output_dir):
    image_data, size = jpeg_to_array(image_path)
    width, height = get_image_dimensions(image_path)

    symbol_base = make_jpeg_symbol_base(image_path.stem)
    array_name = f"{symbol_base}_data"
    size_name = f"{array_name}_len"
    width_macro = f"{symbol_base.upper()}_WIDTH"
    height_macro = f"{symbol_base.upper()}_HEIGHT"
    guard = make_include_guard(image_path.stem)

    c_array = format_c_array(image_data, array_name)
    output_h_file = output_dir / f"{image_path.stem}.h"

    with open(output_h_file, 'w', encoding='utf-8') as f:
        f.write("/******************************************************************************\n"
            "* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
            "* Technologies AG. All rights reserved.
            "* This software, associated documentation and materials ("Software") is
            "* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
            "* and is protected by and subject to worldwide patent protection, worldwide
            "* copyright laws, and international treaty provisions. Therefore, you may use
            "* this Software only as provided in the license agreement accompanying the
            "* software package from which you obtained this Software. If no license
            "* agreement applies, then any use, reproduction, modification, translation, or
            "* compilation of this Software is prohibited without the express written
            "* permission of Infineon.
            "* 
            "* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
            "* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
            "* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
            "* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
            "* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
            "* Infineon reserves the right to make changes to the Software without notice.
            "* You are responsible for properly designing, programming, and testing the
            "* functionality and safety of your intended application of the Software, as
            "* well as complying with any legal requirements related to its use. Infineon
            "* does not guarantee that the Software will be free from intrusion, data theft
            "* or loss, or other breaches ("Security Breaches"), and Infineon shall have
            "* no liability arising out of any Security Breaches. Unless otherwise
            "* explicitly approved by Infineon, the Software may not be used in any
            "* application where a failure of the Product or any consequences of the use
            "* thereof can reasonably be expected to result in personal injury.\n"
            "*******************************************************************************/\n")
        f.write(f"// Auto-generated from {image_path.name}\n")
        f.write(f"// Array size: {size} bytes\n\n")
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write(f"#define {width_macro}    ({width})\n")
        f.write(f"#define {height_macro}   ({height})\n\n")
        f.write(c_array)
        f.write(f"\nconst unsigned int {size_name} = {size};\n\n")
        f.write(f"#endif /* {guard} */\n")

    return {
        "image_name": image_path.name,
        "header_name": output_h_file.name,
        "size": size,
        "array_name": array_name,
        "size_name": size_name,
        "width_macro": width_macro,
        "height_macro": height_macro,
        "width": width,
        "height": height,
    }


def write_image_database_header(entries):
    max_width = max((entry["width"] for entry in entries), default=0)
    max_height = max((entry["height"] for entry in entries), default=0)

    banner = (
        "/******************************************************************************\n"
        "* File Name:   image_database_file.h\n"
        "*\n"
        "* Description: Auto-generated header. Contains the image_info_t structure\n"
        "*              definition, image count/dimension macros, and the extern\n"
        "*              declaration for image_database[] used by the JPEG decoder.\n"
        "*\n"
        "* Note: This file is auto-generated by jpeg_to_array.py. Do not edit manually.\n"
        "*\n"
        "*******************************************************************************\n"
        "* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
        "* Technologies AG. All rights reserved.
        "* This software, associated documentation and materials ("Software") is
        "* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
        "* and is protected by and subject to worldwide patent protection, worldwide
        "* copyright laws, and international treaty provisions. Therefore, you may use
        "* this Software only as provided in the license agreement accompanying the
        "* software package from which you obtained this Software. If no license
        "* agreement applies, then any use, reproduction, modification, translation, or
        "* compilation of this Software is prohibited without the express written
        "* permission of Infineon.
        "* 
        "* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
        "* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
        "* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
        "* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
        "* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
        "* Infineon reserves the right to make changes to the Software without notice.
        "* You are responsible for properly designing, programming, and testing the
        "* functionality and safety of your intended application of the Software, as
        "* well as complying with any legal requirements related to its use. Infineon
        "* does not guarantee that the Software will be free from intrusion, data theft
        "* or loss, or other breaches ("Security Breaches"), and Infineon shall have
        "* no liability arising out of any Security Breaches. Unless otherwise
        "* explicitly approved by Infineon, the Software may not be used in any
        "* application where a failure of the Product or any consequences of the use
        "* thereof can reasonably be expected to result in personal injury.\n"
        "*******************************************************************************/\n"
        "\n"
    )

    with open(DATABASE_HEADER_PATH, 'w', encoding='utf-8') as f:
        f.write(banner)
        f.write("#ifndef IMAGE_DATABASE_FILE_H\n")
        f.write("#define IMAGE_DATABASE_FILE_H\n\n")
        f.write("/*******************************************************************************\n")
        f.write("* Macros\n")
        f.write("*******************************************************************************/\n")
        f.write(f"#define NUM_IMAGES          ({len(entries)})\n")
        f.write(f"#define MAX_IMAGE_WIDTH     ({max_width})\n")
        f.write(f"#define MAX_IMAGE_HEIGHT    ({max_height})\n\n")
        f.write("/*******************************************************************************\n")
        f.write("* Type(s)\n")
        f.write("*******************************************************************************/\n")
        f.write("/* Structure to hold image metadata */\n")
        f.write("typedef struct {\n")
        f.write("    const char* name;\n")
        f.write("    const unsigned char* jpeg_data;\n")
        f.write("    unsigned long jpeg_size;\n")
        f.write("    int width;\n")
        f.write("    int height;\n")
        f.write("} image_info_t;\n\n")
        f.write("/*******************************************************************************\n")
        f.write("* Global Variable(s)\n")
        f.write("*******************************************************************************/\n")
        f.write("extern image_info_t image_database[NUM_IMAGES];\n\n")
        f.write("/*******************************************************************************\n")
        f.write("* Function Prototypes\n")
        f.write("*******************************************************************************/\n")
        f.write("void init_image_database(void);\n\n")
        f.write("#endif /* IMAGE_DATABASE_FILE_H */\n\n")
        f.write("/* [] END OF FILE */\n")


def write_image_database_source(entries):
    banner = (
        "/******************************************************************************\n"
        "* File Name:   image_database_file.c\n"
        "*\n"
        "* Description: Auto-generated source. Includes per-image JPEG array headers\n"
        "*              and implements init_image_database() which populates the\n"
        "*              image_database[] array with metadata for each image.\n"
        "*\n"
        "* Note: This file is auto-generated by jpeg_to_array.py. Do not edit manually.\n"
        "*\n"
        "*******************************************************************************\n"
        "* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
        "* Technologies AG. All rights reserved.
        "* This software, associated documentation and materials ("Software") is
        "* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
        "* and is protected by and subject to worldwide patent protection, worldwide
        "* copyright laws, and international treaty provisions. Therefore, you may use
        "* this Software only as provided in the license agreement accompanying the
        "* software package from which you obtained this Software. If no license
        "* agreement applies, then any use, reproduction, modification, translation, or
        "* compilation of this Software is prohibited without the express written
        "* permission of Infineon.
        "* 
        "* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
        "* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
        "* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
        "* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
        "* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
        "* Infineon reserves the right to make changes to the Software without notice.
        "* You are responsible for properly designing, programming, and testing the
        "* functionality and safety of your intended application of the Software, as
        "* well as complying with any legal requirements related to its use. Infineon
        "* does not guarantee that the Software will be free from intrusion, data theft
        "* or loss, or other breaches ("Security Breaches"), and Infineon shall have
        "* no liability arising out of any Security Breaches. Unless otherwise
        "* explicitly approved by Infineon, the Software may not be used in any
        "* application where a failure of the Product or any consequences of the use
        "* thereof can reasonably be expected to result in personal injury.\n"
        "*******************************************************************************/\n"
        "\n"
    )

    with open(DATABASE_SOURCE_PATH, 'w', encoding='utf-8') as f:
        f.write(banner)
        f.write("/*******************************************************************************\n")
        f.write("* Header Files\n")
        f.write("*******************************************************************************/\n")
        f.write('#include "image_database_file.h"\n\n')
        f.write("/* JPEG image includes */\n")
        for entry in entries:
            f.write(f'#include "../jpeg_image_array/{entry["header_name"]}"\n')
        f.write("\n")
        f.write("/*******************************************************************************\n")
        f.write("* Global Variable(s)\n")
        f.write("*******************************************************************************/\n")
        f.write("image_info_t image_database[NUM_IMAGES];\n\n")
        f.write("/*******************************************************************************\n")
        f.write("* Function Name: init_image_database\n")
        f.write("********************************************************************************\n")
        f.write("* Summary:\n")
        f.write("*  Populates the image_database[] array with metadata (name, JPEG data pointer,\n")
        f.write("*  size, width, height) for each image. Must be called once before accessing\n")
        f.write("*  image_database[].\n")
        f.write("*\n")
        f.write("* Parameters:\n")
        f.write("*  void\n")
        f.write("*\n")
        f.write("* Return:\n")
        f.write("*  void\n")
        f.write("*\n")
        f.write("*******************************************************************************/\n")
        f.write("void init_image_database(void)\n")
        f.write("{\n")

        for index, entry in enumerate(entries):
            f.write(f'    image_database[{index}].name = "Image {index + 1}: {entry["width"]}x{entry["height"]}";\n')
            f.write(f'    image_database[{index}].jpeg_data = {entry["array_name"]};\n')
            f.write(f'    image_database[{index}].jpeg_size = {entry["size_name"]};\n')
            f.write(f'    image_database[{index}].width = {entry["width_macro"]};\n')
            f.write(f'    image_database[{index}].height = {entry["height_macro"]};\n\n')

        f.write("}\n\n")
        f.write("/* [] END OF FILE */\n")


def sort_entries_by_decreasing_size(entries):
    return sorted(
        entries,
        key=lambda entry: (entry["width"] * entry["height"], entry["size"]),
        reverse=True,
    )


def parse_generated_header_entry(header_path):
    text = header_path.read_text(encoding='utf-8')

    guard_match = re.search(r'#ifndef\s+([a-zA-Z_][a-zA-Z0-9_]*)', text)
    width_match = re.search(r'#define\s+([A-Z0-9_]+_WIDTH)\s*\((\d+)\)', text)
    height_match = re.search(r'#define\s+([A-Z0-9_]+_HEIGHT)\s*\((\d+)\)', text)
    array_match = re.search(r'const\s+unsigned\s+char\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\[\]\s*=\s*\{', text)
    size_match = re.search(r'const\s+unsigned\s+int\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(\d+)\s*;', text)

    if not (guard_match and width_match and height_match and array_match and size_match):
        raise ValueError(f"Header format not recognized: {header_path.name}")

    width_macro, width = width_match.group(1), int(width_match.group(2))
    height_macro, height = height_match.group(1), int(height_match.group(2))
    array_name = array_match.group(1)
    size_name, size = size_match.group(1), int(size_match.group(2))

    return {
        "image_name": header_path.stem,
        "header_name": header_path.name,
        "guard": guard_match.group(1),
        "size": size,
        "array_name": array_name,
        "size_name": size_name,
        "width_macro": width_macro,
        "height_macro": height_macro,
        "width": width,
        "height": height,
    }


def collect_entries_from_headers(output_dir):
    header_files = find_headers_in_folder(output_dir)
    entries = []

    for header_file in header_files:
        entries.append(parse_generated_header_entry(header_file))

    return entries


def validate_image_identities(image_files, output_dir):
    output_names = {}
    identifiers = {}
    replacement_names = set()

    def reserve(owners, identity, source):
        if identity in owners:
            raise ValueError(
                f"Identity collision for {identity!r}: {owners[identity]} and {source}"
            )
        owners[identity] = source

    for image_path in image_files:
        header_name = f"{image_path.stem}.h"
        reserve(output_names, header_name.casefold(), image_path)
        replacement_names.add(header_name)
        symbol_base = make_jpeg_symbol_base(image_path.stem)
        for identity in (
            f"{symbol_base}_data", f"{symbol_base}_data_len",
            f"{symbol_base.upper()}_WIDTH", f"{symbol_base.upper()}_HEIGHT",
            make_include_guard(image_path.stem),
        ):
            reserve(identifiers, identity, image_path)

    if output_dir.exists():
        for header_path in find_headers_in_folder(output_dir):
            if header_path.name in replacement_names:
                continue
            reserve(output_names, header_path.name.casefold(), header_path)
            entry = parse_generated_header_entry(header_path)
            for key in ("array_name", "size_name", "width_macro", "height_macro", "guard"):
                reserve(identifiers, entry[key], header_path)


def main():

    if not JPEG_INPUT_DIR.exists():
        print(f"Input folder not found: {JPEG_INPUT_DIR}")
        raise SystemExit(1)

    image_files = find_images_in_folder(JPEG_INPUT_DIR)
    validate_image_identities(image_files, JPEG_OUTPUT_DIR)
    JPEG_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    DATABASE_DIR.mkdir(parents=True, exist_ok=True)

    if not image_files:
        print(f"No image files found in: {JPEG_INPUT_DIR}")
        print("Supported formats: .jpg, .jpeg, .jpe, .jfif")
    else:
        print(f"Found {len(image_files)} image(s) in: {JPEG_INPUT_DIR}\n")

        for image_file in image_files:
            entry = write_header_for_image(image_file, JPEG_OUTPUT_DIR)
            output_h_file = JPEG_OUTPUT_DIR / entry["header_name"]
            size = entry["size"]
            array_name = entry["array_name"]
            size_name = entry["size_name"]
            width_macro = entry["width_macro"]
            height_macro = entry["height_macro"]
            width = entry["width"]
            height = entry["height"]
            print(f"OK {image_file.name} -> {output_h_file.name}")
            print(f"  - bytes: {size} ({size / 1024:.2f} KB)")
            print(f"  - symbols: {array_name}, {size_name}\n")
            print(f"  - dimensions: {width_macro}={width}, {height_macro}={height}\n")

    header_entries = collect_entries_from_headers(JPEG_OUTPUT_DIR)
    if not header_entries:
        print(f"No header files found in: {JPEG_OUTPUT_DIR}")
        raise SystemExit(1)

    sorted_entries = sort_entries_by_decreasing_size(header_entries)
    write_image_database_header(sorted_entries)
    write_image_database_source(sorted_entries)
    print(f"Detected {len(header_entries)} header file(s) in: {JPEG_OUTPUT_DIR}")
    print(f"Updated database header: {DATABASE_HEADER_PATH}")
    print(f"Updated database source: {DATABASE_SOURCE_PATH}")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(f"ERROR: {error}") from error
