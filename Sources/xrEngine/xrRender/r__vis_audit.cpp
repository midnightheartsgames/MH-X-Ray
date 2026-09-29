#include "stdafx.h"

#ifdef DEBUG

#include "..\xrLevel.h"
#include "..\igame_persistent.h"
#include "..\environment.h"
#include "..\fhierrarhyvisual.h"
#include "..\fmesh.h"
#include "r__sector.h"

namespace
{
	const float		audit_eye_height		= 1.7f;
	const float		audit_point_radius		= 0.05f;
	const float		audit_grid_cell			= 8.f;
	const u32		audit_examples			= 12;
	const u32		audit_worst_poses		= 24;
	const u32		audit_distance_buckets	= 5;
	const float		audit_distance_limits[audit_distance_buckets]	= { 25.f, 50.f, 100.f, 200.f, flt_max };

	enum EAuditVerdict
	{
		verdict_drawn,
		verdict_unmatched,
		verdict_hom_visual,
		verdict_hom_portal,
		verdict_ssa,
		verdict_frustum,
		verdict_sector,
		verdict_unreached,
		verdict_count
	};

	LPCSTR			audit_verdict_names[verdict_count]	= { "drawn", "unmatched", "hom_visual", "hom_portal", "ssa", "frustum", "sector", "unreached" };

	struct SAuditPose
	{
		Fvector		eye;
		float		yaw;
		u32			start;
		u32			hits;
		u32			verdicts[verdict_count];
	};

	struct SAuditExample
	{
		Fvector		eye;
		float		yaw;
		Fvector		hit;
		float		distance;
		u32			start;
		u32			sector;
	};

	struct SAuditLeaf
	{
		IRender_Visual*		visual;
		IRender_Visual*		lod;
		Fbox				box;
	};

	class CAuditSectorView
	{
		xr_vector<xr_vector<CFrustum> >		m_frustums;
	public:
		void		capture		(xr_map<IRender_Sector*,u32>& index)
		{
			m_frustums.resize		(index.size());
			for (u32 it=0; it<m_frustums.size(); it++)
				m_frustums[it].clear	();
			for (u32 it=0; it<PortalTraverser.r_sectors.size(); it++)
			{
				CSector*	sector		= (CSector*)PortalTraverser.r_sectors[it];
				m_frustums[index[sector]]	= sector->r_frustums;
			}
		}
		u32			count		() const
		{
			return	m_frustums.size();
		}
		xr_vector<CFrustum>&	frustums	(u32 sector)
		{
			return	m_frustums[sector];
		}
		bool		reached		(u32 sector) const
		{
			return	!m_frustums[sector].empty();
		}
		bool		contains	(u32 sector, Fvector& point) const
		{
			const xr_vector<CFrustum>&	frustums	= m_frustums[sector];
			for (u32 it=0; it<frustums.size(); it++)
				if (frustums[it].testSphere_dirty(point,audit_point_radius))
					return	true;
			return	false;
		}
	};

	class CAuditLeafIndex
	{
		Fbox							m_bounds;
		int								m_size_x;
		int								m_size_z;
		xr_vector<xr_vector<u32> >		m_cells;

