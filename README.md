[![Build Status](https://travis-ci.org/khaledhosny/psautohint.svg?branch=master)](https://travis-ci.org/khaledhosny/psautohint)
[![Build status](https://ci.appveyor.com/api/projects/status/0xy2iyc6wsl5ag4e?svg=true)](https://ci.appveyor.com/project/khaledhosny/psautohint)

# PSAutoHint

A standalone version of [AFDKO](https://github.com/adobe-type-tools/afdko)'s
autohinter.

Still a work in progress. Use at your own risk!

## Building and running

This repository currently consists of a core autohinter written in C, a Python C
extension providing an interface to it, and helper Python code.

To build and install the C extension:

    python setup.py install --user

The Python helpers are not currently installed, the authinter can be used by
running:

    python python/autohint.py

The old `autohintexe` binary can also be built by running:

    make autohintexe

## Testing

We have a very primitive test suite that can be run with:

    make check
