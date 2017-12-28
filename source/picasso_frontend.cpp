#include "picasso.h"

// f24 has:
//  - 1 sign bit
//  - 7 exponent bits
//  - 16 mantissa bits
uint32_t f32tof24(float f)
{
	uint32_t i;
	std::memcpy(&i, &f, sizeof(f));

	uint32_t mantissa = (i << 9) >>  9;
	int32_t  exponent = (i << 1) >> 24;
	uint32_t sign     = (i << 0) >> 31;

	// Truncate mantissa
	mantissa >>= 7;

	// Re-bias exponent
	exponent = exponent - 127 + 63;
	if (exponent < 0)
	{
		// Underflow: flush to zero
		return sign << 23;
	}
	else if (exponent > 0x7F)
	{
		// Overflow: saturate to infinity
		return (sign << 23) | (0x7F << 16);
	}

	return (sign << 23) | (exponent << 16) | mantissa;
}

#ifdef WIN32
static inline void FixMinGWPath(char* buf)
{
	if (buf && *buf == '/')
	{
		buf[0] = buf[1];
		buf[1] = ':';
	}
}
#endif

int usage(const char* prog)
{
	fprintf(stderr,
		"Usage: %s [options] files...\n"
		"Options:\n"
		"  -o, --out=<file>        Specifies the name of the SHBIN file to generate\n"
		"  -h, --header=<file>     Specifies the name of the header file to generate\n"
		"  -n, --no-nop            Disables the automatic insertion of padding NOPs\n"
		"  -v, --version           Displays version information\n"
		, prog);
	return EXIT_FAILURE;
}

