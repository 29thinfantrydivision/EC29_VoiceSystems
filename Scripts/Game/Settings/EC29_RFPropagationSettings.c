[BaseContainerProps(configRoot: true)]
class EC29_RFPropagationSettings
{
    protected static ref EC29_RFPropagationSettings s_Instance;
    protected static bool s_bInitialized = false;
    protected static const string JSON_CONFIG_PATH = "$profile:EC29_RFPropagation.json";

    [Attribute(defvalue: "0", desc: "Enable RF propagation simulation (terrain, obstacles, distance affects signal quality)", category: "RF Propagation", uiwidget: UIWidgets.CheckBox)]
    bool m_bRFPropagationEnabled;

    [Attribute(defvalue: "0", desc: "Enable debug output to RPT log", category: "RF Propagation", uiwidget: UIWidgets.CheckBox)]
    bool m_bDebugEnabled;

    //------------------------------------------------------------------------------------------------
    static EC29_RFPropagationSettings GetInstance()
    {
        // Only the server's copy is ever consumed (values replicate via
        // EC29_RFPropagationNetworkComponent RplProps). Clients must not read or
        // CREATE $profile:EC29_RFPropagation.json - return inert defaults instead.
        if (Replication.IsRunning() && !Replication.IsServer())
        {
            if (!s_Instance)
            {
                s_Instance = new EC29_RFPropagationSettings();
                if (EC29_Debug.VERBOSE)
                    Print("[EC29-DBG][RadioNet] CLIENT RF settings instance created with defaults (no profile JSON IO on clients)");
            }

            return s_Instance;
        }

        if (!s_Instance)
        {
            s_Instance = new EC29_RFPropagationSettings();
            s_Instance.m_bRFPropagationEnabled = false;
            s_Instance.m_bDebugEnabled = false;

            if (!LoadFromJSON())
            {
                LoadFromConf();
            }

            if (!s_bInitialized)
            {
                s_bInitialized = true;
                Print(string.Format("[EC29 RFPropagation] RF Propagation: %1 | Debug: %2",
                    s_Instance.m_bRFPropagationEnabled, s_Instance.m_bDebugEnabled));
            }
        }

        return s_Instance;
    }

    //------------------------------------------------------------------------------------------------
    protected static bool LoadFromJSON()
    {
        SCR_JsonLoadContext loadContext = new SCR_JsonLoadContext();
        if (!loadContext.LoadFromFile(JSON_CONFIG_PATH))
        {
            // A present-but-unparseable file is an admin's hand-edited config;
            // writing defaults over it would destroy their settings.
            if (FileIO.FileExist(JSON_CONFIG_PATH))
                Print("[EC29 RFPropagation] ERROR: config exists but failed to parse - fix or delete it; using defaults this run", LogLevel.ERROR);
            else
                CreateDefaultJSON();
            return false;
        }

        bool rfEnabled, debugEnabled;
        if (loadContext.ReadValue("RFPropagationEnabled", rfEnabled))
            s_Instance.m_bRFPropagationEnabled = rfEnabled;

        if (loadContext.ReadValue("DebugEnabled", debugEnabled))
            s_Instance.m_bDebugEnabled = debugEnabled;

        Print("[EC29 RFPropagation] Loaded settings from JSON");
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected static void CreateDefaultJSON()
    {
        FileHandle file = FileIO.OpenFile(JSON_CONFIG_PATH, FileMode.APPEND);
        if (file)
            file.Close();

        file = FileIO.OpenFile(JSON_CONFIG_PATH, FileMode.WRITE);
        if (!file)
        {
            Print("[EC29 RFPropagation] ERROR: Failed to create JSON config", LogLevel.ERROR);
            return;
        }

        file.WriteLine("{");
        file.WriteLine("    \"RFPropagationEnabled\": false,");
        file.WriteLine("    \"DebugEnabled\": false");
        file.WriteLine("}");
        file.Close();

        Print("[EC29 RFPropagation] Created default JSON config");
    }

    //------------------------------------------------------------------------------------------------
    protected static void LoadFromConf()
    {
        Resource holder = BaseContainerTools.LoadContainer("{C99D2868A888D3BC}Configs/EC29_VONConfig.conf");

        if (holder && holder.GetResource())
        {
            BaseContainer container = holder.GetResource().ToBaseContainer();
            if (container)
            {
                EC29_RFPropagationSettings confSettings = EC29_RFPropagationSettings.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
                if (confSettings)
                {
                    s_Instance.m_bRFPropagationEnabled = confSettings.m_bRFPropagationEnabled;
                    s_Instance.m_bDebugEnabled = confSettings.m_bDebugEnabled;
                    Print("[EC29 RFPropagation] Loaded settings from .conf file");
                    return;
                }
            }
        }

        Print("[EC29 RFPropagation] Using default settings (disabled)", LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    static bool IsRFPropagationEnabled()
    {
        EC29_RFPropagationSettings settings = GetInstance();
        if (settings)
            return settings.m_bRFPropagationEnabled;
        return false;
    }

    //------------------------------------------------------------------------------------------------
    static bool IsDebugEnabled()
    {
        EC29_RFPropagationSettings settings = GetInstance();
        if (settings)
            return settings.m_bDebugEnabled;
        return false;
    }

    //------------------------------------------------------------------------------------------------
    static void SetRFPropagationEnabled(bool enabled)
    {
        EC29_RFPropagationSettings settings = GetInstance();
        if (settings)
        {
            settings.m_bRFPropagationEnabled = enabled;
            Print(string.Format("[EC29 RFPropagation] RF Propagation set to %1", enabled));
        }
    }

    //------------------------------------------------------------------------------------------------
    static void SetDebugEnabled(bool enabled)
    {
        EC29_RFPropagationSettings settings = GetInstance();
        if (settings)
        {
            settings.m_bDebugEnabled = enabled;
            Print(string.Format("[EC29 RFPropagation] Debug mode set to %1", enabled));
        }
    }
}
