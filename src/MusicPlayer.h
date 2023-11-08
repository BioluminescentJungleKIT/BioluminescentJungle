//
// Created by lars on 08.11.23.
//

#ifndef JUNGLE_MUSICPLAYER_H
#define JUNGLE_MUSICPLAYER_H

#include <portaudio.h>
#include <vector>
#include <cstdint>

struct PlaybackInfo {
    uint32_t currentSample;
    uint32_t dataLength;
    uint16_t blockAlign;
    char* data;
};

class MusicPlayer {
public:
    explicit MusicPlayer(const std::string& loop_wav_path);

    void init();
    void play();
    void pause();
    void terminate();
private:
    static int musicCallback(const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData );

    PlaybackInfo playbackInfo{};

    PaStream *stream{};

    uint16_t numChannels;
    PaSampleFormat sampleFormat;
    uint32_t sampleRate;

    std::vector<char> wav;

    uint16_t extractUint16(int offset, const std::vector<char> &vector);

    uint32_t extractUint32(int offset, const std::vector<char> &vector);
};


#endif //JUNGLE_MUSICPLAYER_H
