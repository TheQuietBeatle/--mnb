#define MINIAUDIO_IMPLEMENTATION     // ONLY here, nowhere else
#include "miniaudio.h"
#include "Audio.h"

static ma_engine engine;
static ma_sound  music;
static bool      ready = false;
static float     masterVol = 1.0f;

bool Audio::init(const char* musicPath) {
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) return false;
    ready = true;

    if (ma_sound_init_from_file(&engine, musicPath, MA_SOUND_FLAG_STREAM,
        NULL, NULL, &music) != MA_SUCCESS)
        return false;

    ma_sound_set_looping(&music, MA_TRUE);
    ma_sound_set_volume(&music, 0.6f);
    ma_sound_start(&music);
    return true;
}

void Audio::shutdown() {
    if (!ready) return;
    ma_sound_uninit(&music);
    ma_engine_uninit(&engine);
    ready = false;
}

void Audio::pauseMusic() { ma_sound_stop(&music); }
void Audio::resumeMusic() { ma_sound_start(&music); }
void Audio::restartMusic() { ma_sound_seek_to_pcm_frame(&music, 0); ma_sound_start(&music); }

void Audio::setMasterVolume(float v) {
    masterVol = v < 0 ? 0 : (v > 1 ? 1 : v);
    ma_engine_set_volume(&engine, masterVol);
}
float Audio::getMasterVolume() { return masterVol; }

void Audio::playSfx(const char* path) {
    if (ready) ma_engine_play_sound(&engine, path, NULL);
}