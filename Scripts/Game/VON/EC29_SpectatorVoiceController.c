//------------------------------------------------------------------------------------------------
//! Server-side half of spectator voice: radio state on a client-owned manager is owner-
//! authoritative as the server sees it, so what the SERVER must know (the flag, power, the
//! interpolation switch, the position) is authored here on request from the owner.
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	protected static const float EC29_SNAP_LOG_MS = 2000;
	protected float m_fEC29LastSnapLogMs;

	//------------------------------------------------------------------------------------------------
	//! On a listen server the requester IS the server; an Rpc to Server from the authority does
	//! not necessarily loop back, so the handler is called directly.
	void EC29_AskSpectatorVoice(bool enable)
	{
		if (Replication.IsServer())
			EC29_RpcAsk_SpectatorVoice_Server(enable);
		else
			Rpc(EC29_RpcAsk_SpectatorVoice_Server, enable);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void EC29_RpcAsk_SpectatorVoice_Server(bool enable)
	{
		SCR_EditorManagerEntity mgr = EC29_ManagerOf(GetPlayerId());
		if (!mgr)
		{
			PrintFormat("[EC29-DBG][SpecVon] server: pid %1 has no editor manager - cannot flag", GetPlayerId(), level: LogLevel.WARNING);
			return;
		}

		// Flag first: the native power gate (owner is an active editor) reads it.
		mgr.EC29_SetSpectatorVoice(enable);

		// The server copy moves only through the movement interpolator, at a capped speed: a map
		// teleport left it 13 km behind. With interpolation off nothing but the owner's snap RPC
		// moves it, which is exactly the contract while spectating.
		NwkMovementComponent nwk = NwkMovementComponent.Cast(mgr.FindComponent(NwkMovementComponent));
		if (nwk)
			nwk.EnableInterpolation(!enable);

		bool powered = false;
		BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));
		if (radio)
		{
			radio.SetPower(enable);
			powered = radio.IsPowered();
		}

		if (EC29_Debug.VERBOSE)
		{
			// The manager id pairs with the owner's own EnterSpectate line: a reconnecting player
			// whose core handed back a different manager shows up as two different ids.
			string mgrId = "none";
			RplComponent mgrRpl = RplComponent.Cast(mgr.FindComponent(RplComponent));
			if (mgrRpl)
				mgrId = mgrRpl.Id().AsString();

			PrintFormat("[EC29-DBG][SpecVon] server: pid %1 mgr=%2 spectator-voice=%3 radio powered=%4", GetPlayerId(), mgrId, enable, powered);
		}
	}

	//------------------------------------------------------------------------------------------------
	void EC29_AskManagerSnap(vector pos)
	{
		if (Replication.IsServer())
			EC29_RpcAsk_ManagerSnap_Server(pos);
		else
			Rpc(EC29_RpcAsk_ManagerSnap_Server, pos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void EC29_RpcAsk_ManagerSnap_Server(vector pos)
	{
		SCR_EditorManagerEntity mgr = EC29_ManagerOf(GetPlayerId());
		if (!mgr || !mgr.EC29_IsSpectatorVoice())
			return;

		vector before = mgr.GetOrigin();
		mgr.SetOrigin(pos);

		// Read back rather than trust the call: SetOrigin on a CLIENT-OWNED entity has historically
		// reported success on the server and changed nothing. A "was/asked/now" that disagrees says
		// the authority cannot place this manager at all, which no range tuning can fix.
		if (!EC29_Debug.VERBOSE)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();
		if (now - m_fEC29LastSnapLogMs < EC29_SNAP_LOG_MS)
			return;

		m_fEC29LastSnapLogMs = now;
		PrintFormat("[EC29-DBG][SpecVon] server: snap pid=%1 was=%2 asked=%3 now=%4", GetPlayerId(), before, pos, mgr.GetOrigin());
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_EditorManagerEntity EC29_ManagerOf(int playerId)
	{
		SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
		if (!core)
			return null;

		return core.GetEditorManager(playerId);
	}
}
