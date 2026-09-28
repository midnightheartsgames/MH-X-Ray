#include "StdAfx.h"
#include "..\xrRender\light.h"

		smapvis::smapvis	()
{
	invalidate				();
	frame_sleep				= 0;
	test_count				= 0;
	test_current			= 0;
	testQ_V					= 0;
	testQ_id				= R_occlusion::invalid_id;
	testQ_frame				= 0;
	testQ_pending			= false;
}
		smapvis::~smapvis	()
{
	resetoccq				();
	invalidate				();
}
void	smapvis::invalidate	()
{
	state		=	state_counting;
	frame_sleep	=	Device.dwFrame + ps_r__LightSleepFrames;
	invisible.clear	();
}
void	smapvis::begin		()
{
	RImplementation.clear_Counters		();
	switch	(state)
	{
	case state_counting:	
		// do nothing -> we just prepare for testing process
		break;
	case state_working:
		// mark already known to be invisible visuals, set breakpoint
		testQ_V							= 0;
		testQ_id						= 0;
		mark							();
		RImplementation.set_Feedback	(this,test_current);
		break;
	case state_usingTC:
		// just mark
		mark						();
		break;
	}
}
void	smapvis::end		()
{
	u32	ts,td;
	RImplementation.get_Counters	(ts,td);
	RImplementation.stats.ic_total	+=	ts;
	RImplementation.set_Feedback	(0,0);

	switch	(state)			{
	case state_counting:
		if (sleep())		{
			test_count						= ts;
			test_current					= 0;
			state							= state_working;
		}
		break;
	case state_working:
		if (testQ_V)
		{
			RImplementation.occq_begin				(testQ_id);
			RImplementation.marker					+= 1;
			RImplementation.r_dsgraph_insert_static	(testQ_V);
			RImplementation.r_dsgraph_render_graph	(0);
			RImplementation.occq_end				(testQ_id);
			testQ_frame								= Device.dwFrame + 1;
			testQ_pending							= true;
		}
		break;
	case state_usingTC:
		break;
	}
}

void	smapvis::flushoccq	()
{
	if	(!testQ_pending || (testQ_frame > Device.dwFrame))	return;
	testQ_pending		= false;
	u32	fragments		= RImplementation.occq_get(testQ_id);
	if	((testQ_frame != Device.dwFrame) || (state != state_working))	return;
	if	(0==fragments)			{
		invisible.push_back	(testQ_V);
		test_count			--;
		testQ_V				= 0;
	} else {
		test_current		++;
	}
	if (test_current==test_count)	state	= state_usingTC;
}
void	smapvis::resetoccq	()
{
	if (testQ_frame==(Device.dwFrame+1))		testQ_frame--;
	flushoccq		();
}

void	smapvis::mark				()
{
	RImplementation.stats.ic_culled	+= invisible.size	();
	u32		marker			= RImplementation.marker + 1;	// we are called befor marker increment
	for		(u32 it=0; it<invisible.size(); it++)
		invisible[it]->vis.marker	= marker;				// this effectively disables processing
}

void	smapvis::rfeedback_static	(IRender_Visual* V)
{
	testQ_V							= V;
	RImplementation.set_Feedback	(0,0);
}
