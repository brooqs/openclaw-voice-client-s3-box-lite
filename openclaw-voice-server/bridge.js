const express = require('express');
const multer = require('multer');
const { execSync, execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const app = express();
const uploadDir = '/tmp/jarvis-uploads/';
const saveDir = '/root/.openclaw/workspace/saved_audio/';
fs.mkdirSync(saveDir, { recursive: true });

const upload = multer({ dest: uploadDir });

const STT_SCRIPT = path.join(__dirname, 'stt_eleven.sh');
const TTS_SCRIPT = path.join(__dirname, 'tts_esp32.sh');

app.post('/voice', upload.single('audio'), async (req, res) => {
    try {
        const audioPath = req.file.path;
        console.log(`[Bridge] Received audio: ${audioPath}`);

        // Save a copy of the raw PCM audio for the user to listen to
        const savedAudioPath = path.join(saveDir, `esp32_audio_${Date.now()}.raw`);
        fs.copyFileSync(audioPath, savedAudioPath);
        console.log(`[Bridge] Saved copy of raw audio to: ${savedAudioPath}`);

        console.log('[Bridge] Running STT...');
        const transcript = execSync(`${STT_SCRIPT} ${audioPath} || true`).toString().trim();
        console.log(`[Bridge] Transcript: ${transcript}`);

        console.log('[Bridge] Sending to OpenClaw CLI...');
        let assistantReply = "Anlayamadım.";

        try {
            // Using the explicitly correct session routing flag --agent main
            const cliOutput = execFileSync("openclaw", ["agent", "--agent", "main", "--message", transcript]).toString().trim();
            assistantReply = cliOutput || "Anlayamadım.";
        } catch (e) {
            console.error("[Bridge] CLI Exec Error");
        }

        console.log(`[Bridge] Assistant Reply: ${assistantReply}`);

        console.log('[Bridge] Running TTS...');
        // Strip out newlines, carriage returns, and ANSI color escape sequences/control characters which break JSON rendering
        const sanitizedReply = assistantReply
            .replace(/\n /g, ' ')
            .replace(/\n/g, ' ')
            .replace(/\r/g, ' ')
            .replace(/[\u001b\u009b][[()#;?]*(?:[0-9]{1,4}(?:;[0-9]{0,4})*)?[0-9A-ORZcf-nqry=><]/g, '');

        const outputPath = `/tmp/reply-${Date.now()}.mp3`;
        execFileSync(TTS_SCRIPT, [sanitizedReply, outputPath]);

        console.log('[Bridge] Streaming back...');
        res.setHeader('Content-Type', 'audio/mpeg');
        res.setHeader('Connection', 'close');
        const stream = fs.createReadStream(outputPath);
        stream.pipe(res);

        // Cleanup
        stream.on('end', () => {
            if (fs.existsSync(audioPath)) fs.unlinkSync(audioPath);
            if (fs.existsSync(outputPath)) fs.unlinkSync(outputPath);
        });

    } catch (error) {
        console.error('[Bridge] Error:', error);
        res.status(500).send('Bridge Error');
    }
});

const PORT = 18790;
const server = app.listen(PORT, '0.0.0.0', () => {
    console.log(`[Bridge] Jarvis Bridge listening on port ${PORT}`);
});
server.keepAliveTimeout = 300000;
server.headersTimeout = 300000;
