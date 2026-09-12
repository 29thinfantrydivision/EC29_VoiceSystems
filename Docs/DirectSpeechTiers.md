# Direct-speech tiers (whisper / normal / yell)

Since 2026-09-12 a character's direct speech is transmitted from one of three dedicated VoN
components, and the range of each is a property of that component's ACP. Nothing on the
listener's side computes a volume any more.

## Why

The previous design modulated one global audio variable (`EC29_VonRange`) per incoming packet
on the listener. Every direct stream the listener played shared that value. With two people
talking, the quieter one played at the louder one's gain, which is how a whisper was heard at
10 to 20 m in the field. Script has no per-stream gain in this engine, so no listener-side scheme
could fix that. The engine's own per-source attenuation (the amplitude node in the transmitting
component's ACP) is the only per-speaker volume control there is, and the spectator quiet tier
had already proven it in the field.

## Pieces

| Piece | Where | Role |
|---|---|---|
| `SCR_VoNComponent_EC29Whisper/Normal/Yell` | `Scripts/Game/VON/EC29_VoiceTiers.c` | Empty subclasses of `SCR_VoNComponent`, one per mode. Named so they sort **after** the stock component (a strict prefix), which keeps the stock one the ear. |
| `EC29_VonWhisper/Normal/Yell.acp` | `Sounds/VON/` | Copies of the EC29 `von.acp` graph. Only `AmplitudeClass 23571` (the node `Shader Direct` feeds) differs: whisper 2/6 m, normal 15/20 m, yell 50/80 m. The yell ACP also puts `volume_dB 6` on `VON_DIRECT`. |
| `Character_Base.et` override | `Prefabs/Characters/Core/` | Adds the three components after the stock `SCR_VoNComponent`. |
| `EC29_VoiceTiers` | `EC29_VoiceTiers.c` | `FindTier`, `StockVoN`, `OuterRange` and the script-side mirrors of the ACP ranges (used only for the visual gates). |
| `SCR_VONController` (modded) | `EC29_VON_VONController.c` | `SetVONComponent` override resolves a character's stock component to the tier for its mode. `EC29_ApplyVoiceTier` switches tiers on F3. |

## Behaviour

- **F3** requests the new mode on the stock component (replicated, for labels and icons) and
  switches the transmit tier locally in the same call, before any packet leaves.
- **Held push-to-talk**: an F3 press during a hold is remembered and applied at key-up. The
  sentence keeps the range it started with.
- **Direct-speech toggle**: applied immediately. The toggle's capture is stopped, the tier
  swapped, and the toggle re-armed by `EC29_SelectVonComponent`.
- **Respawn, editor close, spectate exit**: every path that hands the controller a character
  component goes through `SetVONComponent`, so the tier substitution always applies. A fresh
  character starts on whisper (the mode enum's default).
- **Radio keyed while whispering**: the radio transmission goes out through the whisper tier,
  so the local "speaking aloud" side effect of a radio call is whisper-ranged too.
- **Spectators** still hear every mode at full volume out to the manager ear's range. The
  spectator shader uses amplitude node 200010, which the tier ACPs leave untouched.
- **Nametag icon / overlay entry** are gated by the speaker's replicated mode and the tier's
  outer range. They can lag the audio by one replication round trip after an F3 press. There is
  no listener-side signal that says which tier a packet came from, so that lag is accepted.

## Mission header

The range and volume attributes (`m_fWhisperRange`, `m_fYellVolume`, `m_fMinVolume` and the
rest) are gone from `EC29_VON_Settings`. Existing headers that still carry them load with an
unknown-property warning and otherwise work. Ranges are changed by editing the tier ACPs and
the mirror constants in `EC29_VoiceTiers`, together.

## Open items to verify in the field

1. **Yell loudness.** `volume_dB 6` on the yell ACP's `VON_DIRECT` sound is the one unverified
   piece. If it is not honoured, yell is normal loudness with a longer range.
2. **Yell range vs the 40 m wall.** If the engine stops delivering direct speech near 40 m
   regardless of the ACP, yell tops out there (as it does today).
3. **Which component receives.** With VERBOSE on, the first incoming packet logs the class of
   the component it arrived on (`[EC29-DBG][VoN] first incoming packet delivered to component
   class ...`). Per-packet bookkeeping is deduplicated either way.
4. **Mouth animation while talking through a tier.** The `VONAmplitude` signal is expected to
   come from any VoN component on the entity; confirm lips move on a whisperer.
