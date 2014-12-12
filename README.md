# picasso

## Introduction

`picasso` is a PICA200 shader assembler, written in C++. The PICA200 is the GPU used by the Nintendo 3DS.

`picasso` comes with a manual `Manual.md` that explains the shader language. `example.vsh` is simple example that demonstrates it.

## Building

A working C++ compiler for the host is required (Windows users: use TDM-GCC), plus autotools. Use the following commands to build the program:

    ./autogen.sh
    ./configure
    make

## Shout-outs

- **smea** for reverse-engineering the PICA200, writing documentation, working hard & making `aemstro_as.py` (the original homebrew PICA200 shader assembler)
- **neobrain** for making `nihstro-assemble`, whose syntax inspired that of `picasso` and whose usage of boost inspired me to make my own assembler without hefty dependencies.
