[ComponentEditorProps(category: "EC29/Radio", description: "Radio jammer. Receivers inside its range (and cone, when directional) hear degraded radio audio. Needs an RplComponent on the same entity.")]
class EC29_JammerComponentClass : ScriptComponentClass {}

//------------------------------------------------------------------------------------------------
//! A jamming emitter placed in the world.
//!
//! Authoring happens through the four *Config attributes; at post-init the authority copies them
//! into replicated state, and everything at runtime (registry geometry, callers) reads only the
//! replicated values. A client therefore sees range/cone 0 until the first snapshot lands, and a
//! range of 0 jams nothing.
//!
//! State changes are authority-only. There is deliberately no client-to-server request path: an
//! earlier unvalidated toggle RPC was an attack surface and was removed. If clients ever need to
//! switch jammers, add a server-validated RPC with permission and rate checks, never a setter.
//!
//! Every machine (server and clients) registers the component with the world's
//! EC29_JammerRegistry, because the jam value is computed per receiver on the listening client.
class EC29_JammerComponent : ScriptComponent
{
	[Attribute("500", UIWidgets.Slider, "Jamming radius (m)", "100 2000 50")]
	protected float m_fRangeConfig;

	[Attribute("180", UIWidgets.Slider, "Full cone angle (deg). 180 = omnidirectional; anything less aims along the entity's +Z axis", "10 180 5")]
	protected float m_fConeAngleConfig;

	[Attribute("1", UIWidgets.CheckBox, "Jammer is switched on when it spawns")]
	protected bool m_bActiveConfig;

	[Attribute("0 0 0", UIWidgets.Coords, "Emitter point, entity-local (x right, y up, z forward)")]
	protected vector m_vEmitterOffset;

	[RplProp(onRplName: "EC29_OnActiveReplicated")]
	protected bool m_bEC29_IsActive;

	[RplProp()]
	protected float m_fEC29_RangeM;

	[RplProp()]
	protected float m_fEC29_ConeDeg;

	//------------------------------------------------------------------------------------------------
	//! Offline / editor (no replication) counts as authority.
	protected static bool EC29_HasAuthority()
	{
		return !Replication.IsRunning() || Replication.IsServer();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (EC29_HasAuthority())
		{
			m_bEC29_IsActive = m_bActiveConfig;
			m_fEC29_RangeM = m_fRangeConfig;
			m_fEC29_ConeDeg = m_fConeAngleConfig;
			Replication.BumpMe();
		}

		EC29_RadioState.GetInstance().Jammers().RegisterJammer(this);

#ifdef WORKBENCH
		SetEventMask(owner, EntityEvent.FRAME);
#endif
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		EC29_RadioState.GetInstance().Jammers().UnregisterJammer(this);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Proxy-side notification only; the registry reads the replicated values directly.
	protected void EC29_OnActiveReplicated()
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Jammer] Replicated state: active=%1 range=%2 m cone=%3 deg", m_bEC29_IsActive, m_fEC29_RangeM, m_fEC29_ConeDeg);
	}

	//------------------------------------------------------------------------------------------------
	bool IsJammerActive()
	{
		return m_bEC29_IsActive;
	}

	//------------------------------------------------------------------------------------------------
	//! Authority only. On a replicated client this does nothing: a local flip would race the
	//! replicated value and could switch the jammer straight back.
	void SetJammerActive(bool active)
	{
		if (!EC29_HasAuthority())
			return;

		m_bEC29_IsActive = active;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Replicated radius in metres (0 on a client until replication delivers it).
	float GetRange()
	{
		return m_fEC29_RangeM;
	}

	//------------------------------------------------------------------------------------------------
	//! Replicated full cone angle in degrees; 180 or more means omnidirectional.
	float GetConeAngle()
	{
		return m_fEC29_ConeDeg;
	}

	//------------------------------------------------------------------------------------------------
	//! World position of the emitter: the owner origin plus the offset rotated by the owner's
	//! orientation. Axes are normalised, so entity scale does not stretch the offset.
	vector GetPosition()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return vector.Zero;

		vector origin = owner.GetOrigin();
		if (m_vEmitterOffset == vector.Zero)
			return origin;

		vector axes[4];
		owner.GetWorldTransform(axes);

		vector right = axes[0].Normalized();
		vector up = axes[1].Normalized();
		vector forward = axes[2].Normalized();

		return origin + right * m_vEmitterOffset[0] + up * m_vEmitterOffset[1] + forward * m_vEmitterOffset[2];
	}

	//------------------------------------------------------------------------------------------------
	//! The owner's world +Z axis (pitch included, so tilting the entity tilts the cone).
	vector GetForwardVector()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return vector.Forward;

		vector axes[4];
		owner.GetWorldTransform(axes);
		return axes[2].Normalized();
	}

#ifdef WORKBENCH
	//------------------------------------------------------------------------------------------------
	//! Workbench-only visualisation of the AUTHORED values (not the replicated ones), redrawn every
	//! frame. Omni: translucent sphere. Directional: forward arrow plus three rings of rays at
	//! 1/3, 2/3 and 3/3 of the half-angle. No gameplay effect.
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		vector emitter = GetPosition();

		if (m_fConeAngleConfig >= 180)
		{
			int sphereColor = 0x40808080;
			if (m_bActiveConfig)
				sphereColor = 0x40FF0000;

			Shape.CreateSphere(sphereColor, ShapeFlags.ONCE | ShapeFlags.NOZBUFFER | ShapeFlags.TRANSP | ShapeFlags.NOOUTLINE, emitter, m_fRangeConfig);
			return;
		}

		vector axes[4];
		owner.GetWorldTransform(axes);
		vector right = axes[0].Normalized();
		vector up = axes[1].Normalized();
		vector forward = axes[2].Normalized();

		ShapeFlags lineFlags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER;
		Shape.CreateArrow(emitter, emitter + forward * m_fRangeConfig, 1, 0xFF0000FF, lineFlags);

		int rayColor = 0xFF808080;
		if (m_bActiveConfig)
			rayColor = 0xFFFFFF00;

		float halfAngleRad = m_fConeAngleConfig * 0.5 * Math.DEG2RAD;
		for (int ring = 1; ring <= 3; ring++)
		{
			float tilt = halfAngleRad * ring / 3.0;
			float along = Math.Cos(tilt);
			float across = Math.Sin(tilt);

			for (int spoke = 0; spoke < 16; spoke++)
			{
				float around = spoke * Math.PI2 / 16.0;
				vector ray = forward * along + (right * Math.Cos(around) + up * Math.Sin(around)) * across;
				Shape.CreateLine(rayColor, lineFlags, emitter, emitter + ray * m_fRangeConfig);
			}
		}
	}
#endif
}
