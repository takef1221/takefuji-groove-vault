#!/usr/bin/env python3
"""
normalize_wav.py  --  RMS-normalize WAV files to -18 dBFS

Usage:
    python normalize_wav.py <folder_path>

Output goes to <folder_path>\\normalized\\  (original files are untouched).
Already-normalized files are skipped so only new additions are processed on
subsequent runs.
"""

import sys
import os
import numpy as np
from scipy.io import wavfile

TARGET_RMS_DBFS = -18.0


def rms_dbfs(data: np.ndarray) -> float:
    rms = np.sqrt(np.mean(data ** 2))
    if rms == 0.0:
        return -np.inf
    return 20.0 * np.log10(rms)


def to_float64(data: np.ndarray) -> np.ndarray:
    if data.dtype == np.int16:
        return data.astype(np.float64) / 32768.0
    if data.dtype == np.int32:
        return data.astype(np.float64) / 2147483648.0
    if data.dtype == np.uint8:
        return (data.astype(np.float64) - 128.0) / 128.0
    if data.dtype in (np.float32, np.float64):
        return data.astype(np.float64)
    return None


def from_float64(data: np.ndarray, dtype) -> np.ndarray:
    if dtype == np.int16:
        return np.clip(data * 32767.0, -32768, 32767).astype(np.int16)
    if dtype == np.int32:
        return np.clip(data * 2147483647.0, -2147483648, 2147483647).astype(np.int32)
    if dtype == np.uint8:
        return np.clip(data * 128.0 + 128.0, 0, 255).astype(np.uint8)
    if dtype in (np.float32, np.float64):
        return data.astype(dtype)
    return None


def normalize_file(src_path: str, dst_path: str) -> str:
    filename = os.path.basename(src_path)

    rate, raw = wavfile.read(src_path)
    float_data = to_float64(raw)

    if float_data is None:
        return f"{filename}  [SKIPPED - unsupported dtype: {raw.dtype}]"

    before_db = rms_dbfs(float_data)
    if np.isinf(before_db):
        return f"{filename}  [SKIPPED - silent file]"

    gain_db = TARGET_RMS_DBFS - before_db
    normalized = float_data * (10.0 ** (gain_db / 20.0))

    # Clipping protection: if peak exceeds 0 dBFS, pull gain back
    peak = np.max(np.abs(normalized))
    clamp_applied = peak > 1.0
    if clamp_applied:
        normalized = normalized / peak

    after_db = rms_dbfs(normalized)
    out_data = from_float64(normalized, raw.dtype)
    wavfile.write(dst_path, rate, out_data)

    sign = "+" if gain_db >= 0 else ""
    status = "[PEAK CLAMP applied]" if clamp_applied else "[OK]"
    return (
        f"{filename}  "
        f"RMS before: {before_db:6.1f} dBFS  ->  "
        f"after: {after_db:5.1f} dBFS  "
        f"gain: {sign}{gain_db:.1f} dB  "
        f"{status}"
    )


def main():
    if len(sys.argv) < 2:
        print("Usage: python normalize_wav.py <folder_path>")
        sys.exit(1)

    folder = sys.argv[1]
    if not os.path.isdir(folder):
        print(f"Error: '{folder}' is not a valid directory.")
        sys.exit(1)

    out_dir = os.path.join(folder, "normalized")
    os.makedirs(out_dir, exist_ok=True)

    wav_files = sorted(
        f for f in os.listdir(folder)
        if f.lower().endswith(".wav") and os.path.isfile(os.path.join(folder, f))
    )

    if not wav_files:
        print("No WAV files found.")
        return

    processed = skipped = 0
    total = len(wav_files)

    for filename in wav_files:
        src = os.path.join(folder, filename)
        dst = os.path.join(out_dir, filename)

        if os.path.exists(dst):
            print(f"{filename}  [SKIPPED - already exists]")
            skipped += 1
            continue

        try:
            print(normalize_file(src, dst))
            processed += 1
        except Exception as exc:
            print(f"{filename}  [ERROR: {exc}]")
            processed += 1

    print()
    print(f"Done.  Processed: {processed}  /  Skipped: {skipped}  /  Total: {total}")
    print(f"Output: {out_dir}")


if __name__ == "__main__":
    main()
