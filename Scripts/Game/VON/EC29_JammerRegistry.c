class EC29_JammerRegistry
{
    protected ref array<EC29_JammerComponent> m_aJammers = new array<EC29_JammerComponent>();


    void RegisterJammer(EC29_JammerComponent jammer)
    {
        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][Jammer] Registered jammer (total=%1)", m_aJammers.Count() + 1);
        if (!m_aJammers.Contains(jammer))
            m_aJammers.Insert(jammer);
    }

    void UnregisterJammer(EC29_JammerComponent jammer)
    {
        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][Jammer] Unregistered jammer (total before remove=%1)", m_aJammers.Count());
        m_aJammers.RemoveItem(jammer);
    }

    // Returns degradation from jammers: 0.0 = no effect, 1.0 = full jam
    float CalculateJammerDegradation(vector receiverPos)
    {
        float worstDegradation = 0.0;

        foreach (EC29_JammerComponent jammer : m_aJammers)
        {
            if (!jammer || !jammer.IsJammerActive())
                continue;

            float degradation = CalculateSingleJammerEffect(jammer, receiverPos);

            if (degradation > worstDegradation)
                worstDegradation = degradation;
        }

        return worstDegradation;
    }

        protected float CalculateSingleJammerEffect(EC29_JammerComponent jammer, vector receiverPos)
    {
        vector jammerPos = jammer.GetPosition();
        float range = jammer.GetRange();
        float distance = vector.Distance(receiverPos, jammerPos);

        if (distance >= range)
            return 0.0;

        float coneAngle = jammer.GetConeAngle();
        if (coneAngle < 180)
        {
            vector dirToTarget = (receiverPos - jammerPos).Normalized();
            vector jammerForward = jammer.GetForwardVector();
            float dot = vector.Dot(dirToTarget, jammerForward);
            float angle = Math.Acos(dot) * Math.RAD2DEG;

            if (angle > coneAngle * 0.5)
                return 0.0;
        }

        //RF propogation model exists in a different script now - def we can find a way to integrate this with the jammers LOL
        //TODO: in addition to adding terrain obsturction back in a less scuffed way... have inverse square law calculation for the weaker jammers?

        // Area denial jammer - strong throughout, fades at edge (uses ratio squared just like IRL base jammers)
        float ratio = distance / range;
        float quality = ratio * ratio;
        float degradation = 1.0 - quality;
        return degradation;
    }
    int GetActiveJammerCount()
    {
        int count = 0;
        foreach (EC29_JammerComponent jammer : m_aJammers)
        {
            if (jammer && jammer.IsJammerActive())
                count++;
        }
        return count;
    }

    //------------------------------------------------------------------------------------------------
    //! Debug function - call from Workbench console: EC29_JammerRegistry.DebugJammers()
    static void DebugJammers()
    {
        Print("=== EC29 JAMMER DEBUG ===", LogLevel.NORMAL);

        // Get local player position
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
        {
            Print("[Jammer Debug] ERROR: No PlayerController found", LogLevel.ERROR);
            return;
        }

        IEntity controlled = pc.GetControlledEntity();
        if (!controlled)
        {
            Print("[Jammer Debug] ERROR: No controlled entity", LogLevel.ERROR);
            return;
        }

        vector playerPos = controlled.GetOrigin();
        Print(string.Format("[Jammer Debug] Player position: %1", playerPos), LogLevel.NORMAL);

        EC29_JammerRegistry manager = EC29_RadioState.GetInstance().Jammers();
        int totalJammers = manager.m_aJammers.Count();
        Print(string.Format("[Jammer Debug] Total registered jammers: %1", totalJammers), LogLevel.NORMAL);

        if (totalJammers == 0)
        {
            Print("[Jammer Debug] No jammers registered!", LogLevel.WARNING);
            return;
        }

        float worstDegradation = 0.0;

        for (int i = 0; i < totalJammers; i++)
        {
            EC29_JammerComponent jammer = manager.m_aJammers.Get(i);
            if (!jammer)
            {
                Print(string.Format("[Jammer Debug] Jammer %1: NULL REFERENCE", i), LogLevel.WARNING);
                continue;
            }

            vector jammerPos = jammer.GetPosition();
            float range = jammer.GetRange();
            float coneAngle = jammer.GetConeAngle();
            bool active = jammer.IsJammerActive();
            float distance = vector.Distance(playerPos, jammerPos);

            Print(string.Format("[Jammer Debug] --- Jammer %1 ---", i), LogLevel.NORMAL);
            Print(string.Format("  Position: %1", jammerPos), LogLevel.NORMAL);
            Print(string.Format("  Range: %1 m", range), LogLevel.NORMAL);
            Print(string.Format("  Cone Angle: %1 deg", coneAngle), LogLevel.NORMAL);
            Print(string.Format("  Active: %1", active), LogLevel.NORMAL);
            Print(string.Format("  Distance to player: %1 m", distance), LogLevel.NORMAL);

            if (!active)
            {
                Print("  Effect: INACTIVE (0.0)", LogLevel.NORMAL);
                continue;
            }

            if (distance >= range)
            {
                Print(string.Format("  Effect: OUT OF RANGE (%1 >= %2)", distance, range), LogLevel.NORMAL);
                continue;
            }

            // Check cone angle if directional
            if (coneAngle < 180)
            {
                vector dirToTarget = (playerPos - jammerPos).Normalized();
                vector jammerForward = jammer.GetForwardVector();
                float dot = vector.Dot(dirToTarget, jammerForward);
                float angle = Math.Acos(dot) * Math.RAD2DEG;

                if (angle > coneAngle * 0.5)
                {
                    Print(string.Format("  Effect: OUTSIDE CONE (angle %1 > %2)", angle, coneAngle * 0.5), LogLevel.NORMAL);
                    continue;
                }
            }

            // Calculate degradation
            float ratio = distance / range;
            float quality = ratio * ratio;
            float degradation = 1.0 - quality;

            Print(string.Format("  Effect: JAMMING - degradation = %1", degradation), LogLevel.NORMAL);
            Print(string.Format("    (ratio=%1, ratio^2=%2)", ratio, quality), LogLevel.NORMAL);

            if (degradation > worstDegradation)
                worstDegradation = degradation;
        }

        float finalQuality = 1.0 - worstDegradation;
        Print("=== RESULT ===", LogLevel.NORMAL);
        Print(string.Format("  Worst degradation: %1", worstDegradation), LogLevel.NORMAL);
        Print(string.Format("  Final signal quality: %1 (1.0=clear, 0.0=jammed)", finalQuality), LogLevel.NORMAL);
        Print("=== END JAMMER DEBUG ===", LogLevel.NORMAL);
    }
}