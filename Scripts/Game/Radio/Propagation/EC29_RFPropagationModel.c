//! Terrain-aware radio link quality, 0..1 (1 = clean).
//!
//! Plain object owned by EC29_RadioState and rebuilt with it on every world change. It does NOT
//! check whether RF propagation is enabled - callers gate on
//! EC29_RFPropagationNetworkComponent.IsRFPropagationEnabled() before asking.
//!
//! Model, along the horizontal tx->rx line:
//!   total loss = free-space path loss (slant distance)
//!              + knife-edge diffraction over the (up to) three highest terrain obstructions
//!              + a small first-Fresnel-zone penalty for the worst partially cleared sample
//! then a piecewise-linear map from dB to quality. Antennas sit 1.5 m above the terrain under
//! each end; the entities' real heights are ignored (roofs and aircraft count as ground level).
//!
//! Hot path: runs behind EC29_RadioState's 250 ms per-sender cache for voice and on every accepted
//! key-start. It must not allocate per call - the obstruction shortlist lives in member buffers
//! sized once in the constructor (script is single-threaded, so reuse is safe).
class EC29_RFPropagationModel
{
	protected static const float LIGHT_SPEED_MPS = 299792458.0;
	protected static const float FALLBACK_FREQUENCY_KHZ = 60000.0;
	protected static const float MAST_HEIGHT_M = 1.5;
	protected static const float PROFILE_STEP_M = 50.0;
	protected static const float PROFILE_MAX_STEPS = 200;
	protected static const float NEAR_CUTOFF_M = 50.0;
	//! Beyond this the link is reported clean rather than modelled: special nets advertise ~50 km
	//! and marching terrain across unloaded map cells caused the 2026-08-23 server stalls.
	protected static const float FAR_CUTOFF_M = 5000.0;
	protected static const int EDGE_SLOTS = 3;
	protected static const float EARTH_RADIUS_M = 6371000.0;
	protected static const float REFRACTION_K = 1.333;

	//! Obstruction shortlist, tallest first (ties keep the earlier sample first).
	protected ref array<float> m_aEdgeHeight = {};
	protected ref array<float> m_aEdgeNear = {};
	protected ref array<float> m_aEdgeFar = {};
	protected int m_iEdgeCount;

	//------------------------------------------------------------------------------------------------
	void EC29_RFPropagationModel()
	{
		m_aEdgeHeight.Resize(EDGE_SLOTS);
		m_aEdgeNear.Resize(EDGE_SLOTS);
		m_aEdgeFar.Resize(EDGE_SLOTS);
	}

	//------------------------------------------------------------------------------------------------
	//! \param frequencyKHz <= 0 selects the 60 MHz default
	float CalculateSignalQuality(vector txPos, vector rxPos, float frequencyKHz = 0)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 1.0;

		float kHz = frequencyKHz;
		if (kHz <= 0)
			kHz = FALLBACK_FREQUENCY_KHZ;

		float wavelength = LIGHT_SPEED_MPS / (kHz * 1000.0);

		float spanX = rxPos[0] - txPos[0];
		float spanZ = rxPos[2] - txPos[2];
		float ground = Math.Sqrt(spanX * spanX + spanZ * spanZ);

		if (ground < NEAR_CUTOFF_M || ground > FAR_CUTOFF_M)
			return 1.0;

		float txMast = world.GetSurfaceY(txPos[0], txPos[2]) + MAST_HEIGHT_M;
		float rxMast = world.GetSurfaceY(rxPos[0], rxPos[2]) + MAST_HEIGHT_M;
		float rise = rxMast - txMast;

		float pathLoss = EC29_FreeSpaceLossDb(Math.Sqrt(ground * ground + rise * rise), kHz / 1000.0);
		float terrainLoss = EC29_TerrainLossDb(world, txPos, spanX, spanZ, ground, txMast, rxMast, wavelength);
		float totalLoss = pathLoss + terrainLoss;
		float quality = EC29_QualityFromLoss(totalLoss);

