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
};

//-----------------------------------------------------------------------------
// Global data
//-----------------------------------------------------------------------------

// Output buffer
#define MAX_VSH_SIZE 512
typedef std::vector<u32> outputBufType;
typedef outputBufType::iterator outputBufIter;
extern outputBufType g_outputBuf;

enum
{
	SE_PROC,
	SE_FOR,
	SE_IF,
	SE_ARRAY,
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

	inline bool operator <(const Uniform& rhs) const
	{
		return pos < rhs.pos;
	}

	void init(const char* name, int pos, int size, int type)
	{
		this->name = name;
		this->pos = pos;
		this->size = size;
		this->type = type;
	}
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

extern bool g_autoNop;

int AssembleString(char* str, const char* initialFilename);
int RelocateProduct(void);

//-----------------------------------------------------------------------------
// Local data
//-----------------------------------------------------------------------------

enum
{
	OUTTYPE_POS      = 0,
	OUTTYPE_NQUAT    = 1,
	OUTTYPE_CLR      = 2,
	OUTTYPE_TCOORD0  = 3,
	OUTTYPE_TCOORD0W = 4,
	OUTTYPE_TCOORD1  = 5,
	OUTTYPE_TCOORD2  = 6,
	OUTTYPE_VIEW     = 8,
	OUTTYPE_DUMMY    = 9,
};

enum
{
	GSHTYPE_POINT    = 0,
	GSHTYPE_VARIABLE = 1,
	GSHTYPE_FIXED    = 2,
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
	bool nodvle, isGeoShader, isCompatGeoShader, isMerge;
	u16 inputMask, outputMask;
	u8 geoShaderType;
	u8 geoShaderFixedStart;
	u8 geoShaderVariableNum;
	u8 geoShaderFixedNum;

	// Uniforms
	Uniform uniformTable[MAX_UNIFORM];
	int uniformCount;
	size_t symbolSize;

	// Constants
	#define MAX_CONSTANT 0x60
	Constant constantTable[MAX_CONSTANT];
	int constantCount;

	// Outputs
	#define MAX_OUTPUT 16
	u64 outputTable[MAX_OUTPUT];
	u32 outputUsedReg;
	int outputCount;

	bool usesGshSpace() { return isGeoShader && !isCompatGeoShader; }
	int findFreeOutput()
	{
		for (int i = 0; i < maxOutputReg(); i ++)
			if (!(outputMask & BIT(i)))
				return i;
		return -1;
	}

	int findFreeInput()
	{
		for (int i = 0; i < 16; i ++)
			if (!(inputMask & BIT(i)))
				return i;
		return -1;
	}

	int maxOutputReg() const
	{
		return isGeoShader ? 0x07 : 0x10;
	}

	DVLEData(const char* filename) :
		filename(filename), entrypoint("main"),
		nodvle(false), isGeoShader(false), isCompatGeoShader(false), isMerge(false),
		inputMask(0), outputMask(0), geoShaderType(0), geoShaderFixedStart(0), geoShaderVariableNum(0), geoShaderFixedNum(0),
		uniformCount(0), symbolSize(0), constantCount(0), outputUsedReg(0), outputCount(0) { }
};
