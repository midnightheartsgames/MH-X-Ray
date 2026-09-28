#include "stdafx.h"
#pragma hdrstop

#include <dbghelp.h>

#pragma comment(lib,"dbghelp.lib")

#ifndef _M_IX86
extern "C" __declspec(dllimport) void __stdcall RtlCaptureContext	(PCONTEXT context_record);
#endif

static const int	stack_trace_capacity	= 100;
static const int	stack_trace_line_size	= 4096;

char				g_stackTrace[stack_trace_capacity][stack_trace_line_size];
int					g_stackTraceCount		= 0;

static HANDLE		symbol_process			= 0;

static HANDLE	get_symbol_process	()
{
	if (symbol_process)
		return				symbol_process;

	HANDLE					process = 0;
	if (!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&process,0,FALSE,DUPLICATE_SAME_ACCESS))
		return				0;

	string_path				module_folder;
	DWORD					length = GetModuleFileName(0,module_folder,sizeof(module_folder));
	LPSTR					separator = strrchr(module_folder,'\\');
	if (length && separator)
		*separator			= 0;
	else
		module_folder[0]	= 0;

	SymSetOptions			(SymGetOptions() | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (!SymInitialize(process,module_folder[0] ? module_folder : 0,TRUE)) {
		CloseHandle			(process);
		return				0;
	}

	symbol_process			= process;
	return					symbol_process;
}

static void		describe_frame		(HANDLE process, DWORD64 address, LPSTR description)
{
	int						length = sprintf_s(description,stack_trace_line_size,"0x%p",(void*)address);

	string_path				module_path;
	DWORD64					module_base = SymGetModuleBase64(process,address);
	if (module_base && GetModuleFileName((HMODULE)module_base,module_path,sizeof(module_path))) {
		LPCSTR				module_name = strrchr(module_path,'\\');
		length				+= sprintf_s(description + length,stack_trace_line_size - length," %s",module_name ? module_name + 1 : module_path);
	}

	u8						symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
	ZeroMemory				(symbol_buffer,sizeof(symbol_buffer));
	PSYMBOL_INFO			symbol = (PSYMBOL_INFO)symbol_buffer;
	symbol->SizeOfStruct	= sizeof(SYMBOL_INFO);
	symbol->MaxNameLen		= MAX_SYM_NAME;
	DWORD64					symbol_displacement = 0;
	if (SymFromAddr(process,address,&symbol_displacement,symbol))
		length				+= sprintf_s(description + length,stack_trace_line_size - length,", %s()+%u byte(s)",symbol->Name,u32(symbol_displacement));

	IMAGEHLP_LINE64			line;
	ZeroMemory				(&line,sizeof(line));
	line.SizeOfStruct		= sizeof(line);
	DWORD					line_displacement = 0;
	if (SymGetLineFromAddr64(process,address,&line_displacement,&line))
		sprintf_s			(description + length,stack_trace_line_size - length,", %s, line %u",line.FileName,line.LineNumber);
}

static void		walk_stack			(CONTEXT& context)
{
	g_stackTraceCount		= 0;

	HANDLE					process = get_symbol_process();
	if (!process)
		return;

	STACKFRAME64			frame;
	ZeroMemory				(&frame,sizeof(frame));
#ifdef _M_IX86
	DWORD					machine = IMAGE_FILE_MACHINE_I386;
	frame.AddrPC.Offset		= context.Eip;
	frame.AddrStack.Offset	= context.Esp;
	frame.AddrFrame.Offset	= context.Ebp;
#else
	DWORD					machine = IMAGE_FILE_MACHINE_AMD64;
	frame.AddrPC.Offset		= context.Rip;
	frame.AddrStack.Offset	= context.Rsp;
	frame.AddrFrame.Offset	= context.Rbp;
#endif
	frame.AddrPC.Mode		= AddrModeFlat;
	frame.AddrStack.Mode	= AddrModeFlat;
	frame.AddrFrame.Mode	= AddrModeFlat;

	while (g_stackTraceCount < stack_trace_capacity) {
		if (!StackWalk64(machine,process,GetCurrentThread(),&frame,&context,0,SymFunctionTableAccess64,SymGetModuleBase64,0))
			break;

		if (!frame.AddrPC.Offset || !SymGetModuleBase64(process,frame.AddrPC.Offset))
			break;

		describe_frame		(process,frame.AddrPC.Offset,g_stackTrace[g_stackTraceCount]);
		++g_stackTraceCount;
	}
}

void BuildStackTrace	(struct _EXCEPTION_POINTERS *exception_pointers)
{
	CONTEXT					context = *exception_pointers->ContextRecord;
	walk_stack				(context);
}

#ifdef _M_IX86
static __declspec(noinline) void*	current_instruction_address	()
{
	return					_ReturnAddress();
}

#pragma optimize("y",off)
#endif

__declspec(noinline) void BuildStackTrace	()
{
	CONTEXT					context;
	ZeroMemory				(&context,sizeof(context));
#ifdef _M_IX86
	context.ContextFlags	= CONTEXT_CONTROL;
	context.Eip				= DWORD(current_instruction_address());
	context.Ebp				= DWORD(_AddressOfReturnAddress()) - sizeof(DWORD);
	context.Esp				= context.Ebp;
#else
	RtlCaptureContext		(&context);
#endif
	walk_stack				(context);
}

#ifdef _M_IX86
#pragma optimize("",on)
#endif

__declspec(noinline) void OutputDebugStackTrace	(const char *header)
{
	BuildStackTrace			();

	if (header) {
		OutputDebugString	(header);
		OutputDebugString	(":\r\n");
	}

	for (int i=2; i<g_stackTraceCount; ++i) {
		OutputDebugString	(g_stackTrace[i]);
		OutputDebugString	("\r\n");
	}
}
