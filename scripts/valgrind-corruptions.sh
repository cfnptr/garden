#!/bin/bash
cd "$(dirname "$BASH_SOURCE")"

valgrind --leak-check=no ../build/garden-editor
