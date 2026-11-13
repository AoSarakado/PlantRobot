"""Local PC ASR endpoint for 16 kHz, 16-bit, mono PCM from the ESP32-S3."""

import os
import wave
from http.server import BaseHTTPRequestHandler, HTTPServer

import numpy as np
import noisereduce as nr
from faster_whisper import WhisperModel

SAMPLE_RATE = 16000
MAX_SECONDS = 10
MAX_BYTES = SAMPLE_RATE * 2 * MAX_SECONDS


def load_noise_reference(path):
    with wave.open(path, "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, SAMPLE_RATE):
            raise ValueError("noise reference must be mono 16-bit PCM at 16000 Hz")
        return np.frombuffer(wav.readframes(wav.getnframes()), dtype="<i2").astype(np.float32) / 32768.0


def make_handler(model, noise_reference=None):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/transcribe":
                self.send_error(404)
                return
            if self.headers.get("Content-Type") != "application/octet-stream":
                self.send_error(415)
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self.send_error(400)
                return
            if size < SAMPLE_RATE * 2 // 2 or size > MAX_BYTES or size % 2:
                self.send_error(413)
                return
            data = self.rfile.read(size)
            if len(data) != size:
                self.send_error(400)
                return
            debug_wav = os.getenv("PLANT_ASR_DEBUG_WAV")
            if debug_wav:
                with wave.open(debug_wav, "wb") as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(SAMPLE_RATE)
                    wav.writeframes(data)
            audio = np.frombuffer(data, dtype="<i2").astype(np.float32) / 32768.0
            peak = float(np.max(np.abs(audio)))
            rms = float(np.sqrt(np.mean(audio * audio)))
            print(f"ASR input: {len(audio) / SAMPLE_RATE:.1f}s peak={peak:.4f} rms={rms:.4f}", flush=True)
            if noise_reference is not None:
                audio = nr.reduce_noise(y=audio, sr=SAMPLE_RATE, y_noise=noise_reference,
                                        stationary=True, prop_decrease=0.9)
                clean_rms = float(np.sqrt(np.mean(audio * audio)))
                print(f"ASR denoised: rms={clean_rms:.4f}", flush=True)
            try:
                segments, _ = model.transcribe(audio, language="zh", beam_size=5,
                                                vad_filter=False,
                                                condition_on_previous_text=False)
                texts = []
                for segment in segments:
                    print(f"ASR segment: no_speech={segment.no_speech_prob:.3f} text={segment.text!r}",
                          flush=True)
                    texts.append(segment.text)
                transcript = "".join(texts).strip()
            except Exception:
                self.send_error(500)
                return
            print(f"ASR text: {transcript!r}", flush=True)
            payload = transcript.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    return Handler


if __name__ == "__main__":
    noise_path = os.getenv("PLANT_ASR_NOISE_WAV")
    noise_reference = load_noise_reference(noise_path) if noise_path else None
    model = WhisperModel(os.getenv("PLANT_ASR_MODEL", "small"), device="cpu",
                         compute_type="int8")
    host = os.getenv("PLANT_ASR_HOST", "0.0.0.0")
    port = int(os.getenv("PLANT_ASR_PORT", "8765"))
    print(f"ASR listening on {host}:{port}; noise reference={noise_path or 'disabled'}", flush=True)
    HTTPServer((host, port), make_handler(model, noise_reference)).serve_forever()
