//------------------------------------------------------------------------------------------------
//! HUD indicator for the local player's selected voice mode (whisper / normal / yell).
//!
//! Registered on the PLAYER CONTROLLER's HUD manager (Prefabs/Characters/Core/
//! DefaultPlayerController.et), not the character's, so it keeps drawing inside vehicles. The
//! mode itself lives on the controlled character's stock VoN component and is looked up again
//! every frame through the shared cache.
//!
//! Shows the REPLICATED mode, so it can lag a key press by one round trip.
//!
//! Default behaviour: hidden at spawn, fades in for m_iVisibleDurationMs whenever the mode
//! changes (respawning into a different mode counts), then fades out slowly. With a duration of
//! 0 it stays up and only swaps icons. Whenever no voice component resolves (dead, spectating,
//! nothing controlled) it is taken off screen until one does.
//!
//! The fades run on the icon and its caption, not on m_wRoot: the base class drives the root's
//! opacity itself (show/hide and adaptive opacity) and the two would fight.
//!
//! Layout contract: an ImageWidget named "VoiceRangeIcon" anywhere under the root (required) and
//! an optional TextWidget "VoiceRangeLabel" that carries the mode name next to the icon.
class EC29_VoiceRangeDisplay : SCR_InfoDisplay
{
	protected static const string EC29_ICON_WIDGET = "VoiceRangeIcon";
	protected static const string EC29_CAPTION_WIDGET = "VoiceRangeLabel";
	protected static const float EC29_CAPTION_GAP = 6;

	protected static const string EC29_CAPTION_WHISPER = "#EC29-VON_Mode_Whisper";
	protected static const string EC29_CAPTION_NORMAL = "#EC29-VON_Mode_Normal";
	protected static const string EC29_CAPTION_YELL = "#EC29-VON_Mode_Yelling";

	[Attribute("{3262679C50EF4F01}UI/Textures/Icons/icons_wrapperUI.imageset", UIWidgets.ResourceNamePicker, "Imageset the three mode icons come from.", "imageset")]
	protected ResourceName m_sImageSet;

	[Attribute("speak", UIWidgets.EditBox, "Image name for WHISPER.")]
	protected string m_sSpriteWhisper;

	[Attribute("sound-on", UIWidgets.EditBox, "Image name for NORMAL (also used for any unrecognised mode).")]
	protected string m_sSpriteNormal;

	[Attribute("VON_directspeech", UIWidgets.EditBox, "Image name for YELL.")]
	protected string m_sSpriteYell;

	[Attribute("48", UIWidgets.EditBox, "Icon edge length in pixels (square).")]
	protected int m_iIconSize;

	[Attribute("18", UIWidgets.EditBox, "Distance from the left screen edge, pixels.")]
	protected int m_iMarginLeft;

	[Attribute("360", UIWidgets.EditBox, "Distance from the bottom screen edge up to the icon's TOP edge, pixels. Puts it just above the vanilla VoN transmission list.")]
	protected int m_iMarginBottom;

	[Attribute("3000", UIWidgets.EditBox, "How long the icon stays up after a mode change, ms. 0 keeps it up permanently.", "0 inf 100")]
	protected int m_iVisibleDurationMs;

	[Attribute("10", UIWidgets.Slider, "Fade-in speed (a full fade takes about 1/speed seconds). Matches vanilla's fast fade.", "0.5 50 0.5")]
	protected float m_fFadeInRate;

	[Attribute("1", UIWidgets.Slider, "Fade-out speed (a full fade takes about 1/speed seconds). Matches vanilla's slow fade.", "0.5 50 0.5")]
	protected float m_fFadeOutRate;

	protected ImageWidget m_wEC29_Icon;
	protected TextWidget m_wEC29_Caption;
	protected bool m_bEC29_Ready;

	//! Mode currently drawn. Seeded with WHISPER = the stock VoN component's spawn default; if
	//! one changes the other must, or the icon is wrong until the first cycle.
	protected EC29_EVoiceRange m_eEC29_Drawn = EC29_EVoiceRange.WHISPER;

	protected bool m_bEC29_HideArmed;
	protected float m_fEC29_ShownMs;

	//! True while hidden because no voice component resolved.
	protected bool m_bEC29_NoVoice;

