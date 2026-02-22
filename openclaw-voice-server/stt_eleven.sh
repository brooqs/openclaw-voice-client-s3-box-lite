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

curl -s -X POST "https://api.elevenlabs.io/v1/speech-to-text" \
  -H "xi-api-key: $API_KEY" \
  -F "file=@$INPUT_AUDIO" \
  | jq -r '.text'
