#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <getopt.h>
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

#if !defined(WIN32) && !defined(stricmp)
#define stricmp strcasecmp
#endif

enum
{
	COMP_X = 0,
	COMP_Y,
	COMP_Z,
	COMP_W,
};

#define SWIZZLE_COMP(n,v) ((v) << (6-(n)*2))
#define OPSRC_MAKE(neg, sw) ((neg) | ((sw) << 1))
#define OPDESC_MAKE(out, src1, src2, src3) ((out) | ((src1) << 4) | ((src2) << (4+9)) | ((src3) << (4+9*2)))
#define FMT_OPCODE(n) ((n)<<26)
#define OUTPUT_MAKE(i, reg, mask) ((i) | ((reg)<<16) | ((u64)(mask)<<32))

#define DEFAULT_SWIZZLE (SWIZZLE_COMP(0,COMP_X) | SWIZZLE_COMP(1,COMP_Y) | SWIZZLE_COMP(2,COMP_Z) | SWIZZLE_COMP(3,COMP_W))
#define DEFAULT_OPSRC OPSRC_MAKE(0, DEFAULT_SWIZZLE)

#define OPDESC_MASK_D123 OPDESC_MAKE(0xF, 0x1FF, 0x1FF, 0x1FF)
#define OPDESC_MASK_D12  OPDESC_MAKE(0xF, 0x1FF, 0x1FF, 0)
#define OPDESC_MASK_D1   OPDESC_MAKE(0xF, 0x1FF, 0,     0)
#define OPDESC_MASK_1    OPDESC_MAKE(0,   0x1FF, 0,     0)
#define OPDESC_MASK_12   OPDESC_MAKE(0,   0x1FF, 0x1FF, 0)

enum
{
	COND_EQ = 0,
	COND_NE,
	COND_LT,
	COND_LE,
	COND_GT,
	COND_GE,
	COND_UNK1,
	COND_UNK2,
};

//-----------------------------------------------------------------------------
// Global data
//-----------------------------------------------------------------------------

// Output buffer
typedef std::vector<u32> outputBufType;
typedef outputBufType::iterator outputBufIter;
extern outputBufType g_outputBuf;

enum
{
	SE_PROC,
	SE_FOR,
	SE_IF,
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

// Operand descriptor stuff.
#define MAX_OPDESC 128
extern int g_opdescTable[MAX_OPDESC];
extern int g_opdeskMasks[MAX_OPDESC]; // used to keep track of used bits
extern int g_opdescCount;

enum
{
	UTYPE_BOOL = 0,
	UTYPE_IVEC,
	UTYPE_FVEC,
};

struct Uniform
{
	std::string name;
	int pos, size;
	int type;
};

// List of uniforms
#define MAX_UNIFORM 0x60
extern Uniform g_uniformTable[MAX_UNIFORM];
extern int g_uniformCount;

struct DVLEData; // Forward declaration

typedef std::pair<size_t, size_t> procedure; // position, size
typedef std::pair<size_t, std::string> relocation; // position, name

typedef std::map<std::string, procedure> procTableType;
typedef std::map<std::string, size_t> labelTableType;
typedef std::map<std::string, int> aliasTableType;
typedef std::vector<relocation> relocTableType;
typedef std::list<DVLEData> dvleTableType;

typedef procTableType::iterator procTableIter;
typedef labelTableType::iterator labelTableIter;
typedef aliasTableType::iterator aliasTableIter;
typedef relocTableType::iterator relocTableIter;
typedef dvleTableType::iterator dvleTableIter;

extern procTableType g_procTable;
extern dvleTableType g_dvleTable;
extern relocTableType g_procRelocTable;
extern int g_totalDvleCount;

// The following are cleared before each file is processed
extern labelTableType g_labels;
extern relocTableType g_labelRelocTable;
extern aliasTableType g_aliases;

int AssembleString(char* str, const char* initialFilename);
int RelocateProduct(void);

//-----------------------------------------------------------------------------
// Local data
//-----------------------------------------------------------------------------

enum
{
	OUTTYPE_POS = 0,
	OUTTYPE_NQUAT,
	OUTTYPE_CLR,
	OUTTYPE_TCOORD0,
	OUTTYPE_TCOORD0W,
	OUTTYPE_TCOORD1,
	OUTTYPE_TCOORD2,
	OUTTYPE_7,
	OUTTYPE_VIEW,
};

struct Constant
{
	int regId;
	int type;
	union
	{
		float fparam[4];
		u8 iparam[4];
		bool bparam;
	};
};

struct DVLEData
{
	// General config
	std::string filename;
	std::string entrypoint;
	size_t entryStart, entryEnd;
	bool nodvle, isGeoShader;

	// Uniforms
	Uniform uniformTable[MAX_UNIFORM];
	int uniformCount;
	size_t symbolSize;

	// Constants
	#define MAX_CONSTANT 0x60
	Constant constantTable[MAX_CONSTANT];
	int constantCount;

	// Outputs
	#define MAX_OUTPUT 8
	u64 outputTable[MAX_OUTPUT];
	int outputCount;

	DVLEData(const char* filename) :
		filename(filename), entrypoint("main"),
		nodvle(false), isGeoShader(false),
		uniformCount(0), symbolSize(0), constantCount(0), outputCount(0) { }
};
