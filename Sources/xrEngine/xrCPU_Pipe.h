#ifndef xrCPU_PipeH
#define xrCPU_PipeH
#pragma once

struct	ENGINE_API	vertRender;
struct	ENGINE_API	vertBoned1W;
struct	ENGINE_API	vertBoned2W;
class	ENGINE_API	CBoneInstance;

typedef void	__stdcall	xrSkin1W		(vertRender* D, vertBoned1W* S, u32 vCount, CBoneInstance* Bones);
typedef void	__stdcall	xrSkin2W		(vertRender* D, vertBoned2W* S, u32 vCount, CBoneInstance* Bones);

#pragma pack(push,8)
struct xrDispatchTable
{
	xrSkin1W*			skin1W;
	xrSkin2W*			skin2W;
};
#pragma pack(pop)

typedef void	__cdecl	xrBinder	(xrDispatchTable* T);

#endif
