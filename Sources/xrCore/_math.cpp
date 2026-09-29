#include "stdafx.h"
#pragma hdrstop

#include <process.h>

// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>

// Initialized on startup
XRCORE_API	Fmatrix			Fidentity;
XRCORE_API	Dmatrix			Didentity;
XRCORE_API	CRandom			Random;

namespace FPU
{
	static void	set_x87_precision	(unsigned int precision)
	{
#ifdef _M_IX86
		unsigned int	x87_control_word;
		__control87_2	(precision | _RC_NEAR, _MCW_PC | _MCW_RC, &x87_control_word, NULL);
#endif
	}

	XRCORE_API void 	m24r	()	{ set_x87_precision(_PC_24); }
	XRCORE_API void 	m64r	()	{ set_x87_precision(_PC_64); }

	void		initialize		()
	{
		_clear87	();

#ifndef XRCORE_STATIC
		m24r		();
#endif

		::Random.seed	( u32(CPU::GetCLK()%(1i64<<32i64)) );
	}
};

namespace CPU 
{
	XRCORE_API u64				clk_per_second	;
	XRCORE_API u64				clk_per_milisec	;
	XRCORE_API u64				clk_per_microsec;
	XRCORE_API u64				clk_overhead	;
	XRCORE_API float			clk_to_seconds	;
	XRCORE_API float			clk_to_milisec	;
	XRCORE_API float			clk_to_microsec	;
	XRCORE_API u64				qpc_freq		= 0	;
	XRCORE_API u64				qpc_overhead	= 0	;
	XRCORE_API u32				qpc_counter		= 0	;
	
	XRCORE_API _processor_info	ID;

	XRCORE_API u64				QPC	()			{
		u64		_dest	;
		QueryPerformanceCounter			((PLARGE_INTEGER)&_dest);
		qpc_counter	++	;
		return	_dest	;
	}

	void Detect	()
	{
		if (!_cpuid	(&ID))
		{
			abort				();
		}

		u64			start,end;
		u32			dwStart,dwTest;

		SetPriorityClass		(GetCurrentProcess(),REALTIME_PRIORITY_CLASS);

		dwTest	= timeGetTime();
		do { dwStart = timeGetTime(); } while (dwTest==dwStart);
		start	= GetCLK();
		while (timeGetTime()-dwStart<1000) ;
		end		= GetCLK();
		clk_per_second = end-start;

		clk_overhead	= 0;
		u64 dummy		= 0;
		for (int i=0; i<256; i++)	{
			start			=	GetCLK();
			clk_overhead	+=	GetCLK()-start-dummy;
		}
		clk_overhead		/=	256;

		QueryPerformanceFrequency	((PLARGE_INTEGER)&qpc_freq)	;
		qpc_overhead	= 0;
		for (i=0; i<256; i++)	{
			start			=	QPC();
			qpc_overhead	+=	QPC()-start-dummy;
		}
		qpc_overhead		/=	256;

		SetPriorityClass	(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);

		clk_per_second	-=	clk_overhead;
		clk_per_milisec	=	clk_per_second/1000;
		clk_per_microsec	=	clk_per_milisec/1000;

		FPU::m64r	();
		double a,b;
		a = 1;		b = double(clk_per_second);
		clk_to_seconds = float(double(a/b));
		a = 1000;	b = double(clk_per_second);
		clk_to_milisec = float(double(a/b));
		a = 1000000;b = double(clk_per_second);
		clk_to_microsec = float(double(a/b));
	}
};

//------------------------------------------------------------------------------------
void _initialize_cpu	(void) 
{
	Msg("* Detected CPU: %s %s, F%d/M%d/S%d, %.2f mhz, %d-clk 'rdtsc'",
		CPU::ID.v_name,CPU::ID.model_name,
		CPU::ID.family,CPU::ID.model,CPU::ID.stepping,
		float(CPU::clk_per_second/u64(1000000)),
		u32(CPU::clk_overhead)
		);

//	DUMP_PHASE;

	if (strstr(Core.Params,"-x86"))		{
		CPU::ID.feature	&= ~_CPU_FEATURE_3DNOW	;
		CPU::ID.feature	&= ~_CPU_FEATURE_SSE	;
		CPU::ID.feature	&= ~_CPU_FEATURE_SSE2	;
	};

	string128	features;	strcpy_s(features,sizeof(features),"RDTSC");
    if (CPU::ID.feature&_CPU_FEATURE_MMX)	strcat(features,", MMX");
    if (CPU::ID.feature&_CPU_FEATURE_3DNOW)	strcat(features,", 3DNow!");
    if (CPU::ID.feature&_CPU_FEATURE_SSE)	strcat(features,", SSE");
    if (CPU::ID.feature&_CPU_FEATURE_SSE2)	strcat(features,", SSE2");
	Msg("* CPU Features: %s\n",features);

	Fidentity.identity		();	// Identity matrix
	Didentity.identity		();	// Identity matrix
	pvInitializeStatics		();	// Lookup table for compressed normals
	FPU::initialize			();
	_initialize_cpu_thread	();
}

#ifdef M_BORLAND
void _initialize_cpu_thread	()
{
}
#else
// per-thread initialization
#include <xmmintrin.h>
#define _MM_DENORMALS_ZERO_MASK 0x0040
#define _MM_DENORMALS_ZERO_ON 0x0040
#define _MM_FLUSH_ZERO_MASK 0x8000
#define _MM_FLUSH_ZERO_ON 0x8000
#define _MM_SET_FLUSH_ZERO_MODE(mode) _mm_setcsr((_mm_getcsr() & ~_MM_FLUSH_ZERO_MASK) | (mode))
#define _MM_SET_DENORMALS_ZERO_MODE(mode) _mm_setcsr((_mm_getcsr() & ~_MM_DENORMALS_ZERO_MASK) | (mode))
static	BOOL	_denormals_are_zero_supported	= TRUE;
void debug_on_thread_spawn	();

