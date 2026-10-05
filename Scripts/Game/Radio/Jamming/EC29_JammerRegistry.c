//! Per-world list of jammer components and the jam geometry evaluated against it.
//!
//! Owned by EC29_RadioState (EC29_RadioState.GetInstance().Jammers()), so a world change starts
//! with an empty list. Entries are weak: a deleted jammer reads back as null and is skipped.
//!
//! Degradation model (0 = clean, 1 = fully jammed), per jammer with emitter E, range R, cone A:
//!   - outside the 3D sphere |P - E| >= R            -> 0
//!   - directional (A < 180) and angle off-axis > A/2 -> 0 (exactly A/2 is still inside)
//!   - otherwise 1 - (d / R)^2, a quadratic falloff from the emitter to the edge
//! Jammers do not stack: the result is the strongest single jammer. Terrain, occlusion and
//! frequency play no part. Only the EC29_JamStrength audio variable consumes this.
class EC29_JammerRegistry
{
	protected ref array<EC29_JammerComponent> m_aJammers = {};

	//------------------------------------------------------------------------------------------------
	//! Idempotent; a repeated registration only logs.
	void RegisterJammer(EC29_JammerComponent jammer)
	{
		if (jammer && !m_aJammers.Contains(jammer))
			m_aJammers.Insert(jammer);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Jammer] Registered jammer - %1 tracked", m_aJammers.Count());
	}

	//------------------------------------------------------------------------------------------------
	void UnregisterJammer(EC29_JammerComponent jammer)
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Jammer] Unregistering jammer - %1 tracked before removal", m_aJammers.Count());

		m_aJammers.RemoveItem(jammer);
	}

	//------------------------------------------------------------------------------------------------
	//! Strongest single-jammer effect at the receiver position. Sole consumer:
	//! EC29_RadioState.GetJammerStrength.
	float CalculateJammerDegradation(vector receiverPos)
	{
		float strongest = 0;

		foreach (EC29_JammerComponent jammer : m_aJammers)
		{
			if (!jammer || !jammer.IsJammerActive())
				continue;

			float effect = EC29_EffectAt(jammer, receiverPos);
			if (effect > strongest)
				strongest = effect;
		}

		return strongest;
	}

	//------------------------------------------------------------------------------------------------
	int GetActiveJammerCount()
	{
		int active = 0;
		foreach (EC29_JammerComponent jammer : m_aJammers)
		{
			if (jammer && jammer.IsJammerActive())
				active++;
		}

		return active;
	}

	//------------------------------------------------------------------------------------------------
	//! Angle in degrees between the jammer's forward axis and the direction to the point. A point
	//! sitting exactly on the emitter has no direction and reads as 90 degrees. The dot product is
	//! clamped so float error can never push acos outside its domain.
	protected static float EC29_OffAxisDeg(EC29_JammerComponent jammer, vector emitter, vector point, float distance)
	{
		vector toPoint = vector.Zero;
		if (distance > 0)
			toPoint = (point - emitter) * (1.0 / distance);

		float cosine = Math.Clamp(vector.Dot(toPoint, jammer.GetForwardVector()), -1.0, 1.0);
		return Math.Acos(cosine) * Math.RAD2DEG;
	}

	//------------------------------------------------------------------------------------------------
	protected static float EC29_EffectAt(EC29_JammerComponent jammer, vector point)
	{
		float range = jammer.GetRange();
		vector emitter = jammer.GetPosition();
		float distance = vector.Distance(point, emitter);

		if (distance >= range)
			return 0;

		float cone = jammer.GetConeAngle();
		if (cone < 180 && EC29_OffAxisDeg(jammer, emitter, point, distance) > cone * 0.5)
			return 0;

		float fraction = distance / range;
		return 1.0 - fraction * fraction;
	}

	//------------------------------------------------------------------------------------------------
	//! Workbench console tool: call EC29_JammerRegistry.DebugJammers() to see how every registered
	//! jammer affects the local player's position. Ungated on purpose.
	static void DebugJammers()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
		{
			Print("[Jammer Debug] No local player controller", LogLevel.ERROR);
			return;
		}

		IEntity body = controller.GetControlledEntity();
		if (!body)
		{
			Print("[Jammer Debug] Local player controls no entity", LogLevel.ERROR);
			return;
		}

		vector listener = body.GetOrigin();
		EC29_JammerRegistry registry = EC29_RadioState.GetInstance().Jammers();
		array<EC29_JammerComponent> jammers = registry.m_aJammers;

		PrintFormat("[Jammer Debug] Listener at %1 - %2 jammer(s) registered", listener, jammers.Count());
		if (jammers.IsEmpty())
		{
			Print("[Jammer Debug] No jammers registered in this world", LogLevel.WARNING);
			return;
		}

		float worst = 0;
		int index = 0;
		foreach (EC29_JammerComponent jammer : jammers)
		{
			index++;
			if (!jammer)
			{
				PrintFormat("[Jammer Debug] #%1: <deleted>", index);
				continue;
			}

			vector emitter = jammer.GetPosition();
			float range = jammer.GetRange();
			float cone = jammer.GetConeAngle();
			float distance = vector.Distance(listener, emitter);

			PrintFormat("[Jammer Debug] #%1: pos=%2 range=%3 m cone=%4 deg active=%5 distance=%6 m", index, emitter, range, cone, jammer.IsJammerActive(), distance);

			if (!jammer.IsJammerActive())
			{
				PrintFormat("[Jammer Debug] #%1:   inactive", index);
				continue;
			}

			if (distance >= range)
			{
				PrintFormat("[Jammer Debug] #%1:   out of range", index);
				continue;
			}

			if (cone < 180)
			{
				float offAxis = EC29_OffAxisDeg(jammer, emitter, listener, distance);
				if (offAxis > cone * 0.5)
				{
					PrintFormat("[Jammer Debug] #%1:   outside cone (angle %2 deg > half-angle %3 deg)", index, offAxis, cone * 0.5);
					continue;
				}
			}

			float fraction = distance / range;
			float degradation = 1.0 - fraction * fraction;
			PrintFormat("[Jammer Debug] #%1:   JAMMING ratio=%2 ratio^2=%3 degradation=%4", index, fraction, fraction * fraction, degradation);

			if (degradation > worst)
				worst = degradation;
		}

		PrintFormat("[Jammer Debug] Worst degradation=%1 -> jam quality=%2", worst, 1.0 - worst);
	}
}
