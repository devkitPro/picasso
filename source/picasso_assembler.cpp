#include "picasso.h"

//#define DEBUG
#define BUF g_outputBuf
#define NO_MORE_STACK (g_stackPos==MAX_STACK)

static const char* curFile = NULL;
static int curLine = -1;
static bool lastWasEnd = false;

std::vector<u32> g_outputBuf;

StackEntry g_stack[MAX_STACK];
int g_stackPos;

int g_opdescTable[MAX_OPDESC];
int g_opdescCount;
int g_opdescMasks[MAX_OPDESC];
u32 g_opdescIsMad;

Uniform g_uniformTable[MAX_UNIFORM];
int g_uniformCount;

std::vector<Constant> g_constArray;
int g_constArraySize = -1;
const char* g_constArrayName;

bool g_autoNop = true;

class UniformAlloc
{
	int start, end, bound, tend;
public:
	UniformAlloc(int start, int end) : start(start), end(end), bound(end), tend(end) { }
	void ClearLocal(void) { end = tend; }
	void Reinit(int start, int end)
	{
		this->start = start;
		this->end = end;
		this->bound = end;
		this->tend = end;
	}
	int AllocGlobal(int size)
	{
		if ((start+size) > bound) return -1;
		int ret = start;
		start += size;
		return ret;
	}
	int AllocLocal(int size)
	{
		int pos = end - size;
		if (pos < start) return -1;
		bound = pos < bound ? pos : bound;
		end = pos;
		return pos;
	}
};

struct UniformAllocBundle
{
	UniformAlloc fvecAlloc, ivecAlloc, boolAlloc;

	UniformAllocBundle() :
		fvecAlloc(0x20, 0x80), ivecAlloc(0x80, 0x84), boolAlloc(0x88, 0x98) { }

	void clear()
	{
		fvecAlloc.ClearLocal();
		ivecAlloc.ClearLocal();
		boolAlloc.ClearLocal();
	}

	void initForGsh(int firstFree)
	{
		fvecAlloc.Reinit(firstFree, 0x80);
		ivecAlloc.Reinit(0x80, 0x84);
		boolAlloc.Reinit(0x88, 0x97);
	}
};

static UniformAllocBundle unifAlloc[2];

static inline UniformAlloc& getAlloc(int type, DVLEData* dvle)
{
	int x = dvle->usesGshSpace();
	switch (type)
	{
		default:
		case UTYPE_FVEC: return unifAlloc[x].fvecAlloc;
		case UTYPE_IVEC: return unifAlloc[x].ivecAlloc;
		case UTYPE_BOOL: return unifAlloc[x].boolAlloc;
	}
}

procTableType g_procTable;
dvleTableType g_dvleTable;
relocTableType g_procRelocTable;
int g_totalDvleCount;

labelTableType g_labels;
relocTableType g_labelRelocTable;
aliasTableType g_aliases;

static DVLEData* curDvle;

static void ClearStatus(void)
{
	unifAlloc[0].clear();
	g_labels.clear();
	g_labelRelocTable.clear();
	g_aliases.clear();
	curDvle = NULL;
}

static DVLEData* GetDvleData(void)
{
	if (!curDvle)
	{
		g_dvleTable.push_back( DVLEData(curFile) );
		curDvle = &g_dvleTable.back();
		g_totalDvleCount ++;
	}
	return curDvle;
}

static char* mystrtok_pos;
static char* mystrtok(char* str, const char* delim)
{
	if (!str) str = mystrtok_pos;
	if (!*str) return NULL;

	size_t pos = strcspn(str, delim);
	char* ret = str;
	str += pos;
	if (*str)
		*str++ = 0;
	mystrtok_pos = str;
	return ret;
}

static char* mystrtok_spc(char* str)
{
	char* ret = mystrtok(str, " \t");
	if (!ret) return NULL;
	if (*mystrtok_pos)
		for (; *mystrtok_pos && isspace(*mystrtok_pos); mystrtok_pos++);
	return ret;
}

static char* remove_comment(char* buf)
{
	char* pos = strchr(buf, ';');
	if (pos) *pos = 0;
	return buf;
}

static char* trim_whitespace(char* buf)
{
	if (!buf)
		return NULL;

	// Remove trailing whitespace
	int pos;
	for(pos = strlen(buf)-1; pos >= 0 && isspace(buf[pos]); pos --) buf[pos] = '\0';

	// Remove leading whitespace
	char* newbuf = buf;
	for(; isspace(*newbuf); newbuf ++);

	return newbuf;
}

static bool validateIdentifier(const char* id)
{
	int len = strlen(id);
	bool valid = true;
	for (int i = 0; valid && i < len; i ++)
	{
		int c = id[i];
		valid = isalpha(c) || c == '_' || c == '$' || (i > 0 && isdigit(c));
	}
	return valid;
}

static int throwError(const char* msg, ...)
{
	va_list v;

	fprintf(stderr, "%s:%d: error: ", curFile, curLine);

	va_start(v, msg);
	vfprintf(stderr, msg, v);
	va_end(v);

	return 1;
}

static int parseInt(char* pos, int& out, long long min, long long max)
{
	char* endptr = NULL;
	long long res = strtoll(pos, &endptr, 0);
	if (pos == endptr)
		return throwError("Invalid value: %s\n", pos);
	if (res < min || res > max)
		return throwError("Value out of range (%d..%u): %d\n", (int)min, (unsigned int)max, (int)res);
	out = res;
	return 0;
}

#define safe_call(x) do \
	{ \
		int _ = (x); \
		if (_ != 0) return _; \
	} while(0)

static int ProcessCommand(const char* cmd);
static int FixupLabelRelocations();

int AssembleString(char* str, const char* initialFilename)
{
	curFile = initialFilename;
	curLine = 1;

	ClearStatus();

	int nextLineIncr = 0;
	char* nextStr = NULL;
	for (; str; str = nextStr, curLine += nextLineIncr)
	{
		size_t len = strcspn(str, "\n");
		int linedelim = str[len];
		str[len] = 0;
		nextStr = linedelim ? (str + len + 1) : NULL;
		nextLineIncr = linedelim == '\n' ? 1 : 0;

		char* line = trim_whitespace(remove_comment(str));

		char* colonPos = NULL;
		for (;;)
		{
			colonPos = strchr(line, ':');
			if (!colonPos)
				break;
			*colonPos = 0;
			char* labelName = line;
			line = trim_whitespace(colonPos + 1);

			if (!validateIdentifier(labelName))
				return throwError("invalid label name: %s\n", labelName);

			std::pair<labelTableIter,bool> ret = g_labels.insert( std::pair<std::string,size_t>(labelName, BUF.size()) );
			if (!ret.second)
				return throwError("duplicate label: %s\n", labelName);

			//printf("Label: %s\n", labelName);
		};

		if (!*line)
			continue;

		if (*line == '#')
		{
			line = trim_whitespace(line + 1);
			nextLineIncr = 0;
			size_t pos = strcspn(line, " \t");
			line[pos] = 0;
			curLine = atoi(line);
			line = trim_whitespace(line + pos + 1);
			if (*line == '"')
			{
				line ++;
				line[strlen(line)-1] = 0;
			}
			curFile = line;
			continue;
		}

		char* tok = mystrtok_spc(line);
		safe_call(ProcessCommand(tok));
	}

	if (g_stackPos)
		return throwError("unclosed block(s)\n");

	safe_call(FixupLabelRelocations());
	
	return 0;
}

int FixupLabelRelocations()
{
	for (relocTableIter it = g_labelRelocTable.begin(); it != g_labelRelocTable.end(); ++it)
	{
		relocation& r = *it;
		u32& inst = BUF[r.first];
		labelTableIter lbl = g_labels.find(r.second);
		if (lbl == g_labels.end())
			return throwError("label '%s' is undefined\n", r.second.c_str());
		u32 dst = lbl->second;
		inst &= ~(0xFFF << 10);
		inst |= dst << 10;
	}
	return 0;
}

