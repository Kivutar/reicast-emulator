/*
	aica interface
		Handles RTC, Display mode reg && arm reset reg !
	arm7 is handled on a separate arm plugin now
*/
#include "types.h"
#include "aica_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/holly/sb.h"
#include "types.h"
#include "hw/holly/holly_intc.h"

#include <time.h>

VArray2 aica_ram;
u32 VREG;//video reg =P
u32 ARMRST;//arm reset reg
u32 rtc_EN=0;

u32 GetRTC_now(void)
{
	
	time_t rawtime=0;
	tm  timeinfo={0};
	timeinfo.tm_year=1998-1900;
	timeinfo.tm_mon=11-1;
	timeinfo.tm_mday=27;
	timeinfo.tm_hour=0;
	timeinfo.tm_min=0;
	timeinfo.tm_sec=0;

	rawtime=mktime( &timeinfo );
	
	rawtime=time (0)-rawtime;//get delta of time since the known dc date
	
	time_t temp=time(0);
	timeinfo=*localtime(&temp);
	if (timeinfo.tm_isdst)
		rawtime+=24*3600;//add an hour if dst (maybe rtc has a reg for that ? *watch* and add it if yes :)

	u32 RTC=0x5bfc8900 + (u32)rawtime;// add delta to known dc time
	return RTC;
}

u32 ReadMem_aica_rtc(u32 addr,u32 sz)
{
   //settings.dreamcast.RTC=GetRTC_now();
   switch( addr & 0xFF )
   {
      case 0:
         return settings.dreamcast.RTC>>16;
      case 4:
         return settings.dreamcast.RTC &0xFFFF;
      case 8:
         break;
      default:
         printf("ReadMem_aica_rtc : invalid address\n");
         break;
   }

   return 0;
}

void WriteMem_aica_rtc(u32 addr,u32 data,u32 sz)
{
   switch( addr & 0xFF )
   {
      case 0:
         if (rtc_EN)
         {
            settings.dreamcast.RTC&=0xFFFF;
            settings.dreamcast.RTC|=(data&0xFFFF)<<16;
            rtc_EN=0;
            SaveSettings();
         }
         break;
      case 4:
         if (rtc_EN)
         {
            settings.dreamcast.RTC&=0xFFFF0000;
            settings.dreamcast.RTC|= data&0xFFFF;
            //TODO: Clean the internal timer ?
         }
         break;
      case 8:
         rtc_EN = data&1;
         break;
   }
}

u32 ReadMem_aica_reg(u32 addr,u32 sz)
{
	addr&=0x7FFF;

   switch (addr)
   {
      case 0x2C00:
         if (sz == 1)
            return ARMRST;
         return (VREG<<8) | ARMRST;
      case 0x2C01:
         if (sz == 1)
            return VREG;
         /* fall-through */
      default:
         break;
   }

   return libAICA_ReadReg(addr, sz);
}

void ArmSetRST(void)
{
	ARMRST&=1;
	libARM_SetResetState(ARMRST);
}

void WriteMem_aica_reg(u32 addr,u32 data,u32 sz)
{
	addr&=0x7FFF;

   switch (addr)
   {
      case 0x2C00: /* ARMRST */
         ARMRST = data;

         if (sz != 1)
         {
            VREG   = (data>>8)&0xFF;
            ARMRST &= 0xFF;
         }
         ArmSetRST();
         break;
      case 0x2C01: /* VREG */
         if (sz == 1)
         {
            VREG=data;
            break;
         }
         /* fall-through */
      default:
         libAICA_WriteReg(addr,data,sz);
         break;
   }
}

//Init/res/term
void aica_Init(void)
{
	//mmnnn ? gotta fill it w/ something
}

void aica_Reset(bool Manual)
{
	if (!Manual)
		aica_ram.Zero();
}

void aica_Term(void)
{

}

s32 aica_pending_dma = 0;

void aica_periodical(u32 cycl)
{
	if (aica_pending_dma > 0)
	{
		verify(SB_ADST==1);

		cycl = (aica_pending_dma <= 0) ? 0 : cycl;
		aica_pending_dma-=cycl;

		if (aica_pending_dma <= 0)
		{
			//log("%u %d\n",cycl,(s32)aica_pending_dma);
			asic_RaiseInterrupt(holly_SPU_DMA);
			aica_pending_dma = 0;
			SB_ADST=0;
		}
	}
}


