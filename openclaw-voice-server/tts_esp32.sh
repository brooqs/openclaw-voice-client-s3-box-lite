#!/bin/bash
# ElevenLabs TTS Script for ESP32 (outputs MP3 directly & normalizes it)
# Usage: ./tts_esp32.sh "Text to speak" [output.mp3] [voice_id]

TEXT="$1"
FINAL_OUTPUT="${2:-output_esp32.mp3}"
# Replace with your desired ElevenLabs Voice ID
VOICE_ID="${3:-YOUR_ELEVENLABS_VOICE_ID_HERE}"
# Replace with your actual ElevenLabs API key
API_KEY="YOUR_ELEVENLABS_API_KEY_HERE"

if [ -z "$TEXT" ]; then
    echo "Usage: $0 \"text to speak\" [output.mp3] [voice_id]"
    exit 1
fi

TMP_OUTPUT="${FINAL_OUTPUT}.tmp"

# Safely escape text using jq to handle newlines, quotes, emojis etc.
JSON_PAYLOAD=$(jq -n -c --arg text "$TEXT" '{
  text: $text,
  model_id: "eleven_multilingual_v2",
  output_format: "mp3_22050_32",
  voice_settings: {
    stability: 0.5,
    similarity_boost: 0.75
  }
}')

echo "$JSON_PAYLOAD" | curl -s -X POST "https://api.elevenlabs.io/v1/text-to-speech/$VOICE_ID" \
  -H "xi-api-key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d @- \
  --output "$TMP_OUTPUT"

# Normalize: 16kHz (to match native I2S clocks), Mono, Volume Boost 1.5x
if [ -f "$TMP_OUTPUT" ]; then
    if grep -q "{\"detail\":" "$TMP_OUTPUT"; then
        echo "Error: ElevenLabs API returned an error."
        cat "$TMP_OUTPUT"
        rm -f "$TMP_OUTPUT"
        exit 1
    fi
    # Resample to exactly 16000Hz to match the native ESP32 I2S codec default avoiding pitch/speed issues
    ffmpeg -y -i "$TMP_OUTPUT" -ar 16000 -ac 1 -filter:a "volume=1.5" "$FINAL_OUTPUT" > /dev/null 2>&1
    rm -f "$TMP_OUTPUT"
    echo "Success: $FINAL_OUTPUT created."
else
    echo "Error: MP3 generation failed."
    exit 1
fi