int RelocateProduct()
{
	for (relocTableIter it = g_procRelocTable.begin(); it != g_procRelocTable.end(); ++it)
	{
		relocation& r = *it;
		u32& inst = BUF[r.first];
		procTableIter proc = g_procTable.find(r.second);
		if (proc == g_procTable.end())
			return throwError("procedure '%s' is undefined\n", r.second.c_str());
		u32 dst = proc->second.first;
		u32 num = proc->second.second;
		inst &= ~0x3FFFFF;
		inst |= num | (dst << 10);
	}

	if (g_totalDvleCount == 0)
		return throwError("no DVLEs can be generated from the given input file(s)\n");

	for (dvleTableIter it = g_dvleTable.begin(); it != g_dvleTable.end(); ++it)
	{
		if (it->nodvle) continue;
		curFile = it->filename.c_str();
		curLine = 1;
		procTableIter mainIt = g_procTable.find(it->entrypoint);
		if (mainIt == g_procTable.end())
			return throwError("entrypoint '%s' is undefined\n", it->entrypoint.c_str());
		it->entryStart = mainIt->second.first;
		it->entryEnd = it->entryStart + mainIt->second.second;
	}
	return 0;
}

// --------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------

static char* nextArg()
{
	return trim_whitespace(mystrtok(NULL, ","));
}

static char* nextArgCParen()
{
	return trim_whitespace(mystrtok(NULL, "("));
}

static char* nextArgSpc()
{
	return trim_whitespace(mystrtok_spc(NULL));
}

static int missingParam()
{
	return throwError("missing parameter\n");
}

typedef struct
{
	const char* name;
	int (* func) (const char*, int, int);
	int opcode, opcodei;
} cmdTableType;

#define NEXT_ARG(_varName) char* _varName; do \
	{ \
		_varName = nextArg(); \
		if (!_varName) return missingParam(); \
	} while (0)

#define NEXT_ARG_SPC(_varName) char* _varName; do \
	{ \
		_varName = nextArgSpc(); \
		if (!_varName) return missingParam(); \
	} while (0)

#define NEXT_ARG_CPAREN(_varName) char* _varName; do \
	{ \
		_varName = nextArgCParen(); \
		if (!_varName) return missingParam(); \
	} while (0)

#define NEXT_ARG_OPT(_varName, _opt) char* _varName; do \
	{ \
		_varName = nextArg(); \
		if (!_varName) _varName = (char*)(_opt); \
	} while (0)

#define DEF_COMMAND(name) \
	static int cmd_##name(const char* cmdName, int opcode, int opcodei)

