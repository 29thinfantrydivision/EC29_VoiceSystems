class EC29_JammerComponentClass : ScriptComponentClass
{
}

class EC29_JammerComponent : ScriptComponent
{
	[Attribute("500", UIWidgets.Slider, "Jam range in meters", "100 2000 50")]
	protected float m_fRangeConfig;

	[Attribute("180", UIWidgets.Slider, "Cone angle in degrees (180 = omnidirectional)", "10 180 5")]
	protected float m_fConeAngleConfig;

	[Attribute("1", UIWidgets.CheckBox, "Is jammer active on spawn")]
	protected bool m_bActiveConfig;

	[Attribute("0 0 0", UIWidgets.Coords, "Emitter offset from entity origin (local space)")]
	protected vector m_vEmitterOffset;

	[RplProp(onRplName: "OnActiveChanged")]
	protected bool m_bActive;

	[RplProp()]
	protected float m_fRange;

	[RplProp()]
	protected float m_fConeAngle;

	#ifdef WORKBENCH
	protected ref array<ref Shape> m_aDebugShapes;
	#endif

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (Replication.IsServer() || !Replication.IsRunning())
		{
			m_fRange = m_fRangeConfig;
			m_fConeAngle = m_fConeAngleConfig;
			m_bActive = m_bActiveConfig;
			Replication.BumpMe();
		}

		EC29_RadioState.GetInstance().Jammers().RegisterJammer(this);

		#ifdef WORKBENCH
		SetEventMask(owner, EntityEvent.FRAME);
		#endif
	}

	#ifdef WORKBENCH
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		DebugDrawJammer(owner);
	}

	protected void DebugDrawJammer(IEntity owner)
	{
		if (!owner)
			return;

		if (!m_aDebugShapes)
			m_aDebugShapes = new array<ref Shape>();
		m_aDebugShapes.Clear();

		vector mat[4];
		owner.GetWorldTransform(mat);
		vector forward = mat[2];
		vector right = mat[0];
		vector up = mat[1];
		vector origin = GetPosition();

		float range = m_fRangeConfig;
		float coneAngle = m_fConeAngleConfig;

		int sphereColor = 0x40808080;
		int lineColor = Color.GRAY;
		if (m_bActive)
		{
			sphereColor = 0x40FF0000;
			lineColor = Color.YELLOW;
		}

		if (coneAngle >= 180)
		{
			Shape sphereShape = Shape.CreateSphere(sphereColor, ShapeFlags.ONCE | ShapeFlags.TRANSP, origin, range);
			if (sphereShape)
				m_aDebugShapes.Insert(sphereShape);
		}
		else
		{
			vector forwardEnd = origin + forward * range;
			Shape arrowShape = Shape.CreateArrow(origin, forwardEnd, 5.0, Color.BLUE, ShapeFlags.NOZBUFFER | ShapeFlags.ONCE);
			if (arrowShape)
				m_aDebugShapes.Insert(arrowShape);

			float halfAngleRad = (coneAngle * 0.5) * Math.DEG2RAD;

			int numConeLines = 16;
			int numRings = 3;
			float coneStep = (Math.PI * 2.0) / numConeLines;

			for (int ring = 1; ring <= numRings; ring++)
			{
				float ringFraction = ring / (float)numRings;
				float ringAngleRad = halfAngleRad * ringFraction;
				float cosAngle = Math.Cos(ringAngleRad);
				float sinAngle = Math.Sin(ringAngleRad);

				for (int i = 0; i < numConeLines; i++)
				{
					float angle = i * coneStep;
					vector radialDir = right * Math.Cos(angle) + up * Math.Sin(angle);
					vector coneDir = (forward * cosAngle + radialDir * sinAngle).Normalized() * range;

					Shape coneLine = Shape.CreateArrow(origin, origin + coneDir, 0.1, lineColor, ShapeFlags.NOZBUFFER | ShapeFlags.ONCE);
					if (coneLine)
						m_aDebugShapes.Insert(coneLine);
				}
			}
		}
	}
	#endif

	override void OnDelete(IEntity owner)
	{
		EC29_RadioState.GetInstance().Jammers().UnregisterJammer(this);
		super.OnDelete(owner);
	}

	bool IsJammerActive()
	{
		return m_bActive;
	}

	//! Authority-only by design. The single caller (EC29_JammerToggleUserAction)
	//! already early-outs on non-authority, so a client->server request path
	//! would be unreachable from our own code - it existed only as an
	//! unvalidated attack surface and was removed. If client-initiated toggling
	//! is ever needed, add a server-validated RPC (permission + rate checks),
	//! not a blind setter.
	void SetJammerActive(bool active)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		m_bActive = active;
		Replication.BumpMe();
	}

	protected void OnActiveChanged()
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Jammer] Active state replicated: %1 (range=%2 cone=%3)", m_bActive, m_fRange, m_fConeAngle);
	}

	float GetRange()
	{
		return m_fRange;
	}

	float GetConeAngle()
	{
		return m_fConeAngle;
	}

	vector GetPosition()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return vector.Zero;

		if (m_vEmitterOffset == vector.Zero)
			return owner.GetOrigin();

		vector mat[4];
		owner.GetWorldTransform(mat);
		vector worldOffset = m_vEmitterOffset[0] * mat[0] + m_vEmitterOffset[1] * mat[1] + m_vEmitterOffset[2] * mat[2];
		return mat[3] + worldOffset;
	}

	vector GetForwardVector()
	{
		return GetOwner().GetTransformAxis(2);
	}
}
