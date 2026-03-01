from birdnetlib import Recording
from birdnetlib.analyzer import Analyzer
from datetime import datetime
import tempfile
import os
import wave

# Load and initialize the BirdNET-Analyzer models.
analyzer = Analyzer()

SAMPLE_RATE = 8000
CHANNELS = 1
SAMPLE_WIDTH_BYTES = 2

def analyze_recording(binary_audio: bytes):
    # Parse raw PCM16LE audio into a valid .wav file and save it to a temporary file.
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as temp_audio_file:
        with wave.open(temp_audio_file, "wb") as wav_file:
            wav_file.setnchannels(CHANNELS)
            wav_file.setsampwidth(SAMPLE_WIDTH_BYTES)
            wav_file.setframerate(SAMPLE_RATE)
            wav_file.writeframes(binary_audio)
        temp_audio_file.flush()
    
        recording = Recording(
            analyzer,
            temp_audio_file.name,
            lat=35.4244,
            lon=-120.7463,
            date=datetime(year=2022, month=5, day=10), # use date or week_48
            min_conf=0.25,
        )

        recording.analyze()
    
    # Clean up the temporary file
    os.remove(temp_audio_file.name)

    return recording.detections