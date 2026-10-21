"""Synthesizes the demo's soundtrack from scratch: no samples, no licensing.

Every musical and sound-effect cue is placed from ../timeline.json, the same
file the video reads, so picture and sound line up to the frame.

    python scripts/make-audio.py      ->  public/soundtrack.wav (48 kHz stereo, 16-bit)

Palette: four-on-the-floor kick at 120 BPM, a sidechained sub bass on an
Am-F-C-G progression, a dark pad, noise risers into every drop, sub-boom
impacts, inharmonic anvil clangs for the four optimization passes, and small
UI sounds (key clicks, token ticks, glitches, pings) for the on-screen action.
"""

import json
import pathlib
import wave

import numpy as np
from scipy.signal import butter, fftconvolve, sosfilt

ROOT = pathlib.Path(__file__).resolve().parent.parent
T = json.loads((ROOT / "timeline.json").read_text())
SR = 48000
FPS = T["fps"]
N = int(T["total"] / FPS * SR) + SR  # one extra second for reverb tails
rng = np.random.default_rng(7)

dry = np.zeros((N, 2))
verb = np.zeros((N, 2))  # reverb send bus


def at(frame):
    return int(frame / FPS * SR)


def secs(s):
    return int(s * SR)


def t_axis(dur):
    return np.arange(secs(dur)) / SR


def env(dur, attack=0.002, decay=0.2):
    t = t_axis(dur)
    a = np.clip(t / max(attack, 1e-6), 0, 1)
    return a * np.exp(-t / decay)


def place(sig, frame, gain=1.0, pan=0.0, send=0.0, offset_s=0.0):
    """Mix a mono signal at a frame (equal-power pan, optional reverb send)."""
    start = at(frame) + secs(offset_s)
    if start >= N:
        return
    sig = sig[: N - start] * gain
    left, right = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    dry[start : start + len(sig), 0] += sig * left
    dry[start : start + len(sig), 1] += sig * right
    if send:
        verb[start : start + len(sig), 0] += sig * left * send
        verb[start : start + len(sig), 1] += sig * right * send


def filt(sig, kind, freq, order=2):
    sos = butter(order, freq, btype=kind, fs=SR, output="sos")
    return sosfilt(sos, sig)


def layer(*parts):
    """Sum signals of different lengths (zero-padded to the longest)."""
    out = np.zeros(max(len(p) for p in parts))
    for p in parts:
        out[: len(p)] += p
    return out


def noise(dur):
    return rng.uniform(-1, 1, secs(dur))


def sweep_sine(f0, f1, dur, curve=3.0):
    t = t_axis(dur)
    f = f1 + (f0 - f1) * np.exp(-t * curve / dur * 4)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


# ------------------------------------------------------------------ sounds

def kick():
    body = sweep_sine(160, 42, 0.45, curve=6) * env(0.45, 0.001, 0.16)
    click = filt(noise(0.01), "highpass", 3000) * env(0.01, 0.0005, 0.003)
    return layer(np.tanh(1.6 * body), 0.35 * click)


def hat():
    return filt(noise(0.06), "highpass", 7000, 4) * env(0.06, 0.0005, 0.018)


def snare():
    tone = np.sin(2 * np.pi * 190 * t_axis(0.25)) * env(0.25, 0.001, 0.06)
    body = filt(noise(0.25), "bandpass", [900, 6000]) * env(0.25, 0.001, 0.09)
    return layer(0.5 * tone, body)


def impact():
    boom = sweep_sine(70, 28, 2.4, curve=1.5) * env(2.4, 0.003, 0.9)
    crack = filt(noise(0.6), "lowpass", 2500) * env(0.6, 0.001, 0.12)
    return layer(np.tanh(1.4 * boom), 0.8 * crack)


def clang(base=380.0):
    """An anvil: inharmonic partials, a hard transient and a sub thump."""
    t = t_axis(2.0)
    partials = [(1.0, 1.0, 0.9), (2.76, 0.6, 0.5), (5.40, 0.45, 0.28), (8.93, 0.3, 0.16), (13.3, 0.2, 0.09)]
    ring = sum(a * np.sin(2 * np.pi * base * r * t + r) * np.exp(-t / d) for r, a, d in partials)
    hit = filt(noise(2.0), "highpass", 2500) * np.exp(-t / 0.02)
    thump = sweep_sine(120, 40, 2.0, curve=5) * np.exp(-t / 0.18)
    return 0.55 * ring + 0.7 * hit + 0.9 * thump


