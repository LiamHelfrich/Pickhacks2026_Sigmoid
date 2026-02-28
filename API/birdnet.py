from birdnetlib import Recording
from birdnetlib.analyzer import Analyzer
from datetime import datetime
import tempfile
import os
import wave
from pydub import AudioSegment

# Load and initialize the BirdNET-Analyzer models.
analyzer = Analyzer()

SAMPLE_RATE = 8000
CHANNELS = 1
SAMPLE_WIDTH_BYTES = 2

def analyze_recording(binary_audio: bytes) -> tuple[list, bytes]:
    # Parse raw PCM16LE audio into a valid .wav file and save it to a temporary file.
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_audio_file:
        wav_path = temp_audio_file.name
        with wave.open(temp_audio_file, "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH_BYTES)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(binary_audio)
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

    return recording.detections, mp3_bytes