		int			cell_x		(float x) const
		{
			int		result		= iFloor((x-m_bounds.min.x)/audit_grid_cell);
			clamp	(result,0,m_size_x-1);
			return	result;
		}
		int			cell_z		(float z) const
		{
			int		result		= iFloor((z-m_bounds.min.z)/audit_grid_cell);
			clamp	(result,0,m_size_z-1);
			return	result;
		}
	public:
		void		build		(xr_vector<SAuditLeaf>& leaves)
		{
			m_bounds.invalidate	();
			for (u32 it=0; it<leaves.size(); it++)
				m_bounds.merge	(leaves[it].box);
			m_size_x		= iFloor((m_bounds.max.x-m_bounds.min.x)/audit_grid_cell)+1;
			m_size_z		= iFloor((m_bounds.max.z-m_bounds.min.z)/audit_grid_cell)+1;
			m_cells.clear	();
			m_cells.resize	(m_size_x*m_size_z);
			for (u32 it=0; it<leaves.size(); it++)
			{
				Fbox&	box		= leaves[it].box;
				for (int x=cell_x(box.min.x); x<=cell_x(box.max.x); x++)
					for (int z=cell_z(box.min.z); z<=cell_z(box.max.z); z++)
						m_cells[x*m_size_z+z].push_back	(it);
			}
		}
		void		query		(xr_vector<SAuditLeaf>& leaves, Fvector& point, xr_vector<u32>& result) const
		{
			result.clear	();
			if (m_cells.empty())
				return;
			const xr_vector<u32>&	cell	= m_cells[cell_x(point.x)*m_size_z+cell_z(point.z)];
			for (u32 it=0; it<cell.size(); it++)
			{
				Fbox	box		= leaves[cell[it]].box;
				box.grow		(audit_point_radius);
				if (box.contains(point))
					result.push_back	(cell[it]);
			}
		}
	};

	class CVisAudit
	{
		xr_map<IRender_Sector*,u32>		m_sector_index;
		CAuditSectorView				m_rendered;
		CAuditSectorView				m_without_hom;
		CAuditSectorView				m_without_culling;
		xr_vector<SAuditLeaf>			m_leaves;
		CAuditLeafIndex					m_leaf_index;
		xr_vector<u8>					m_drawn;
		xr_vector<u8>					m_drawn_without_hom;
		xr_vector<u32>					m_candidates;
		xrXRC							m_xrc;
		u32								m_rays_x;
		u32								m_rays_y;
		u32								m_no_sector;
		u64								m_rays;
		u64								m_hits;
		u64								m_verdicts[verdict_count];
		u64								m_verdict_distances[verdict_count][audit_distance_buckets];
		xr_vector<SAuditExample>		m_examples[verdict_count];
		xr_vector<SAuditPose>			m_poses;

		static float	dual_portal_radius	()
		{
#ifdef XRRENDER_R1_EXPORTS
			return		EPS_L*2;
#else
			return		VIEWPORT_NEAR+EPS_L;
#endif
		}

		void		collect_leaves		()
		{
			xr_map<IRender_Visual*,IRender_Visual*>	lod_parent;
			xr_vector<IRender_Visual*>&	visuals		= RImplementation.Visuals;
			for (u32 it=0; it<visuals.size(); it++)
			{
				if (MT_LOD!=visuals[it]->Type)
					continue;
				FHierrarhyVisual*	lod	= (FHierrarhyVisual*)visuals[it];
				for (u32 child=0; child<lod->children.size(); child++)
					lod_parent[lod->children[child]]	= lod;
			}
			m_leaves.clear		();
			for (u32 it=0; it<visuals.size(); it++)
			{
				IRender_Visual*		visual	= visuals[it];
				if (MT_HIERRARHY==visual->Type || MT_LOD==visual->Type || MT_PARTICLE_GROUP==visual->Type)
					continue;
				SAuditLeaf			leaf;
				leaf.visual			= visual;
				leaf.box			= visual->vis.box;
				xr_map<IRender_Visual*,IRender_Visual*>::iterator	parent	= lod_parent.find(visual);
				leaf.lod			= (parent==lod_parent.end()) ? 0 : parent->second;
				m_leaves.push_back	(leaf);
			}
			m_leaf_index.build	(m_leaves);
		}

		void		reset_hom_cache		()
		{
			xr_vector<IRender_Visual*>&	visuals		= RImplementation.Visuals;
			for (u32 it=0; it<visuals.size(); it++)
				visuals[it]->vis.hom_frame	= 0;
		}

