[![Travis](https://travis-ci.org/adobe-type-tools/psautohint.svg?branch=master)](https://travis-ci.org/adobe-type-tools/psautohint)
[![AppVeyor](https://ci.appveyor.com/api/projects/status/frpwwnql34k70drl?svg=true)](https://ci.appveyor.com/project/adobe-type-tools/psautohint)
[![Codacy](https://api.codacy.com/project/badge/Grade/171cdb2c833f484f8d2d85253123bd39)](https://www.codacy.com/app/adobe-type-tools/psautohint?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=adobe-type-tools/psautohint&amp;utm_campaign=Badge_Grade)
[![Codecov](https://codecov.io/gh/adobe-type-tools/psautohint/branch/master/graph/badge.svg)](https://codecov.io/gh/adobe-type-tools/psautohint)
[![PyPI](https://img.shields.io/pypi/v/psautohint.svg)](https://pypi.org/project/psautohint)

PSAutoHint
==========

A standalone version of [AFDKO](https://github.com/adobe-type-tools/afdko)’s
autohinter.

Building and running
--------------------

This repository currently consists of a core autohinter written in C, a
Python C extension providing an interface to it, and helper Python code.

To build the C extension:

    python setup.py build

To install the C extension and the helper scripts globally:

    pip install -r requirements.txt .

Alternatively to install them for the current user:

    pip install -r requirements.txt --user .

The autohinter can be used by running:

    psautohint

To build just the `autohintexe` binary:

    python setup.py build_exe

Testing
-------

We have a test suite that can be run with:

    tox
