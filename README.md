# ESP32-S3-BOX-Lite OpenClaw Client

This repository provides a lightweight, standalone ESP-IDF firmware client designed to connect the **ESP32-S3-BOX-Lite** to an **OpenClaw (Jarvis)** voice assistant bridge over Wi-Fi. It streams local microphone audio synchronously to the bridge and directly decodes incoming MP3 responses back to the device's internal speaker using the I2S hardware pipeline.

## 🌟 Features

- **Push-to-Talk (PTT)**: Uses the integrated push button on the ESP-BOX-Lite to trigger audio recording.
- **Microphone Integration**: High-fidelity Voice Activity streaming directly from the ES7243 ADC.
- **On-board MP3 Decoding**: Real-time MP3 decoding of incoming OpenClaw voice payloads directly to the ES8156 DAC speaker.
- **Dynamic LCD Avatar**: Displays a visual indicator / avatar on the device's SPI LCD screen when the client is active.
- **Home Assistant Media Player (REST)**: Acts as an endpoint to receive `api/play` external HTTP URLs for unified smart home notifications.
- **Safe OTA Support**: Memory partition optimizations for safe PSRAM and Flaş interactions using an ESP32-S3 Octal layout.

## 🛠️ Prerequisites

This firmware requires the official Espressif IoT Development Framework.

1.  **ESP-IDF Version**: `v5.1.2` is required natively (Due to ESP-DSP audio decoder compatibility).
2.  **Hardware**: ESP32-S3-BOX-Lite.

## 🚀 Setup & Configuration

1. Clone or download this directory directly into your workspace.
2. Initialize and activate your ESP-IDF environment in your terminal:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```
3. Set your target chipset:
   ```bash
   idf.py set-target esp32s3
   ```
4. Open the environment configuration menu:
   ```bash
   idf.py menuconfig
   ```
5. Navigate to **OpenClaw Client Configuration**:
   - Set your **WiFi SSID**.
   - Set your **WiFi Password**.
   - Specify the **OpenClaw Voice Endpoint URL** (e.g., `http://192.168.1.100:18790/voice`).
6. Exit and save the configuration.

## 🔌 Setting up the Node.js OpenClaw Bridge

The ESP32 client requires a Node.js intermediary server to handle chunked HTTP bridging and audio format conversions efficiently before piping payloads to the actual OpenClaw LLM inference engine. This bundled server is located in the `openclaw-voice-server/` directory.

### Bridge Prerequisites
*   Node.js (`v18` or higher)
*   `ffmpeg` installed on the host OS for real-time audio down-sampling (ensuring 16kHz optimizations).
*   `jq` installed for JSON manipulations.

### Bridge Setup

1.  Navigate into the server directory:
    ```bash
    cd openclaw-voice-server/
    ```
2.  Install dependencies:
    ```bash
    npm install
    ```
3.  **Configure API Keys**: Edit the bundled bash scripts (`stt_eleven.sh` and `tts_esp32.sh`) and insert your real ElevenLabs API keys where `YOUR_ELEVENLABS_API_KEY_HERE` placeholders are marked.
4.  Launch the Relay Bridge:
    ```bash
    npm start
    ```
    *The bridge will now actively listen on port `18790` and await payloads from the ESP32.*

## ⚡ Building and Flashing

Build the firmware, flash it over USB, and open the serial monitor to view exactly what your box is doing!

```bash
idf.py build
sudo chmod a+rw /dev/ttyACM0
idf.py -p /dev/ttyACM0 flash monitor
```

## 📝 License

This specific integration project (OpenClaw Client) is released under the **MIT License**. See the `LICENSE` file for details.

> **Note:** The underlying dependencies and hardware components (such as `esp-bsp` and Espressif's ESP-IDF submodules) are subject to their own respective licenses (typically Apache 2.0). Ensure you comply with those licenses if distributing the compiled firmware.