void Write_SB_ADST(u32 addr, u32 data)
{
   //0x005F7800	SB_ADSTAG	RW	AICA:G2-DMA G2 start address 
   //0x005F7804	SB_ADSTAR	RW	AICA:G2-DMA system memory start address 
   //0x005F7808	SB_ADLEN	RW	AICA:G2-DMA length 
   //0x005F780C	SB_ADDIR	RW	AICA:G2-DMA direction 
   //0x005F7810	SB_ADTSEL	RW	AICA:G2-DMA trigger select 
   //0x005F7814	SB_ADEN	RW	AICA:G2-DMA enable 
   //0x005F7818	SB_ADST	RW	AICA:G2-DMA start 
   //0x005F781C	SB_ADSUSP	RW	AICA:G2-DMA suspend 
   u32 src, dst, len;
   bool check = (data & 1) && (SB_ADEN & 1);

   if (!check)
      return;

   src=SB_ADSTAR;
   dst=SB_ADSTAG;
   len=SB_ADLEN & 0x7FFFFFFF;

   u32 total_bytes=0;

   if ((SB_ADDIR&1)==1)
   {
      //swap direction
      u32 tmp=src;
      src=dst;
      dst=tmp;
#ifndef NDEBUG
      printf("**AICA DMA : SB_ADDIR==1: Not sure this works, please report if broken/missing sound or crash\n**");
#endif
   }

   WriteMemBlock_nommu_dma(dst,src,len);
   SB_ADEN    = 0;

   if (SB_ADLEN & 0x80000000)
      SB_ADEN = 1;

   SB_ADSTAR += len;
   SB_ADSTAG += len;
   total_bytes+=len;
   SB_ADLEN   = 0x00000000;
   if (settings.aica.InterruptHack)
      SB_ADST    = 1;
   else
      SB_ADST    = 0x00000000;//dma done

   aica_pending_dma=((total_bytes*200000000)/65536)+1;

   if (!settings.aica.InterruptHack)
      asic_RaiseInterruptWait(holly_SPU_DMA);
}

void Write_SB_E1ST(u32 addr, u32 data)
{
   //0x005F7800	SB_ADSTAG	RW	AICA:G2-DMA G2 start address 
   //0x005F7804	SB_ADSTAR	RW	AICA:G2-DMA system memory start address 
   //0x005F7808	SB_ADLEN	RW	AICA:G2-DMA length 
   //0x005F780C	SB_ADDIR	RW	AICA:G2-DMA direction 
   //0x005F7810	SB_ADTSEL	RW	AICA:G2-DMA trigger select 
   //0x005F7814	SB_ADEN	RW	AICA:G2-DMA enable 
   //0x005F7818	SB_ADST	RW	AICA:G2-DMA start 
   //0x005F781C	SB_ADSUSP	RW	AICA:G2-DMA suspend 
   u32 src, dst, len;
   bool check = (data & 1) && (SB_E1EN & 1);

   src=SB_E1STAR;
   dst=SB_E1STAG;
   len=SB_E1LEN & 0x7FFFFFFF;

   if (SB_E1DIR==1)
   {
      u32 t=src;
      src=dst;
      dst=t;
      printf("G2-EXT1 DMA : SB_E1DIR==1 DMA Read to 0x%X from 0x%X %d bytes\n",dst,src,len);
   }
   else
      printf("G2-EXT1 DMA : SB_E1DIR==0:DMA Write to 0x%X from 0x%X %d bytes\n",dst,src,len);

   WriteMemBlock_nommu_dma(dst,src,len);
   SB_E1EN=0;

   if (SB_E1LEN & 0x80000000)
      SB_E1EN=1;

   SB_E1STAR+=len;
   SB_E1STAG+=len;
   SB_E1ST = 0x00000000;//dma done
   SB_E1LEN = 0x00000000;

   asic_RaiseInterruptWait(holly_EXT_DMA1);
}

void aica_sb_Init(void)
{
	//NRM
	//6
	sb_rio_register(SB_ADST_addr,RIO_WF,0,&Write_SB_ADST);
	//sb_regs[((SB_ADST_addr-SB_BASE)>>2)].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	//sb_regs[((SB_ADST_addr-SB_BASE)>>2)].writeFunction=Write_SB_ADST;

	//I really need to implement G2 dma (and rest dmas actually) properly
	//THIS IS NOT AICA, its G2-EXT (BBA)

	sb_rio_register(SB_E1ST_addr,RIO_WF,0,&Write_SB_E1ST);

	//sb_regs[((SB_E1ST_addr-SB_BASE)>>2)].flags=REG_32BIT_READWRITE | REG_READ_DATA;
	//sb_regs[((SB_E1ST_addr-SB_BASE)>>2)].writeFunction=Write_SB_E1ST;
}

void aica_sb_Reset(bool Manual)
{
}

void aica_sb_Term()
{
}
