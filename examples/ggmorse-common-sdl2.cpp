#include "ggmorse-common-sdl2.h"

#include "ggmorse-common.h"

#include "ggmorse/ggmorse.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <chrono>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

std::string g_defaultCaptureDeviceName = "";

SDL_AudioDeviceID g_devIdInp = 0;
SDL_AudioDeviceID g_devIdOut = 0;

SDL_AudioSpec g_obtainedSpecInp;
SDL_AudioSpec g_obtainedSpecOut;

GGMorse *g_ggMorse = nullptr;

}

// JS interface

extern "C" {
    EMSCRIPTEN_KEEPALIVE
        int sendData(int textLength, const char * text, int protocolId, int volume) {
            // todo
            return 0;
        }

    EMSCRIPTEN_KEEPALIVE
        int getText(char * text) {
            std::copy(g_ggMorse->getRxData().begin(), g_ggMorse->getRxData().end(), text);
            return 0;
        }

    EMSCRIPTEN_KEEPALIVE
        float getSampleRate()           { return g_ggMorse->getSampleRateInp(); }

    EMSCRIPTEN_KEEPALIVE
        int hasDeviceOutput()           { return g_devIdOut; }

    EMSCRIPTEN_KEEPALIVE
        int hasDeviceCapture()          { return g_devIdInp; }

    EMSCRIPTEN_KEEPALIVE
        int doInit()                    {
            return GGMorse_init(-1, -1);
        }
}

void GGMorse_setDefaultCaptureDeviceName(std::string name) {
    g_defaultCaptureDeviceName = std::move(name);
}

bool GGMorse_init(
        const int playbackId,
        const int captureId,
        const float sampleRateOffset) {

    if (g_devIdInp && g_devIdOut) {
        return false;
    }

    if (g_devIdInp == 0 && g_devIdOut == 0) {
        SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s\n", SDL_GetError());
            return (1);
        }

        SDL_SetHintWithPriority(SDL_HINT_AUDIO_RESAMPLING_MODE, "medium", SDL_HINT_OVERRIDE);

        {
            int nDevices = SDL_GetNumAudioDevices(SDL_FALSE);
            printf("Found %d playback devices:\n", nDevices);
            for (int i = 0; i < nDevices; i++) {
                printf("    - Playback device #%d: '%s'\n", i, SDL_GetAudioDeviceName(i, SDL_FALSE));
            }
        }
        {
            int nDevices = SDL_GetNumAudioDevices(SDL_TRUE);
            printf("Found %d capture devices:\n", nDevices);
            for (int i = 0; i < nDevices; i++) {
                printf("    - Capture device #%d: '%s'\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
            }
        }
    }

    bool reinit = false;

    if (g_devIdOut == 0) {
        printf("Initializing playback ...\n");

        SDL_AudioSpec playbackSpec;
        SDL_zero(playbackSpec);

        playbackSpec.freq = GGMorse::kBaseSampleRate + sampleRateOffset;
        playbackSpec.format = AUDIO_S16SYS;
        playbackSpec.channels = 1;
        playbackSpec.samples = GGMorse::kDefaultSamplesPerFrame;
        playbackSpec.callback = NULL;

        SDL_zero(g_obtainedSpecOut);

        if (playbackId >= 0) {
            printf("Attempt to open playback device %d : '%s' ...\n", playbackId, SDL_GetAudioDeviceName(playbackId, SDL_FALSE));
            g_devIdOut = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(playbackId, SDL_FALSE), SDL_FALSE, &playbackSpec, &g_obtainedSpecOut, 0);
        } else {
            printf("Attempt to open default playback device ...\n");
            g_devIdOut = SDL_OpenAudioDevice(NULL, SDL_FALSE, &playbackSpec, &g_obtainedSpecOut, 0);
        }

        if (!g_devIdOut) {
            printf("Couldn't open an audio device for playback: %s!\n", SDL_GetError());
            g_devIdOut = 0;
        } else {
            printf("Obtained spec for output device (SDL Id = %d):\n", g_devIdOut);
            printf("    - Sample rate:       %d (required: %d)\n", g_obtainedSpecOut.freq, playbackSpec.freq);
            printf("    - Format:            %d (required: %d)\n", g_obtainedSpecOut.format, playbackSpec.format);
            printf("    - Channels:          %d (required: %d)\n", g_obtainedSpecOut.channels, playbackSpec.channels);
            printf("    - Samples per frame: %d (required: %d)\n", g_obtainedSpecOut.samples, playbackSpec.samples);

            if (g_obtainedSpecOut.format != playbackSpec.format ||
                g_obtainedSpecOut.channels != playbackSpec.channels ||
                g_obtainedSpecOut.samples != playbackSpec.samples) {
                g_devIdOut = 0;
                SDL_CloseAudio();
                fprintf(stderr, "Failed to initialize playback SDL_OpenAudio!");

                return false;
            }

            reinit = true;
        }
    }

    if (g_devIdInp == 0) {
        SDL_AudioSpec captureSpec;
        captureSpec = g_obtainedSpecOut;
        captureSpec.freq = GGMorse::kBaseSampleRate + sampleRateOffset;
        captureSpec.format = AUDIO_F32SYS;
        captureSpec.samples = GGMorse::kDefaultSamplesPerFrame;

        SDL_zero(g_obtainedSpecInp);

        if (captureId >= 0) {
            printf("Attempt to open capture device %d : '%s' ...\n", captureId, SDL_GetAudioDeviceName(captureId, SDL_TRUE));
            g_devIdInp = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(captureId, SDL_TRUE), SDL_TRUE, &captureSpec, &g_obtainedSpecInp, 0);
        } else {
            printf("Attempt to open default capture device ...\n");
            g_devIdInp = SDL_OpenAudioDevice(g_defaultCaptureDeviceName.empty() ? nullptr : g_defaultCaptureDeviceName.c_str(),
                                            SDL_TRUE, &captureSpec, &g_obtainedSpecInp, 0);
        }
        if (!g_devIdInp) {
            printf("Couldn't open an audio device for capture: %s!\n", SDL_GetError());
            g_devIdInp = 0;
        } else {
            printf("Obtained spec for input device (SDL Id = %d):\n", g_devIdInp);
            printf("    - Sample rate:       %d\n", g_obtainedSpecInp.freq);
            printf("    - Format:            %d (required: %d)\n", g_obtainedSpecInp.format, captureSpec.format);
            printf("    - Channels:          %d (required: %d)\n", g_obtainedSpecInp.channels, captureSpec.channels);
            printf("    - Samples per frame: %d\n", g_obtainedSpecInp.samples);

            reinit = true;
        }
    }

    GGMorse::SampleFormat sampleFormatInp = GGMORSE_SAMPLE_FORMAT_UNDEFINED;
    GGMorse::SampleFormat sampleFormatOut = GGMORSE_SAMPLE_FORMAT_UNDEFINED;

    switch (g_obtainedSpecInp.format) {
        case AUDIO_U8:      sampleFormatInp = GGMORSE_SAMPLE_FORMAT_U8;  break;
        case AUDIO_S8:      sampleFormatInp = GGMORSE_SAMPLE_FORMAT_I8;  break;
        case AUDIO_U16SYS:  sampleFormatInp = GGMORSE_SAMPLE_FORMAT_U16; break;
        case AUDIO_S16SYS:  sampleFormatInp = GGMORSE_SAMPLE_FORMAT_I16; break;
        case AUDIO_S32SYS:  sampleFormatInp = GGMORSE_SAMPLE_FORMAT_F32; break;
        case AUDIO_F32SYS:  sampleFormatInp = GGMORSE_SAMPLE_FORMAT_F32; break;
    }

    switch (g_obtainedSpecOut.format) {
        case AUDIO_U8:      sampleFormatOut = GGMORSE_SAMPLE_FORMAT_U8;  break;
        case AUDIO_S8:      sampleFormatOut = GGMORSE_SAMPLE_FORMAT_I8;  break;
        case AUDIO_U16SYS:  sampleFormatOut = GGMORSE_SAMPLE_FORMAT_U16; break;
        case AUDIO_S16SYS:  sampleFormatOut = GGMORSE_SAMPLE_FORMAT_I16; break;
        case AUDIO_S32SYS:  sampleFormatOut = GGMORSE_SAMPLE_FORMAT_F32; break;
        case AUDIO_F32SYS:  sampleFormatOut = GGMORSE_SAMPLE_FORMAT_F32; break;
            break;
    }

    if (reinit) {
        if (g_ggMorse) delete g_ggMorse;

        g_ggMorse = new GGMorse({
            (float) g_obtainedSpecInp.freq,
            (float) g_obtainedSpecOut.freq,
            GGMorse::kDefaultSamplesPerFrame,
            sampleFormatInp,
            sampleFormatOut});
    }

    return true;
}

