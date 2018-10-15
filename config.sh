#!/bin/bash

# Define custom utilities
# Test for OSX with [ -n "$IS_OSX" ]

function pre_build {
    # Any stuff that you need to do before you start building the wheels
    # Runs in the root directory of this repository.

    # Travis only clones the latest 50 commits. We need the full repository
    # to compute the version string from the git metadata:
    # https://github.com/travis-ci/travis-ci/issues/3412#issuecomment-83993903
    # https://github.com/pypa/setuptools_scm/issues/93
    git fetch --unshallow
}

function run_tests {
    # The function is called from an empty temporary directory.
    cd ..

    # Get absolute path to the pre-compiled wheel
    wheelhouse=$(abspath wheelhouse)
    wheel=$(ls ${wheelhouse}/psautohint*.whl | head -n 1)
    if [ ! -e "${wheel}" ]; then
        echo "error: can't find wheel in ${wheelhouse} folder" 1>&2
        exit 1
    fi

    # select tox environment based on the current python version
    # E.g.: '2.7' -> 'py27-cov'
    TOXENV="py${MB_PYTHON_VERSION//\./}-cov"
    # append the "-tx" tox factor to run the tests with afdko, but only
    # for python 2.7 (at least for the time being)
    if [ "$MB_PYTHON_VERSION" == "2.7" ]; then
        TOXENV="$TOXENV-tx"
    fi

    # Install pre-compiled wheel and run tests against it
    tox --installpkg "${wheel}" -e "${TOXENV}"

    # clean up after us, or else running tox later on outside the docker
    # container can lead to permission errors
    rm -rf .tox
}

# override default 'install_delocate' as a temporary workaround for
# autohintexe embedded executable losing execute permissions on macOS
# https://github.com/matthew-brett/delocate/issues/42
# https://github.com/matthew-brett/delocate/pull/43
# https://github.com/adobe-type-tools/psautohint/pull/116
# TODO: remove this once new delocate with the above patch is released
function install_delocate {
    check_pip
    $PIP_CMD install git+https://github.com/matthew-brett/delocate.git#egg=delocate
}
