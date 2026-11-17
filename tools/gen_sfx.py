"""生成占位音效（WAV），输出到 client/godot/assets/sfx/。

纯标准库合成，不依赖任何素材：方便你之后用真实音效逐条替换（文件名固定）。
用法：python tools/gen_sfx.py
"""
import math
import random
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "client" / "godot" / "assets" / "sfx"
RATE = 44100
random.seed(7)


def envelope(i: int, total: int, attack: float = 0.01, release: float = 0.25) -> float:
    t = i / total
    if t < attack:
        return t / attack
    if t > 1.0 - release:
        return max(0.0, (1.0 - t) / release)
    return 1.0


def write(name: str, samples: list[float]) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    data = bytearray()
    for s in samples:
        value = int(max(-1.0, min(1.0, s / peak * 0.85)) * 32767)
        data += struct.pack("<h", value)
    path = OUT / f"{name}.wav"
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(bytes(data))
    print(f"  {path.relative_to(ROOT)}  {len(samples) / RATE:.2f}s")


def tone(freq: float, seconds: float, kind: str = "sine", decay: float = 6.0) -> list[float]:
    n = int(RATE * seconds)
    out = []
    for i in range(n):
        t = i / RATE
        env = math.exp(-decay * t)
        if kind == "square":
            wave_v = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
        elif kind == "saw":
            wave_v = 2.0 * ((t * freq) % 1.0) - 1.0
        else:
            wave_v = math.sin(2 * math.pi * freq * t)
        out.append(wave_v * env)
    return out


def noise(seconds: float, decay: float = 12.0, lowpass: float = 0.5) -> list[float]:
    n = int(RATE * seconds)
    out = []
    prev = 0.0
    for i in range(n):
        t = i / RATE
        value = random.uniform(-1.0, 1.0)
        prev = prev * lowpass + value * (1.0 - lowpass)
        out.append(prev * math.exp(-decay * t))
    return out


def amp(track: list[float], gain: float) -> list[float]:
    return [v * gain for v in track]


def mix(*tracks: list[float]) -> list[float]:
    length = max(len(t) for t in tracks)
    out = [0.0] * length
    for track in tracks:
        for i, v in enumerate(track):
            out[i] += v
    return out


def seq(*tracks: list[float]) -> list[float]:
    out: list[float] = []
    for track in tracks:
        out.extend(track)
    return out


def main() -> None:
    print("生成占位音效：")
    write("click", amp(tone(880, 0.05, "square", 40), 0.5))
    write("place", amp(mix(tone(220, 0.18, "sine", 18), noise(0.10, 30, 0.7)), 0.7))
    write("build_done", amp(seq(tone(523, 0.10, "sine", 12), tone(784, 0.18, "sine", 10)), 0.6))
    write("hit", amp(mix(noise(0.06, 40, 0.4), tone(160, 0.05, "saw", 40)), 0.6))
    write("explosion", amp(mix(noise(0.55, 6.0, 0.85), tone(70, 0.55, "sine", 5.0)), 0.9))
    write("upgrade", amp(seq(tone(659, 0.08, "sine", 14), tone(880, 0.08, "sine", 14),
                             tone(1175, 0.16, "sine", 10)), 0.55))
    write("victory", amp(seq(tone(523, 0.14, "sine", 8), tone(659, 0.14, "sine", 8),
                             tone(784, 0.14, "sine", 8), tone(1047, 0.34, "sine", 5)), 0.6))
    write("defeat", amp(seq(tone(392, 0.18, "sine", 8), tone(330, 0.18, "sine", 8),
                            tone(262, 0.40, "sine", 4)), 0.6))
    print("完成。")


if __name__ == "__main__":
    main()
