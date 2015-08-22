# picasso Changelog

# v2.0

- (**Breaking change**) Command line format changed.
- Added support for assembling multiple shaders (DVLEs) into a single SHBIN.
- Added new directives: `.entry`, `.nodvle`, `.gsh`, `.setf`, `.seti`, `.setb`.
- Added auto-detection of inverted forms of opcodes. (Explicitly using `dphi`, `sgei`, `slti` and `madi` is now deprecated)
- Several miscellaneous bug fixes.

# v1.0

- Initial release.