int main(int argc, char* argv[])
{
	char *shbinFile = NULL, *hFile = NULL;

	static struct option long_options[] =
	{
		{ "out",    required_argument, NULL, 'o' },
		{ "header", required_argument, NULL, 'h' },
		{ "help",   no_argument,       NULL, '?' },
		{ "no-nop", no_argument,       NULL, 'n' },
		{ "version",no_argument,       NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};

	int opt, optidx = 0;
	while ((opt = getopt_long(argc, argv, "o:h:?nv", long_options, &optidx)) != -1)
	{
		switch (opt)
		{
			case 'o': shbinFile = optarg; break;
			case 'h': hFile     = optarg; break;
			case '?': usage(argv[0]); return EXIT_SUCCESS;
			case 'n': g_autoNop = false; break;
			case 'v': printf("%s - Built on %s %s\n", PACKAGE_STRING, __DATE__, __TIME__); return EXIT_SUCCESS;
			default:  return usage(argv[0]);
		}
	}

#ifdef WIN32
	FixMinGWPath(shbinFile);
	FixMinGWPath(hFile);
#endif

	if (optind == argc)
	{
		fprintf(stderr, "%s: no input files are specified\n", argv[0]);
		return usage(argv[0]);
	}

	if (!shbinFile)
	{
		fprintf(stderr, "%s: no output file is specified\n", argv[0]);
		return usage(argv[0]);
	}

	int rc = 0;
	for (int i = optind; i < argc; i ++)
	{
		char* vshFile = argv[i];

#ifdef WIN32
		FixMinGWPath(vshFile);
#endif

		char* sourceCode = StringFromFile(vshFile);
		if (!sourceCode)
		{
			fprintf(stderr, "error: cannot open input file: %s\n", vshFile);
			return EXIT_FAILURE;
		}

		rc = AssembleString(sourceCode, vshFile);
		free(sourceCode);
		if (rc != 0)
			return EXIT_FAILURE;
	}

	rc = RelocateProduct();
	if (rc != 0)
		return EXIT_FAILURE;

	FileClass f(shbinFile, "wb");

	if (f.openerror())
	{
		fprintf(stderr, "Can't open output file!");
		return EXIT_FAILURE;
	}

	u32 progSize = g_outputBuf.size();
	u32 dvlpSize = 10*4 + progSize*4 + g_opdescCount*8;

	// Write DVLB header
	f.WriteWord(0x424C5644); // DVLB
	f.WriteWord(g_totalDvleCount); // Number of DVLEs

	// Calculate and write DVLE offsets
	u32 curOff = 2*4 + g_totalDvleCount*4 + dvlpSize;
	for (dvleTableIter dvle = g_dvleTable.begin(); dvle != g_dvleTable.end(); ++dvle)
	{
		if (dvle->nodvle) continue;
		f.WriteWord(curOff);
		curOff += 16*4; // Header
		curOff += dvle->constantCount*20;
		curOff += dvle->outputCount*8;
		curOff += dvle->uniformCount*8;
		curOff += dvle->symbolSize;
		curOff  = (curOff + 3) &~ 3; // Word alignment
	}

	// Write DVLP header
	f.WriteWord(0x504C5644); // DVLP
	f.WriteWord(0); // version
	f.WriteWord(10*4); // offset to shader binary blob
	f.WriteWord(progSize); // size of shader binary blob
	f.WriteWord(10*4 + progSize*4); // offset to opdesc table
	f.WriteWord(g_opdescCount); // number of opdescs
	f.WriteWord(dvlpSize); // offset to symtable (TODO)
	f.WriteWord(0); // ????
	f.WriteWord(0); // ????
	f.WriteWord(0); // ????

	// Write program
	for (outputBufIter it = g_outputBuf.begin(); it != g_outputBuf.end(); ++it)
		f.WriteWord(*it);

	// Write opdescs
	for (int i = 0; i < g_opdescCount; i ++)
		f.WriteDword(g_opdescTable[i]);

	// Write DVLEs
	for (dvleTableIter dvle = g_dvleTable.begin(); dvle != g_dvleTable.end(); ++dvle)
	{
		if (dvle->nodvle) continue;
		curOff = 16*4;

		f.WriteWord(0x454C5644); // DVLE
		f.WriteHword(0x1002); // maybe version?
		f.WriteByte(dvle->isGeoShader ? 1 : 0); // Shader type
		f.WriteByte(dvle->isMerge ? 1 : 0);
		f.WriteWord(dvle->entryStart); // offset to main
		f.WriteWord(dvle->entryEnd); // offset to end of main
		f.WriteHword(dvle->inputMask);
		f.WriteHword(dvle->outputMask);
		f.WriteByte(dvle->geoShaderType);
		f.WriteByte(dvle->geoShaderFixedStart);
		f.WriteByte(dvle->geoShaderVariableNum);
		f.WriteByte(dvle->geoShaderFixedNum);
		f.WriteWord(curOff); // offset to constant table
		f.WriteWord(dvle->constantCount); // size of constant table
		curOff += dvle->constantCount*5*4;
		f.WriteWord(curOff); // offset to label table (TODO)
		f.WriteWord(0); // size of label table (TODO)
		f.WriteWord(curOff); // offset to output table
		f.WriteWord(dvle->outputCount); // size of output table
		curOff += dvle->outputCount*8;
		f.WriteWord(curOff); // offset to uniform table
		f.WriteWord(dvle->uniformCount); // size of uniform table
		curOff += dvle->uniformCount*8;
		f.WriteWord(curOff); // offset to symbol table
		u32 temp = f.Tell();
		f.WriteWord(dvle->symbolSize); // size of symbol table

		// Sort uniforms by position
		std::sort(dvle->uniformTable, dvle->uniformTable + dvle->uniformCount);

		// Write constants
		for (int i = 0; i < dvle->constantCount; i ++)
		{
			Constant& ct = dvle->constantTable[i];
			f.WriteHword(ct.type);
			if (ct.type == UTYPE_FVEC)
			{
				f.WriteHword(ct.regId-0x20);
				for (int j = 0; j < 4; j ++)
					f.WriteWord(f32tof24(ct.fparam[j]));
			} else if (ct.type == UTYPE_IVEC)
			{
				f.WriteHword(ct.regId-0x80);
				for (int j = 0; j < 4; j ++)
					f.WriteByte(ct.iparam[j]);
			} else if (ct.type == UTYPE_BOOL)
			{
				f.WriteHword(ct.regId-0x88);
				f.WriteWord(ct.bparam ? 1 : 0);
			}
			if (ct.type != UTYPE_FVEC)
				for (int j = 0; j < 3; j ++)
					f.WriteWord(0); // Padding
		}

		// Write outputs
		for (int i = 0; i < dvle->outputCount; i ++)
			f.WriteDword(dvle->outputTable[i]);

		// Write uniforms
		size_t sp = 0;
		for (int i = 0; i < dvle->uniformCount; i ++)
		{
			Uniform& u = dvle->uniformTable[i];
			size_t l = u.name.length()+1;
			f.WriteWord(sp); sp += l;
			int pos = u.pos;
			if (pos >= 0x20)
				pos -= 0x10;
			f.WriteHword(pos);
			f.WriteHword(pos+u.size-1);
		}

		// Write symbols
		for (int i = 0; i < dvle->uniformCount; i ++)
		{
			std::string u(dvle->uniformTable[i].name);
			std::replace(u.begin(), u.end(), '$', '.');
			size_t l = u.length()+1;
			f.WriteRaw(u.c_str(), l);
		}

		// Word alignment
		int pos = f.Tell();
		int pad = ((pos+3)&~3)-pos;
		for (int i = 0; i < pad; i ++)
			f.WriteByte(0);
	}

	if (hFile)
	{
		FILE* f2 = fopen(hFile, "w");
		if (!f2)
		{
			fprintf(stderr, "Can't open header file!\n");
			return 1;
		}

		fprintf(f2, "// Generated by picasso\n");
		fprintf(f2, "#pragma once\n");
		const char* prefix = g_dvleTable.front().isGeoShader ? "GSH" : "VSH"; // WARNING: HORRIBLE HACK - PLEASE FIX!!!!!!!
		for (int i = 0; i < g_uniformCount; i ++)
		{
			Uniform& u = g_uniformTable[i];
			const char* name = u.name.c_str();
			if (*name == '_') continue; // Hidden uniform
			if (u.type == UTYPE_FVEC)
				fprintf(f2, "#define %s_FVEC_%s 0x%02X\n", prefix, name, u.pos-0x20);
			else if (u.type == UTYPE_IVEC)
				fprintf(f2, "#define %s_IVEC_%s 0x%02X\n", prefix, name, u.pos-0x80);
			else if (u.type == UTYPE_BOOL)
			{
				if (u.size == 1)
					fprintf(f2, "#define %s_FLAG_%s BIT(%d)\n", prefix, name, u.pos-0x88);
				else
					fprintf(f2, "#define %s_FLAG_%s(_n) BIT(%d+(_n))\n", prefix, name, u.pos-0x88);
			}
			fprintf(f2, "#define %s_ULEN_%s %d\n", prefix, name, u.size);
		}

		fclose(f2);
	}

	return EXIT_SUCCESS;
}
