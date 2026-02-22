#!/bin/bash
# ElevenLabs STT Script
# Usage: ./stt_eleven.sh <input_audio.raw>

INPUT_AUDIO="$1"
# Replace with your actual ElevenLabs API key
API_KEY="YOUR_ELEVENLABS_API_KEY_HERE"

if [ -z "$INPUT_AUDIO" ]; then
    echo "Usage: $0 <input_audio.raw>"
    exit 1
fi

WAV_OUTPUT="${INPUT_AUDIO}.wav"
# The ESP32 specifically captures 16-bit Signed Little-Endian (s16le) PCM at 16000Hz on 1 Mono Channel
ffmpeg -y -f s16le -ar 16000 -ac 1 -i "$INPUT_AUDIO" "$WAV_OUTPUT" > /dev/null 2>&1

curl -s -X POST "https://api.elevenlabs.io/v1/speech-to-text" \
  -H "xi-api-key: $API_KEY" \
  -F "file=@$WAV_OUTPUT" \
  | jq -r '.text'

rm -f "$WAV_OUTPUT"
