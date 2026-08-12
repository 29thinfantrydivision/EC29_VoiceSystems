class EC29_SignalManager
{
    private static ref EC29_SignalManager s_Instance;

    static EC29_SignalManager GetInstance()
    {
        if (!s_Instance)
            s_Instance = new EC29_SignalManager();

        return s_Instance;
    }

    float GetSignalQuality(vector transmitterPos, vector receiverPos, float frequencyKHz = 0)
    {
        EC29_RFPropagationModel propagation = EC29_RFPropagationModel.GetInstance();
        return propagation.CalculateSignalQuality(transmitterPos, receiverPos, frequencyKHz);
    }

    float GetJammerStrength(vector receiverPos)
    {
        EC29_JammerManager jammerManager = EC29_JammerManager.GetInstance();
        return jammerManager.CalculateJammerDegradation(receiverPos);
    }
}