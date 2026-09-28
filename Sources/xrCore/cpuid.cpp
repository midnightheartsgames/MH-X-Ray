#include "stdafx.h"
#pragma hdrstop

#include "cpuid.h"
#include <intrin.h>

static const u32 mmx_feature_bit		= 1u << 23;
static const u32 sse_feature_bit		= 1u << 25;
static const u32 sse2_feature_bit		= 1u << 26;
static const u32 amd_3dnow_feature_bit	= 1u << 31;

static const int standard_leaf			= 0;
static const int feature_leaf			= 1;
static const int extended_leaf			= int(0x80000000);
static const int extended_feature_leaf	= int(0x80000001);

void map_mname( int family, int model, const char * v_name, char *m_name)
{
    if (!strncmp("AuthenticAMD", v_name, 12))
    {
        switch (family)
        {
        case 4:
            strcpy (m_name,"Am486");
            break;

        case 5:
            switch (model)
            {
            case 0:		strcpy (m_name,"K5 Model 0");	break;
            case 1:		strcpy (m_name,"K5 Model 1");	break;
            case 2:		strcpy (m_name,"K5 Model 2");	break;
            case 3:		strcpy (m_name,"K5 Model 3");	break;
            case 4:     break;
            case 5:     break;
            case 6:		strcpy (m_name,"K6 Model 1");	break;
            case 7:		strcpy (m_name,"K6 Model 2");	break;
            case 8:		strcpy (m_name,"K6-2");			break;
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:	strcpy (m_name,"K6-3");			break;
            default:	strcpy (m_name,"K6 family");	break;
            }
            break;

        case 6:
            switch(model)
            {
            case 1:		strcpy (m_name,"ATHLON Model 1");	break;
			case 2:		strcpy (m_name,"ATHLON Model 2");	break;
			case 3:		strcpy (m_name,"DURON");			break;
			case 4:
			case 5:		strcpy (m_name,"ATHLON TB");		break;
			case 6:		strcpy (m_name,"ATHLON XP");		break;
			case 7:		strcpy (m_name,"DURON XP");			break;
            default:    strcpy (m_name,"K7 Family");		break;
			}
            break;
        }
    } else if ( !strncmp("GenuineIntel", v_name, 12))
    {
        switch (family)
        {
        case 4:
            switch (model)
            {
            case 0:
            case 1:		strcpy (m_name,"i486DX");			break;
            case 2:		strcpy (m_name,"i486SX");			break;
            case 3:		strcpy (m_name,"i486DX2");			break;
            case 4:		strcpy (m_name,"i486SL");			break;
            case 5:		strcpy (m_name,"i486SX2");			break;
            case 7:		strcpy (m_name,"i486DX2E");			break;
            case 8:		strcpy (m_name,"i486DX4");			break;
            default:    strcpy (m_name,"i486 family");		break;
            }
            break;
        case 5:
            switch (model)
            {
            case 1:
            case 2:
            case 3:		strcpy (m_name,"Pentium");			break;
            case 4:		strcpy (m_name,"Pentium-MMX");		break;
            default:	strcpy (m_name,"P5 family");		break;
            }
            break;
        case 6:
            switch (model)
            {
            case 1:		strcpy (m_name,"Pentium-Pro");		break;
            case 3:		strcpy (m_name,"Pentium-II");		break;
            case 5:		strcpy (m_name,"Pentium-II");		break;
            case 6:		strcpy (m_name,"Celeron");			break;
            case 7:		strcpy (m_name,"Pentium-III");		break;
			case 8:		strcpy (m_name,"P3 Coppermine");	break;
            default:	strcpy (m_name,"P3 family");		break;
            }
            break;
		case 15:
			switch (model)
			{
			case 2:		strcpy	(m_name,"Pentium 4");		break;
			default:	strcpy	(m_name,"P4 family");		break;
			}
        }
    } else
    {
        strcpy (m_name, "Unknown");
    }
}

int _cpuid (_processor_info *pinfo)
{
	int		registers[4];

	__cpuid	(registers, standard_leaf);
	int		max_standard_leaf	= registers[0];

	char	vendor[12+1];
	memcpy	(vendor + 0, &registers[1], 4);
	memcpy	(vendor + 4, &registers[3], 4);
	memcpy	(vendor + 8, &registers[2], 4);
	vendor[12]	= 0;

	u32		standard	= 0;
	u32		features	= 0;
	if (max_standard_leaf >= feature_leaf)
	{
		__cpuid		(registers, feature_leaf);
		standard	= u32(registers[0]);
		features	= u32(registers[3]);
	}

	u32		extended_features	= 0;
	__cpuid	(registers, extended_leaf);
	if (u32(registers[0]) >= u32(extended_feature_leaf))
	{
		__cpuid				(registers, extended_feature_leaf);
		extended_features	= u32(registers[3]);
	}

	int		feature	= 0;
	if (features & mmx_feature_bit)				feature |= _CPU_FEATURE_MMX;
	if (extended_features & amd_3dnow_feature_bit)	feature |= _CPU_FEATURE_3DNOW;
	if (features & sse_feature_bit)				feature |= _CPU_FEATURE_SSE;
	if (features & sse2_feature_bit)			feature |= _CPU_FEATURE_SSE2;

	if (pinfo)
	{
		memset		(pinfo, 0, sizeof(_processor_info));
		pinfo->os_support	= feature;
		pinfo->feature		= feature;
		pinfo->family		= (standard >> 8) & 0xF;
		pinfo->model		= (standard >> 4) & 0xF;
		pinfo->stepping		= standard & 0xF;
		strcpy		(pinfo->v_name, vendor);
		map_mname	(pinfo->family, pinfo->model, pinfo->v_name, pinfo->model_name);
	}
	return feature;
}
