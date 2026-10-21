# File Name: test_jpeg_to_array.py
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

import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import jpeg_to_array as converter


class ImageIdentityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.inputs = self.root / "images"
        self.outputs = self.root / "headers"
        self.database = self.root / "database"
        for folder in (self.inputs, self.outputs, self.database):
            folder.mkdir()
        self.header = self.database / "image_database_file.h"
        self.source = self.database / "image_database_file.c"
        self.header.write_text("original header", encoding="utf-8")
        self.source.write_text("original source", encoding="utf-8")
        settings = patch.multiple(
            converter, JPEG_INPUT_DIR=self.inputs, JPEG_OUTPUT_DIR=self.outputs,
            DATABASE_DIR=self.database, DATABASE_HEADER_PATH=self.header,
            DATABASE_SOURCE_PATH=self.source,
        )
        settings.start()
        self.addCleanup(settings.stop)
        dimensions = patch.object(converter, "get_image_dimensions", return_value=(2, 3))
        dimensions.start()
        self.addCleanup(dimensions.stop)

    def image(self, name):
        image_path = self.inputs / name
        image_path.write_bytes(b"\xff\xd8test image")
        return image_path

    def existing_header(self, name):
        image_path = self.root / name
        image_path.write_bytes(b"\xff\xd8existing image")
        entry = converter.write_header_for_image(image_path, self.outputs)
        return self.outputs / entry["header_name"]

    def snapshot(self):
        return {path.relative_to(self.root): path.read_bytes()
                for folder in (self.outputs, self.database)
                for path in folder.iterdir() if path.is_file()}

    def assert_rejected_without_writes(self, *names):
        before = self.snapshot()
        with self.assertRaises(ValueError) as caught:
            converter.main()
        for name in names:
            self.assertIn(name, str(caught.exception))
        self.assertEqual(before, self.snapshot())

    def test_equal_stems_do_not_overwrite(self):
        self.existing_header("photo.jpg")
        self.image("photo.jpg")
        self.image("photo.jpeg")
        self.assert_rejected_without_writes("photo.jpg", "photo.jpeg")

    def test_normalized_names_are_rejected(self):
        for first, second in (("a-b.jpg", "a_b.jpg"),
                              ("name.jpg", "jpeg_name.jpg"),
                              ("_name.jpg", "name.jpg")):
            with self.subTest(first=first, second=second):
                converter.validate_image_identities([], self.outputs)
                with self.assertRaises(ValueError):
                    converter.validate_image_identities(
                        [self.inputs / first, self.inputs / second], self.outputs)

    def test_normalized_collision_leaves_files_unchanged(self):
        self.existing_header("retained.jpg")
        self.image("a-b.jpg")
        self.image("a_b.jpg")
        self.assert_rejected_without_writes("a-b.jpg", "a_b.jpg")

    def test_case_insensitive_output_collision(self):
        with self.assertRaises(ValueError):
            converter.validate_image_identities(
                [self.inputs / "Photo.jpg", self.inputs / "photo.jpeg"], self.outputs)

    def test_collision_with_retained_header(self):
        self.existing_header("a-b.jpg")
        self.image("a_b.jpg")
        self.assert_rejected_without_writes("a-b.h", "a_b.jpg")

    def test_case_only_replacement_is_rejected(self):
        self.existing_header("Photo.jpg")
        self.image("photo.jpeg")
        self.assert_rejected_without_writes("Photo.h", "photo.jpeg")

    def test_actual_existing_symbols_are_checked(self):
        header_path = self.existing_header("a-b.jpg")
        header_path.rename(self.outputs / "renamed.h")
        self.image("a_b.jpg")
        self.assert_rejected_without_writes("renamed.h", "a_b.jpg")

    def test_existing_header_collisions_are_rejected(self):
        self.existing_header("a-b.jpg")
        self.existing_header("a_b.jpg")
        self.assert_rejected_without_writes("a-b.h", "a_b.h")

    def test_valid_replacement_and_retained_header(self):
        replaced = self.existing_header("photo.jpg")
        retained = self.existing_header("retained.jpg")
        before = retained.read_bytes()
        old = replaced.read_bytes()
        self.image("photo.jpeg")
        self.image("new.jpg")
        with contextlib.redirect_stdout(io.StringIO()):
            converter.main()
        self.assertEqual(before, retained.read_bytes())
        self.assertNotEqual(old, replaced.read_bytes())
        entries = converter.collect_entries_from_headers(self.outputs)
        self.assertEqual({entry["image_name"] for entry in entries},
                         {"photo", "retained", "new"})
        self.assertIn("#define NUM_IMAGES          (3)", self.header.read_text())


if __name__ == "__main__":
    unittest.main()