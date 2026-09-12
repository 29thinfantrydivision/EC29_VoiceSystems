//------------------------------------------------------------------------------------------------
//! DIRECT-SPEECH TIERS: whisper / normal / yell as three transmit components on every character.
//!
//! WHY COMPONENTS AND NOT A GAIN. Until 2026-09-12 the three modes were one shared audio
//! variable (EC29_VonRange) rewritten per incoming packet on the LISTENER's machine. Every direct
//! stream a listener played was modulated by that one value, so with two people talking the
//! quieter one played at the louder one's gain (the whisper-heard-at-20-m field report), and a
//! sender the listener could not resolve fell through to full volume. Script has no per-stream
//! gain in this engine - the receive callback names the speaker but hands over no handle to their
//! stream - so no amount of listener-side bookkeeping could make that reliable.
//!
//! What the engine DOES do per source, with no script in the loop, is spatial attenuation from
//! the transmitting component's ACP (the amplitude node's inner/outer range). The spectator quiet
//! tier is that mechanism and the field confirmed it: a component whose ACP ranges are 0 / 0.0001
//! is inaudible to everyone. So each mode is its own component with its own ACP, the speaker's
//! machine picks which one transmits BEFORE the first packet leaves, and every listener hears the
//! right range by construction. Nothing races, nothing is shared between streams, nothing
//! depends on the listener resolving the sender.
//!
//! THE THREE ACPs are byte-for-byte the stock EC29 von.acp graph except:
//!   - AmplitudeClass 23571 (the node "Shader Direct" actually feeds - see EC29_SpectatorVonTiers.c
//!     for why 200010 is not it) gets innerRange / outerRange per tier,
//!   - the yell ACP adds volume_dB on the VON_DIRECT sound so a shout is louder, not just longer.
//! Everything else in the graph (radio chain, ear routing, spectator gate) is identical, so a
//! radio keyed from any tier sounds exactly as before. The ranges here are MIRRORS of the ACP
//! values for the UI gates (nametag icon, overlay entry); the ACP is the truth. Change both.
//!
//! CLASS NAMES ARE LOAD-BEARING. An entity orders its components by class name and the engine
//! plays incoming streams through the FIRST VoN component (field-measured on the editor manager,
//! 2026-09-05, five runs). On a character that first component must stay the stock
//! SCR_VoNComponent - it is the ear, it carries the replicated voice mode, and vanilla's
//! FindComponent(SCR_VoNComponent) resolves it. "SCR_VoNComponent" is a strict prefix of every
//! tier name below, so the tiers sort after it under any string ordering. Do not rename them to
//! an EC29_ prefix: 'E' < 'S' would make a tier the ear and every listener would hear the world
//! through a whisper range.
//!
//! WHAT STAYS ON THE STOCK COMPONENT: the replicated mode enum (m_eEC29VoiceRange) and its RPC.
//! Tiers carry no state; they are picked, not told.
//!
//! KNOWN COSMETICS. SCR_VoNComponent.IsTransmiting()/IsTransmitingRadio() answer for the component
//! that captured, and vanilla asks the stock one (SCR_AvailableActionsConditionData) - the
//! "using radio" action-hint condition reads false while a tier transmits. Nothing audible.
//------------------------------------------------------------------------------------------------
class SCR_VoNComponent_EC29TierClass : SCR_VoNComponentClass
{
}

class SCR_VoNComponent_EC29Tier : SCR_VoNComponent
{
}

class SCR_VoNComponent_EC29WhisperClass : SCR_VoNComponent_EC29TierClass
{
}

class SCR_VoNComponent_EC29Whisper : SCR_VoNComponent_EC29Tier
{
}

class SCR_VoNComponent_EC29NormalClass : SCR_VoNComponent_EC29TierClass
{
}

class SCR_VoNComponent_EC29Normal : SCR_VoNComponent_EC29Tier
{
}

class SCR_VoNComponent_EC29YellClass : SCR_VoNComponent_EC29TierClass
{
}

class SCR_VoNComponent_EC29Yell : SCR_VoNComponent_EC29Tier
{
}

//------------------------------------------------------------------------------------------------
//! Lookup and the UI-side mirror of the ACP ranges.
class EC29_VoiceTiers
{
	//! Outer (silent) range per tier, metres - MIRRORS of the ACP amplitude nodes:
	//!   EC29_VonWhisper.acp  innerRange 2  / outerRange 6
	//!   EC29_VonNormal.acp   innerRange 15 / outerRange 20
	//!   EC29_VonYell.acp     innerRange 50 / outerRange 80  (+6 dB on VON_DIRECT)
	//! Used only to gate visuals (nametag icon, overlay entry) so they stop where the audio
	//! stops. Audio itself never reads these.
	static const float WHISPER_OUTER_M = 6.0;
	static const float NORMAL_OUTER_M  = 20.0;
	static const float YELL_OUTER_M    = 80.0;

	//------------------------------------------------------------------------------------------------
	static float OuterRange(EC29_EVoiceRange mode)
	{
		switch (mode)
		{
			case EC29_EVoiceRange.WHISPER: return WHISPER_OUTER_M;
			case EC29_EVoiceRange.YELL:    return YELL_OUTER_M;
		}

		return NORMAL_OUTER_M;
	}

	//------------------------------------------------------------------------------------------------
	//! The tier component for a mode on a character, or null when the character has none (a
	//! character prefab that does not inherit EC29's Character_Base override).
	static SCR_VoNComponent FindTier(IEntity character, EC29_EVoiceRange mode)
	{
		if (!character)
			return null;

		switch (mode)
		{
			case EC29_EVoiceRange.WHISPER:
				return SCR_VoNComponent_EC29Whisper.Cast(character.FindComponent(SCR_VoNComponent_EC29Whisper));
			case EC29_EVoiceRange.YELL:
				return SCR_VoNComponent_EC29Yell.Cast(character.FindComponent(SCR_VoNComponent_EC29Yell));
		}

		return SCR_VoNComponent_EC29Normal.Cast(character.FindComponent(SCR_VoNComponent_EC29Normal));
	}

	//------------------------------------------------------------------------------------------------
	//! True for a tier component (as opposed to the stock ear or another system's component).
	static bool IsTier(SCR_VoNComponent comp)
	{
		return comp && comp.IsInherited(SCR_VoNComponent_EC29Tier);
	}

	//------------------------------------------------------------------------------------------------
	//! The entity's STOCK SCR_VoNComponent - exact type, never a subclass. This is the ear, and
	//! the one that carries the replicated voice mode. Scans instead of FindComponent so the
	//! answer does not depend on component order or on what FindComponent does with subclasses.
	static SCR_VoNComponent StockVoN(IEntity entity)
	{
		if (!entity)
			return null;

		array<Managed> comps = {};
		entity.FindComponents(SCR_VoNComponent, comps);
		foreach (Managed comp : comps)
		{
			if (comp.Type() == SCR_VoNComponent)
				return SCR_VoNComponent.Cast(comp);
		}

		return null;
	}
}