		void		visual_pass			(CAuditSectorView& view, bool hom, xr_vector<u8>& drawn)
		{
			reset_hom_cache		();
			if (!hom)
				RImplementation.HOM.Disable	();
			RImplementation.marker	++;
			RImplementation.r_pmask	(false,false);
			for (u32 sector=0; sector<view.count(); sector++)
			{
				xr_vector<CFrustum>&	frustums	= view.frustums(sector);
				IRender_Visual*			root		= ((CSector*)RImplementation.Sectors[sector])->root();
				for (u32 it=0; it<frustums.size(); it++)
				{
					RImplementation.set_Frustum		(&frustums[it]);
					RImplementation.add_Geometry	(root);
				}
			}
			RImplementation.r_pmask	(true,true);
			if (!hom)
				RImplementation.HOM.Enable	();

			xr_vector<IRender_Visual*>	lods;
			for (R_dsgraph::mapLOD_Node* node=RImplementation.mapLOD.begin(); node!=RImplementation.mapLOD.end(); node++)
				lods.push_back	(node->val.pVisual);
			RImplementation.mapLOD.clear	();
			std::sort			(lods.begin(),lods.end());

			u32		marker		= RImplementation.marker;
			drawn.assign		(m_leaves.size(),0);
			for (u32 it=0; it<m_leaves.size(); it++)
			{
				SAuditLeaf&	leaf	= m_leaves[it];
				if (leaf.visual->vis.marker==marker)
					drawn[it]	= 1;
				else if (leaf.lod && std::binary_search(lods.begin(),lods.end(),leaf.lod))
					drawn[it]	= 1;
			}
		}

		void		mark_dual_portals	(Fvector& eye)
		{
			if (!RImplementation.rmPortals)
				return;
			float		radius		= dual_portal_radius();
			Fvector		box_radius;	box_radius.set(radius,radius,radius);
			RImplementation.Sectors_xrc.box_options	(CDB::OPT_FULL_TEST);
			RImplementation.Sectors_xrc.box_query	(RImplementation.rmPortals,eye,box_radius);
			for (int it=0; it<RImplementation.Sectors_xrc.r_count(); it++)
			{
				CPortal*	portal	= (CPortal*)RImplementation.Portals[RImplementation.rmPortals->get_tris()[RImplementation.Sectors_xrc.r_begin()[it].id].dummy];
				portal->bDualRender	= TRUE;
			}
		}

		void		clear_dual_portals	()
		{
			for (u32 it=0; it<RImplementation.Portals.size(); it++)
				((CPortal*)RImplementation.Portals[it])->bDualRender	= FALSE;
		}

		void		traverse			(CAuditSectorView& view, IRender_Sector* start, CFrustum& frustum, Fvector& eye, u32 options)
		{
			mark_dual_portals			(eye);
			PortalTraverser.traverse	(start,frustum,eye,Device.mFullTransform,options);
			view.capture				(m_sector_index);
			clear_dual_portals			();
		}

		EAuditVerdict	classify_sector	(u32 sector, Fvector& hit)
		{
			if (m_without_hom.contains(sector,hit))			return verdict_hom_portal;
			if (m_without_culling.contains(sector,hit))		return verdict_ssa;
			if (m_without_culling.reached(sector))			return verdict_frustum;
			return		verdict_sector;
		}

		EAuditVerdict	classify		(u32 sector, Fvector& hit)
		{
			m_leaf_index.query	(m_leaves,hit,m_candidates);
			if (m_candidates.empty())
				return	verdict_unmatched;
			for (u32 it=0; it<m_candidates.size(); it++)
				if (m_drawn[m_candidates[it]])
					return	verdict_drawn;
			for (u32 it=0; it<m_candidates.size(); it++)
				if (m_drawn_without_hom[m_candidates[it]])
					return	verdict_hom_visual;
			if (!m_rendered.contains(sector,hit))
				return	classify_sector(sector,hit);
			return		verdict_unreached;
		}