def riser(dur):
    """Noise through a rising band, plus a rising saw, swelling into a drop."""
    n = secs(dur)
    t = np.arange(n) / SR
    x = noise(dur)
    out = np.zeros(n)
    chunk = 1024
    for i in range(0, n, chunk):  # a crude time-varying bandpass, chunk by chunk
        frac = i / n
        lo = 300 + 5000 * frac**2
        seg = filt(x[i : i + chunk + 2048], "bandpass", [lo, lo * 2.5])[: min(chunk, n - i)]
        out[i : i + len(seg)] = seg
    f = 110 * 2 ** (3 * t / dur)
    saw = 2 * ((np.cumsum(f) / SR) % 1.0) - 1
    swell = (t / dur) ** 2.2
    return (out * 1.4 + 0.18 * filt(saw, "lowpass", 3000)) * swell


def whoosh(dur=0.6):
    n = secs(dur)
    t = np.arange(n) / SR
    shape = np.sin(np.pi * t / dur) ** 2
    return filt(noise(dur), "bandpass", [500, 3500]) * shape


def key_click():
    f = rng.uniform(2500, 4500)
    return filt(noise(0.012), "bandpass", [f * 0.7, f]) * env(0.012, 0.0003, 0.003)


def tick():
    f = rng.uniform(1700, 2900)
    return np.sin(2 * np.pi * f * t_axis(0.03)) * env(0.03, 0.0005, 0.008)


def ding():
    t = t_axis(1.6)
    return (np.sin(2 * np.pi * 1318.5 * t) + 0.5 * np.sin(2 * np.pi * 2637 * t) + 0.2 * np.sin(2 * np.pi * 3951 * t)) * np.exp(-t / 0.5)


