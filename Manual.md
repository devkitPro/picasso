# picasso Manual

## Basic concepts

Comments are introduced by the semicolon character. E.g.

```
; This is a comment
.fvec myFloat ; They can also appear in the same line
```

Identifiers follow the same rules as C identifiers. Additionally, the dollar sign (`$`) is allowed in identifiers; mostly as a substitute for the period character (`.`) since the latter is used in `picasso` syntax.

Labels consist of an identifier plus a colon. E.g.

```
myLabel:
	mov r0, r1
```

Procedures are delimited using the `.proc` and `.end` directives. E.g.

```
.proc normalize3
	dp4 r15, r8, r8
	rsq r15, r15
	mul r8, r15, r8
.end
```

Instructions consist of an opcode name and a comma-delimited list of arguments.

Directives are special statements that start with a period and control certain aspects of `picasso`'s code emission; such as defining procedures, uniforms, constants and more.

PICA200 registers are often used as arguments to instructions. There exist the following registers:

- `o0` through `o15`: Output registers (usable as a destination operand). The range `o7` through `o15` is only available in vertex shaders.
- `v0` through `v15`: Input registers (usable as a source operand).
- `r0` through `r15`: Scratch registers (usable as both destination and source operands).
- `c0` through `c95`: Floating-point vector uniforms (usable as a special type of source operand called SRC1).
- `i0` through `i3`: Integer vector uniforms (special purpose).
- `b0` through `b15`: Boolean uniforms (special purpose).

All registers contain 24-bit floating point vectors; except for integer vector uniforms (containing 8-bit integers) and boolean uniforms. Vectors have 4 components: x, y, z and w. The components may alternatively be referred to as r, g, b and a (respectively); or s, t, p and q (respectively). Uniforms are special registers that are writable by the CPU; thus they are used to pass configuration parameters to the shader such as transformation matrices. Sometimes they are preloaded with constant values that may be used in the logic of the shader.

