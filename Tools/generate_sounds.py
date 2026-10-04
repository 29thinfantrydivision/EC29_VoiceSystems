"""Generates every EC29 UI/radio sound effect from scratch.

All shipped .wav files under Sounds/ are produced by this script - pure synthesis
(sine tones, filtered noise, envelopes), no sampled or third-party audio. Re-run it
after editing a recipe; the .meta files (resource GUIDs) are left untouched, so
nothing that references a sound needs to change.

    python Tools/generate_sounds.py

Requires numpy. Output: 48 kHz, mono, 16-bit PCM.
"""

import os
import wave

import numpy as np

SR = 48000
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
RNG = np.random.default_rng(29)  # fixed seed: regenerating gives byte-identical files


def t(dur):
    return np.arange(int(SR * dur)) / SR


def env(n, attack=0.004, release=0.02):
    """Linear attack, flat body, exponential-ish release - no clicks at the edges."""
    e = np.ones(n)
    a = max(1, int(SR * attack))
    r = max(1, int(SR * release))
    e[:a] = np.linspace(0.0, 1.0, a)
    e[-r:] *= np.linspace(1.0, 0.0, r) ** 2
    return e


def tone(freq, dur, harmonics=(1.0,), attack=0.004, release=0.02):
    x = t(dur)
    y = sum(amp * np.sin(2 * np.pi * freq * (i + 1) * x) for i, amp in enumerate(harmonics))
    return y * env(len(x), attack, release)


def sweep(f0, f1, dur, attack=0.004, release=0.02):
    x = t(dur)
    phase = 2 * np.pi * (f0 * x + (f1 - f0) * x * x / (2 * dur))
    return np.sin(phase) * env(len(x), attack, release)


def silence(dur):
    return np.zeros(int(SR * dur))


def bandpass_noise(dur, lo, hi):
    n = int(SR * dur)
    spec = np.fft.rfft(RNG.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    spec[(f < lo) | (f > hi)] = 0
    y = np.fft.irfft(spec, n)
    return y / np.abs(y).max()


def decay(n, tau):
    return np.exp(-np.arange(n) / (SR * tau))


def cat(*parts):
    return np.concatenate(parts)


def normalize(y, peak):
    return y / np.abs(y).max() * peak


def write(rel_path, y, peak):
    y = normalize(y, peak)
    pcm = (np.clip(y, -1, 1) * 32767).astype("<i2")
    path = os.path.join(ROOT, rel_path)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"wrote {rel_path} ({len(y) / SR:.3f}s)")


# --- Push-to-talk beep styles (Sounds/VON/EC29_beep.acp) -------------------------------------

# HIGH style: one bright, short square-ish chirp.
write("Sounds/EC29_Beeps/beep_high.wav",
      tone(1050, 0.10, harmonics=(1.0, 0.0, 0.18, 0.0, 0.06)), 0.55)

# LOW style: a lower, longer two-step tone.
write("Sounds/EC29_Beeps/beep_low.wav",
      cat(tone(520, 0.14, harmonics=(1.0, 0.25)), silence(0.03),
          tone(470, 0.18, harmonics=(1.0, 0.25), release=0.06)), 0.5)

# Release click: a damped low thump with a short noise transient on top.
n = int(SR * 0.12)
thump = np.sin(2 * np.pi * 340 * t(0.12)) * decay(n, 0.025)
snap = bandpass_noise(0.12, 1500, 6000) * decay(n, 0.004)
write("Sounds/EC29_Beeps/click_off.wav", (thump + 0.6 * snap) * env(n, 0.0005, 0.01), 0.9)

# CLASSIC style, key-down: a short upward chirp.
write("Sounds/VON/EC29_FX/StartRadioTransmissionBeep.wav",
      sweep(900, 1250, 0.09, release=0.03), 0.3)

# CLASSIC style, key-up: two descending pips.
write("Sounds/VON/EC29_FX/EndRadioTransmission2.wav",
      cat(tone(1300, 0.07), silence(0.035), tone(950, 0.11, release=0.04)), 0.3)

# Receive squelch tail: band-limited hiss that dies away.
n = int(SR * 0.30)
write("Sounds/EC29_Sound/squelch_tail_01.wav",
      bandpass_noise(0.30, 600, 5200) * decay(n, 0.07) * env(n, 0.002, 0.04), 0.65)

# --- Radial-menu feedback (Scripts/Game/VON/EC29_VON_VONController.c) -------------------------

# Rejected input: a buzzy descending pair.
write("Sounds/EC29_Sound/errorbeep.wav",
      cat(tone(740, 0.07, harmonics=(1.0, 0.0, 0.3)), silence(0.025),
          tone(555, 0.09, harmonics=(1.0, 0.0, 0.3))), 0.75)

# Channel cycle: a soft double tick.
n = int(SR * 0.03)
tick = bandpass_noise(0.03, 800, 4000) * decay(n, 0.005)
write("Sounds/VON/EC29_FX/RadioCycle.wav", cat(tick, silence(0.05), 0.7 * tick, silence(0.02)), 0.2)

# Local speaker on: rising two-note blip.
write("Sounds/VON/EC29_FX/RadioLocalOn.wav",
      cat(tone(620, 0.06), silence(0.015), tone(880, 0.08, release=0.03)), 0.3)

# Local speaker off: one short falling blip.
write("Sounds/VON/EC29_FX/RadioLocalOff.wav", sweep(640, 470, 0.065, release=0.025), 0.25)