		IRender_Sector*	prepare_pose	(Fvector& eye, Fvector& dir)
		{
			Fvector		up;
			up.set		(0.f,1.f,0.f);

			Device.vCameraPosition.set	(eye);
			Device.vCameraDirection.set	(dir);
			Device.vCameraTop.set		(up);
			Device.vCameraRight.crossproduct	(up,dir);
			Device.mView.build_camera_dir		(eye,dir,up);
			Device.mFullTransform.mul			(Device.mProject,Device.mView);

			CFrustum	frustum;
			frustum.CreateFromMatrix		(Device.mFullTransform,FRUSTUM_P_LRTB|FRUSTUM_P_FAR);
			RImplementation.HOM.reset_skip	();
			RImplementation.HOM.Enable		();
			RImplementation.HOM.Render		(frustum);

			IRender_Sector*	start			= RImplementation.detectSector(eye);
			if (0==start)
				return	0;

			traverse	(m_rendered,start,frustum,eye,CPortalTraverser::VQ_HOM+CPortalTraverser::VQ_SSA+CPortalTraverser::VQ_FADE);
			traverse	(m_without_hom,start,frustum,eye,CPortalTraverser::VQ_SSA+CPortalTraverser::VQ_FADE);
			traverse	(m_without_culling,start,frustum,eye,0);
			visual_pass	(m_rendered,true,m_drawn);
			visual_pass	(m_rendered,false,m_drawn_without_hom);
			return		start;
		}

		Fvector		screen_ray			(Fvector& dir, float x, float y)
		{
			Fvector	ray;
			ray.set	(dir);
			ray.mad	(Device.vCameraRight,x/Device.mProject._11);
			ray.mad	(Device.vCameraTop,y/Device.mProject._22);
			return	ray.normalize();
		}

		void		audit_pose			(Fvector& position, float yaw)
		{
			Fvector		eye,dir;
			eye.set		(position.x,position.y+audit_eye_height,position.z);
			dir.set		(_sin(yaw),0.f,_cos(yaw));
			IRender_Sector*	start	= prepare_pose(eye,dir);
			if (0==start)
			{
				m_no_sector					++;
				return;
			}

			SAuditPose	pose;
			ZeroMemory	(&pose,sizeof(pose));
			pose.eye	= eye;
			pose.yaw	= yaw;
			pose.start	= m_sector_index[start];

			float		far_plane	= g_pGamePersistent->Environment().CurrentEnv.far_plane;
			CDB::MODEL*	model		= g_pGameLevel->ObjectSpace.GetStaticModel();
			m_xrc.ray_options		(CDB::OPT_ONLYNEAREST|CDB::OPT_CULL);
			for (u32 j=0; j<m_rays_y; j++)
			{
				for (u32 i=0; i<m_rays_x; i++)
				{
					float	x		= (float(i)+.5f)/float(m_rays_x)*2.f-1.f;
					float	y		= 1.f-(float(j)+.5f)/float(m_rays_y)*2.f;
					Fvector	ray		= screen_ray(dir,x,y);
					m_rays	++;

					m_xrc.ray_query	(model,eye,ray,far_plane*2.f);
					if (0==m_xrc.r_count())
						continue;
					CDB::RESULT*	result	= m_xrc.r_begin();
					Fvector		hit;
					hit.mad		(eye,ray,result->range);
					float		depth	= result->range*ray.dotproduct(dir);
					if (depth>far_plane)
						continue;

					u32				sector	= result->sector;
					EAuditVerdict	verdict	= classify(sector,hit);
					u32				bucket	= 0;
					while (result->range>audit_distance_limits[bucket])
						bucket				++;
					m_hits					++;
					pose.hits				++;
					m_verdicts[verdict]		++;
					m_verdict_distances[verdict][bucket]	++;
					pose.verdicts[verdict]	++;
					if (verdict!=verdict_drawn && m_examples[verdict].size()<audit_examples)
					{
						SAuditExample	example;
						example.eye			= eye;
						example.yaw			= yaw;
						example.hit			= hit;
						example.distance	= result->range;
						example.start		= pose.start;
						example.sector		= sector;
						m_examples[verdict].push_back	(example);
					}
				}
			}
			m_poses.push_back	(pose);
		}

