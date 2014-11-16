# picasso

## Introduction

`picasso` is a PICA200 shader assembler, written in C++. The PICA200 is the GPU used by the Nintendo 3DS.

Currently there's no documentation; refer to `example.vsh` in order to figure out the syntax.

## Building

A working C++11 compiler for the host is required (Windows users: use TDM-GCC). Use `make` to build the program.

## Shout-outs

- **smea** for reverse-engineering the PICA200, writing documentation, working hard & making `aemstro_as.py` (the original homebrew PICA200 shader assembler)
- **neobrain** for making `nihstro-assemble`, whose syntax inspired that of `picasso` and whose usage of boost inspired me to make my own assembler without hefty dependencies.