		if (EC29_RFPropagationNetworkComponent.IsDebugEnabled())
			PrintFormat("[RFPropagation] distance=%1 m path=%2 dB diffraction=%3 dB total=%4 dB quality=%5",
				Math.Round(ground), Math.Round(pathLoss), Math.Round(terrainLoss), Math.Round(totalLoss), quality);

		return quality;
	}

	//------------------------------------------------------------------------------------------------
	//! Walks the profile once: samples above the line of sight compete for the edge shortlist,
	//! samples below it compete for "deepest Fresnel intrusion in metres".
	protected float EC29_TerrainLossDb(BaseWorld world, vector txPos, float spanX, float spanZ, float ground, float txMast, float rxMast, float wavelength)
	{
		float steps = Math.Floor(ground / PROFILE_STEP_M);
		if (steps > PROFILE_MAX_STEPS)
			steps = PROFILE_MAX_STEPS;

		if (steps < 1)
			return 0;

		m_iEdgeCount = 0;
		float deepestIntrusion = 0;
		float deepestRadius = 0;
		float bulgeDivisor = 2.0 * EARTH_RADIUS_M * REFRACTION_K;

		for (int i = 1; i < steps; i++)
		{
			float nearLeg = PROFILE_STEP_M * i;
			float farLeg = ground - nearLeg;
			float t = nearLeg / ground;

			float terrain = world.GetSurfaceY(txPos[0] + spanX * t, txPos[2] + spanZ * t);
			float obstacleTop = terrain + nearLeg * farLeg / bulgeDivisor;
			float sightLine = txMast + (rxMast - txMast) * t;
			float excess = obstacleTop - sightLine;

			if (excess > 0)
			{
				EC29_OfferEdge(excess, nearLeg, farLeg);
				continue;
			}

			float zoneRadius = Math.Sqrt(wavelength * nearLeg * farLeg / ground);
			float intrusion = zoneRadius + excess; // radius minus clearance (clearance = -excess)
			if (intrusion > deepestIntrusion)
			{
				deepestIntrusion = intrusion;
				deepestRadius = zoneRadius;
			}
		}

		float loss = 0;
		for (int e = 0; e < m_iEdgeCount; e++)
		{
			loss += EC29_KnifeEdgeLossDb(m_aEdgeHeight[e], m_aEdgeNear[e], m_aEdgeFar[e], ground, wavelength);
		}

		if (deepestIntrusion > 0 && deepestRadius > 0)
			loss += EC29_FresnelPenaltyDb(1.0 - deepestIntrusion / deepestRadius);

		return loss;
	}

	//------------------------------------------------------------------------------------------------
	//! Insert into the tallest-first shortlist. An equal height never displaces an earlier entry.
	protected void EC29_OfferEdge(float height, float nearLeg, float farLeg)
	{
		int slot = 0;
		while (slot < m_iEdgeCount && m_aEdgeHeight[slot] >= height)
		{
			slot++;
		}

		if (slot >= EDGE_SLOTS)
			return;

		int tail = m_iEdgeCount;
		if (tail >= EDGE_SLOTS)
			tail = EDGE_SLOTS - 1;

		for (int i = tail; i > slot; i--)
		{
			m_aEdgeHeight[i] = m_aEdgeHeight[i - 1];
			m_aEdgeNear[i] = m_aEdgeNear[i - 1];
			m_aEdgeFar[i] = m_aEdgeFar[i - 1];
		}

		m_aEdgeHeight[slot] = height;
		m_aEdgeNear[slot] = nearLeg;
		m_aEdgeFar[slot] = farLeg;

		if (m_iEdgeCount < EDGE_SLOTS)
			m_iEdgeCount++;
	}

	//------------------------------------------------------------------------------------------------
	protected static float EC29_FreeSpaceLossDb(float slantM, float mHz)
	{
		if (slantM <= 1)
			return 0;

		return Math.Max(0, 20.0 * Math.Log10(slantM) + 20.0 * Math.Log10(mHz) - 27.55);
	}

	//------------------------------------------------------------------------------------------------
	//! Single knife edge (ITU-style approximation), 0..40 dB.
	protected static float EC29_KnifeEdgeLossDb(float height, float nearLeg, float farLeg, float ground, float wavelength)
	{
		if (nearLeg <= 0 || farLeg <= 0)
			return 0;

		float v = height * Math.Sqrt(2.0 * ground / (wavelength * nearLeg * farLeg));
		if (v < -0.78)
			return 0;

		float shifted = v - 0.1;
		float loss = 6.9 + 20.0 * Math.Log10(Math.Sqrt(shifted * shifted + 1.0) + shifted);
		return Math.Clamp(loss, 0, 40);
	}

	//------------------------------------------------------------------------------------------------
	//! Stepped penalty from the cleared fraction of the first Fresnel radius.
	protected static float EC29_FresnelPenaltyDb(float clearedFraction)
	{
		if (clearedFraction < 0.2)
			return 3;

		if (clearedFraction < 0.4)
			return 2;

		if (clearedFraction < 0.6)
			return 1;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! <=75 dB clean, 85 dB half, 92 dB 0.2, >=100 dB dead; linear in between.
	protected static float EC29_QualityFromLoss(float lossDb)
	{
		if (lossDb <= 75)
			return 1.0;

		if (lossDb <= 85)
			return 1.0 - 0.5 * (lossDb - 75) / 10.0;

		if (lossDb <= 92)
			return 0.5 - 0.3 * (lossDb - 85) / 7.0;

		if (lossDb <= 100)
			return 0.2 - 0.2 * (lossDb - 92) / 8.0;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Workbench console tool: call EC29_RFPropagationModel.DebugPropagation() to print a full
	//! terrain profile from the local player to a point 1000 m straight ahead at 60 MHz.
	//! Differences from the live model, acceptable for a diagnostic: no 50 m / 5000 m gates, only
	//! PARTIAL samples compete for the worst Fresnel intrusion, obstructions carry their sample
	//! index. Ungated.
	static void DebugPropagation()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller || !controller.GetControlledEntity())
		{
			Print("[RF Debug] No local controlled entity", LogLevel.ERROR);
			return;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("[RF Debug] No world", LogLevel.ERROR);
			return;
		}

		IEntity body = controller.GetControlledEntity();
		vector axes[4];
		body.GetWorldTransform(axes);

		vector txPos = body.GetOrigin();
		vector rxPos = txPos + axes[2].Normalized() * 1000.0;

		float kHz = FALLBACK_FREQUENCY_KHZ;
		float wavelength = LIGHT_SPEED_MPS / (kHz * 1000.0);
		float spanX = rxPos[0] - txPos[0];
		float spanZ = rxPos[2] - txPos[2];
		float ground = Math.Sqrt(spanX * spanX + spanZ * spanZ);

		float txTerrain = world.GetSurfaceY(txPos[0], txPos[2]);
		float rxTerrain = world.GetSurfaceY(rxPos[0], rxPos[2]);
		float txMast = txTerrain + MAST_HEIGHT_M;
		float rxMast = rxTerrain + MAST_HEIGHT_M;

		float steps = Math.Floor(ground / PROFILE_STEP_M);
		if (steps > PROFILE_MAX_STEPS)
			steps = PROFILE_MAX_STEPS;

		float effectiveRadius = EARTH_RADIUS_M * REFRACTION_K;
		float halfSpan = ground * 0.5;

		Print("[RF Debug] ===== RF propagation profile =====");
		PrintFormat("[RF Debug] TX %1  RX %2", txPos, rxPos);
		PrintFormat("[RF Debug] terrain TX=%1 RX=%2  antenna TX=%3 RX=%4", txTerrain, rxTerrain, txMast, rxMast);
		PrintFormat("[RF Debug] frequency=%1 kHz wavelength=%2 m distance=%3 m samples=%4", kHz, wavelength, ground, steps);
		PrintFormat("[RF Debug] k-factor=%1 effective earth radius=%2 m midpoint bulge=%3 m", REFRACTION_K, effectiveRadius, halfSpan * halfSpan / (2.0 * effectiveRadius));

		array<float> edgeHeight = {};
		array<float> edgeNear = {};
		array<float> edgeFar = {};
		array<int> edgeSample = {};
		float worstIntrusion = 0;
		float worstRadius = 0;

		for (int i = 1; i < steps; i++)
		{
			float nearLeg = PROFILE_STEP_M * i;
			float farLeg = ground - nearLeg;
			float t = nearLeg / ground;

			float terrain = world.GetSurfaceY(txPos[0] + spanX * t, txPos[2] + spanZ * t);
			float bulge = nearLeg * farLeg / (2.0 * effectiveRadius);
			float sightLine = txMast + (rxMast - txMast) * t;
			float zoneRadius = Math.Sqrt(wavelength * nearLeg * farLeg / ground);
			float excess = terrain + bulge - sightLine;

			string label;
			if (excess > 0)
			{
				label = "BLOCKED";
				int slot = 0;
				while (slot < edgeHeight.Count() && edgeHeight[slot] >= excess)
				{
					slot++;
				}

				if (slot < EDGE_SLOTS)
				{
					edgeHeight.InsertAt(excess, slot);
					edgeNear.InsertAt(nearLeg, slot);
					edgeFar.InsertAt(farLeg, slot);
					edgeSample.InsertAt(i, slot);
					if (edgeHeight.Count() > EDGE_SLOTS)
					{
						edgeHeight.Resize(EDGE_SLOTS);
						edgeNear.Resize(EDGE_SLOTS);
						edgeFar.Resize(EDGE_SLOTS);
						edgeSample.Resize(EDGE_SLOTS);
					}
				}
			}
			else
			{
				float clearance = -excess;
				if (clearance >= zoneRadius)
				{
					label = "CLEAR";
				}
				else if (clearance >= 0.6 * zoneRadius)
				{
					label = "GOOD";
				}
				else
				{
					label = "PARTIAL";
					float intrusion = zoneRadius - clearance;
					if (intrusion > worstIntrusion)
					{
						worstIntrusion = intrusion;
						worstRadius = zoneRadius;
					}
				}
			}

			PrintFormat("[RF Debug] #%1 d=%2 m terrain=%3 bulge=%4 los=%5 fresnel=%6 excess=%7 %8", i, nearLeg, terrain, bulge, sightLine, zoneRadius, excess, label);
		}

		float diffraction = 0;
		for (int e = 0; e < edgeHeight.Count(); e++)
		{
			float edgeLoss = EC29_KnifeEdgeLossDb(edgeHeight[e], edgeNear[e], edgeFar[e], ground, wavelength);
			diffraction += edgeLoss;
			PrintFormat("[RF Debug] obstruction at sample #%1: height=%2 m loss=%3 dB", edgeSample[e], edgeHeight[e], edgeLoss);
		}

		float fresnel = 0;
		if (worstIntrusion > 0 && worstRadius > 0)
			fresnel = EC29_FresnelPenaltyDb(1.0 - worstIntrusion / worstRadius);

		float rise = rxMast - txMast;
		float pathLoss = EC29_FreeSpaceLossDb(Math.Sqrt(ground * ground + rise * rise), kHz / 1000.0);
		float total = pathLoss + diffraction + fresnel;

		PrintFormat("[RF Debug] fresnel loss=%1 dB  path loss=%2 dB  total=%3 dB  quality=%4", fresnel, pathLoss, total, EC29_QualityFromLoss(total));
	}
}
