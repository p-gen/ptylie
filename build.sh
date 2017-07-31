#!/bin/sh

# Ensure that aclocal wont' be called
# """""""""""""""""""""""""""""""""""
touch aclocal.m4
touch Makefile.in configure config.h.in

# Build the executable
# """"""""""""""""""""
./configure "$@"
make
