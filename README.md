# PSAutoHint

[![Build Status](https://travis-ci.org/khaledhosny/psautohint.svg?branch=master)](https://travis-ci.org/khaledhosny/psautohint)
[![Build status](https://ci.appveyor.com/api/projects/status/0xy2iyc6wsl5ag4e?svg=true)](https://ci.appveyor.com/project/khaledhosny/psautohint)

A standalone version of [AFDKO](https://github.com/adobe-type-tools/afdko)
autohinter.

Still work on progress, use at your own risk.

## Building and running

This repository currently consists of core autohinter written in C, a Python C
extension providing an interface to it and helper Python code.

To build and install the C extension:

    python setup.py install --user

The Python helpers are not currently installed, the authinter can be used by
running:

    python python/autohint.py

The old `authintexe` binary can be also built by running:

    make

## Testing

We have a very primitive test suite that can be run with:

    make check