#define DEC_COMMAND(name, fun) \
	{ #name, cmd_##fun, MAESTRO_##name, -1 }

#define DEC_COMMAND2(name, fun) \
	{ #name, cmd_##fun, MAESTRO_##name, MAESTRO_##name##I }, \
	{ #name "i", cmd_##fun, MAESTRO_##name, MAESTRO_##name##I }

#define DEF_DIRECTIVE(name) \
	static int dir_##name(const char* cmdName, int dirParam, int _unused)

#define DEC_DIRECTIVE(name) \
	{ #name, dir_##name, 0, 0 }

#define DEC_DIRECTIVE2(name, fun, opc) \
	{ #name, dir_##fun, opc, 0 }

static int ensureNoMoreArgs()
{
	return nextArg() ? throwError("too many parameters\n") : 0;
}

static int duplicateIdentifier(const char* id)
{
	return throwError("identifier already used: %s\n", id);
}

static int ensureTarget(const char* target)
{
	if (!validateIdentifier(target))
		return throwError("invalid target: %s\n", target);
	return 0;
}

static inline int ensure_valid_dest(int reg, const char* name)
{
	if (reg < 0x00 || reg >= 0x20)
		return throwError("invalid destination register: %s\n", name);
	return 0;
}

static inline int ensure_valid_src_wide(int reg, const char* name, int srcId)
{
	if (reg < 0x00 || reg >= 0x80)
		return throwError("invalid source%d register: %s\n", srcId, name);
	return 0;
}

static inline int ensure_valid_src_narrow(int reg, const char* name, int srcId)
{
	if (reg < 0x00 || reg >= 0x20)
		return throwError("invalid source%d register: %s\n", srcId, name);
	return 0;
}

static inline int ensure_no_idxreg(int idxreg, int srcId)
{
	if (idxreg > 0)
		return throwError("index register not allowed in source%d\n", srcId);
	return 0;
}

static inline int ensure_valid_ireg(int reg, const char* name)
{
	if (reg < 0x80 || reg >= 0x88)
		return throwError("invalid integer vector uniform: %s\n", name);
	return 0;
}

static inline int ensure_valid_breg(int reg, const char* name)
{
	if (reg < 0x88 || reg >= 0x98)
		return throwError("invalid boolean uniform: %s\n", name);
	return 0;
}

static inline int ensure_valid_condop(int condop, const char* name)
{
	if (condop < 0)
		return throwError("invalid conditional operator: %s\n", name);
	return 0;
}

#define ENSURE_NO_MORE_ARGS() safe_call(ensureNoMoreArgs())

#define ARG_TO_INT(_varName, _argName, _min, _max) \
	int _varName = 0; \
	safe_call(parseInt(_argName, _varName, _min, _max))

#define ARG_TO_REG(_varName, _argName) \
	int _varName = 0, _varName##Sw = 0; \
	safe_call(parseReg(_argName, _varName, _varName##Sw));

#define ARG_TO_REG2(_varName, _argName) \
	int _varName = 0, _varName##Sw = 0, _varName##Idx = 0; \
	safe_call(parseReg(_argName, _varName, _varName##Sw, &_varName##Idx));

#define ARG_TO_CONDOP(_varName, _argName) \
	int _varName = parseCondOp(_argName); \
	safe_call(ensure_valid_condop(_varName, _argName))

#define ARG_TARGET(_argName) \
	safe_call(ensureTarget(_argName))

#define ARG_TO_DEST_REG(_reg, _name) \
	ARG_TO_REG(_reg, _name); \
	safe_call(ensure_valid_dest(_reg, _name))

#define ARG_TO_SRC1_REG(_reg, _name) \
	ARG_TO_REG(_reg, _name); \
	safe_call(ensure_valid_src_wide(_reg, _name, 1))

#define ARG_TO_SRC1_REG2(_reg, _name) \
	ARG_TO_REG2(_reg, _name); \
	safe_call(ensure_valid_src_wide(_reg, _name, 1))

#define ARG_TO_SRC2_REG(_reg, _name) \
	ARG_TO_REG(_reg, _name); \
	safe_call(ensure_valid_src_narrow(_reg, _name, 2))

#define ARG_TO_IREG(_reg, _name) \
	ARG_TO_REG(_reg, _name); \
	safe_call(ensure_valid_ireg(_reg, _name))

#define ARG_TO_BREG(_reg, _name) \
	ARG_TO_REG(_reg, _name); \
	safe_call(ensure_valid_breg(_reg, _name))

static int parseSwizzling(const char* b)
{
	int i, out = 0, q = COMP_X;
	for (i = 0; b[i] && i < 4; i ++)
	{
		switch (tolower(b[i]))
		{
			case 'x': case 'r': case 's': q = COMP_X; break;
			case 'y': case 'g': case 't': q = COMP_Y; break;
			case 'z': case 'b': case 'p': q = COMP_Z; break;
			case 'w': case 'a': case 'q': q = COMP_W; break;
			default: return -1;
		}
		out |= SWIZZLE_COMP(i, q);
	}
	if (b[i])
		return -1;
	// Fill in missing bits
	for (int j = i; j < 4; j ++)
		out |= SWIZZLE_COMP(j, q);
	return out<<1;
}

static int maskFromSwizzling(int sw, bool reverse = true)
{
	sw >>= 1; // get rid of negation bit
	int out = 0;
	for (int i = 0; i < 4; i ++)
	{
		int bitid = (sw>>(i*2))&3;
		if (reverse) bitid = 3 - bitid;
		out |= BIT(bitid);
	}
	return out;
}

static void optimizeOpdesc(int& mask, int opcode, int opdesc)
{
	int unused1 = 0, unused2 = 0, unused3 = 0;
	bool optimize = false;

	switch (opcode)
	{
		case MAESTRO_ADD:
		case MAESTRO_MUL:
		case MAESTRO_SGE:
		case MAESTRO_SLT:
		case MAESTRO_FLR:
		case MAESTRO_MAX:
		case MAESTRO_MIN:
		case MAESTRO_MOV:
		case MAESTRO_MAD:
			for (int i = 0; i < 4; i ++)
				if (!(opdesc & BIT(3-i)))
					unused1 |= SWIZZLE_COMP(i,3);
			unused2 = unused1;
			unused3 = unused1;
			break;

		case MAESTRO_DP3:
			unused1 = SWIZZLE_COMP(3,3);
			unused2 = SWIZZLE_COMP(3,3);
			break;

		case MAESTRO_DPH:
			unused1 = SWIZZLE_COMP(3,3);
			break;

		case MAESTRO_EX2:
		case MAESTRO_LG2:
		case MAESTRO_RCP:
		case MAESTRO_RSQ:
			unused1 = SWIZZLE_COMP(1,3) | SWIZZLE_COMP(2,3) | SWIZZLE_COMP(3,3);
			break;

		case MAESTRO_MOVA:
			if (!(opdesc & BIT(3-COMP_X))) unused1 |= SWIZZLE_COMP(0,3);
			if (!(opdesc & BIT(3-COMP_Y))) unused1 |= SWIZZLE_COMP(1,3);
		case MAESTRO_CMP:
			unused1 |= SWIZZLE_COMP(2,3) | SWIZZLE_COMP(3,3);
			break;
	}

	mask &= ~OPDESC_MAKE(0,OPSRC_MAKE(0,unused1),OPSRC_MAKE(0,unused2),OPSRC_MAKE(0,unused3));
}

static int findOrAddOpdesc(int opcode, int& out, int opdesc, int mask)
{
	optimizeOpdesc(mask, opcode, opdesc);

	for (int i = 0; i < g_opdescCount; i ++)
	{
		int minMask = mask & g_opdescMasks[i];
		if ((opdesc&minMask) == (g_opdescTable[i]&minMask))
		{
			// Update opdesc to include extra bits (if any)
			g_opdescTable[i] = (g_opdescTable[i]&~mask) | (opdesc & mask);
			g_opdescMasks[i] |= mask;
			out = i;
			return 0;
		}
	}
	if (g_opdescCount == MAX_OPDESC)
		return throwError("too many operand descriptors (limit is %d)\n", MAX_OPDESC);
	g_opdescTable[g_opdescCount] = opdesc;
	g_opdescMasks[g_opdescCount] = mask;
	out = g_opdescCount++;
	return 0;
}

static void swapOpdesc(u32 from, u32 to)
{
	std::swap(g_opdescTable[from], g_opdescTable[to]);
	std::swap(g_opdescMasks[from], g_opdescMasks[to]);
	for (size_t i = 0; i < BUF.size(); i ++)
	{
		u32& opword = BUF[i];
		u32 opcode = opword>>26;
		if (opcode < 0x20 || (opcode&~1)==MAESTRO_CMP)
		{
			u32 cur_opdesc = opword & 0x7F;
			if (cur_opdesc==from)
				cur_opdesc=to;
			else if (cur_opdesc==to)
				cur_opdesc=from;
			opword = (opword &~ 0x7F) | cur_opdesc;
		}
	}
}

static inline bool isregp(int x)
{
	x = tolower(x);
	return x=='o' || x=='v' || x=='r' || x=='c' || x=='i' || x=='b';
}

static inline int convertIdxRegName(const char* reg)
{
	if (stricmp(reg, "a0")==0) return 1;
	if (stricmp(reg, "a1")==0) return 2;
	if (stricmp(reg, "a2")==0 || stricmp(reg, "lcnt")==0) return 3;
	return 0;
}

static inline int parseCondOp(const char* name)
{
	if (stricmp(name, "eq")==0) return COND_EQ;
	if (stricmp(name, "ne")==0) return COND_NE;
	if (stricmp(name, "lt")==0) return COND_LT;
	if (stricmp(name, "le")==0) return COND_LE;
	if (stricmp(name, "gt")==0) return COND_GT;
	if (stricmp(name, "ge")==0) return COND_GE;
	return -1;
}

static int parseReg(char* pos, int& outReg, int& outSw, int* idxType = NULL)
{
	outReg = 0;
	outSw = DEFAULT_OPSRC;
	if (idxType) *idxType = 0;
	if (*pos == '-')
	{
		pos++;
		outSw |= 1; // negation bit
	}
	char* dotPos = strchr(pos, '.');
	if (dotPos)
	{
		*dotPos++ = 0;
		outSw = parseSwizzling(dotPos) | (outSw&1);
		if (outSw < 0)
			return throwError("invalid swizzling mask: %s\n", dotPos);
	}
	int regOffset = 0;
	char* offPos = strchr(pos, '[');
	if (offPos)
	{
		char* closePos = strchr(offPos, ']');
		if (!closePos)
			return throwError("missing closing bracket: %s\n", pos);
		*closePos = 0;
		*offPos++ = 0;
		offPos = trim_whitespace(offPos);

		// Check for idxreg+offset
		int temp = convertIdxRegName(offPos);
		if (temp>0)
		{
			if (!idxType)
				return throwError("index register not allowed here: %s\n", offPos);
			*idxType = temp;
		} else do
		{
			char* plusPos = strchr(offPos, '+');
			if (!plusPos)
				break;
			if (!idxType)
				return throwError("index register not allowed here: %s\n", offPos);
			*plusPos++ = 0;
			char* idxRegName = trim_whitespace(offPos);
			offPos = trim_whitespace(plusPos);
			*idxType = convertIdxRegName(idxRegName);
			if (*idxType < 0)
				return throwError("invalid index register: %s\n", idxRegName);
		} while (0);

		regOffset = atoi(offPos);
		if (regOffset < 0)
			return throwError("invalid register offset: %s\n", offPos);
	}
	aliasTableIter it = g_aliases.find(pos);
	if (it != g_aliases.end())
	{
		int x = it->second;
		outReg = x & 0xFF;
		outReg += regOffset;
		outSw ^= (x>>8)&1;
		x >>= 9;
		// Combine swizzling
		int temp = outSw & 1;
		for (int j = 0; j < 4; j ++)
		{
			int comp = (outSw >> (7 - j*2)) & 3;
			comp = (x >> (6 - comp*2)) & 3;
			temp |= SWIZZLE_COMP(j, comp)<<1;
		}
		outSw = temp;
		return 0;
	}

	if (!isregp(pos[0]) || !isdigit(pos[1]))
		return throwError("invalid register: %s\n", pos);

	safe_call(parseInt(pos+1, outReg, 0, 255));
	switch (*pos)
	{
		case 'o': // Output registers
			if (outReg < 0x00 || outReg >= GetDvleData()->maxOutputReg())
				return throwError("invalid output register: %s\n", pos);
			break;
		case 'v': // Input attributes
			if (outReg < 0x00 || outReg >= 0x0F)
				return throwError("invalid input register: %s\n", pos);
			break;
		case 'r': // Temporary registers
			outReg += 0x10;
			if (outReg < 0x10 || outReg >= 0x20)
				return throwError("invalid temporary register: %s\n", pos);
			break;
		case 'c': // Floating-point vector uniform registers
			outReg += 0x20;
			if (outReg < 0x20 || outReg >= 0x80)
				return throwError("invalid floating-point vector uniform register: %s\n", pos);
			break;
		case 'i': // Integer vector uniforms
			outReg += 0x80;
			if (outReg < 0x80 || outReg >= 0x88)
				return throwError("invalid integer vector uniform register: %s\n", pos);
			break;
		case 'b': // Boolean uniforms
			outReg += 0x88;
			if (outReg < 0x88 || outReg >= 0x98)
				return throwError("invalid boolean uniform register: %s\n", pos);
			break;
	}
	outReg += regOffset;
	return 0;
}

static int parseCondExpOp(char* str, u32& outFlags, int& which)
{
	int negation = 0;
	for (; *str == '!'; str++) negation ^= 1;
	if (stricmp(str, "cmp.x")==0)
	{
		which = 0;
		outFlags ^= negation<<25;
		return 0;
	}
	if (stricmp(str, "cmp.y")==0)
	{
		which = 1;
		outFlags ^= negation<<24;
		return 0;
	}
	return throwError("invalid condition register: %s\n", str);
}

static int parseCondExp(char* str, u32& outFlags)
{
	outFlags = BIT(24) | BIT(25);
	size_t len = strlen(str);
	size_t pos = strcspn(str, "&|");
	int op2 = -1;
	if (pos < len)
	{
		char* str2 = str + pos;
		int type = *str2;
		*str2++ = 0;
		if (*str2 == type)
			str2++;
		str = trim_whitespace(str);
		str2 = trim_whitespace(str2);
		if (type == '&')
			outFlags |= 1<<22;
		safe_call(parseCondExpOp(str2, outFlags, op2));
	}
	int op1 = -1;
	safe_call(parseCondExpOp(str, outFlags, op1));
	if (op1 == op2)
		return throwError("condition register checked twice\n");
	if (op2 < 0)
		outFlags |= (op1+2)<<22;
	return 0;
}

static inline bool isBadInputRegCombination(int a, int b)
{
	return a < 0x10 && b < 0x10 && a != b;
}

static inline bool isBadInputRegCombination(int a, int b, int c)
{
	return isBadInputRegCombination(a,b) || isBadInputRegCombination(b,c) || isBadInputRegCombination(c,a);
}

static void insertPaddingNop()
{
	if (g_autoNop)
		BUF.push_back(FMT_OPCODE(MAESTRO_NOP));
	else
		fprintf(stderr, "%s:%d: warning: a padding NOP is required here\n", curFile, curLine);
}

DEF_COMMAND(format0)
{
	ENSURE_NO_MORE_ARGS();

	BUF.push_back(FMT_OPCODE(opcode));
	return 0;
}

DEF_COMMAND(format1)
{
	NEXT_ARG(destName);
	NEXT_ARG(src1Name);
	NEXT_ARG(src2Name);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_DEST_REG(rDest, destName);
	ARG_TO_REG2(rSrc1, src1Name);
	ARG_TO_REG2(rSrc2, src2Name);

	bool inverted = opcodei >= 0 && rSrc1 < 0x20 && rSrc2 >= 0x20;

	if (!inverted)
	{
		safe_call(ensure_valid_src_wide(rSrc1, src1Name, 1));
		safe_call(ensure_valid_src_narrow(rSrc2, src2Name, 2));
		safe_call(ensure_no_idxreg(rSrc2Idx, 2));
	} else
	{
		safe_call(ensure_valid_src_narrow(rSrc1, src1Name, 1));
		safe_call(ensure_no_idxreg(rSrc1Idx, 1));
		safe_call(ensure_valid_src_wide(rSrc2, src2Name, 2));
	}

	if (isBadInputRegCombination(rSrc1, rSrc2))
		return throwError("source operands must be different input registers (v0..v15)\n");

	int opdesc = 0;
	safe_call(findOrAddOpdesc(opcode, opdesc, OPDESC_MAKE(maskFromSwizzling(rDestSw), rSrc1Sw, rSrc2Sw, 0), OPDESC_MASK_D12));

#ifdef DEBUG
	printf("%s:%02X d%02X, d%02X, d%02X (0x%X)\n", cmdName, opcode, rDest, rSrc1, rSrc2, opdesc);
#endif
	if (!inverted)
		BUF.push_back(FMT_OPCODE(opcode)  | opdesc | (rSrc2<<7) | (rSrc1<<12) | (rSrc1Idx<<19) | (rDest<<21));
	else
		BUF.push_back(FMT_OPCODE(opcodei) | opdesc | (rSrc2<<7) | (rSrc1<<14) | (rSrc2Idx<<19) | (rDest<<21));

	return 0;
}

DEF_COMMAND(format1u)
{
	NEXT_ARG(destName);
	NEXT_ARG(src1Name);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_DEST_REG(rDest, destName);
	ARG_TO_SRC1_REG2(rSrc1, src1Name);

	int opdesc = 0;
	safe_call(findOrAddOpdesc(opcode, opdesc, OPDESC_MAKE(maskFromSwizzling(rDestSw), rSrc1Sw, 0, 0), OPDESC_MASK_D1));

#ifdef DEBUG
	printf("%s:%02X d%02X, d%02X (0x%X)\n", cmdName, opcode, rDest, rSrc1, opdesc);
#endif
	BUF.push_back(FMT_OPCODE(opcode) | opdesc | (rSrc1<<12) | (rSrc1Idx<<19) | (rDest<<21));

	return 0;
}

DEF_COMMAND(format1c)
{
	NEXT_ARG(src1Name);
	NEXT_ARG(cmpxName);
	NEXT_ARG(cmpyName);
	NEXT_ARG(src2Name);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_SRC1_REG2(rSrc1, src1Name);
	ARG_TO_CONDOP(cmpx, cmpxName);
	ARG_TO_CONDOP(cmpy, cmpyName);
	ARG_TO_SRC2_REG(rSrc2, src2Name);

	int opdesc = 0;
	safe_call(findOrAddOpdesc(opcode, opdesc, OPDESC_MAKE(0, rSrc1Sw, rSrc2Sw, 0), OPDESC_MASK_12));

#ifdef DEBUG
	printf("%s:%02X d%02X, %d, %d, d%02X (0x%X)\n", cmdName, opcode, rSrc1, cmpx, cmpy, rSrc2, opdesc);
#endif
	BUF.push_back(FMT_OPCODE(opcode) | opdesc | (rSrc2<<7) | (rSrc1<<12) | (rSrc1Idx<<19) | (cmpy<<21) | (cmpx<<24));

	return 0;
}

DEF_COMMAND(format5)
{
	NEXT_ARG(destName);
	NEXT_ARG(src1Name);
	NEXT_ARG(src2Name);
	NEXT_ARG(src3Name);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_DEST_REG(rDest, destName);
	ARG_TO_SRC2_REG(rSrc1, src1Name);
	ARG_TO_REG2(rSrc2, src2Name);
	ARG_TO_REG2(rSrc3, src3Name);

	bool inverted = opcodei >= 0 && rSrc2 < 0x20 && (rSrc3 >= 0x20 || (rSrc3Idx && !rSrc2Idx));

	if (!inverted)
	{
		safe_call(ensure_valid_src_wide(rSrc2, src2Name, 2));
		safe_call(ensure_valid_src_narrow(rSrc3, src3Name, 3));
		safe_call(ensure_no_idxreg(rSrc3Idx, 2));
	} else
	{
		safe_call(ensure_valid_src_narrow(rSrc2, src2Name, 2));
		safe_call(ensure_valid_src_wide(rSrc3, src3Name, 3));
		safe_call(ensure_no_idxreg(rSrc2Idx, 2));
	}

	if (isBadInputRegCombination(rSrc1, rSrc2, rSrc3))
		return throwError("source registers must be different input registers (v0..v15)\n");

	int opdesc = 0;
	safe_call(findOrAddOpdesc(opcode, opdesc, OPDESC_MAKE(maskFromSwizzling(rDestSw), rSrc1Sw, rSrc2Sw, rSrc3Sw), OPDESC_MASK_D123));

	if (opdesc >= 32)
	{
		int which;
		for (which = 0; which < 32; which ++)
			if (!(g_opdescIsMad & BIT(which)))
				break;
		if (which == 32)
			return throwError("opdesc allocation error\n");
		swapOpdesc(which, opdesc);
		opdesc = which;
	}

	g_opdescIsMad |= BIT(opdesc);

#ifdef DEBUG
	printf("%s:%02X d%02X, d%02X, d%02X, d%02X (0x%X)\n", cmdName, opcode, rDest, rSrc1, rSrc2, rSrc3, opdesc);
#endif
	if (!inverted)
		BUF.push_back(FMT_OPCODE(opcode)  | opdesc | (rSrc3<<5) | (rSrc2<<10) | (rSrc1<<17) | (rSrc2Idx<<22) | (rDest<<24));
	else
		BUF.push_back(FMT_OPCODE(opcodei) | opdesc | (rSrc3<<5) | (rSrc2<<12) | (rSrc1<<17) | (rSrc3Idx<<22) | (rDest<<24));

	return 0;
}

DEF_COMMAND(formatmova)
{
	NEXT_ARG(targetReg);
	NEXT_ARG(src1Name);
	ENSURE_NO_MORE_ARGS();

	int mask;
	if      (strcmp(targetReg, "a0")==0)  mask = BIT(3);
	else if (strcmp(targetReg, "a1")==0)  mask = BIT(2);
	else if (strcmp(targetReg, "a01")==0) mask = BIT(3) | BIT(2);
	else return throwError("invalid destination register for mova: %s\n", targetReg);

	ARG_TO_SRC1_REG2(rSrc1, src1Name);

	int opdesc = 0;
	safe_call(findOrAddOpdesc(opcode, opdesc, OPDESC_MAKE(mask, rSrc1Sw, 0, 0), OPDESC_MASK_D1));

#ifdef DEBUG
	printf("%s:%02X d%02X (0x%X)\n", cmdName, opcode, rSrc1, opdesc);
#endif
	BUF.push_back(FMT_OPCODE(opcode) | opdesc | (rSrc1<<12) | (rSrc1Idx<<19));

	return 0;
}

static inline int parseSetEmitFlags(char* flags, bool& isPrim, bool& isInv)
{
	isPrim = false;
	isInv = false;
	if (!flags)
		return 0;

	mystrtok_pos = flags;
	while (char* flag = mystrtok_spc(NULL))
	{
		if (stricmp(flag, "prim")==0 || stricmp(flag, "primitive")==0)
			isPrim = true;
		else if (stricmp(flag, "inv")==0 || stricmp(flag, "invert")==0)
			isInv = true;
		else
			throwError("unknown setemit flag: %s\n", flag);

	}
	return 0;
}

DEF_COMMAND(formatsetemit)
{
	NEXT_ARG(vtxIdStr);
	NEXT_ARG_OPT(flagStr, NULL);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_INT(vtxId, vtxIdStr, 0, 2);
	bool isPrim, isInv;
	safe_call(parseSetEmitFlags(flagStr, isPrim, isInv));

	DVLEData* dvle = GetDvleData();
	if (!dvle->isGeoShader)
	{
		dvle->isGeoShader = true;
		dvle->isCompatGeoShader = true;
	}

#ifdef DEBUG
	printf("%s:%02X vtx%d, %s, %s\n", cmdName, opcode, vtxId, isPrim?"true":"false", isInv?"true":"false");
#endif
	BUF.push_back(FMT_OPCODE(opcode) | ((u32)isInv<<22) | ((u32)isPrim<<23) | (vtxId<<24));

	return 0;
}

DEF_COMMAND(formatcall)
{
	NEXT_ARG(procName);
	ENSURE_NO_MORE_ARGS();

	ARG_TARGET(procName);

	g_procRelocTable.push_back( std::make_pair(BUF.size(), procName) );

	BUF.push_back(FMT_OPCODE(opcode));

#ifdef DEBUG
	printf("%s:%02X %s\n", cmdName, opcode, procName);
#endif
	return 0;
}

DEF_COMMAND(formatfor)
{
	NEXT_ARG(regName);
	ENSURE_NO_MORE_ARGS();

	ARG_TO_IREG(regId, regName);

	if (NO_MORE_STACK)
		return throwError("too many nested blocks\n");

	StackEntry& elem = g_stack[g_stackPos++];
	elem.type = SE_FOR;
	elem.pos = BUF.size();

	BUF.push_back(FMT_OPCODE(opcode) | ((regId-0x80) << 22));

#ifdef DEBUG
	printf("%s:%02X d%02X\n", cmdName, opcode, regId);
#endif
	return 0;
}

DEF_COMMAND(format2)
{
	NEXT_ARG(condExp);

	u32 instruction = 0;
	safe_call(parseCondExp(condExp, instruction));

	switch (opcode)
	{
		case MAESTRO_BREAKC:
		{
			ENSURE_NO_MORE_ARGS();

#ifdef DEBUG
			printf("%s:%02X %s\n", cmdName, opcode, condExp);
#endif
			break;
		}

		case MAESTRO_CALLC:
		case MAESTRO_JMPC:
		{
			NEXT_ARG(targetName);
			ENSURE_NO_MORE_ARGS();

			ARG_TARGET(targetName);

			relocTableType& rt = opcode==MAESTRO_CALLC ? g_procRelocTable : g_labelRelocTable;
			rt.push_back( std::make_pair(BUF.size(), targetName) );

#ifdef DEBUG
			printf("%s:%02X %s, %s\n", cmdName, opcode, condExp, targetName);
#endif
			break;
		}

		case MAESTRO_IFC:
		{
			ENSURE_NO_MORE_ARGS();

			if (NO_MORE_STACK)
				return throwError("too many nested blocks\n");

			StackEntry& elem = g_stack[g_stackPos++];
			elem.type = SE_IF;
			elem.pos = BUF.size();
			elem.uExtra = 0;

#ifdef DEBUG
			printf("%s:%02X %s\n", cmdName, opcode, condExp);
#endif
			break;
		}
	}

	BUF.push_back(FMT_OPCODE(opcode) | instruction);

	return 0;
}

DEF_COMMAND(format3)
{
	NEXT_ARG(regName);

	u32 negation = 0;
	if (*regName == '!')
	{
		if (opcode == MAESTRO_JMPU)
		{
			negation = 1;
			regName ++;
		} else
			return throwError("Inverting the condition is not supported by %s\n", opcode==MAESTRO_CALLU ? "CALLU" : "IFU");
	}

	ARG_TO_BREG(regId, regName);

	switch (opcode)
	{
		case MAESTRO_CALLU:
		case MAESTRO_JMPU:
		{
			NEXT_ARG(targetName);
			ENSURE_NO_MORE_ARGS();

			ARG_TARGET(targetName);

			relocTableType& rt = opcode==MAESTRO_CALLU ? g_procRelocTable : g_labelRelocTable;
			rt.push_back( std::make_pair(BUF.size(), targetName) );

#ifdef DEBUG
			printf("%s:%02X d%02X, %s\n", cmdName, opcode, regId, targetName);
#endif
			break;
		}

		case MAESTRO_IFU:
		{
			ENSURE_NO_MORE_ARGS();

			if (NO_MORE_STACK)
				return throwError("too many nested blocks\n");

			StackEntry& elem = g_stack[g_stackPos++];
			elem.type = SE_IF;
			elem.pos = BUF.size();
			elem.uExtra = 0;

#ifdef DEBUG
			printf("%s:%02X d%02X\n", cmdName, opcode, regId);
#endif
			break;
		}
	}

	BUF.push_back(FMT_OPCODE(opcode) | ((regId-0x88) << 22) | negation);

	return 0;
}

static const cmdTableType cmdTable[] =
{
	DEC_COMMAND(NOP, format0),
	DEC_COMMAND(END, format0),
	DEC_COMMAND(EMIT, format0),

	DEC_COMMAND(ADD, format1),
	DEC_COMMAND(DP3, format1),
	DEC_COMMAND(DP4, format1),
	DEC_COMMAND2(DPH, format1),
	DEC_COMMAND(MUL, format1),
	DEC_COMMAND2(SGE, format1),
	DEC_COMMAND2(SLT, format1),
	DEC_COMMAND(MAX, format1),
	DEC_COMMAND(MIN, format1),

	DEC_COMMAND(EX2, format1u),
	DEC_COMMAND(LG2, format1u),
	DEC_COMMAND(FLR, format1u),
	DEC_COMMAND(RCP, format1u),
	DEC_COMMAND(RSQ, format1u),
	DEC_COMMAND(MOV, format1u),

	DEC_COMMAND(MOVA, formatmova),

	DEC_COMMAND(CMP, format1c),

	DEC_COMMAND(CALL, formatcall),

	DEC_COMMAND(FOR, formatfor),

	DEC_COMMAND(BREAKC, format2),
	DEC_COMMAND(CALLC, format2),
	DEC_COMMAND(IFC, format2),
	DEC_COMMAND(JMPC, format2),

	DEC_COMMAND(CALLU, format3),
	DEC_COMMAND(IFU, format3),
	DEC_COMMAND(JMPU, format3),

	DEC_COMMAND2(MAD, format5),

	DEC_COMMAND(SETEMIT, formatsetemit),

	{ NULL, NULL },
};

// --------------------------------------------------------------------
// Directives
// --------------------------------------------------------------------

DEF_DIRECTIVE(proc)
{
	NEXT_ARG(procName);
	ENSURE_NO_MORE_ARGS();

	if (NO_MORE_STACK)
		return throwError("too many nested blocks\n");

	StackEntry& elem = g_stack[g_stackPos++];
	elem.type = SE_PROC;
	elem.pos = BUF.size();
	elem.strExtra = procName;

	if (g_procTable.find(procName) != g_procTable.end())
		return throwError("proc already exists: %s\n", procName);

#ifdef DEBUG
	printf("Defining %s\n", procName);
#endif
	return 0;
}

DEF_DIRECTIVE(else)
{
	ENSURE_NO_MORE_ARGS();
	if (!g_stackPos)
		return throwError(".else with unmatched IF\n");

	StackEntry& elem = g_stack[g_stackPos-1];
	if (elem.type != SE_IF)
		return throwError(".else with unmatched IF\n");
	if (elem.uExtra)
		return throwError("spurious .else\n");

	// Automatically add padding NOPs when necessary
	if (lastWasEnd)
	{
		insertPaddingNop();
		lastWasEnd = false;
	} else
	{
		u32 p = BUF.size();
		u32 lastOpcode = BUF[p-1] >> 26;
		if (lastOpcode == MAESTRO_JMPC || lastOpcode == MAESTRO_JMPU
			|| lastOpcode == MAESTRO_CALL || lastOpcode == MAESTRO_CALLC || lastOpcode == MAESTRO_CALLU
			|| (p - elem.pos) < 2)
			insertPaddingNop();
	}

	u32 curPos = BUF.size();
	elem.uExtra = curPos;
	u32& inst = BUF[elem.pos];
	inst &= ~(0xFFF << 10);
	inst |= curPos << 10;

#ifdef DEBUG
	printf("ELSE\n");
#endif

	return 0;
}

DEF_DIRECTIVE(end)
{
	ENSURE_NO_MORE_ARGS();
	if (!g_stackPos)
		return throwError(".end with unmatched block\n");
	
	StackEntry& elem = g_stack[--g_stackPos];

	// Automatically add padding NOPs when necessary
	if (elem.type != SE_ARRAY && lastWasEnd)
	{
		insertPaddingNop();
		lastWasEnd = false;
	}

	else if (elem.type == SE_PROC || elem.type == SE_FOR || (elem.type == SE_IF && BUF.size() > 0))
	{
		u32 p = BUF.size();
		u32 lastOpcode = BUF[p-1] >> 26;
		if (lastOpcode == MAESTRO_JMPC || lastOpcode == MAESTRO_JMPU
			|| lastOpcode == MAESTRO_CALL || lastOpcode == MAESTRO_CALLC || lastOpcode == MAESTRO_CALLU
			|| (elem.type == SE_FOR && lastOpcode == MAESTRO_BREAKC)
			|| (elem.type != SE_ARRAY && (p - elem.pos) < (elem.type != SE_PROC ? 2 : 1)))
			insertPaddingNop();
	}

	u32 curPos = BUF.size();
	u32 size = curPos - elem.pos;

	switch (elem.type)
	{
		case SE_PROC:
		{
#ifdef DEBUG
			printf("proc: %s(%u, size:%u)\n", elem.strExtra, elem.pos, size);
#endif
			g_procTable.insert( std::pair<std::string, procedure>(elem.strExtra, procedure(elem.pos, size)) );
			break;
		}

		case SE_FOR:
		{
#ifdef DEBUG
			printf("ENDFOR\n");
#endif
			u32& inst = BUF[elem.pos];
			inst &= ~(0xFFF << 10);
			inst |= (curPos-1) << 10;
			lastWasEnd = true;
			break;
		}

		case SE_IF:
		{
#ifdef DEBUG
			printf("ENDIF\n");
#endif
			u32& inst = BUF[elem.pos];
			if (!elem.uExtra)
			{
				// IF with no ELSE
				inst &= ~(0xFFF << 10);
				inst |= curPos << 10;
			} else
			{
				// IF with an ELSE
				inst &= ~0x3FF;
				inst |= curPos - elem.uExtra;
			}
			lastWasEnd = true;
			break;
		}

		case SE_ARRAY:
		{
#ifdef DEBUG
			printf("ENDARRAY\n");
#endif
			DVLEData* dvle = GetDvleData();
			UniformAlloc& alloc = getAlloc(UTYPE_FVEC, dvle);

			if (g_aliases.find(g_constArrayName) != g_aliases.end())
				return duplicateIdentifier(g_constArrayName);

			int size = g_constArray.size();
			if (g_constArraySize >= 0) for (; size < g_constArraySize; size ++)
			{
				Constant c;
				memset(&c, 0, sizeof(c));
				c.type = UTYPE_FVEC;
				g_constArray.push_back(c);
			}

			if (size == 0)
				return throwError("no elements have been specified in array '%s'\n", g_constArrayName);

			int uniformPos = alloc.AllocLocal(size);
			if (uniformPos < 0)
				return throwError("not enough space for local constant array '%s'\n", g_constArrayName);

			if ((dvle->constantCount+size) > MAX_CONSTANT)
				return throwError("too many local constants\n");

			for (int i = 0; i < size; i ++)
			{
				Constant& src = g_constArray[i];
				Constant& dst = dvle->constantTable[dvle->constantCount++];
				src.regId = uniformPos+i;
				memcpy(&dst, &src, sizeof(src));
			}

			g_aliases.insert( std::pair<std::string,int>(g_constArrayName, uniformPos | (DEFAULT_OPSRC<<8)) );

			g_constArray.clear();
			g_constArraySize = -1;
			g_constArrayName = NULL;
			break;
		}
	}

	return 0;
}

DEF_DIRECTIVE(alias)
{
	NEXT_ARG_SPC(aliasName);
	NEXT_ARG_SPC(aliasReg);
	ENSURE_NO_MORE_ARGS();

	if (!validateIdentifier(aliasName))
		return throwError("invalid alias name: %s\n", aliasName);
	if (isregp(aliasName[0]) && isdigit(aliasName[1]))
		return throwError("cannot redefine register\n");
	ARG_TO_REG(rAlias, aliasReg);

	if (g_aliases.find(aliasName) != g_aliases.end())
		return duplicateIdentifier(aliasName);

	g_aliases.insert( std::pair<std::string,int>(aliasName, rAlias | (rAliasSw<<8)) );
	return 0;
}

DEF_DIRECTIVE(uniform)
{
	DVLEData* dvle = GetDvleData();
	UniformAlloc& alloc = getAlloc(dirParam, dvle);
	bool useSharedSpace = !dvle->usesGshSpace();

	for (;;)
	{
		char* argText = nextArg();
		if (!argText) break;

		int uSize = 1;
		char* sizePos = strchr(argText, '[');
		if (sizePos)
		{
			char* closePos = strchr(sizePos, ']');
			if (!closePos)
				return throwError("missing closing bracket: %s\n", argText);
			*closePos = 0;
			*sizePos++ = 0;
			sizePos = trim_whitespace(sizePos);
			uSize = atoi(sizePos);
			if (uSize < 1)
				return throwError("invalid uniform size: %s[%s]\n", argText, sizePos);
		}
		if (!validateIdentifier(argText))
			return throwError("invalid uniform name: %s\n", argText);
		if (g_aliases.find(argText) != g_aliases.end())
			return duplicateIdentifier(argText);

		int uniformPos = -1;

		// Find the uniform in the table
		int i;
		for (i = 0; useSharedSpace && i < g_uniformCount; i ++)
		{
			Uniform& uniform = g_uniformTable[i];
			if (uniform.name == argText)
			{
				if (uniform.type != dirParam)
					return throwError("mismatched uniform type: %s\n", argText);
				if (uniform.size != uSize)
					return throwError("uniform '%s' previously declared as having size %d\n", argText, uniform.size);
				uniformPos = uniform.pos;
				break;
			}
		}

		// If not found, create it
		if (uniformPos < 0)
		{
			if (g_uniformCount == MAX_UNIFORM)
				return throwError("too many global uniforms: %s\n", argText);

			uniformPos = alloc.AllocGlobal(uSize);
			if (uniformPos < 0)
				return throwError("not enough uniform space: %s[%d]\n", argText, uSize);
		}

		if (useSharedSpace)
			g_uniformTable[g_uniformCount++].init(argText, uniformPos, uSize, dirParam);

		if (*argText != '_')
		{
			// Add the uniform to the table
			if (dvle->uniformCount == MAX_UNIFORM)
				return throwError("too many referenced uniforms: %s\n", argText);
			dvle->uniformTable[dvle->uniformCount++].init(argText, uniformPos, uSize, dirParam);
			dvle->symbolSize += strlen(argText)+1;
		}

		g_aliases.insert( std::pair<std::string,int>(argText, uniformPos | (DEFAULT_OPSRC<<8)) );

#ifdef DEBUG
		printf("uniform %s[%d] @ d%02X:d%02X\n", argText, uSize, uniformPos, uniformPos+uSize-1);
#endif
	}
	return 0;
}

DEF_DIRECTIVE(const)
{
	DVLEData* dvle = GetDvleData();
	UniformAlloc& alloc = getAlloc(dirParam, dvle);

	NEXT_ARG_CPAREN(constName);
	NEXT_ARG(arg0Text);
	NEXT_ARG(arg1Text);
	NEXT_ARG(arg2Text);
	char* arg3Text = mystrtok_pos;
	if (!mystrtok_pos) return missingParam();
	char* parenPos = strchr(arg3Text, ')');
	if (!parenPos) return throwError("invalid syntax\n");
	*parenPos = 0;
	arg3Text = trim_whitespace(arg3Text);

	if (g_aliases.find(constName) != g_aliases.end())
		return duplicateIdentifier(constName);

	int uniformPos = alloc.AllocLocal(1);
	if (uniformPos < 0)
		return throwError("not enough space for local constant '%s'\n", constName);

	if (dvle->constantCount == MAX_CONSTANT)
		return throwError("too many local constants\n");

	Constant& ct = dvle->constantTable[dvle->constantCount++];
	ct.regId = uniformPos;
	ct.type = dirParam;
	if (dirParam == UTYPE_FVEC)
	{
		ct.fparam[0] = atof(arg0Text);
		ct.fparam[1] = atof(arg1Text);
		ct.fparam[2] = atof(arg2Text);
		ct.fparam[3] = atof(arg3Text);
	} else if (dirParam == UTYPE_IVEC)
	{
		ct.iparam[0] = atoi(arg0Text) & 0xFF;
		ct.iparam[1] = atoi(arg1Text) & 0xFF;
		ct.iparam[2] = atoi(arg2Text) & 0xFF;
		ct.iparam[3] = atoi(arg3Text) & 0xFF;
	}

	g_aliases.insert( std::pair<std::string,int>(constName, ct.regId | (DEFAULT_OPSRC<<8)) );

#ifdef DEBUG
	if (dirParam == UTYPE_FVEC)
		printf("constant %s(%f, %f, %f, %f) @ d%02X\n", constName, ct.fparam[0], ct.fparam[1], ct.fparam[2], ct.fparam[3], ct.regId);
	else if (dirParam == UTYPE_IVEC)
		printf("constant %s(%u, %u, %u, %u) @ d%02X\n", constName, ct.iparam[0], ct.iparam[1], ct.iparam[2], ct.iparam[3], ct.regId);
#endif
	return 0;
};

DEF_DIRECTIVE(constfa)
{
	bool inArray = g_stackPos && g_stack[g_stackPos-1].type == SE_ARRAY;

	if (!inArray)
	{
		NEXT_ARG(constName);
		ENSURE_NO_MORE_ARGS();

		if (NO_MORE_STACK)
			return throwError("too many nested blocks\n");

		char* sizePos = strchr(constName, '[');
		if (!sizePos)
			return throwError("missing opening bracket: %s\n", constName);

		char* closePos = strchr(sizePos, ']');
		if (!closePos)
			return throwError("missing closing bracket: %s\n", constName);

		*closePos++ = 0;
		*sizePos++ = 0;
		closePos = trim_whitespace(closePos);
		sizePos = trim_whitespace(sizePos);

		if (*closePos)
			return throwError("garbage found: %s\n", closePos);

		if (*sizePos)
		{
			g_constArraySize = atoi(sizePos);
			if (g_constArraySize <= 0)
				return throwError("invalid array size: %s[%s]\n", constName, sizePos);
		}

		if (!validateIdentifier(constName))
			return throwError("invalid array name: %s\n", constName);

		g_constArrayName = constName;

		StackEntry& elem = g_stack[g_stackPos++];
		elem.type = SE_ARRAY;

	} else
	{
		if (g_constArraySize >= 0 && g_constArraySize == g_constArray.size())
			return throwError("too many elements in the array, expected %d\n", g_constArraySize);

		NEXT_ARG(arg0Text);
		if (*arg0Text != '(')
			return throwError("invalid syntax\n");
		arg0Text++;

		NEXT_ARG(arg1Text);
		NEXT_ARG(arg2Text);
		char* arg3Text = mystrtok_pos;
		if (!mystrtok_pos) return missingParam();
		char* parenPos = strchr(arg3Text, ')');
		if (!parenPos) return throwError("invalid syntax\n");
		*parenPos = 0;
		arg3Text = trim_whitespace(arg3Text);

		Constant ct;
		ct.type = UTYPE_FVEC;
		ct.fparam[0] = atof(arg0Text);
		ct.fparam[1] = atof(arg1Text);
		ct.fparam[2] = atof(arg2Text);
		ct.fparam[3] = atof(arg3Text);
		g_constArray.push_back(ct);
	}

	return 0;
}

DEF_DIRECTIVE(setfi)
{
	DVLEData* dvle = GetDvleData();

	NEXT_ARG_CPAREN(constName);
	NEXT_ARG(arg0Text);
	NEXT_ARG(arg1Text);
	NEXT_ARG(arg2Text);
	char* arg3Text = mystrtok_pos;
	if (!mystrtok_pos) return missingParam();
	char* parenPos = strchr(arg3Text, ')');
	if (!parenPos) return throwError("invalid syntax\n");
	*parenPos = 0;
	arg3Text = trim_whitespace(arg3Text);

	ARG_TO_REG(constReg, constName);
	if (dirParam == UTYPE_FVEC)
	{
		if (constReg < 0x20 || constReg >= 0x80)
			return throwError("invalid floating point vector uniform: %s\n", constName);
	} else if (dirParam == UTYPE_IVEC)
	{
		if (constReg < 0x80 || constReg >= 0x84)
			return throwError("invalid integer vector uniform: %s\n", constName);
	}

	if (dvle->constantCount == MAX_CONSTANT)
		return throwError("too many local constants\n");

	Constant& ct = dvle->constantTable[dvle->constantCount++];
	ct.regId = constReg;
	ct.type = dirParam;
	if (dirParam == UTYPE_FVEC)
	{
		ct.fparam[0] = atof(arg0Text);
		ct.fparam[1] = atof(arg1Text);
		ct.fparam[2] = atof(arg2Text);
		ct.fparam[3] = atof(arg3Text);
	} else if (dirParam == UTYPE_IVEC)
	{
		ct.iparam[0] = atoi(arg0Text) & 0xFF;
		ct.iparam[1] = atoi(arg1Text) & 0xFF;
		ct.iparam[2] = atoi(arg2Text) & 0xFF;
		ct.iparam[3] = atoi(arg3Text) & 0xFF;
	}

	return 0;
}

int parseBool(bool& out, const char* text)
{
	if (stricmp(text, "true")==0 || stricmp(text, "on")==0 || stricmp(text, "1")==0)
	{
		out = true;
		return 0;
	}
	if (stricmp(text, "false")==0 || stricmp(text, "off")==0 || stricmp(text, "0")==0)
	{
		out = false;
		return 0;
	}
	return throwError("invalid bool value: %s\n", text);
}

DEF_DIRECTIVE(setb)
{
	DVLEData* dvle = GetDvleData();

	NEXT_ARG_SPC(constName);
	NEXT_ARG_SPC(valueText);
	ENSURE_NO_MORE_ARGS();
	ARG_TO_BREG(constReg, constName);

	bool constVal = false;
	safe_call(parseBool(constVal, valueText));

	if (dvle->constantCount == MAX_CONSTANT)
		return throwError("too many local constants\n");

	Constant& ct = dvle->constantTable[dvle->constantCount++];
	ct.regId = constReg;
	ct.type = UTYPE_BOOL;
	ct.bparam = constVal;

	return 0;
}

static int parseOutType(const char* text)
{
	if (stricmp(text,"pos")==0 || stricmp(text,"position")==0)
		return OUTTYPE_POS;
	if (stricmp(text,"nquat")==0 || stricmp(text,"normalquat")==0)
		return OUTTYPE_NQUAT;
	if (stricmp(text,"clr")==0 || stricmp(text,"color")==0)
		return OUTTYPE_CLR;
	if (stricmp(text,"tcoord0")==0 || stricmp(text,"texcoord0")==0)
		return OUTTYPE_TCOORD0;
	if (stricmp(text,"tcoord0w")==0 || stricmp(text,"texcoord0w")==0)
		return OUTTYPE_TCOORD0W;
	if (stricmp(text,"tcoord1")==0 || stricmp(text,"texcoord1")==0)
		return OUTTYPE_TCOORD1;
	if (stricmp(text,"tcoord2")==0 || stricmp(text,"texcoord2")==0)
		return OUTTYPE_TCOORD2;
	if (stricmp(text,"view")==0)
		return OUTTYPE_VIEW;
	if (stricmp(text,"dummy")==0)
		return OUTTYPE_DUMMY;
	return -1;
}

DEF_DIRECTIVE(in)
{
	DVLEData* dvle = GetDvleData();

	NEXT_ARG_SPC(inName);
	char* inRegName = nextArgSpc();
	ENSURE_NO_MORE_ARGS();

	if (!validateIdentifier(inName))
		return throwError("invalid identifier: %s\n", inName);
	if (g_aliases.find(inName) != g_aliases.end())
		return duplicateIdentifier(inName);

	int oid = -1;
	if (inRegName)
	{
		ARG_TO_REG(inReg, inRegName);
		if (inReg < 0x00 || inReg >= 0x10)
			return throwError("invalid input register: %s\n", inReg);
		oid = inReg;
	} else
		oid = dvle->findFreeInput();
	if (oid < 0)
		return throwError("too many inputs\n");
	if (dvle->uniformCount == MAX_UNIFORM)
		return throwError("too many uniforms in DVLE\n");

	dvle->inputMask |= BIT(oid);
	dvle->uniformTable[dvle->uniformCount++].init(inName, oid, 1, UTYPE_FVEC);
	dvle->symbolSize += strlen(inName)+1;
	g_aliases.insert( std::pair<std::string,int>(inName, oid | (DEFAULT_OPSRC<<8)) );
	return 0;
}

DEF_DIRECTIVE(out)
{
	DVLEData* dvle = GetDvleData();

	NEXT_ARG_SPC(outName);
	NEXT_ARG_SPC(outType);
	char* outDestRegName = nextArgSpc();
	ENSURE_NO_MORE_ARGS();

	int oid = -1;
	int sw = DEFAULT_OPSRC;

	if (outName[0]=='-' && !outName[1])
		outName = NULL;
	else if (!validateIdentifier(outName))
		return throwError("invalid identifier: %s\n", outName);

	if (outDestRegName)
	{
		ARG_TO_REG(outDestReg, outDestRegName);
		if (outDestReg < 0x00 || outDestReg >= dvle->maxOutputReg())
			return throwError("invalid output register: %s\n", outDestRegName);
		oid = outDestReg;
		sw = outDestRegSw;
	}

	if (oid < 0)
	{
		char* dotPos = strchr(outType, '.');
		if (dotPos)
		{
			*dotPos++ = 0;
			sw = parseSwizzling(dotPos);
			if (sw < 0)
				return throwError("invalid output mask: %s\n", dotPos);
		}
	}

	int mask = maskFromSwizzling(sw, false);
	int type = parseOutType(outType);
	if (type < 0)
		return throwError("invalid output type: %s\n", outType);

	if (oid < 0)
		oid = dvle->findFreeOutput();
	else if (dvle->outputUsedReg & (mask << (4*oid)))
		return throwError("this output collides with another one previously defined\n");

	if (oid < 0 || dvle->outputCount==MAX_OUTPUT)
		return throwError("too many outputs\n");

	if (outName && g_aliases.find(outName) != g_aliases.end())
		return duplicateIdentifier(outName);

	if (oid >= 7 && type != OUTTYPE_DUMMY)
		return throwError("this register (o%d) can only be a dummy output\n", oid);

#ifdef DEBUG
	printf("output %s <- o%d (%d:%X)\n", outName, oid, type, mask);
#endif

	dvle->outputTable[dvle->outputCount++] = OUTPUT_MAKE(type, oid, mask);
	dvle->outputMask |= BIT(oid);
	dvle->outputUsedReg |= mask << (4*oid);
	if (outName)
		g_aliases.insert( std::pair<std::string,int>(outName, oid | (DEFAULT_OPSRC<<8)) );
	if (type == OUTTYPE_DUMMY && dvle->usesGshSpace())
		dvle->isMerge = true;
	return 0;
}

DEF_DIRECTIVE(entry)
{
	DVLEData* dvle = GetDvleData();

	NEXT_ARG_SPC(entrypoint);
	ENSURE_NO_MORE_ARGS();

	if (!validateIdentifier(entrypoint))
		return throwError("invalid identifier: %s\n", entrypoint);

	dvle->entrypoint = entrypoint;
	return 0;
}

DEF_DIRECTIVE(nodvle)
{
	DVLEData* dvle = GetDvleData();
	ENSURE_NO_MORE_ARGS();

	if (!dvle->nodvle)
	{
		dvle->nodvle = true;
		g_totalDvleCount --;
	}

	return 0;
}

static inline int parseGshType(const char* text)
{
	if (stricmp(text,"point")==0)
		return GSHTYPE_POINT;
	if (stricmp(text,"variable")==0 || stricmp(text,"subdivision")==0)
		return GSHTYPE_VARIABLE;
	if (stricmp(text,"fixed")==0 || stricmp(text,"particle")==0)
		return GSHTYPE_FIXED;
	return -1;
}

DEF_DIRECTIVE(gsh)
{
	DVLEData* dvle = GetDvleData();
	char* gshMode = nextArgSpc();
	if (!gshMode)
	{
		dvle->isGeoShader = true;
		dvle->isCompatGeoShader = true;
		return 0;
	}

	if (dvle->isGeoShader)
		return throwError(".gsh had already been used\n");
	if (dvle->constantCount || dvle->uniformCount || dvle->outputMask)
		return throwError(".gsh must be used before any constant, uniform or output is declared\n");

	int mode = parseGshType(gshMode);
	if (mode < 0)
		return throwError("invalid geometry shader mode: %s\n", gshMode);

	dvle->isGeoShader = true;
	dvle->geoShaderType = mode;

	NEXT_ARG_SPC(firstFreeRegName);
	ARG_TO_REG(firstFreeReg, firstFreeRegName);
	if (firstFreeReg < 0x20 || firstFreeReg >= 0x80)
		return throwError("invalid float uniform register: %s\n", firstFreeRegName);

	unifAlloc[1].initForGsh(firstFreeReg);

	switch (mode)
	{
		case GSHTYPE_POINT:
			ENSURE_NO_MORE_ARGS();
			break;
		case GSHTYPE_VARIABLE:
		{
			NEXT_ARG_SPC(vtxNumText);
			ENSURE_NO_MORE_ARGS();

			ARG_TO_INT(vtxNum, vtxNumText, 0, 255);
			dvle->geoShaderVariableNum = vtxNum;
			break;
		}
		case GSHTYPE_FIXED:
		{
			NEXT_ARG_SPC(arrayStartText);
			NEXT_ARG_SPC(vtxNumText);
			ENSURE_NO_MORE_ARGS();

			ARG_TO_REG(arrayStart, arrayStartText);
			ARG_TO_INT(vtxNum, vtxNumText, 0, 255);

			if (arrayStart < 0x20 || arrayStart >= 0x80)
				return throwError("invalid float uniform register: %s\n", arrayStartText);
			if (arrayStart >= firstFreeReg)
				return throwError("specified location overlaps uniform allocation pool: %s\n", arrayStartText);

			dvle->geoShaderFixedStart = arrayStart - 0x20;
			dvle->geoShaderFixedNum = vtxNum;
			break;
		}
	}

	return 0;
}


static const cmdTableType dirTable[] =
{
	DEC_DIRECTIVE(proc),
	DEC_DIRECTIVE(else),
	DEC_DIRECTIVE(end),
	DEC_DIRECTIVE(alias),
	DEC_DIRECTIVE2(fvec, uniform, UTYPE_FVEC),
	DEC_DIRECTIVE2(ivec, uniform, UTYPE_IVEC),
	DEC_DIRECTIVE2(bool, uniform, UTYPE_BOOL),
	DEC_DIRECTIVE2(constf, const, UTYPE_FVEC),
	DEC_DIRECTIVE2(consti, const, UTYPE_IVEC),
	DEC_DIRECTIVE(constfa),
	DEC_DIRECTIVE(in),
	DEC_DIRECTIVE(out),
	DEC_DIRECTIVE(entry),
	DEC_DIRECTIVE(nodvle),
	DEC_DIRECTIVE(gsh),
	DEC_DIRECTIVE2(setf, setfi, UTYPE_FVEC),
	DEC_DIRECTIVE2(seti, setfi, UTYPE_IVEC),
	DEC_DIRECTIVE(setb),
	{ NULL, NULL },
};

int ProcessCommand(const char* cmd)
{
	const cmdTableType* table = cmdTable;
	if (*cmd == '.')
	{
		cmd ++;
		table = dirTable;
	} else if (!g_stackPos)
		return throwError("instruction outside block\n");
	else
	{
		lastWasEnd = false;
		if (!GetDvleData()->isGeoShader && g_outputBuf.size() >= MAX_VSH_SIZE)
			return throwError("instruction outside vertex shader code memory (max %d instructions, currently %d)\n", MAX_VSH_SIZE, g_outputBuf.size());
	}

	for (int i = 0; table[i].name; i ++)
		if (stricmp(table[i].name, cmd) == 0)
			return table[i].func(cmd, table[i].opcode, table[i].opcodei);

	return throwError("invalid instruction: %s\n", cmd);
}