		static void	load_positions		(float step, xr_vector<Fvector>& positions)
		{
			string_path		file_name;
			FS.update_path	(file_name,"$level$","level.ai");
			if (!FS.exist(file_name))
				return;

			IReader*		reader		= FS.r_open(file_name);
			hdrNODES		header;
			reader->r		(&header,sizeof(header));
			u32				row_length	= iFloor((header.aabb.max.z-header.aabb.min.z)/header.size+EPS_L+1.5f);
			u32				stride		= _max(1,iFloor(step/header.size+.5f));
			for (u32 it=0; it<header.count; it++)
			{
				NodeCompressed	node;
				reader->r		(&node,sizeof(node));
				u32				x		= node.p.x(row_length);
				u32				z		= node.p.z(row_length);
				if ((x%stride) || (z%stride))
					continue;
				Fvector			position;
				position.x		= float(x)*header.size+header.aabb.min.x;
				position.z		= float(z)*header.size+header.aabb.min.z;
				position.y		= (float(node.p.y())/65535)*header.size_y+header.aabb.min.y;
				positions.push_back	(position);
			}
			FS.r_close		(reader);
		}

		static u32	pose_errors			(const SAuditPose& pose)
		{
			u32		result	= 0;
			for (u32 it=verdict_hom_visual; it<verdict_count; it++)
				result		+= pose.verdicts[it];
			return	result;
		}

		static bool	worse_pose			(const SAuditPose& a, const SAuditPose& b)
		{
			return		pose_errors(a) > pose_errors(b);
		}

		void		report				(u32 positions, float seconds)
		{
			Msg		("* vis_audit: %u positions, %u poses, %u leaves, %I64u rays, %I64u hits, %u without sector, %.1f s",positions,u32(m_poses.size()),u32(m_leaves.size()),m_rays,m_hits,m_no_sector,seconds);
			for (u32 verdict=0; verdict<verdict_count; verdict++)
			{
				string256	line	= "";
				for (u32 bucket=0; bucket<audit_distance_buckets; bucket++)
				{
					string64	item;
					sprintf_s	(item,sizeof(item)," %I64u",m_verdict_distances[verdict][bucket]);
					strcat_s	(line,sizeof(line),item);
				}
				Msg		("* vis_audit: %-10s %10I64u  by distance <25 <50 <100 <200 more:%s",audit_verdict_names[verdict],m_verdicts[verdict],line);
			}

			for (u32 verdict=verdict_unmatched; verdict<verdict_count; verdict++)
			{
				for (u32 it=0; it<m_examples[verdict].size(); it++)
				{
					SAuditExample&	example	= m_examples[verdict][it];
					Msg		("* vis_audit: %s eye [%.2f %.2f %.2f] yaw %.0f hit [%.2f %.2f %.2f] dist %.1f start %u sector %u",
						audit_verdict_names[verdict],VPUSH(example.eye),rad2deg(example.yaw),VPUSH(example.hit),example.distance,example.start,example.sector);
				}
			}

			std::sort	(m_poses.begin(),m_poses.end(),worse_pose);
			for (u32 it=0; it<m_poses.size() && it<audit_worst_poses; it++)
			{
				SAuditPose&	pose	= m_poses[it];
				if (0==pose_errors(pose))
					break;
				Msg		("* vis_audit: pose eye [%.2f %.2f %.2f] yaw %.0f start %u hits %u hom_visual %u hom_portal %u ssa %u frustum %u sector %u unreached %u",
					VPUSH(pose.eye),rad2deg(pose.yaw),pose.start,pose.hits,pose.verdicts[verdict_hom_visual],pose.verdicts[verdict_hom_portal],pose.verdicts[verdict_ssa],pose.verdicts[verdict_frustum],pose.verdicts[verdict_sector],pose.verdicts[verdict_unreached]);
			}
		}
	public:
		void		run					(float step, u32 yaws, u32 rays_x, u32 rays_y)
		{
			m_rays_x		= rays_x;
			m_rays_y		= rays_y;
			m_no_sector		= 0;
			m_rays			= 0;
			m_hits			= 0;
			for (u32 it=0; it<verdict_count; it++)
			{
				m_verdicts[it]	= 0;
				for (u32 bucket=0; bucket<audit_distance_buckets; bucket++)
					m_verdict_distances[it][bucket]	= 0;
				m_examples[it].clear	();
			}
			m_poses.clear	();
			m_sector_index.clear	();
			for (u32 it=0; it<RImplementation.Sectors.size(); it++)
				m_sector_index[RImplementation.Sectors[it]]	= it;
			collect_leaves	();

			xr_vector<Fvector>	positions;
			load_positions		(step,positions);

			Fvector		saved_position		= Device.vCameraPosition;
			Fvector		saved_direction		= Device.vCameraDirection;
			Fvector		saved_top			= Device.vCameraTop;
			Fvector		saved_right			= Device.vCameraRight;
			Fmatrix		saved_view			= Device.mView;
			Fmatrix		saved_full			= Device.mFullTransform;
			CFrustum*	saved_frustum		= RImplementation.View;

			CTimer		timer;
			timer.Start	();
			for (u32 it=0; it<positions.size(); it++)
				for (u32 yaw=0; yaw<yaws; yaw++)
					audit_pose	(positions[it],PI_MUL_2*float(yaw)/float(yaws));

			float		seconds				= timer.GetElapsed_sec();
			report		(positions.size(),seconds);

			Device.vCameraPosition		= saved_position;
			Device.vCameraDirection		= saved_direction;
			Device.vCameraTop			= saved_top;
			Device.vCameraRight			= saved_right;
			Device.mView				= saved_view;
			Device.mFullTransform		= saved_full;
			RImplementation.View		= saved_frustum;
			RImplementation.HOM.reset_skip	();
			reset_hom_cache				();
		}
	};
}

