[![Build Status](https://travis-ci.org/adobe-type-tools/psautohint.svg?branch=master)](https://travis-ci.org/adobe-type-tools/psautohint)
[![AppVeyor status](https://ci.appveyor.com/api/projects/status/frpwwnql34k70drl?svg=true)](https://ci.appveyor.com/project/adobe-type-tools/psautohint)


# PSAutoHint

A standalone version of [AFDKO](https://github.com/adobe-type-tools/afdko)'s
autohinter.

Still a work in progress. Use at your own risk!

## Building and running

This repository currently consists of a core autohinter written in C, a Python C
extension providing an interface to it, and helper Python code.

To build the C extension:

    make build

To install the C extension and the helper scripts globally:

    make install

Alternatively to install them for the current user:

    make PIP_OPTIONS=--user install

The authinter can be used by running:

    psautohint

The old `autohintexe` binary can also be built from `libpsautohint` directory:

    autoreconf -if
    ./configure
    make

## Testing

We have a very primitive test suite that can be run with:

    make check