In most situations, vectors may be [swizzled](http://en.wikipedia.org/wiki/Swizzling_%28computer_graphics%29), that is; their components may be rearranged. Register arguments support specifying a swizzling mask: `r0.wwxy`. The swizzling mask usually has 4 components (but not more), if it has less the last component is repeated to fill the mask. The default mask applied to registers is `xyzw`; that is, identity (no effect).

Output parameters have an output mask instead of a swizzling mask. This allows the shader to write to some components of a register without affecting the others. In `picasso`, the output mask is parsed exactly the same way as the swizzling mask, enabling write access for the components that are used in it. By default it is also `xyzw`; that is, writing to all components.

Registers may also be assigned additional names in order to make the code more legible. These additional names are called aliases. Aliases may also contain a swizzling mask; if a swizzling mask is applied to an alias afterwards the masks are combined. For example, provided that `someAlias` is an alias for `c0.wyxz`, `someAlias.xxww` would be equivalent to `c0.wwzz`. Aliases may be created by several directives which reserve certain kinds of registers.

For convenience, registers may be addressed using an offset from a known register. This is called indexing. For example, `c8[4]` is equivalent to `c12`; and `r4[-2]` is equivalent to `r2`. Indexing is useful for addressing arrays of registers (such as matrices).

Some source operands of instructions (called SRC1) support relative addressing. This means that it is possible to use one of the three built-in indexing registers (`a0`, `a1` and `a2` aka `lcnt`) to address a register, e.g. `someArray[lcnt]`. Adding an offset is also supported, e.g. `someArray[lcnt+2]`. This is useful in FOR loops. Index registers can only be used with floating-point vector uniform registers, though.

Normal floating-point vector registers may also be negated by prepending a minus sign before it, e.g. `-r2` or `-someArray[lcnt+2]`.

In geometry shaders, `b15` is automatically set to true *after* each execution of the geometry shader. This can be useful to detect whether program state should be initialized - GPU management code usually resets all unused boolean uniforms to false when setting up the PICA200's shader processing units.

## Command Line Usage

```
Usage: picasso [options] files...
Options:
  -o, --out=<file>        Specifies the name of the SHBIN file to generate
  -h, --header=<file>     Specifies the name of the header file to generate
  -n, --no-nop            Disables the automatic insertion of padding NOPs
  -v, --version           Displays version information
```

DVLEs are generated in the same order as the files in the command line.

## Linking Model

`picasso` takes one or more source code files, and assembles them into a single `.shbin` file. A DVLE object is generated for each source code file, unless the `.nodvle` directive is used (see below). Procedures are shared amongst all source code files, and they may be defined and called wherever. Uniform space for vertex shaders is also shared, that is, if two vertex shader source code files declare the same uniform, they are assigned the same location. Geometry shaders however do not share uniforms, and each geometry shader source code file will have its own uniform allocation map. On the other hand, constants are never shared, and the same space is reused for the constants of each DVLE. Outputs and aliases are, by necessity, never shared either.

The entry point of a DVLE may be set with the `.entry` directive. If this directive is not used, `main` is assumed as the entrypoint.

A DVLE by default is a vertex shader, unless the `.gsh` directive is used (in the case of which a geometry shader is specified).

Uniforms that start with the underscore (`_`) character are not exposed in the DVLE table of uniforms. This allows for creating private uniforms that can be internally used to configure the behaviour of shared procedures. Additionally, dollar signs (`$`) are automatically translated to period characters (`.`) in the DVLE uniform table.

**Note**: Older versions of `picasso` handled geometry shaders in a different way. Specifically, uniform space was shared with vertex shaders and it was possible to use `.gsh` without parameters or `setemit` to flag a DVLE as a geometry shader. For backwards compatibility purposes this functionality has been retained, however its use is not recommended.

## PICA200 Caveats & Errata

The PICA200's shader units have numerous implementation caveats and errata that should be taken into account when designing and writing shader code. Some of these include:

- Certain flow of control statements may not work at the end of another block, including the closing of other nested blocks. picasso detects these situations and automatically inserts padding NOP instructions (unless the `--no-nop` command line flag is used).
- The `mova` instruction is finicky and for instance two consecutive `mova` instructions will freeze the PICA200.
- Only a single input register is able to be referenced reliabily at a time in the source registers of an operand. That is, while specifying the same input register in one or more source registers will behave correctly, specifying different input registers will produce incorrect results. picasso detects this situation and displays an error message.

## Supported Directives

### .proc
```
.proc procName
```
Introduces a procedure called `procName`. The procedure is terminated with `.end`.

### .else
```
.else
```
Introduces the ELSE section of an IF statement.

### .end
```
.end
```
Terminates a procedure, an IF statement or a FOR statement.

### .alias
```
.alias aliasName register
```
Creates a new alias for `register` called `aliasName`. The specified register may also have a swizzling mask.

### .fvec
```
.fvec unifName1, unifName2[size], unifName3, ...
```
Allocates new floating-point vector uniforms (or arrays of uniforms) and creates aliases for them that point to the allocated registers. Example:

```
.fvec scaler
.fvec projMatrix[4], modelViewMatrix[4]
```

### .ivec
```
.ivec unifName1, unifName2[size], unifName3, ...
```
Allocates new integer vector uniforms (or arrays of uniforms) and creates aliases for them that point to the allocated registers.

### .bool
```
.bool unifName1, unifName2[size], unifName3, ...
```
Allocates new boolean uniforms (or arrays of uniforms) and creates aliases for them that point to the allocated registers. Example:

```
.bool useLight[4]
.bool useRawVertexColor
```

### .constf
```
.constf constName(x, y, z, w)
```
Reserves a new floating-point vector uniform to be preloaded with the specified constant; creates an alias for it that points to the allocated register. Example:

```
.constf floatConsts(0.0, 1.0, -1.0, 3.14159)
```

### .consti
```
.consti constName(x, y, z, w)
```
Reserves a new integer vector uniform to be preloaded with the specified constant; creates an alias for it that points to the allocated register. Example:

```
.consti loopParams(16, 0, 1, 0)
```

### .constfa
```
.constfa arrayName[]
.constfa arrayName[size]
.constfa (x, y, z, w)
```
Reserves a new array of floating-point vector uniforms to be preloaded with the specified constants; creates an alias for it that points to the first element. Example:

```
; Create an array of two elements
.constfa myArray[]
.constfa (1.0, 2.0, 3.0, 4.0)
.constfa (5.0, 6.0, 7.0, 8.0)
.end
```

Optionally the size of the array may be specified. If a number of elements less than the size is specified, the missing elements are initialized to zero. Example:

```
.constfa myArray[4]
.constfa (1.0, 2.0, 3.0, 4.0)
.constfa (5.0, 6.0, 7.0, 8.0)
; The remaining two elements are vectors full of zeroes.
.end
```

### .in
```
.in inName
.in inName register
```
Reserves an input register and creates an alias for it called `inName`. If no input register is specified it is automatically allocated. The input register is added to the DVLE's uniform table.

Example:

```
.in position
.in texcoord
.in special v15
```

### .out
```
.out outName propName
.out outName propName register
.out - propName register
```
Wires an output register to a certain output property and (optionally) creates an alias for it called `outName` (specify a dash in order not to create the alias). If no output register is specified it is automatically allocated. The following property names are supported:

- `position` (or `pos`): Represents the position of the outputted vertex.
- `normalquat` (or `nquat`): Used in fragment lighting, this represents the quaternion associated to the normal vector of the vertex.
- `color` (or `clr`): Represents the color of the outputted vertex. Its format is (R, G, B, A) where R,G,B,A are values ranging from 0.0 to 1.0.
- `texcoord0` (or `tcoord0`): Represents the first texture coordinate, which is always fed to the Texture Unit 0. Only the first two components are used.
- `texcoord0w` (or `tcoord0w`): Represents the third component of the first texture coordinate, used for 3D/cube textures.
- `texcoord1` (or `tcoord1`): Similarly to `texcoord0`, this is the second texture coordinate, which is usually but not always fed to Texture Unit 1.
- `texcoord2` (or `tcoord2`): Similarly `texcoord0`, this is the third texture coordinate, which is usually but not always fed to Texture Unit 2.
- `view`: Used in fragment lighting, this represents the view vector associated to the vertex. The fourth component is not used.
- `dummy`: Used in vertex shaders to pass generic semanticless parameters to the geometry shader, and in geometry shaders to use the appropriate property type from the output map of the vertex shader, thus 'merging' the output maps.

An output mask that specifies to which components of the output register should the property be wired to is also accepted. If the output register is explicitly specified, it attaches to it (e.g. `o2.xy`); otherwise it attaches to the property name (e.g. `texcoord0.xy`).

Example:

```
.out outPos position
.out outClr color.rgba
.out outTex texcoord0.xy
.out -      texcoord0w outTex.p
```

### .entry
```
.entry procedureName
```
Specifies the name of the procedure to use as the entrypoint of the current DVLE. If this directive is not used, `main` is assumed.

### .nodvle
```
.nodvle
```
This directive tells `picasso` not to generate a DVLE for the source code file that is being processed. This allows for writing files that contain shared procedures to be used by other files.

### .gsh
```
.gsh point firstReg
.gsh variable firstReg vtxNum
.gsh fixed firstReg arrayStartReg vtxNum
```
This directive flags the current DVLE as a geometry shader and specifies the geometry shader operation mode, which can be one of the following:

- `point` mode: In this mode the geometry shader is called according to the input stride and input permutation configured by the user. On entry, the data is stored starting at the `v0` register. This type of geometry shader can be used with both array-drawing mode (aka `C3D_DrawArrays`) and element-drawing mode (aka `C3D_DrawElements`).
- `variable` mode (also called `subdivision` mode): In this mode the geometry shader processes variable-sized primitives, which are required to have `vtxNum` vertices for which full attribute information will be stored, and **one or more** additional vertices for which only position information will be stored. On entry the register `c0` stores in all its components the total number of vertices of the primitive, and subsequent registers store vertex information in order. This type of geometry shader can only used with element-drawing mode - inside the index array each primitive is prefixed with the number of vertices in it.
- `fixed` mode (also called `particle` mode): In this mode the geometry shader processes fixed-size primitives, which always have `vtxNum` vertices. On entry, the array of vertex information will be stored starting at the float uniform register `arrayStartReg`. This type of geometry shader can only used with element-drawing mode.

The `firstReg` parameter specifies the first float uniform register that is available for use in float uniform register allocation (this is especially useful in variable and fixed mode).

Examples:

```
.gsh point c0
.gsh variable c48 3
.gsh fixed c48 c0 4
```

**Note**: For backwards compatibility reasons, a legacy mode which does not accept any parameters is accepted; however it should not be used.

### .setf
```
.setf register(x, y, z, w)
```
Similar to `.constf`, this directive adds a DVLE constant entry for the specified floating-point vector uniform register to be loaded with the specified value. This is useful in order to instantiate a generalized shared procedure with the specified parameters.

### .seti
```
.seti register(x, y, z, w)
```
Similar to `.consti`, this directive adds a DVLE constant entry for the specified integer vector uniform register to be loaded with the specified value. This is useful in order to instantiate a generalized shared procedure with the specified parameters.

### .setb
```
.setb register value
```
This directive adds a DVLE constant entry for the specified boolean uniform register to be loaded with the specified value (which may be `true`, `false`, `on`, `off`, `1` or `0`). This is useful in order to control the flow of a generalized shared procedure.

## Supported Instructions

See [Shader Instruction Set](http://3dbrew.org/wiki/Shader_Instruction_Set) for more details.

Syntax                            | Description
--------------------------------- | -----------------------------------
`nop`                             | No operation.
`end`                             | Signals the end of the program.
`emit`                            | (Geoshader-only) Emits a vertex configured by a prior `setemit`.
`setemit vtxId, emitFlags`        | (Geoshader-only) Configures a vertex for emission. The `emitFlags` parameter can be omitted.
`add rDest, rSrc1, rSrc2`         |
`dp3 rDest, rSrc1, rSrc2`         |
`dp4 rDest, rSrc1, rSrc2`         |
`dph rDest, rSrc1, rSrc2`         |
`dst rDest, rSrc1, rSrc2`         |
`mul rDest, rSrc1, rSrc2`         |
`sge rDest, rSrc1, rSrc2`         |
`slt rDest, rSrc1, rSrc2`         |
`max rDest, rSrc1, rSrc2`         |
`min rDest, rSrc1, rSrc2`         |
`ex2 rDest, rSrc1`                |
`lg2 rDest, rSrc1`                |
`litp rDest, rSrc1`               |
`flr rDest, rSrc1`                |
`rcp rDest, rSrc1`                |
`rsq rDest, rSrc1`                |
`mov rDest, rSrc1`                |
`mova idxReg, rSrc1`              |
`cmp rSrc1, opx, opy, rSrc2`      |
`call procName`                   |
`for iReg`                        |
`break`                           | (not recommended)
`breakc condExp`                  |
`callc condExp, procName`         |
`ifc condExp`                     |
`jmpc condExp, labelName`         |
`callu bReg, procName`            |
`ifu bReg`                        |
`jmpu [!]bReg, labelName`         |
`mad rDest, rSrc1, rSrc2, rSrc3`  |

### Description of operands

- `rDest`: Represents a destination operand (register).
- `rSrc1`/`rSrc2`/`rSrc3`: Represents a source operand (register). Depending on the position, some registers may be supported and some may not.
	- Narrow source operands are limited to input and scratch registers.
	- Wide source operands also support floating-point vector uniforms and relative addressing.
	- In instructions that take one source operand, it is always wide.
	- In instructions that take two source operands, the first is wide and the second is narrow.
	- `dph`/`sge`/`slt` have a special form where the first operand is narrow and the second is wide. This usage is detected automatically by `picasso`.
	- `mad`, which takes three source operands, has two forms: the first is narrow-wide-narrow, and the second is narrow-narrow-wide. This is also detected automatically.
- `idxReg`: Represents an indexing register to write to using the mova instruction. Can be `a0`, `a1` or `a01` (the latter writes to both `a0` and `a1`).
- `iReg`: Represents an integer vector uniform source operand.
- `bReg`: Represents a boolean uniform source operand.
- `procName`: Represents the name of a procedure.
- `labelName`: Represents the name of a label.
- `opx` and `opy`: They represent a conditional operator that is applied to the source registers and whose result is stored in the appropriate flag (`cmp.x` and `cmp.y` respectively). Supported values include:
	- `eq`: Equal
	- `ne`: Not equal
	- `lt`: Less than
	- `le`: Less or equal than
	- `gt`: Greater than
	- `ge`: Greater or equal than
- `condExp`: Represents a conditional expression, which uses the conditional flags `cmp.x` and `cmp.y` set by the CMP instruction. These flags may be negated using the `!` symbol, e.g. `!cmp.x`. The conditional expression can take any of the following forms:
	- `flag1`: It tests a single flag.
	- `flag1 && flag2`: It performs AND between the two flags. Optionally, a single `&` may be specified.
	- `flag1 || flag2`: It performs OR between the two flags. Optionally, a single `|` may be specified.
- `vtxId`: An integer ranging from 0 to 2 specifying the vertex ID used in geoshader vertex emission.
- `emitFlags`: A space delimited combination of the following words:
	- `prim` (or `primitive`): Specifies that after emitting the vertex, a primitive should also be emitted.
	- `inv` (or `invert`): Specifies that the order of the vertices in the emitted primitive is inverted.
