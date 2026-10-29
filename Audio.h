#pragma once

namespace Audio {
    bool init(const char* musicPath);
    void shutdown();

    void pauseMusic();
    void resumeMusic();
    void restartMusic();
    void setMasterVolume(float v);   // 0.0 – 1.0 (everything: music + sfx)
    float getMasterVolume();
    void setMusicVolume(float v);    // 0.0 – 1.0 (theme music only, default 0.6)
    float getMusicVolume();

    void playSfx(const char* path);  // fire-and-forget
}