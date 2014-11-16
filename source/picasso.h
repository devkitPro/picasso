#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef WIN32
#include <fcntl.h>
#endif
#include "types.h"

#include <vector>
#include <list>
#include <map>
#include <string>
#include <algorithm>

#include "FileClass.h"

#include "maestro_opcodes.h"

enum
{
	COMP_X = 0,
	COMP_Y,
	COMP_Z,
	COMP_W,
};

#define SWIZZLE_COMP(n,v) ((v) << (6-(n)*2))
#define OPDESC_MAKE(out, src1, src2) ((out) | ((src1) << 5) | ((src2) << (5+8+1)))
#define FMT_OPCODE(n) ((n)<<26)
#define OUTPUT_MAKE(i, reg, mask) ((i) | ((reg)<<16) | ((u64)(mask)<<32))

#define DEFAULT_SWIZZLE (SWIZZLE_COMP(0,COMP_X) | SWIZZLE_COMP(1,COMP_Y) | SWIZZLE_COMP(2,COMP_Z) | SWIZZLE_COMP(3,COMP_W))

extern std::vector<u32> g_outputBuf;

enum
{
	SE_PROC,
	SE_IFB,
};

struct StackEntry
{
	int type;
	size_t pos;
	union
	{
		const char* strExtra;
		size_t uExtra;
	};
};

// Stack used to keep track of stuff.
#define MAX_STACK 32
extern StackEntry g_stack[MAX_STACK];
extern int g_stackPos;

#define MAX_OPDESC 128
extern int g_opdescTable[MAX_OPDESC];
extern int g_opdescCount;

struct Uniform
{
	const char* name;
	int pos, size;
};

#define MAX_UNIFORM 0x60
extern Uniform g_uniformTable[MAX_UNIFORM];
extern int g_uniformCount;

enum
{
	OUTTYPE_POS = 0,
	OUTTYPE_CLR = 2,
	OUTTYPE_TCOORD0,
	OUTTYPE_TCOORD1 = 5,
	OUTTYPE_TCOORD2,
};

#define MAX_OUTPUT 8
extern u64 g_outputTable[MAX_OUTPUT];
extern int g_outputCount;

struct Constant
{
	int regId;
	float param[4];
};

#define MAX_CONSTANT 0x60
extern Constant g_constantTable[MAX_CONSTANT];
extern int g_constantCount;

typedef std::pair<size_t, size_t> procedure; // position, size
typedef std::pair<size_t, const char*> relocation;
extern std::map<std::string, procedure> g_procTable;
extern std::map<std::string, size_t> g_labels;
extern std::map<std::string, int> g_aliases;

int AssembleString(char* str, const char* initialFilename);
