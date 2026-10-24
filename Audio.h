#pragma once

namespace Audio {
    bool init(const char* musicPath);
    void shutdown();

    void pauseMusic();
    void resumeMusic();
    void restartMusic();
    void setMasterVolume(float v);   // 0.0 – 1.0
    float getMasterVolume();

    void playSfx(const char* path);  // fire-and-forget
}