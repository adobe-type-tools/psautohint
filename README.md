![Test and Build](https://github.com/adobe-type-tools/psautohint/workflows/Test%20and%20Build/badge.svg)
[![Codecov](https://codecov.io/gh/adobe-type-tools/psautohint/branch/master/graph/badge.svg)](https://codecov.io/gh/adobe-type-tools/psautohint)
[![PyPI](https://img.shields.io/pypi/v/psautohint.svg)](https://pypi.org/project/psautohint)
[![Language grade: Python](https://img.shields.io/lgtm/grade/python/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/context:python)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/alerts/)

PSAutoHint
==========

A standalone version of the [AFDKO](https://github.com/adobe-type-tools/afdko)
autohinter.

**NOTE**: As of 2022 the package is "pure" Python instead of a Python
wrapper around C code. See these [notes](doc/NOTES.md) for more information
about the Python port.

Building and running
--------------------

This repository consists of an autohinter written in Python and related
documentation.

To install the module system-wide:

    pip install -r requirements.txt .

Or alternatively to install it for the current user:

    pip install -r requirements.txt --user .

The autohinter can then be used by running:

    psautohint

Testing
-------

The package test suite can be run with:

    pytest
