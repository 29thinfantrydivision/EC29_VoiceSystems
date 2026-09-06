//------------------------------------------------------------------------------------------------
//! Every player's editor manager spawns from EC29_EditorManager.et: the vanilla manager plus the
//! spectator transceiver, the loud ear and the quiet transmit tier (see EC29_SpectatorVonService,
//! and EC29_SpectatorVonTiers for why the ear's CLASS NAME matters). The core
//! consults this getter for every manager it creates and spawns a settings entity itself when
//! the world has none. Derived from the vanilla prefab, never overriding it - ACE Finger already
//! overrides EditorManager.et and stays intact underneath. A world-placed override still wins.
//------------------------------------------------------------------------------------------------
modded class SCR_EditorSettingsEntity
{
	protected static const ResourceName EC29_EDITOR_MANAGER_PREFAB = "{6A2C5D1E8B37F4D0}Prefabs/Editor/EC29_EditorManager.et";

	//------------------------------------------------------------------------------------------------
	override ResourceName GetPrefab(ResourceName basePrefab)
	{
		ResourceName prefab = super.GetPrefab(basePrefab);
		if (prefab != basePrefab)
			return prefab;

		return EC29_EDITOR_MANAGER_PREFAB;
	}
}