	//------------------------------------------------------------------------------------------------
	override protected event void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);

		m_bEC29_Ready = false;

		if (!m_wRoot)
		{
			Print("[EC29_VON] EC29_VoiceRangeDisplay: root null after start draw - layout failed to load", LogLevel.WARNING);
			return;
		}

		m_wEC29_Icon = ImageWidget.Cast(m_wRoot.FindAnyWidget(EC29_ICON_WIDGET));
		if (!m_wEC29_Icon)
		{
			Print("[EC29_VON] EC29_VoiceRangeDisplay: VoiceRangeIcon widget not found in layout", LogLevel.WARNING);
			return;
		}

		m_wEC29_Caption = TextWidget.Cast(m_wRoot.FindAnyWidget(EC29_CAPTION_WIDGET));

		EC29_Place();

		m_eEC29_Drawn = EC29_EVoiceRange.WHISPER;
		EC29_Draw(m_eEC29_Drawn);

		m_bEC29_HideArmed = false;
		m_fEC29_ShownMs = 0;
		m_bEC29_NoVoice = false;

		if (EC29_AutoHides())
			EC29_SetOpacityNow(0);
		else
			EC29_SetOpacityNow(1);

		m_bEC29_Ready = true;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoiceHUD] voice-range display active (controller override applied), imageset %1", m_sImageSet);
	}

	//------------------------------------------------------------------------------------------------
	override protected event void UpdateValues(IEntity owner, float timeSlice)
	{
		super.UpdateValues(owner, timeSlice);

		if (!m_bEC29_Ready)
			return;

		SCR_VoNComponent von = EC29_LocalStockVoN();
		if (!von)
		{
			EC29_HideForNoVoice();
			return;
		}

		if (m_bEC29_NoVoice)
		{
			m_bEC29_NoVoice = false;

			// Permanent mode comes straight back; auto-hide mode waits for the next change.
			if (!EC29_AutoHides())
				EC29_FadeTo(1, m_fFadeInRate);
		}

		EC29_EVoiceRange mode = von.EC29_GetVoiceRange();
		if (mode != m_eEC29_Drawn)
		{
			m_eEC29_Drawn = mode;
			EC29_Draw(mode);

			if (EC29_AutoHides())
			{
				EC29_FadeTo(1, m_fFadeInRate);
				m_fEC29_ShownMs = 0;
				m_bEC29_HideArmed = true;
			}
		}

		if (!m_bEC29_HideArmed)
			return;

		m_fEC29_ShownMs += timeSlice * 1000;
		if (m_fEC29_ShownMs >= m_iVisibleDurationMs)
		{
			EC29_FadeTo(0, m_fFadeOutRate);
			m_bEC29_HideArmed = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool EC29_AutoHides()
	{
		return m_iVisibleDurationMs > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's STOCK VoN component, via the shared per-player cache (which also rejects
	//! a corpse left over from the previous life).
	protected SCR_VoNComponent EC29_LocalStockVoN()
	{
		PlayerController localController = GetGame().GetPlayerController();
		if (!localController)
			return null;

		return SCR_VoNComponent.EC29_GetVoNForPlayer(localController.GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	//! No component means no voice: the indicator would only be describing a body (or nothing).
	protected void EC29_HideForNoVoice()
	{
		if (m_bEC29_NoVoice)
			return;

		m_bEC29_NoVoice = true;
		m_bEC29_HideArmed = false;
		EC29_FadeTo(0, m_fFadeOutRate);
	}

	//------------------------------------------------------------------------------------------------
	//! Bottom-left anchor; the margins measure to the icon's top-left corner, so the icon hangs
	//! down from that point. The caption sits to the right, centred on the icon.
	protected void EC29_Place()
	{
		FrameSlot.SetAnchorMin(m_wEC29_Icon, 0, 1);
		FrameSlot.SetAnchorMax(m_wEC29_Icon, 0, 1);
		FrameSlot.SetAlignment(m_wEC29_Icon, 0, 0);
		FrameSlot.SetSizeToContent(m_wEC29_Icon, false);
		FrameSlot.SetSize(m_wEC29_Icon, m_iIconSize, m_iIconSize);
		FrameSlot.SetPos(m_wEC29_Icon, m_iMarginLeft, -m_iMarginBottom);

		if (!m_wEC29_Caption)
			return;

		FrameSlot.SetAnchorMin(m_wEC29_Caption, 0, 1);
		FrameSlot.SetAnchorMax(m_wEC29_Caption, 0, 1);
		FrameSlot.SetAlignment(m_wEC29_Caption, 0, 0.5);
		FrameSlot.SetSizeToContent(m_wEC29_Caption, true);
		FrameSlot.SetPos(m_wEC29_Caption, m_iMarginLeft + m_iIconSize + EC29_CAPTION_GAP, -m_iMarginBottom + m_iIconSize * 0.5);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_Draw(EC29_EVoiceRange mode)
	{
		string sprite = m_sSpriteNormal;
		string caption = EC29_CAPTION_NORMAL;

		if (mode == EC29_EVoiceRange.WHISPER)
		{
			sprite = m_sSpriteWhisper;
			caption = EC29_CAPTION_WHISPER;
		}
		else if (mode == EC29_EVoiceRange.YELL)
		{
			sprite = m_sSpriteYell;
			caption = EC29_CAPTION_YELL;
		}

		m_wEC29_Icon.LoadImageFromSet(0, m_sImageSet, sprite);

		if (m_wEC29_Caption)
			m_wEC29_Caption.SetText(caption);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoiceHUD] mode changed -> icon '%1'", sprite);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_FadeTo(float opacity, float rate)
	{
		AnimateWidget.Opacity(m_wEC29_Icon, opacity, rate);

		if (m_wEC29_Caption)
			AnimateWidget.Opacity(m_wEC29_Caption, opacity, rate);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_SetOpacityNow(float opacity)
	{
		AnimateWidget.StopAnimation(m_wEC29_Icon, WidgetAnimationOpacity);
		m_wEC29_Icon.SetOpacity(opacity);

		if (!m_wEC29_Caption)
			return;

		AnimateWidget.StopAnimation(m_wEC29_Caption, WidgetAnimationOpacity);
		m_wEC29_Caption.SetOpacity(opacity);
	}
}