static string_path	g_vis_capture_name	= "";

void r_vis_capture_request	(LPCSTR name)
{
	strcpy_s			(g_vis_capture_name,sizeof(g_vis_capture_name),name);
}

void r_vis_capture_flush	()
{
	if (0==g_vis_capture_name[0])
		return;

	D3DSURFACE_DESC		desc;
	HW.pBaseRT->GetDesc	(&desc);
	IDirect3DSurface9*	resolved	= 0;
	IDirect3DSurface9*	copy		= 0;
	ID3DXBuffer*		image		= 0;
	if (SUCCEEDED(HW.pDevice->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,0))
		&& SUCCEEDED(HW.pDevice->StretchRect(HW.pBaseRT,0,resolved,0,D3DTEXF_NONE))
		&& SUCCEEDED(HW.pDevice->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,0))
		&& SUCCEEDED(HW.pDevice->GetRenderTargetData(resolved,copy))
		&& SUCCEEDED(D3DXSaveSurfaceToFileInMemory(&image,D3DXIFF_PNG,copy,0,0)))
	{
		string_path		file_name;
		strconcat		(sizeof(file_name),file_name,g_vis_capture_name,".png");
		IWriter*		writer	= FS.w_open("$screenshots$",file_name);
		if (writer)
		{
			writer->w		(image->GetBufferPointer(),image->GetBufferSize());
			FS.w_close		(writer);
			Msg				("* vis_capture: %s",file_name);
		}
	}
	else
		Msg				("! vis_capture: failed to capture %s",g_vis_capture_name);
	_RELEASE			(image);
	_RELEASE			(copy);
	_RELEASE			(resolved);
	g_vis_capture_name[0]	= 0;
}

void r_vis_audit	(float step, u32 yaws, u32 rays_x, u32 rays_y)
{
	CVisAudit*	audit	= xr_new<CVisAudit>();
	audit->run			(step,yaws,rays_x,rays_y);
	xr_delete			(audit);
}

#endif
