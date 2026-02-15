import os

import lit.formats

config.name = "pyxc-chapter06"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".pyxc"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

chapter_dir = os.path.abspath(os.path.join(config.test_source_root, ".."))
config.substitutions.append(("%pyxc", os.path.join(chapter_dir, "build", "pyxc")))
