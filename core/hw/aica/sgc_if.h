#pragma once
#include "aica.h"

void AICA_Sample();
void AICA_Sample32();

//u32 ReadChannelReg(u32 channel,u32 reg);
void WriteChannelReg8(u32 channel,u32 reg);

void sgc_Init();
void sgc_Term();

union fp_22_10
{
	struct
	{
#ifdef MSB_FIRST
		u32 ip:22;
		u32 fp:10;
#else
		u32 fp:10;
		u32 ip:22;
#endif
	};
	u32 full;
};
union fp_s_22_10
{
	struct
	{
#ifdef MSB_FIRST
		s32 ip:22;
		u32 fp:10;
#else
		u32 fp:10;
		s32 ip:22;
#endif
	};
	s32 full;
};
union fp_20_12
{
	struct
	{
#ifdef MSB_FIRST
		u32 ip:20;
		u32 fp:12;
#else
		u32 fp:12;
		u32 ip:20;
#endif
	};
	u32 full;
};

//#define SAMPLE_TYPE_SHIFT (8)
typedef s32 SampleType;

void ReadCommonReg(u32 reg,bool byte);
void WriteCommonReg8(u32 reg,u32 data);
