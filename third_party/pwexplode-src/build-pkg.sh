#!/bin/bash

# Make sure that the latest PyPA’s build is installed
python3 -m pip install --upgrade build

# Build the package (will create dist and *.egg-info subdirectories)
python3 -m build



