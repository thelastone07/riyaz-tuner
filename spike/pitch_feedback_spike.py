"""
M0 validation spike — throwaway prototype, not part of the real app.

Purpose: answer ONE question before any JUCE/ONNX Runtime investment happens —
is live numeric/visual cents-from-Sa feedback actually useful while singing?

Not the real pitch engine (that's CREPE via onnxcrepe, decided separately).
This uses a small hand-rolled YIN detector purely so the spike has zero heavy
dependencies and starts in seconds.

Usage:
    python spike/pitch_feedback_spike.py

Flow:
    1. Calibrates Sa: sing a steady note for 3 seconds when prompted.
    2. Then shows a live terminal meter of your pitch relative to that Sa.
    3. Ctrl+C to stop.

Judge: does watching this number/bar while singing meend/gamak actually help
you correct pitch, or is it distracting / not granular enough to matter?
"""

import queue
import sys

import numpy as np
import sounddevice as sd

SAMPLE_RATE = 44100
BLOCK_SIZE = 2048  # ~46ms per block, enough periods for voice down to ~70Hz
FMIN, FMAX = 70.0, 1000.0
YIN_THRESHOLD = 0.15

SWAR_NAMES = ["S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N"]
OCTAVE_NAMES = {-1: "mandra", 0: "madhya", 1: "taar"}


def yin_pitch(signal: np.ndarray, sample_rate: int) -> tuple[float | None, float]:
    """Minimal YIN pitch detector. Returns (frequency_hz or None, aperiodicity)."""
    tau_min = int(sample_rate / FMAX)
    tau_max = min(int(sample_rate / FMIN), len(signal) // 2)

    diff = np.zeros(tau_max)
    for tau in range(tau_min, tau_max):
        d = signal[: len(signal) - tau] - signal[tau : tau + (len(signal) - tau)]
        diff[tau] = np.sum(d * d)

    cmnd = np.ones(tau_max)
    running_sum = 0.0
    for tau in range(1, tau_max):
        running_sum += diff[tau]
        cmnd[tau] = diff[tau] * tau / running_sum if running_sum > 0 else 1.0

    tau_est = None
    for tau in range(tau_min, tau_max):
        if cmnd[tau] < YIN_THRESHOLD:
            # refine: walk to local minimum
            while tau + 1 < tau_max and cmnd[tau + 1] < cmnd[tau]:
                tau += 1
            tau_est = tau
            break

    if tau_est is None:
        return None, float(np.min(cmnd[tau_min:tau_max])) if tau_max > tau_min else 1.0

    return sample_rate / tau_est, float(cmnd[tau_est])


def cents_from_sa(freq_hz: float, sa_hz: float) -> float:
    return 1200.0 * np.log2(freq_hz / sa_hz)


def swar_label(cents: float) -> tuple[str, str, float]:
    """Nearest-swar quantization, no hysteresis — fine for a throwaway spike."""
    nearest = round(cents / 100.0)
    octave = nearest // 12
    semitone_index = nearest % 12
    deviation = cents - nearest * 100.0
    octave_name = OCTAVE_NAMES.get(octave, f"oct{octave:+d}")
    return SWAR_NAMES[semitone_index], octave_name, deviation


def meter_bar(deviation_cents: float, width: int = 21) -> str:
    clipped = max(-50.0, min(50.0, deviation_cents))
    pos = int(round((clipped + 50.0) / 100.0 * (width - 1)))
    center = width // 2
    chars = ["-"] * width
    chars[center] = "|"
    chars[pos] = "*"
    return "".join(chars)


def calibrate_sa(max_attempts: int = 5) -> float:
    default_input = sd.query_devices(sd.default.device[0])
    print(f"\nRecording from: {default_input['name']}")

    for attempt in range(1, max_attempts + 1):
        print(f"\nCalibrating Sa (attempt {attempt}/{max_attempts}) — "
              f"sing a steady, comfortable note for 3 seconds...")
        print("(starting in 1 second)")
        sd.sleep(1000)
        recording = sd.rec(
            int(3 * SAMPLE_RATE), samplerate=SAMPLE_RATE, channels=1, dtype="float32"
        )
        sd.wait()
        samples = recording[:, 0]

        peak_amplitude = float(np.max(np.abs(samples)))
        rms = float(np.sqrt(np.mean(samples**2)))

        detections = []
        best_aperiodicity = 1.0
        for start in range(0, len(samples) - BLOCK_SIZE, BLOCK_SIZE):
            block = samples[start : start + BLOCK_SIZE]
            freq, aperiodicity = yin_pitch(block, SAMPLE_RATE)
            best_aperiodicity = min(best_aperiodicity, aperiodicity)
            if freq is not None and aperiodicity < YIN_THRESHOLD:
                detections.append(freq)

        print(f"  peak amplitude={peak_amplitude:.4f}  rms={rms:.4f}  "
              f"best aperiodicity={best_aperiodicity:.3f} (need < {YIN_THRESHOLD})")

        if detections:
            sa_hz = float(np.median(detections))
            print(f"Sa calibrated: {sa_hz:.1f} Hz (from {len(detections)} confident frames)\n")
            return sa_hz

        if peak_amplitude < 0.01:
            print("  -> Signal is near-silent. Mic may be muted, wrong device, "
                  "or input volume too low in Windows sound settings.")
        else:
            print("  -> Signal detected but not periodic enough (best aperiodicity "
                  f"{best_aperiodicity:.3f} never dropped below {YIN_THRESHOLD}). "
                  "Try singing louder/steadier, or closer to the mic.")

    raise RuntimeError(
        f"Couldn't calibrate Sa after {max_attempts} attempts. "
        "Check the diagnostics above — if peak amplitude stayed near 0, "
        "the wrong input device is likely selected (see 'Recording from:' above)."
    )


def live_feedback_loop(sa_hz: float) -> None:
    audio_queue: queue.Queue[np.ndarray] = queue.Queue()

    def callback(indata, frames, time_info, status):
        if status:
            print(status, file=sys.stderr)
        audio_queue.put(indata[:, 0].copy())

    print("Live feedback running. Sing against your Sa. Ctrl+C to stop.\n")

    with sd.InputStream(
        samplerate=SAMPLE_RATE,
        channels=1,
        blocksize=BLOCK_SIZE,
        dtype="float32",
        callback=callback,
    ):
        try:
            while True:
                block = audio_queue.get()
                freq, aperiodicity = yin_pitch(block, SAMPLE_RATE)
                if freq is None or aperiodicity >= YIN_THRESHOLD:
                    print(f"\r{'(silence / unvoiced)':60s}", end="", flush=True)
                    continue

                cents = cents_from_sa(freq, sa_hz)
                swar, octave, deviation = swar_label(cents)
                bar = meter_bar(deviation)
                line = (
                    f"\rfreq={freq:6.1f}Hz  swar={swar:>2s} ({octave:6s})  "
                    f"dev={deviation:+6.1f}c  [{bar}]"
                )
                print(line, end="", flush=True)
        except KeyboardInterrupt:
            print("\n\nStopped.")


if __name__ == "__main__":
    sa = calibrate_sa()
    live_feedback_loop(sa)
