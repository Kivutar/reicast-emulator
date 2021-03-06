#include "spg.h"
#include "Renderer_if.h"
#include "pvr_regs.h"
#include "hw/holly/holly_intc.h"
#include "hw/sh4/sh4_sched.h"

//SPG emulation; Scanline/Raster beam registers & interrupts
//Time to emulate that stuff correctly ;)

u32 in_vblank=0;
u32 clc_pvr_scanline;
u32 pvr_numscanlines=512;
u32 prv_cur_scanline=-1;
u32 vblk_cnt=0;

float last_fps=0;

u32 Line_Cycles=0;
u32 Frame_Cycles=0;
static int render_end_sched;
static int vblank_sched;
int time_sync;

double speed_load_mspdf;

int mips_counter;

double full_rps;

u32 fskip=0;

void CalculateSync(void)
{
   /*                          00=VGA    01=NTSC   10=PAL,   11=illegal/undocumented */
   const int spg_clks[4]   = { 26944080, 13458568, 13462800, 26944080 };
	float scale_x           = 1;
   float scale_y           = 1;
	u32 pixel_clock         = spg_clks[(SPG_CONTROL.full >> 6) & 3];
	pvr_numscanlines        = SPG_LOAD.vcount+1;
	Line_Cycles             = (u32)((u64)SH4_MAIN_CLOCK*(u64)(SPG_LOAD.hcount+1)/(u64)pixel_clock);
	
	if (SPG_CONTROL.interlace)
	{
		//this is a temp hack
		Line_Cycles         /= 2;
		u32 interl_mode      = VO_CONTROL.field_mode;
		
		//if (interl_mode==2)//3 will be funny =P
		//  scale_y=0.5f;//single interlace
		//else
			scale_y=1;
	}
	else
	{
		if (FB_R_CTRL.vclk_div)
			scale_y           = 1.0f;//non interlaced VGA mode has full resolution :)
		else
			scale_y           = 0.5f;//non interlaced modes have half resolution
	}

	rend_set_fb_scale(scale_x,scale_y);
	
	Frame_Cycles            = pvr_numscanlines*Line_Cycles;
	prv_cur_scanline        = 0;

	sh4_sched_request(vblank_sched, Line_Cycles);
}

static int elapse_time(int tag, int cycl, int jit)
{
	return min(max(Frame_Cycles,(u32)1*1000*1000),(u32)8*1000*1000);
}

//called from sh4 context , should update pvr/ta state and everything else
static int spg_line_sched(int tag, int cycl, int jit)
{
	clc_pvr_scanline       += cycl;

	while (clc_pvr_scanline >=  Line_Cycles)//60 ~hertz = 200 mhz / 60=3333333.333 cycles per screen refresh
	{
		//ok .. here , after much effort , we did one line
		//now , we must check for raster beam interrupts and vblank
		prv_cur_scanline     = (prv_cur_scanline+1) % pvr_numscanlines;
		clc_pvr_scanline    -= Line_Cycles;
		//Check for scanline interrupts -- really need to test the scanline values
		
		if (SPG_VBLANK_INT.vblank_in_interrupt_line_number == prv_cur_scanline)
			asic_RaiseInterrupt(holly_SCANINT1);

		if (SPG_VBLANK_INT.vblank_out_interrupt_line_number == prv_cur_scanline)
			asic_RaiseInterrupt(holly_SCANINT2);

		if (SPG_VBLANK.vstart == prv_cur_scanline)
			in_vblank = 1;

		if (SPG_VBLANK.vbend == prv_cur_scanline)
			in_vblank = 0;

		SPG_STATUS.vsync    = in_vblank;
		SPG_STATUS.scanline = prv_cur_scanline;
		
		//Vblank start -- really need to test the scanline values
		if (prv_cur_scanline==0)
		{
         SPG_STATUS.fieldnum = 0;
			if (SPG_CONTROL.interlace)
				SPG_STATUS.fieldnum = ~SPG_STATUS.fieldnum;

			/* Vblank counter */
			vblk_cnt++;
			asic_RaiseInterrupt(holly_HBLank);// -> This turned out to be HBlank btw , needs to be emulated ;(
			//TODO : rend_if_VBlank();
			rend_vblank();//notify for vblank :)
			
		}
	}

	//interrupts
	//0
	//vblank_in_interrupt_line_number
	//vblank_out_interrupt_line_number
	//vstart
	//vbend
	//pvr_numscanlines
	u32 min_scanline=prv_cur_scanline+1;
	u32 min_active=pvr_numscanlines;

	if (min_scanline<SPG_VBLANK_INT.vblank_in_interrupt_line_number)
		min_active=min(min_active,SPG_VBLANK_INT.vblank_in_interrupt_line_number);

	if (min_scanline<SPG_VBLANK_INT.vblank_out_interrupt_line_number)
		min_active=min(min_active,SPG_VBLANK_INT.vblank_out_interrupt_line_number);

	if (min_scanline<SPG_VBLANK.vstart)
		min_active=min(min_active,SPG_VBLANK.vstart);

	if (min_scanline<SPG_VBLANK.vbend)
		min_active=min(min_active,SPG_VBLANK.vbend);

	if (min_scanline<pvr_numscanlines)
		min_active=min(min_active,pvr_numscanlines);

	min_active=max(min_active,min_scanline);

	return (min_active-prv_cur_scanline)*Line_Cycles;
}

static int rend_end_sch(int tag, int cycl, int jitt)
{
	asic_RaiseInterrupt(holly_RENDER_DONE);
	asic_RaiseInterrupt(holly_RENDER_DONE_isp);
	asic_RaiseInterrupt(holly_RENDER_DONE_vd);
	rend_end_render();
	return 0;
}

bool spg_Init(void)
{
	render_end_sched = sh4_sched_register(0,&rend_end_sch);
	vblank_sched     = sh4_sched_register(0,&spg_line_sched);
	time_sync        = sh4_sched_register(0,&elapse_time);

	sh4_sched_request(time_sync,8*1000*1000);

	return true;
}

void spg_Term(void)
{
}

void spg_Reset(bool Manual)
{
	CalculateSync();
}

void SetREP(TA_context* cntx)
{
   unsigned pending_cycles = 4096;
	if (cntx && !cntx->rend.Overrun)
	{
		pending_cycles  = cntx->rend.verts.used()*60;
		pending_cycles += 500000*3;
		VertexCount    += cntx->rend.verts.used();
	}

   sh4_sched_request(render_end_sched, pending_cycles);
}
