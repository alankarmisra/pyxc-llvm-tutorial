import os
import shutil

import lit.formats

config.name = "pyxc-chapter13"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".pyxc"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root
config.excludes = ["mandel.pyxc", "test.pyxc", "demo.pyxc"]

chapter_dir = os.path.abspath(os.path.join(config.test_source_root, ".."))
config.substitutions.append(("%pyxc", os.path.join(chapter_dir, "build", "pyxc")))

# Path to the C runtime helper used by emit-obj tests.
config.substitutions.append(
    ("%runtime_c", os.path.join(config.test_source_root, "runtime.c"))
)

# Resolve clang for linking emitted object files.
clang = "/usr/bin/clang" if os.path.exists("/usr/bin/clang") else (shutil.which("clang") or "clang")
config.substitutions.append(("%clang", clang))
