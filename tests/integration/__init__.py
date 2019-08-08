import os
import py.path

here = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
DATA_DIR = os.path.relpath(os.path.join(here, "data"), os.getcwd())


def make_temp_copy(tmpdir, path):
    src = py.path.local(path)
    dst = tmpdir / src.basename
    src.copy(dst)
    return str(dst)
