#pragma	once

class	smapvis		: public	R_feedback
{
public:
	enum			{
		state_counting	= 0,
		state_working	= 1,
		state_usingTC	= 3,
	}							state;
	xr_vector<IRender_Visual*>	invisible;

	u32							frame_sleep;
	u32							test_count;
	u32							test_current;
	IRender_Visual*				testQ_V;
	u32							testQ_id;
	u32							testQ_frame;
	bool						testQ_pending;
public:
	smapvis			();
	~smapvis		();

	void			invalidate	();
	void			begin		();
	void			end			();
	void			mark		();
	void			flushoccq	();

	void			resetoccq	();

	IC	bool		sleep		()			{ return Device.dwFrame > frame_sleep; }

	virtual		void	rfeedback_static	(IRender_Visual*	V);
};
