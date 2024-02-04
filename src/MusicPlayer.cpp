// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#include <iostream>
#include "MusicPlayer.h"
#include "VulkanHelper.h"

#define PA_CHECK_ERROR(err) if( (err) != paNoError ) {std::cout << "[PortAudio] ERR: " << Pa_GetErrorText(err) << std::endl;return;}

int MusicPlayer::musicCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                               const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                               void *userData) {
    auto *playbackInfo = (PlaybackInfo *) userData;
    auto *out = (char *) outputBuffer;
    (void) inputBuffer;

    for (int i = 0; i < framesPerBuffer; i++) {
        for (int j = 0; j < playbackInfo->blockAlign; j++) {
            *out = playbackInfo->data[44 + playbackInfo->currentSample * playbackInfo->blockAlign + j];
            out++;
        }
        playbackInfo->currentSample++;
        playbackInfo->currentSample %= (playbackInfo->dataLength / playbackInfo->blockAlign);
    }
    return 0;
}

void MusicPlayer::init() {
    PA_CHECK_ERROR(Pa_Initialize())
    PA_CHECK_ERROR(Pa_OpenDefaultStream(&stream, 0, numChannels, sampleFormat, sampleRate, paFramesPerBufferUnspecified,
                                        musicCallback, &playbackInfo))
}

void MusicPlayer::terminate() {
    PA_CHECK_ERROR(Pa_CloseStream(stream))
    PA_CHECK_ERROR(Pa_Terminate())
}

MusicPlayer::MusicPlayer(const std::string &loop_wav_path) {
    wav = readFile(loop_wav_path);

    // real wave file
    assert(wav[0] == 'R');
    assert(wav[1] == 'I');
    assert(wav[2] == 'F');
    assert(wav[3] == 'F');
    assert(wav[8] == 'W');
    assert(wav[9] == 'A');
    assert(wav[10] == 'V');
    assert(wav[11] == 'E');
    // assume header at front
    assert(wav[12] == 'f');
    assert(wav[13] == 'm');
    assert(wav[14] == 't');
    assert(wav[15] == ' ');
    // assume header size 16
    assert(wav[16] == 16);
    assert(wav[17] == 0);
    assert(wav[18] == 0);
    assert(wav[19] == 0);
    // assume format pcm
    assert(wav[20] == 1);
    assert(wav[21] == 0);
    numChannels = extractUint16(22, wav);
    sampleRate = extractUint32(24, wav);
    // ignore bytes per sec
    uint16_t blockAlign = extractUint16(32, wav);
    uint32_t bitsPerSample = extractUint16(34, wav);
    switch (bitsPerSample) {
        case 8:
            sampleFormat = paInt8;
            break;
        case 16:
            sampleFormat = paInt16;
            break;
        case 24:
            sampleFormat = paInt24;
            break;
        case 32:
            sampleFormat = paInt32;
            break;
        default:
            sampleFormat = paCustomFormat;
    }
    // data after header
    assert(wav[36] == 'd');
    assert(wav[37] == 'a');
    assert(wav[38] == 't');
    assert(wav[39] == 'a');
    uint32_t dataLength = extractUint32(40, wav);

    playbackInfo.currentSample = 0;
    playbackInfo.dataLength = dataLength;
    playbackInfo.blockAlign = blockAlign;
    playbackInfo.data = wav.data();
}

uint16_t MusicPlayer::extractUint16(int offset, const std::vector<char> &vector) {
    return static_cast<uint16_t>(static_cast<unsigned char>(vector[offset])) |
           (static_cast<uint16_t>(static_cast<unsigned char>(wav[offset + 1])) << 8);
}

uint32_t MusicPlayer::extractUint32(int offset, const std::vector<char> &vector) {
    return static_cast<uint32_t>(static_cast<unsigned char>(vector[offset])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(wav[offset + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(wav[offset + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(wav[offset + 3])) << 24);
}

void MusicPlayer::play() {
    PA_CHECK_ERROR(Pa_StartStream(stream))
}

void MusicPlayer::pause() {
    PA_CHECK_ERROR(Pa_AbortStream(stream))
}
