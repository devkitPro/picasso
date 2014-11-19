#pragma once
enum
{
	MAESTRO_ADD = 0x00,
	MAESTRO_DP3,
	MAESTRO_DP4,

	MAESTRO_MUL = 0x08,

	MAESTRO_MAX = 0x0C,
	MAESTRO_MIN,
	MAESTRO_RCP,
	MAESTRO_RSQ,

	MAESTRO_ARL = 0x12,
	MAESTRO_MOV,

	MAESTRO_NOP = 0x21,
	MAESTRO_END,

	MAESTRO_CALL = 0x24,

	MAESTRO_CALLC = 0x26,
	MAESTRO_IFB,
	MAESTRO_IF, // ???

	MAESTRO_EMIT = 0x2A, // Geometry shader related
	MAESTRO_SETEMIT, // Geometry shader related

	MAESTRO_CMP = 0x2E,
	MAESTRO_CMP2, // ???

	MAESTRO_MAD = 0x38, // only the upper 3 bits are used for the opcode
};
