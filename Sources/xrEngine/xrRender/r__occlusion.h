#pragma once

const		u32					occq_size			= 2*768;

class R_occlusion
{
private:
	struct	_Q	{
		u32					order;
		IDirect3DQuery9*	Q;
	};

	BOOL					enabled;
	xr_vector<_Q>			pool;
	xr_vector<_Q>			used;
	xr_vector<u32>			fids;

	bool			issued			(u32	ID		) const;
public:
	static const u32		invalid_id		= u32(-1);

	R_occlusion		();
	~R_occlusion	();

	void			occq_create		(u32	limit	);
	void			occq_destroy	(				);
	u32				occq_begin		(u32&	ID		);
	void			occq_end		(u32&	ID		);
	u32				occq_get		(u32&	ID		);
};
