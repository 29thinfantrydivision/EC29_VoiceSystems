class EC29_VoiceRangeDisplay : SCR_InfoDisplay
{
	[Attribute(defvalue: "{8C4856BE7FC357C0}UI/Imagesets/HUD/VON/VON_Icons.imageset", uiwidget: UIWidgets.ResourceNamePicker, desc: "Imageset containing the voice-range mode icons", params: "imageset")]
	protected ResourceName m_sImageSet;

	[Attribute(defvalue: "EC29_VON_Whisper", uiwidget: UIWidgets.EditBox, desc: "Sprite name for whisper mode within the imageset")]
	protected string m_sSpriteWhisper;

	[Attribute(defvalue: "EC29_VON_Normal", uiwidget: UIWidgets.EditBox, desc: "Sprite name for normal mode within the imageset")]
	protected string m_sSpriteNormal;

	[Attribute(defvalue: "EC29_VON_Yelling", uiwidget: UIWidgets.EditBox, desc: "Sprite name for yell mode within the imageset")]
	protected string m_sSpriteYell;

	[Attribute(defvalue: "48", uiwidget: UIWidgets.EditBox, desc: "Icon size in pixels (square)")]
	protected int m_iIconSize;

	[Attribute(defvalue: "18", uiwidget: UIWidgets.EditBox, desc: "Pixels from left edge of screen.")]
	protected int m_iMarginLeft;

	[Attribute(defvalue: "360", uiwidget: UIWidgets.EditBox, desc: "Pixels above bottom edge of screen (icon top edge). 360 puts it just above the VoN transmission slot in the default HUD; bump higher if it overlaps action prompts when many are stacked.")]
	protected int m_iMarginBottom;

	[Attribute(defvalue: "3000", uiwidget: UIWidgets.EditBox, desc: "Milliseconds to keep the icon visible after a mode change. Set to 0 to keep the icon permanently visible.", params: "0 inf 100")]
	protected int m_iVisibleDurationMs;

	[Attribute(defvalue: "10", uiwidget: UIWidgets.EditBox, desc: "Fade-in animation rate (higher = faster, full fade ~ 1/rate seconds). Matches vanilla UIConstants.FADE_RATE_FAST.", params: "0.5 50 0.5")]
	protected float m_fFadeInRate;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.EditBox, desc: "Fade-out animation rate (lower = slower fade, full fade ~ 1/rate seconds). Matches vanilla UIConstants.FADE_RATE_SLOW which BI uses for elements meant to linger before fading.", params: "0.5 50 0.5")]
	protected float m_fFadeOutRate;

	protected ImageWidget m_wIcon;
	protected int m_iLastRange = -1;
	protected float m_fVisibleAccumMs;
	protected bool m_bIsFadingIn;

	//------------------------------------------------------------------------------------------------
	override void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);

		if (!m_wRoot)
		{
			Print("[EC29_VON] EC29_VoiceRangeDisplay: m_wRoot is null after super.OnStartDraw - layout failed to load", LogLevel.WARNING);
			return;
		}

		m_wIcon = ImageWidget.Cast(m_wRoot.FindAnyWidget("VoiceRangeIcon"));
		if (!m_wIcon)
		{
			Print("[EC29_VON] EC29_VoiceRangeDisplay: VoiceRangeIcon widget not found in layout", LogLevel.WARNING);
			return;
		}

		// Position the icon explicitly via FrameSlot - matches the pattern in SCR_CharacterControllerComponent.
		// Anchor point at parent's bottom-left (0, 1), then size + position from there.
		// Negative Y in SetPos moves up (anchor sits at parent bottom, screen Y grows downward).
		FrameSlot.SetAnchorMin(m_wIcon, 0, 1);
		FrameSlot.SetAnchorMax(m_wIcon, 0, 1);
		FrameSlot.SetSize(m_wIcon, m_iIconSize, m_iIconSize);
		FrameSlot.SetPos(m_wIcon, m_iMarginLeft, -m_iMarginBottom);

		// Initial display: NORMAL. Imagesets reload on each mode change rather than
		// pre-register multiple slots - vanilla code (SCR_GroupFlagImageComponent etc.)
		// uniformly uses LoadImageFromSet with idx 0.
		m_wIcon.LoadImageFromSet(0, m_sImageSet, m_sSpriteNormal);
		m_iLastRange = (int)EC29_EVoiceRange.NORMAL;

		// Start hidden if we're auto-fading; visible if duration is 0 (permanent mode).
		if (m_iVisibleDurationMs > 0)
			m_wRoot.SetOpacity(0);
	}

	//------------------------------------------------------------------------------------------------
	protected string EC29_GetSpriteForRange(int range)
	{
		switch (range)
		{
			case (int)EC29_EVoiceRange.WHISPER: return m_sSpriteWhisper;
			case (int)EC29_EVoiceRange.YELL:    return m_sSpriteYell;
		}

		return m_sSpriteNormal;
	}

	//------------------------------------------------------------------------------------------------
	override void UpdateValues(IEntity owner, float timeSlice)
	{
		if (!m_wIcon)
			return;

		// Display is registered on the player controller (so it persists into vehicles),
		// so the VoN component lives on the controlled character, not on owner. The shared
		// cache re-runs FindComponent only when the controlled entity changes.
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		SCR_VoNComponent vonComp = SCR_VoNComponent.EC29_GetVoNForPlayer(pc.GetPlayerId());
		if (!vonComp)
			return;

		int range = (int)vonComp.EC29_GetVoiceRange();
		bool changed = range != m_iLastRange;

		if (changed)
		{
			m_wIcon.LoadImageFromSet(0, m_sImageSet, EC29_GetSpriteForRange(range));
			m_iLastRange = range;

			// Mode just changed: kick off fade-in and reset the visibility timer.
			if (m_iVisibleDurationMs > 0)
			{
				AnimateWidget.Opacity(m_wRoot, 1.0, m_fFadeInRate);
				m_fVisibleAccumMs = 0;
				m_bIsFadingIn = true;
			}
		}

		// Auto-hide: once enough time has passed since the last change, fade out.
		// 0 duration means "permanent" - skip the timer entirely.
		if (m_iVisibleDurationMs > 0 && m_bIsFadingIn)
		{
			m_fVisibleAccumMs += timeSlice * 1000;
			if (m_fVisibleAccumMs >= m_iVisibleDurationMs)
			{
				AnimateWidget.Opacity(m_wRoot, 0.0, m_fFadeOutRate);
				m_bIsFadingIn = false;
			}
		}
	}
}
