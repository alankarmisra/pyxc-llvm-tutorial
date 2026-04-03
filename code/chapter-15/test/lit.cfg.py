import os
import platform
import shutil

import lit.formats

config.name = "pyxc-chapter15"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".pyxc"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root
config.excludes = ["Inputs", "mandel.pyxc", "test.pyxc", "demo.pyxc"]

chapter_dir = os.path.abspath(os.path.join(config.test_source_root, ".."))
config.substitutions.append(("%pyxc", os.path.join(chapter_dir, "build", "pyxc")))

# Path to the C runtime helper used by emit-obj tests.
config.substitutions.append(
    ("%runtime_c", os.path.join(config.test_source_root, "runtime.c"))
)

# Resolve clang for linking emitted object files.
clang = "/usr/bin/clang" if os.path.exists("/usr/bin/clang") else (shutil.which("clang") or "clang")
config.substitutions.append(("%clang", clang))

# Optional tools for debug-info tests.
dwarfdump = shutil.which("llvm-dwarfdump") or shutil.which("dwarfdump") or ""
readelf = shutil.which("llvm-readelf") or shutil.which("readelf") or ""
config.substitutions.append(("%dwarfdump", dwarfdump))
config.substitutions.append(("%readelf", readelf))
if dwarfdump:
  config.available_features.add("llvm-dwarfdump")
if readelf:
  config.available_features.add("llvm-readelf")

if platform.system() == "Darwin":
  config.available_features.add("system-darwin")