def glitch():
    n = secs(0.18)
    hold = rng.integers(40, 180)
    steps = rng.uniform(-1, 1, n // hold + 1).repeat(hold)[:n]
    sq = np.sign(np.sin(2 * np.pi * rng.uniform(80, 400) * np.arange(n) / SR))
    crushed = np.round((0.6 * steps + 0.4 * sq) * 6) / 6  # bit crush
    return crushed * env(0.18, 0.001, 0.07)


def ping(f=880.0):
    t = t_axis(0.35)
    return np.sin(2 * np.pi * f * t) * np.exp(-t / 0.09)


def sparkle():
    out = np.zeros(secs(1.5))
    for k in range(14):
        b = np.sin(2 * np.pi * rng.uniform(2500, 6500) * t_axis(0.2)) * env(0.2, 0.001, 0.04)
        s = int(k * 0.07 * SR)
        out[s : s + len(b)] += b * (1 - k / 16)
    return out


# ------------------------------------------------------------------- music

beat, bar, total = T["beat"], T["bar"], T["total"]
breaks = T["kickBreaks"]
in_break = lambda f: any(a <= f < b for a, b in breaks)
music_start, music_end = T["drops"][0], T["scenes"]["outro"][0]

kicks = [f for f in range(music_start, music_end, beat) if not in_break(f)]
for f in kicks:
    place(kick(), f, gain=0.95)
for f in range(T["scenes"]["lex"][0], music_end, beat):
    if not in_break(f):
        place(hat(), f + beat // 2, gain=0.22, pan=0.35)
for f in range(T["scenes"]["forge"][0], music_end, bar):  # backbeat snare on 2 and 4 from the forge on
    for k in (1, 3):
        g = f + k * beat
        if not in_break(g) and g < music_end:
            place(snare(), g, gain=0.32, pan=-0.1, send=0.25)

# Sub bass and pad: Am - F - C - G, one chord per bar, ducked by the kick.
ROOTS = [55.00, 43.65, 65.41, 49.00]
CHORDS = [[220.0, 261.63, 329.63], [174.61, 220.0, 261.63], [196.0, 261.63, 329.63], [196.0, 246.94, 293.66]]
duck = np.ones(N)
for f in kicks:
    s = at(f)
    d = secs(0.22)
    duck[s : s + d] = np.minimum(duck[s : s + d], 1 - 0.85 * np.exp(-np.arange(d) / (0.06 * SR)))
bass = np.zeros(N)
pad = np.zeros(N)
for b, f in enumerate(range(music_start, music_end, bar)):
    s, n = at(f), secs(bar / FPS)
    t = np.arange(n) / SR
    root = ROOTS[b % 4]
    bass[s : s + n] += np.tanh(2.2 * np.sin(2 * np.pi * root * t)) * 0.5
    chord = sum(np.sin(2 * np.pi * hz * t + i) + 0.4 * np.sin(2 * np.pi * hz * 2.003 * t) for i, hz in enumerate(CHORDS[b % 4]))
    fade = np.minimum(1, np.minimum(t / 0.3, (t[-1] - t + 1e-3) / 0.3))
    pad[s : s + n] += chord * fade
bass = filt(bass, "lowpass", 180) * duck
pad = filt(pad, "lowpass", 1400) * (0.35 + 0.65 * duck)
for ch in (0, 1):
    dry[:, ch] += 0.55 * bass + 0.05 * pad
    verb[:, ch] += 0.06 * pad

# Intro drone (before the first drop) so the cold open isn't silent.
drone_n = at(music_start)
tt = np.arange(drone_n) / SR
drone = (np.sin(2 * np.pi * 55 * tt) + 0.5 * np.sin(2 * np.pi * 82.4 * tt)) * np.minimum(1, tt / 1.5) * 0.25
dry[:drone_n, 0] += drone
dry[:drone_n, 1] += drone

# --------------------------------------------------------------------- SFX

for d in T["drops"]:
    place(riser(bar * 2 / FPS if d != T["drops"][0] else 3.6), d, gain=0.55, send=0.3,
          offset_s=-(bar * 2 / FPS if d != T["drops"][0] else 3.6))
    place(impact(), d, gain=1.0, send=0.5)

ty = T["typing"]
for k in range(len(ty["text"])):
    place(key_click(), ty["start"] + (ty["end"] - ty["start"]) * k / len(ty["text"]), gain=0.55, pan=rng.uniform(-0.3, 0.3))

tk = T["tokens"]
for k in range(36):
    place(tick(), tk["start"] + (tk["end"] - tk["start"]) * k / 36, gain=0.25, pan=rng.uniform(-0.7, 0.7))
place(impact()[: secs(0.8)] * np.linspace(1, 0, secs(0.8)), T["astSnap"], gain=0.5, send=0.3)

for f in T["whooshes"]:
    place(whoosh(), f, gain=0.6, pan=-0.4, offset_s=-0.3)
place(whoosh(1.4), T["cfgDraw"][0], gain=0.35, pan=0.3)
place(ding(), T["phiPop"], gain=0.4, send=0.6, pan=0.2)

for i, f in enumerate(T["hammers"]):
    place(clang(380 * (1 + 0.06 * i)), f, gain=0.85, send=0.45, pan=(-0.2, 0.2, -0.1, 0.1)[i])
place(whoosh(0.9)[::-1], T["collapse"], gain=0.5)

for f in T["glitches"]:
    place(glitch(), f, gain=0.6, pan=rng.uniform(-0.5, 0.5))
place(snare(), T["ptxFreeze"], gain=0.4, send=0.4)

g0, g1 = T["gridWave"]
wave_s = (g1 - g0) / FPS
place(riser(wave_s) * 0.7, g0, gain=0.45, send=0.2)
for k in range(24):
    place(tick(), g0 + (g1 - g0) * k / 24, gain=0.18, pan=-0.8 + 1.6 * k / 24)
place(impact(), T["passStamp"], gain=1.0, send=0.5)
for i, f in enumerate(T["terminal"]):
    place(ping(660 * 2 ** (i / 12 * 3)), f, gain=0.35, send=0.3)

for i, f in enumerate(T["statHits"]):
    place(snare(), f, gain=0.6, send=0.35, pan=(-0.3, 0.3)[i % 2])
    place(kick(), f, gain=0.5)

i0, i1 = T["implode"]
place(riser((i1 - i0) / FPS)[::-1][::-1], i0, gain=0.6, send=0.3)
place(sparkle(), T["finalSparkle"], gain=0.4, send=0.7)

# ------------------------------------------------------------------- master

ir_t = np.arange(secs(2.2)) / SR
ir = rng.standard_normal((len(ir_t), 2)) * np.exp(-ir_t / 0.55)[:, None]
ir[:, 0] = filt(ir[:, 0], "lowpass", 6000)
ir[:, 1] = filt(ir[:, 1], "lowpass", 6000)
wet = np.stack([fftconvolve(verb[:, c], ir[:, c])[:N] for c in (0, 1)], axis=1) * 0.12
mix = dry + wet

end = at(total)
fade = secs(2.0)  # fade out over the last two seconds of the film
mix[end - fade : end] *= np.linspace(1, 0, fade)[:, None] ** 1.5
mix = mix[:end]
mix = np.tanh(1.2 * mix / (np.max(np.abs(mix)) + 1e-9)) / np.tanh(1.2)  # gentle soft clip
mix *= 10 ** (-1 / 20)  # -1 dBFS peak

out = ROOT / "public" / "soundtrack.wav"
out.parent.mkdir(exist_ok=True)
with wave.open(str(out), "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix * 32767).astype("<i2").tobytes())
print(f"wrote {out.relative_to(ROOT)}: {len(mix) / SR:.2f}s, {len(kicks)} kicks, peak -1 dBFS")
