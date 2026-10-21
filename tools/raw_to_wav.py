# What SDL's disk audio driver wrote (the format src/main.cpp's reset_audio asks for: 32-bit float stereo at 48 kHz) as a
# 16-bit WAV, for tools/run_linux.sh and tools/play_linux.sh.
#
#     python3 -I tools/raw_to_wav.py in.raw out.wav
import array
import sys
import wave

samples = array.array("f", open(sys.argv[1], "rb").read())
# A NaN, or anything past full scale, is clipped rather than wrapped, and counted: the program should write neither.
bad = sum(1 for x in samples if not -1.0 <= x <= 1.0)
pcm = array.array("h", (max(-32768, min(32767, int(x * 32767))) if x == x else 0 for x in samples))
with wave.open(sys.argv[2], "wb") as out:
    out.setnchannels(2)
    out.setsampwidth(2)
    out.setframerate(48000)
    out.writeframes(pcm.tobytes())
print(f"{sys.argv[2]}: {len(samples) / 96000:.1f} s" + (f", {bad} samples out of range" if bad else ""))
