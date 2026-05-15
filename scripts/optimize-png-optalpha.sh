#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

oxipng --version > /dev/null
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to get Oxipng version, please check if it's installed."
    exit $status
fi

oxipng --opt max --strip all --alpha --zopfli --zi 255 *.png
status=$?

if [ $status -ne 0 ]; then
    echo "Failed to reduce size of the PNG images."
    exit $status
fi

exit 0