GGMorse *& GGMorse_instance() { return g_ggMorse; }

bool GGMorse_mainLoop() {
    if (g_devIdInp == 0 && g_devIdOut == 0) {
        return false;
    }

    static GGMorse::CBWaveformOut cbWaveformOut = [&](const void * data, uint32_t nBytes) {
        SDL_QueueAudio(g_devIdOut, data, nBytes);
    };

    static GGMorse::CBWaveformInp cbWaveformInp = [&](void * data, uint32_t nMaxBytes) {
        return SDL_DequeueAudio(g_devIdInp, data, nMaxBytes);
    };

    if (g_ggMorse->hasTxData() == false) {
        SDL_PauseAudioDevice(g_devIdOut, SDL_FALSE);
        SDL_PauseAudioDevice(g_devIdInp, SDL_FALSE);
        g_ggMorse->decode(cbWaveformInp);
        if ((int) SDL_GetQueuedAudioSize(g_devIdInp) > 32*g_ggMorse->getSamplesPerFrame()*g_ggMorse->getSampleSizeBytesInp()) {
            fprintf(stderr, "Warning: slow processing, clearing queued audio buffer of %d bytes ...", SDL_GetQueuedAudioSize(g_devIdInp));
            SDL_ClearQueuedAudio(g_devIdInp);
        }
    } else {
        SDL_PauseAudioDevice(g_devIdOut, SDL_TRUE);
        //SDL_PauseAudioDevice(g_devIdInp, SDL_TRUE);

        g_ggMorse->encode(cbWaveformOut);
    }

    return true;
}

bool GGMorse_deinit() {
    if (g_devIdInp == 0 && g_devIdOut == 0) {
        return false;
    }

    delete g_ggMorse;
    g_ggMorse = nullptr;

    SDL_PauseAudioDevice(g_devIdInp, 1);
    SDL_CloseAudioDevice(g_devIdInp);
    SDL_PauseAudioDevice(g_devIdOut, 1);
    SDL_CloseAudioDevice(g_devIdOut);

    g_devIdInp = 0;
    g_devIdOut = 0;

    return true;
}
