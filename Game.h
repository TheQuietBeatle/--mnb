#pragma once
#include "Renderer.h"
#include "Characters.h"
#include "Config.h"
#include "UI.h"

#include <random>
#include <vector>

// One character on the battlefield (player or enemy)
struct Unit {
    int   def = 0;            // index into g_chars
    int   lane = 0;
    float x = 0.0f;           // centre x in world px
    int   hp = 0;
    float attackTimer = 0.0f;
    float swingTime = 0.0f;   // > 0 while the strike frame is shown
    float animTime = 0.0f;
    float jumpY = 0.0f, jumpVel = 0.0f;
    float duckTimer = 0.0f;
    bool  fighting = false;   // touching an opponent -> stops moving
};

struct Projectile {
    int   lane = 0;
    float x = 0.0f;
    float yOff = 0.0f;        // height above the lane bottom
    float vx = 0.0f;
    bool  fromEnemy = false;
    bool  high = false;       // enemy only: true = duck to dodge, false = jump to dodge
    int   damage = 1;
    bool  alive = true;
};

struct Collectible {
    int   lane = 0;
    float x = 0.0f;
    float age = 0.0f;
    bool  alive = true;
};

enum class GameState { Playing, Paused, Won, Lost };

class Game {
public:
    Game();

    void reset();
    void update(float dt);
    void render(Renderer& r) const;

    // input (world coordinates, y up)
    void onMouseMove(float wx, float wy);
    void onMouseButton(int button, float wx, float wy);
    void onKey(int key);

    GameState state() const { return st; }

private:
    void buildWaveQueue();
    void spawnEnemy(int def, int lane);
    void placePlayer(int def, int lane);

    void updateWaves(float dt);
    void updatePlayers(float dt);
    void updateEnemies(float dt);
    void updateProjectiles(float dt);
    void cleanup();

    Unit* blockingEnemy(const Unit& player);
    Unit* blockingPlayer(const Unit& enemy);
    bool  isUnlocked(int card) const;
    int   laneAt(float wx, float wy) const;
    void  drawUnit(Renderer& r, const Unit& u, bool enemy, float alpha = 1.0f) const;

    std::vector<Unit>        players;
    std::vector<Unit>        enemies;
    std::vector<Projectile>  projectiles;
    std::vector<Collectible> collectibles;

    int  coins = 0;
    int  armedCard = -1;      // card picked, waiting for a lane click
    int  hoveredCard = -1;
    int  hoveredLane = -1;
    bool selectedLanes[cfg::NUM_LANES] = {};

    int   waveIndex = 0;
    int   spawnedInWave = 0;
    float spawnTimer = 0.0f;
    std::vector<int> waveQueue;   // enemy types left to spawn this wave

    float     clock = 0.0f;
    GameState st = GameState::Playing;
    std::mt19937 rng;
    CardBar   cards;
};