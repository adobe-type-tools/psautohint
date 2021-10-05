![Test and Build](https://github.com/adobe-type-tools/psautohint/workflows/Test%20and%20Build/badge.svg)
[![Codecov](https://codecov.io/gh/adobe-type-tools/psautohint/branch/master/graph/badge.svg)](https://codecov.io/gh/adobe-type-tools/psautohint)
[![PyPI](https://img.shields.io/pypi/v/psautohint.svg)](https://pypi.org/project/psautohint)
[![Language grade: Python](https://img.shields.io/lgtm/grade/python/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/context:python)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/alerts/)

PSAutoHint
==========

A standalone version of the [AFDKO](https://github.com/adobe-type-tools/afdko)
autohinter.

Building and running
--------------------

This repository consists of an autohinter written in pure python and
related documentation.

To install the module system-wide:

    pip install -r requirements.txt .

Or alternatively to install it for the current user:

    pip install -r requirements.txt --user .

The autohinter can then be used by running:

    psautohint

Testing
-------

We have a test suite that can be run with:

    pytest
