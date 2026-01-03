#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

doxygen --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get Doxygen version, please check if it's installed."
    exit $status
fi

git --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get Git version, please check if it's installed."
    exit $status
fi

if [ ! -d "../cppreference-doxygen" ]; then
    git clone https://github.com/cfnptr/cppreference-doxygen ../cppreference-doxygen
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to clone cppreference-doxygen repository."
        exit $status
    fi
else
    git -C ../cppreference-doxygen pull
    status=$?

    if [ $status -ne 0 ]; then
        echo "Failed to pull cppreference-doxygen repository changes."
        exit $status
    fi
fi

cd "../"

doxygen Doxyfile
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to generate documentation."
    exit $status
fi

exit 0