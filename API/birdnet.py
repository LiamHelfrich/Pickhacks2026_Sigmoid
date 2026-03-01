from birdnetlib import Recording
from birdnetlib.analyzer import Analyzer
from datetime import datetime
import tempfile
import os
import wave
from pydub import AudioSegment
from collections import deque

# Load and initialize the BirdNET-Analyzer models.
analyzer = Analyzer()

SAMPLE_RATE = 32000
CHANNELS = 1
SAMPLE_WIDTH_BYTES = 2
BUFFER_SIZE = 1

# Rolling buffer of raw PCM payloads
_audio_buffer: deque[bytes] = deque(maxlen=BUFFER_SIZE)

# Stored results from the last inference run
last_detections: list = []
last_mp3_bytes: bytes = b""


def analyze_recording(binary_audio: bytes) -> tuple[list, bytes]:
    global last_detections, last_mp3_bytes

    _audio_buffer.append(binary_audio)

    # Concatenate all buffered payloads
    combined_audio = b"".join(_audio_buffer)

    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_audio_file:
        wav_path = temp_audio_file.name
        with wave.open(temp_audio_file, "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH_BYTES)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(combined_audio)
        temp_audio_file.flush()

        recording = Recording(
            analyzer,
            wav_path,
            lat=35.4244,
            lon=-120.7463,
            date=datetime(year=2022, month=5, day=10),
            min_conf=0.25,
        )
        recording.analyze()

    # Convert WAV to MP3
    mp3_path = wav_path.replace(".wav", ".mp3")
    try:
        audio = AudioSegment.from_wav(wav_path)
        audio.export(mp3_path, format="mp3")
        with open(mp3_path, "rb") as mp3_file:
            mp3_bytes = mp3_file.read()
    finally:
        os.remove(wav_path)
        if os.path.exists(mp3_path):
            os.remove(mp3_path)

    # Store results
    last_detections = recording.detections
    last_mp3_bytes = mp3_bytes

    return last_detections, last_mp3_bytes