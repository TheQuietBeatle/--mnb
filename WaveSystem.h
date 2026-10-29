#pragma once
#include <vector>

// Declarations matching the globals and functions in WaveSystem.cpp (teammate's file).

const int WAVE_TOTAL = 3;

enum class EnemyType {
    Easy,
    Medium,
    Hard
};

struct Enemy {
    EnemyType type;
    int laneIndex;   // 0 to 3
    float x;         // Horizontal OpenGL coordinate
    bool active;     // false when defeated
};

extern std::vector<Enemy> enemies;
extern int currentWave;
extern int spawnedInCurrentWave;
extern double lastSpawnTime;
extern bool gameOver;
extern bool playerWon;

void updateWaveSystem(double currentTime);
void removeDefeatedEnemies();
int  getActiveEnemyCount();

// Resets all wave-system globals; call on game start and restart.
void resetWaves();