void _initialize_cpu_thread	()
{
	debug_on_thread_spawn	();
#ifndef XRCORE_STATIC
	// fpu & sse 
	FPU::m24r	();
#endif  // XRCORE_STATIC
	if (CPU::ID.feature&_CPU_FEATURE_SSE)	{
		//_mm_setcsr ( _mm_getcsr() | (_MM_FLUSH_ZERO_ON+_MM_DENORMALS_ZERO_ON) );
		_MM_SET_FLUSH_ZERO_MODE			(_MM_FLUSH_ZERO_ON);
		if (_denormals_are_zero_supported)	{
			__try	{
				_MM_SET_DENORMALS_ZERO_MODE	(_MM_DENORMALS_ZERO_ON);
			} __except(EXCEPTION_EXECUTE_HANDLER) {
				_denormals_are_zero_supported	= FALSE;
			}
		}
	}
}
#endif
#pragma pack(push,8)
struct THREAD_NAME	{
	DWORD	dwType;
	LPCSTR	szName;
	DWORD	dwThreadID;
	DWORD	dwFlags;
};
void	thread_name	(const char* name)
{
	THREAD_NAME		tn;
	tn.dwType		= 0x1000;
	tn.szName		= name;
	tn.dwThreadID	= DWORD(-1);
	tn.dwFlags		= 0;
	__try
	{
		RaiseException(0x406D1388,0,sizeof(tn)/sizeof(ULONG_PTR),(ULONG_PTR*)&tn);
	}
	__except(EXCEPTION_CONTINUE_EXECUTION)
	{
	}
}
#pragma pack(pop)

struct	THREAD_STARTUP
{
	thread_t*	entry	;
	char*		name	;
	void*		args	;
};
void	__cdecl			thread_entry	(void*	_params )	{
	// initialize
	THREAD_STARTUP*		startup	= (THREAD_STARTUP*)_params	;
	thread_name			(startup->name);
	thread_t*			entry	= startup->entry;
	void*				arglist	= startup->args;
	xr_delete			(startup);
	_initialize_cpu_thread		();

	// call
	entry				(arglist);
}

void	thread_spawn	(thread_t*	entry, const char*	name, unsigned	stack, void* arglist )
{
	THREAD_STARTUP*		startup	= xr_new<THREAD_STARTUP>	();
	startup->entry		= entry;
	startup->name		= (char*)name;
	startup->args		= arglist;
	_beginthread		(thread_entry,stack,startup);
}

void spline1	( float t, Fvector *p, Fvector *ret )
{
	float     t2  = t * t;
	float     t3  = t2 * t;
	float     m[4];

	ret->x=0.0f;
	ret->y=0.0f;
	ret->z=0.0f;
	m[0] = ( 0.5f * ( (-1.0f * t3) + ( 2.0f * t2) + (-1.0f * t) ) );
	m[1] = ( 0.5f * ( ( 3.0f * t3) + (-5.0f * t2) + ( 0.0f * t) + 2.0f ) );
	m[2] = ( 0.5f * ( (-3.0f * t3) + ( 4.0f * t2) + ( 1.0f * t) ) );
	m[3] = ( 0.5f * ( ( 1.0f * t3) + (-1.0f * t2) + ( 0.0f * t) ) );

	for( int i=0; i<4; i++ )
	{
		ret->x += p[i].x * m[i];
		ret->y += p[i].y * m[i];
		ret->z += p[i].z * m[i];
	}
}

void spline2( float t, Fvector *p, Fvector *ret )
{
	float	s= 1.0f - t;
	float   t2 = t * t;
	float   t3 = t2 * t;
	float   m[4];

	m[0] = s*s*s;
	m[1] = 3.0f*t3 - 6.0f*t2 + 4.0f;
	m[2] = -3.0f*t3 + 3.0f*t2 + 3.0f*t +1;
	m[3] = t3;

	ret->x = (p[0].x*m[0]+p[1].x*m[1]+p[2].x*m[2]+p[3].x*m[3])/6.0f;
	ret->y = (p[0].y*m[0]+p[1].y*m[1]+p[2].y*m[2]+p[3].y*m[3])/6.0f;
	ret->z = (p[0].z*m[0]+p[1].z*m[1]+p[2].z*m[2]+p[3].z*m[3])/6.0f;
}

#define beta1 1.0f
#define beta2 0.8f

void spline3( float t, Fvector *p, Fvector *ret )
{
	float	s= 1.0f - t;
	float   t2 = t * t;
	float   t3 = t2 * t;
	float	b12=beta1*beta2;
	float	b13=b12*beta1;
	float	delta=2.0f-b13+4.0f*b12+4.0f*beta1+beta2+2.0f;
	float	d=1.0f/delta;
	float	b0=2.0f*b13*d*s*s*s;
	float	b3=2.0f*t3*d;
	float	b1=d*(2*b13*t*(t2-3*t+3)+2*b12*(t3-3*t2+2)+2*beta1*(t3-3*t+2)+beta2*(2*t3-3*t2+1));
	float	b2=d*(2*b12*t2*(-t+3)+2*beta1*t*(-t2+3)+beta2*t2*(-2*t+3)+2*(-t3+1));

	ret->x = p[0].x*b0+p[1].x*b1+p[2].x*b2+p[3].x*b3;
	ret->y = p[0].y*b0+p[1].y*b1+p[2].y*b2+p[3].y*b3;
	ret->z = p[0].z*b0+p[1].z*b1+p[2].z*b2+p[3].z*b3;
}
