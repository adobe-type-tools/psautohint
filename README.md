![Test and Build](https://github.com/adobe-type-tools/psautohint/workflows/Test%20and%20Build/badge.svg)
[![Codecov](https://codecov.io/gh/adobe-type-tools/psautohint/branch/master/graph/badge.svg)](https://codecov.io/gh/adobe-type-tools/psautohint)
[![PyPI](https://img.shields.io/pypi/v/psautohint.svg)](https://pypi.org/project/psautohint)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/context:cpp)
[![Language grade: Python](https://img.shields.io/lgtm/grade/python/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/context:python)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/adobe-type-tools/psautohint.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/adobe-type-tools/psautohint/alerts/)

Important Note: `otfautohint`
=============================

This repository is now **retired**. We have added a python port of its code to
version 4.0.0 of [AFDKO](https://github.com/adobe-type-tools/afdko) (to be
released shortly). There is more information
[here](https://github.com/adobe-type-tools/afdko/blob/develop/docs/otfautohint_Notes.md).
The new tool can be called from the command line as `otfautohint`, allowing it
and `psautohint` to be used at the same time during a transition period.  Please switch to
`otfautohint` and file any new issues or PRs against that tool.

(`psautohint` was split off from `AFDKO` a number of years ago for technical reasons having
to do with code organization. The Python port resolves those issues so we are adding it
back into AFDKO to simplify distribution of our tools.)

PSAutoHint
----------

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

    pytest

Debugging
---------

For standard debugging, build with:

    python setup.py build --debug

It is also possible to build a debug version with [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer) ("ASan") support (currently _for Mac OS X only_)  with:

    python setup.py build --asan
    pip install .

Once it is installed, you can use the `util/launch-asan.sh` shell script to launch a Python process that invokes the ASan libraries needed for debugging. Attach Xcode the launched process, then execute code in the process that triggers memory usage problems and wait for ASan to do its magic.

NOTE: be sure to build and install `psautohint` as described above; using other techniques such as `python setup.py install` will cause a re-build _without_ ASan and debug support, which won't work.
