import tempfile
import unittest
import zipfile
import tarfile
from pathlib import Path
from scripts.package import write_normalized_zip_member, normalize_tarinfo, FIXED_ZIP_DATETIME


class TestPackageTimestamps(unittest.TestCase):
    def test_zip_timestamps_are_normalized(self):
        with tempfile.TemporaryDirectory() as tempdir:
            temp_path = Path(tempdir)
            sample_file = temp_path / "test.dll"
            sample_file.write_bytes(b"dummy binary data")

            zip_path = temp_path / "test.zip"
            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
                write_normalized_zip_member(zf, sample_file, "build/test.dll")

            with zipfile.ZipFile(zip_path, "r") as zf:
                info = zf.getinfo("build/test.dll")
                self.assertEqual(info.date_time, FIXED_ZIP_DATETIME)

    def test_tar_timestamps_are_normalized(self):
        ti = tarfile.TarInfo("build/test.so")
        ti.mtime = 1761600000  # Some future time
        normalized = normalize_tarinfo(ti)
        self.assertEqual(normalized.mtime, 0)


if __name__ == "__main__":
    unittest.main()
