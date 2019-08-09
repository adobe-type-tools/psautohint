import os

here = os.path.abspath(os.path.realpath(os.path.dirname(__file__)))
DATA_DIR = os.path.relpath(os.path.join(here, "data"), os.getcwd())
