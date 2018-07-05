from __future__ import print_function, absolute_import, division
import os

ROOT = os.path.abspath(
    os.path.realpath(os.path.dirname(os.path.dirname(__file__)))
)
DATA_DIR = os.path.relpath(os.path.join(ROOT, "tests", "data"), os.getcwd())
