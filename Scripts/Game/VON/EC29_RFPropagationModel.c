class EC29_RFPropagationModel
{
    protected static const float DEFAULT_FREQUENCY_KHZ = 60000.0;
    protected static const float SPEED_OF_LIGHT = 299792458.0;
    protected static const float ANTENNA_HEIGHT = 1.5;

    protected static const float SAMPLE_INTERVAL = 50.0;
    protected static const int MAX_SAMPLES = 200;
    protected static const float MIN_DISTANCE = 50.0;
    protected static const int MAX_KNIFE_EDGES = 3;

    protected static const float EARTH_RADIUS = 6371000.0;
    protected static const float K_FACTOR = 1.333;

    protected static const float EXCELLENT_THRESHOLD = 75.0;
    protected static const float GOOD_THRESHOLD = 85.0;
    protected static const float POOR_THRESHOLD = 92.0;
    protected static const float CUTOFF_THRESHOLD = 100.0;

    //! Scratch buffers for CalculateTerrainDiffractionLoss. The model is a
    //! long-lived member of EC29_RadioState and the method runs on the
    //! per-packet hot path - reusing these avoids three array allocations per
    //! call. Not reentrant, which is fine: script runs single-threaded.
    protected ref array<float> m_aScratchPeakHeights = new array<float>();
    protected ref array<float> m_aScratchPeakD1 = new array<float>();
    protected ref array<float> m_aScratchPeakD2 = new array<float>();


    //------------------------------------------------------------------------------------------------

    //------------------------------------------------------------------------------------------------
    float CalculateSignalQuality(vector txPos, vector rxPos, float frequencyKHz = 0)
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return 1.0;

        if (frequencyKHz <= 0)
            frequencyKHz = DEFAULT_FREQUENCY_KHZ;

        float frequencyHz = frequencyKHz * 1000.0;
        float wavelength = SPEED_OF_LIGHT / frequencyHz;
        float frequencyMHz = frequencyKHz / 1000.0;

        float dx = rxPos[0] - txPos[0];
        float dz = rxPos[2] - txPos[2];
        float horizontalDist = Math.Sqrt(dx * dx + dz * dz);

        if (horizontalDist < MIN_DISTANCE)
            return 1.0;

        float txTerrainY = world.GetSurfaceY(txPos[0], txPos[2]);
        float rxTerrainY = world.GetSurfaceY(rxPos[0], rxPos[2]);
        float txHeight = txTerrainY + ANTENNA_HEIGHT;
        float rxHeight = rxTerrainY + ANTENNA_HEIGHT;

        float dy = rxHeight - txHeight;
        float totalDistance = Math.Sqrt(horizontalDist * horizontalDist + dy * dy);

        float diffractionLoss = CalculateTerrainDiffractionLoss(world, txPos, rxPos, txHeight, rxHeight, horizontalDist, wavelength);
        float pathLoss = CalculatePathLoss(totalDistance, frequencyMHz);

        float totalLoss = pathLoss + diffractionLoss;
        float quality = LossToQuality(totalLoss);

        if (EC29_RFPropagationNetworkComponent.IsDebugEnabled())
        {
            Print(string.Format("[RFPropagation] Dist: %1m | PathLoss: %2dB | Diffraction: %3dB | Total: %4dB | Quality: %5",
                Math.Round(totalDistance), Math.Round(pathLoss), Math.Round(diffractionLoss), Math.Round(totalLoss), quality));
        }

        return quality;
    }

    //------------------------------------------------------------------------------------------------
    protected float CalculateTerrainDiffractionLoss(BaseWorld world, vector txPos, vector rxPos, float txHeight, float rxHeight, float horizontalDist, float wavelength)
    {
        int numSamples = Math.Floor(horizontalDist / SAMPLE_INTERVAL);
        if (numSamples < 1)
            return 0.0;
        if (numSamples > MAX_SAMPLES)
            numSamples = MAX_SAMPLES;

        float dx = rxPos[0] - txPos[0];
        float dz = rxPos[2] - txPos[2];
        float dirX = dx / horizontalDist;
        float dirZ = dz / horizontalDist;

        array<float> peakHeights = m_aScratchPeakHeights;
        array<float> peakD1 = m_aScratchPeakD1;
        array<float> peakD2 = m_aScratchPeakD2;
        peakHeights.Clear();
        peakD1.Clear();
        peakD2.Clear();

        float worstFresnelIntrusion = 0.0;
        float worstFresnelRadius = 0.0;

        for (int i = 1; i < numSamples; i++)
        {
            float d1 = i * SAMPLE_INTERVAL;
            float d2 = horizontalDist - d1;

            float sampleX = txPos[0] + dirX * d1;
            float sampleZ = txPos[2] + dirZ * d1;

            float terrainY = world.GetSurfaceY(sampleX, sampleZ);
            float earthBulge = CalculateEarthBulge(d1, d2);
            float effectiveTerrainY = terrainY + earthBulge;
            float losHeight = txHeight + (rxHeight - txHeight) * (d1 / horizontalDist);
            float fresnelRadius = CalculateFresnelRadius(d1, d2, horizontalDist, wavelength);
            float heightAboveLOS = effectiveTerrainY - losHeight;

            if (heightAboveLOS > 0)
            {
                InsertPeakSorted(peakHeights, peakD1, peakD2, heightAboveLOS, d1, d2);
            }
            else
            {
                float fresnelClearance = -heightAboveLOS;
                float fresnelIntrusion = fresnelRadius - fresnelClearance;
                if (fresnelIntrusion > worstFresnelIntrusion)
                {
                    worstFresnelIntrusion = fresnelIntrusion;
                    worstFresnelRadius = fresnelRadius;
                }
            }
        }

        float totalLoss = 0.0;

        int numPeaks = peakHeights.Count();
        if (numPeaks > MAX_KNIFE_EDGES)
            numPeaks = MAX_KNIFE_EDGES;

        for (int p = 0; p < numPeaks; p++)
        {
            float h = peakHeights.Get(p);
            float d1 = peakD1.Get(p);
            float d2 = peakD2.Get(p);
            float diffractionLoss = CalculateDiffractionLoss(h, d1, d2, horizontalDist, wavelength);
            totalLoss += diffractionLoss;
        }

        if (worstFresnelIntrusion > 0 && worstFresnelRadius > 0)
        {
            float clearancePercent = 1.0 - (worstFresnelIntrusion / worstFresnelRadius);

            float fresnelLoss = 0.0;
            if (clearancePercent < 0.2)
                fresnelLoss = 3.0;
            else if (clearancePercent < 0.4)
                fresnelLoss = 2.0;
            else if (clearancePercent < 0.6)
                fresnelLoss = 1.0;

            totalLoss += fresnelLoss;
        }

        return totalLoss;
    }

    //------------------------------------------------------------------------------------------------
    protected void InsertPeakSorted(array<float> heights, array<float> d1Arr, array<float> d2Arr, float h, float d1, float d2)
    {
        int insertIdx = heights.Count();
        for (int i = 0; i < heights.Count(); i++)
        {
            if (h > heights.Get(i))
            {
                insertIdx = i;
                break;
            }
        }

        heights.InsertAt(h, insertIdx);
        d1Arr.InsertAt(d1, insertIdx);
        d2Arr.InsertAt(d2, insertIdx);

        while (heights.Count() > MAX_KNIFE_EDGES)
        {
            int lastIdx = heights.Count() - 1;
            heights.Remove(lastIdx);
            d1Arr.Remove(lastIdx);
            d2Arr.Remove(lastIdx);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected float CalculateFresnelRadius(float d1, float d2, float totalDist, float wavelength)
    {
        if (totalDist <= 0)
            return 0.0;

        float radius = Math.Sqrt(wavelength * d1 * d2 / totalDist);
        return radius;
    }

    //------------------------------------------------------------------------------------------------
    protected float CalculateEarthBulge(float d1, float d2)
    {
        float effectiveRadius = EARTH_RADIUS * K_FACTOR;
        float bulge = (d1 * d2) / (2.0 * effectiveRadius);
        return bulge;
    }

    //------------------------------------------------------------------------------------------------
    protected float CalculateDiffractionLoss(float h, float d1, float d2, float totalDist, float wavelength)
    {
        if (d1 <= 0 || d2 <= 0)
            return 0.0;

        float v = h * Math.Sqrt(2.0 * totalDist / (wavelength * d1 * d2));

        if (v < -0.78)
            return 0.0;

        float vMinusPoint1 = v - 0.1;
        float sqrtTerm = Math.Sqrt(vMinusPoint1 * vMinusPoint1 + 1.0);
        float loss = 6.9 + 20.0 * Math.Log10(sqrtTerm + vMinusPoint1);

        if (loss < 0)
            loss = 0;
        if (loss > 40)
            loss = 40;

        return loss;
    }

    //------------------------------------------------------------------------------------------------
    protected float CalculatePathLoss(float distance, float frequencyMHz)
    {
        if (distance <= 1.0)
            return 0.0;

        float loss = 20.0 * Math.Log10(distance) + 20.0 * Math.Log10(frequencyMHz) - 27.55;

        if (loss < 0)
            loss = 0;

        return loss;
    }

    //------------------------------------------------------------------------------------------------
    protected float LossToQuality(float lossdB)
    {
        if (lossdB <= EXCELLENT_THRESHOLD)
            return 1.0;

        if (lossdB <= GOOD_THRESHOLD)
        {
            float t = (lossdB - EXCELLENT_THRESHOLD) / (GOOD_THRESHOLD - EXCELLENT_THRESHOLD);
            return 1.0 - t * 0.5;
        }

        if (lossdB <= POOR_THRESHOLD)
        {
            float t = (lossdB - GOOD_THRESHOLD) / (POOR_THRESHOLD - GOOD_THRESHOLD);
            return 0.5 - t * 0.3;
        }

        if (lossdB <= CUTOFF_THRESHOLD)
        {
            float t = (lossdB - POOR_THRESHOLD) / (CUTOFF_THRESHOLD - POOR_THRESHOLD);
            return 0.2 - t * 0.2;
        }

        return 0.0;
    }

    //------------------------------------------------------------------------------------------------
    static void DebugPropagation()
    {
        Print("------------------- EC29 RF PROPAGATION DEBUG -------------------", LogLevel.NORMAL);

        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
        {
            Print("[RF Debug] ERROR: No PlayerController found", LogLevel.ERROR);
            return;
        }

        IEntity controlled = pc.GetControlledEntity();
        if (!controlled)
        {
            Print("[RF Debug] ERROR: No controlled entity", LogLevel.ERROR);
            return;
        }

        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            Print("[RF Debug] ERROR: No world", LogLevel.ERROR);
            return;
        }

        vector playerPos = controlled.GetOrigin();
        Print(string.Format("[RF Debug] Player position: %1", playerPos), LogLevel.NORMAL);

        float testDistance = 1000.0;
        vector forward = controlled.GetTransformAxis(2);
        vector targetPos = playerPos + forward * testDistance;

        Print(string.Format("[RF Debug] Test target: %1 (%2 m forward)", targetPos, testDistance), LogLevel.NORMAL);

        float playerTerrainY = world.GetSurfaceY(playerPos[0], playerPos[2]);
        float targetTerrainY = world.GetSurfaceY(targetPos[0], targetPos[2]);
        float playerHeight = playerTerrainY + ANTENNA_HEIGHT;
        float targetHeight = targetTerrainY + ANTENNA_HEIGHT;

        Print(string.Format("[RF Debug] Player terrain: %1 m, antenna: %2 m", playerTerrainY.ToString(1), playerHeight.ToString(1)), LogLevel.NORMAL);
        Print(string.Format("[RF Debug] Target terrain: %1 m, antenna: %2 m", targetTerrainY.ToString(1), targetHeight.ToString(1)), LogLevel.NORMAL);

        float dx = targetPos[0] - playerPos[0];
        float dz = targetPos[2] - playerPos[2];
        float horizontalDist = Math.Sqrt(dx * dx + dz * dz);

        int numSamples = Math.Floor(horizontalDist / SAMPLE_INTERVAL);
        if (numSamples > MAX_SAMPLES)
            numSamples = MAX_SAMPLES;

        float debugFreqKHz = DEFAULT_FREQUENCY_KHZ;
        float debugFreqHz = debugFreqKHz * 1000.0;
        float wavelength = SPEED_OF_LIGHT / debugFreqHz;
        float debugFreqMHz = debugFreqKHz / 1000.0;

        Print(string.Format("[RF Debug] Frequency: %1 MHz, Wavelength: %2 m", debugFreqMHz, wavelength), LogLevel.NORMAL);
        Print(string.Format("[RF Debug] Horizontal distance: %1 m, samples: %2", horizontalDist, numSamples), LogLevel.NORMAL);

        float effectiveEarthRadius = EARTH_RADIUS * K_FACTOR;
        float maxBulgeD1 = horizontalDist * 0.5;
        float maxBulgeD2 = horizontalDist * 0.5;
        float maxBulge = (maxBulgeD1 * maxBulgeD2) / (2.0 * effectiveEarthRadius);
        Print(string.Format("[RF Debug] Atmospheric K-factor: %1 (effective earth radius: %2 km)", K_FACTOR, (effectiveEarthRadius / 1000.0).ToString(0)), LogLevel.NORMAL);
        Print(string.Format("[RF Debug] Max earth bulge at midpoint: %1 m", maxBulge.ToString(2)), LogLevel.NORMAL);

        Print("[RF Debug] --- Terrain Profile ---", LogLevel.NORMAL);

        float dirX = dx / horizontalDist;
        float dirZ = dz / horizontalDist;

        ref array<float> peakHeights = new array<float>();
        ref array<float> peakD1 = new array<float>();
        ref array<float> peakD2 = new array<float>();
        ref array<int> peakSamples = new array<int>();

        float worstFresnelIntrusion = 0.0;
        float worstFresnelRadius = 0.0;

        EC29_RFPropagationModel model = new EC29_RFPropagationModel();

        for (int i = 1; i < numSamples; i++)
        {
            float d1 = i * SAMPLE_INTERVAL;
            float d2 = horizontalDist - d1;

            float sampleX = playerPos[0] + dirX * d1;
            float sampleZ = playerPos[2] + dirZ * d1;
            float terrainY = world.GetSurfaceY(sampleX, sampleZ);

            float earthBulge = model.CalculateEarthBulge(d1, d2);
            float effectiveTerrainY = terrainY + earthBulge;

            float losHeight = playerHeight + (targetHeight - playerHeight) * (d1 / horizontalDist);
            float fresnelRadius = model.CalculateFresnelRadius(d1, d2, horizontalDist, wavelength);
            float heightAboveLOS = effectiveTerrainY - losHeight;

            string status;
            if (heightAboveLOS > 0)
            {
                status = "BLOCKED";
                DebugInsertPeak(peakHeights, peakD1, peakD2, peakSamples, heightAboveLOS, d1, d2, i);
            }
            else
            {
                float fresnelClearance = -heightAboveLOS;
                float clearancePercent = fresnelClearance / fresnelRadius * 100.0;

                if (clearancePercent >= 100.0)
                    status = "CLEAR";
                else if (clearancePercent >= 60.0)
                    status = "GOOD";
                else
                {
                    status = "PARTIAL";
                    float fresnelIntrusion = fresnelRadius - fresnelClearance;
                    if (fresnelIntrusion > worstFresnelIntrusion)
                    {
                        worstFresnelIntrusion = fresnelIntrusion;
                        worstFresnelRadius = fresnelRadius;
                    }
                }
            }

            Print(string.Format("  Sample %1: d=%2m, terrain=%3m, bulge=%4m, LOS=%5m, aboveLOS=%6m [%7]",
                i, d1, terrainY, earthBulge.ToString(2), losHeight, heightAboveLOS.ToString(2), status), LogLevel.NORMAL);
        }

        Print("[RF Debug] --- Calculations ---", LogLevel.NORMAL);

        float dy = targetHeight - playerHeight;
        float totalDistance = Math.Sqrt(horizontalDist * horizontalDist + dy * dy);

        float pathLoss = model.CalculatePathLoss(totalDistance, debugFreqMHz);

        Print(string.Format("[RF Debug] Path Loss (FSPL): %1 dB", pathLoss), LogLevel.NORMAL);

        float diffLoss = 0.0;
        int numPeaks = peakHeights.Count();
        if (numPeaks > 0)
        {
            Print(string.Format("[RF Debug] Detected %1 obstructions (using top %2):", numPeaks, MAX_KNIFE_EDGES), LogLevel.NORMAL);
            int peaksToUse = numPeaks;
            if (peaksToUse > MAX_KNIFE_EDGES)
                peaksToUse = MAX_KNIFE_EDGES;

            for (int p = 0; p < peaksToUse; p++)
            {
                float h = peakHeights.Get(p);
                float pd1 = peakD1.Get(p);
                float pd2 = peakD2.Get(p);
                int sampleIdx = peakSamples.Get(p);
                float peakDiffLoss = model.CalculateDiffractionLoss(h, pd1, pd2, horizontalDist, wavelength);
                diffLoss += peakDiffLoss;
                Print(string.Format("  Peak %1: Sample %2, h=%3m above LOS, loss=%4 dB", p + 1, sampleIdx, h, peakDiffLoss), LogLevel.NORMAL);
            }
            Print(string.Format("[RF Debug] Total Diffraction Loss: %1 dB", diffLoss), LogLevel.NORMAL);
        }
        else
        {
            Print("[RF Debug] No terrain above LOS (clear line-of-sight)", LogLevel.NORMAL);
        }

        float fresnelLoss = 0.0;
        if (worstFresnelIntrusion > 0 && worstFresnelRadius > 0)
        {
            float clearancePercent = 1.0 - (worstFresnelIntrusion / worstFresnelRadius);
            if (clearancePercent < 0.2)
                fresnelLoss = 3.0;
            else if (clearancePercent < 0.4)
                fresnelLoss = 2.0;
            else if (clearancePercent < 0.6)
                fresnelLoss = 1.0;

            if (fresnelLoss > 0)
                Print(string.Format("[RF Debug] Fresnel Zone Intrusion: %1 dB (clearance %2%%)", fresnelLoss, (clearancePercent * 100.0).ToString(0)), LogLevel.NORMAL);
        }

        float totalLoss = pathLoss + diffLoss + fresnelLoss;
        float quality = model.LossToQuality(totalLoss);

        Print("[RF Debug] === RESULT ===", LogLevel.NORMAL);
        Print(string.Format("[RF Debug] Total Loss: %1 dB", totalLoss), LogLevel.NORMAL);
        Print(string.Format("[RF Debug] Signal Quality: %1 (1.0=perfect, 0.0=no signal)", quality), LogLevel.NORMAL);
        Print("=== END RF DEBUG ===", LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    protected static void DebugInsertPeak(array<float> heights, array<float> d1Arr, array<float> d2Arr, array<int> samples, float h, float d1, float d2, int sampleIdx)
    {
        int insertIdx = heights.Count();
        for (int i = 0; i < heights.Count(); i++)
        {
            if (h > heights.Get(i))
            {
                insertIdx = i;
                break;
            }
        }

        heights.InsertAt(h, insertIdx);
        d1Arr.InsertAt(d1, insertIdx);
        d2Arr.InsertAt(d2, insertIdx);
        samples.InsertAt(sampleIdx, insertIdx);

        while (heights.Count() > MAX_KNIFE_EDGES)
        {
            int lastIdx = heights.Count() - 1;
            heights.Remove(lastIdx);
            d1Arr.Remove(lastIdx);
            d2Arr.Remove(lastIdx);
            samples.Remove(lastIdx);
        }
    }
}
