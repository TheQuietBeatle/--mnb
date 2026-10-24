#include "Game.h"
#include <GLFW/glfw3.h>   // key / mouse button codes

#include <algorithm>
#include <cmath>
#include <string>

using namespace cfg;

namespace {

struct WaveDef {
    int   count;     // number of enemies in the wave
    int   maxType;   // strongest enemy type allowed (CH_EASY .. CH_HARD)
    float minGap, maxGap;   // seconds between spawns
};

const WaveDef kWaves[] = {
    {  3, CH_EASY,   4.0f, 6.0f },   // wave 1: easy only
    {  5, CH_MEDIUM, 3.0f, 5.0f },   // wave 2: easy + medium
    { 10, CH_HARD,   2.0f, 4.0f },   // wave 3: easy + medium + hard
};
constexpr int NUM_WAVES = (int)(sizeof(kWaves) / sizeof(kWaves[0]));

constexpr float PLAYER_BOLT_SPEED = 420.0f;
constexpr float ENEMY_BOLT_SPEED  = 260.0f;
constexpr float WAVE_BREAK = 4.0f;
constexpr float COIN_HOVER = 0.5f;   // seconds a dropped coin stays put
constexpr float COIN_FLY = 0.5f;   // seconds to fly to the counter 

bool touching(const Unit& a, const Unit& b)
{
    return a.lane == b.lane &&
           std::fabs(a.x - b.x) < unitHalfWidth(a.def) + unitHalfWidth(b.def);
}

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
Game::Game() : rng(std::random_device{}())
{
    reset();
}

void Game::reset()
{
    players.clear();
    enemies.clear();
    projectiles.clear();
    collectibles.clear();

    coins = START_COINS;
    armedCard = hoveredCard = hoveredLane = -1;
    for (bool& s : selectedLanes) s = false;

    waveIndex = 0;
    spawnedInWave = 0;
    spawnTimer = 2.0f;
    buildWaveQueue();

    clock = 0.0f;
    st = GameState::Playing;
}

void Game::buildWaveQueue()
{
    waveQueue.clear();
    if (waveIndex >= NUM_WAVES) return;

    // cycle through the allowed types so every type appears, then shuffle
    const WaveDef& w = kWaves[waveIndex];
    int numTypes = w.maxType - CH_EASY + 1;
    for (int i = 0; i < w.count; ++i)
        waveQueue.push_back(CH_EASY + i % numTypes);
    std::shuffle(waveQueue.begin(), waveQueue.end(), rng);
}

void Game::spawnEnemy(int def, int lane)
{
    Unit e;
    e.def = def;
    e.lane = lane;
    e.x = GRID_RIGHT - unitHalfWidth(def);
    e.hp = g_chars[def].maxHp;
    e.attackTimer = 0.5f;   // starts attacking right after it appears
    enemies.push_back(e);
}

void Game::placePlayer(int def, int lane)
{
    const CharacterDef& d = g_chars[def];
    if (coins < d.cost) return;
    coins -= d.cost;

    Unit p;
    p.def = def;
    p.lane = lane;
    p.x = GRID_LEFT + unitHalfWidth(def);   // leftmost position of the lane
    p.hp = d.maxHp;
    p.attackTimer = 0.2f;
    players.push_back(p);
}

bool Game::isUnlocked(int card) const
{
    return card >= 0 && card < NUM_HERO_TYPES && coins >= g_chars[card].cost;
}

int Game::laneAt(float wx, float wy) const
{
    float gridBottom = laneBottom(NUM_LANES - 1);
    if (wx < GRID_LEFT || wx > GRID_RIGHT || wy < gridBottom || wy >= GRID_TOP) return -1;
    return std::clamp((int)((GRID_TOP - wy) / LANE_H), 0, NUM_LANES - 1);
}


// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void Game::update(float dt)
{
    if (st != GameState::Playing) return;   // paused / won / lost: everything freezes
    clock += dt;

    // a card can re-lock when coins drop below its cost
    if (armedCard >= 0 && !isUnlocked(armedCard)) armedCard = -1;

    updateWaves(dt);
    updatePlayers(dt);
    updateEnemies(dt);
    if (st != GameState::Playing) return;   // an enemy reached the base
    updateProjectiles(dt);
    cleanup();

    if (waveIndex >= NUM_WAVES && enemies.empty()) st = GameState::Won;
}

void Game::updateWaves(float dt)
{
    if (waveIndex >= NUM_WAVES) return;

    if (spawnedInWave < (int)waveQueue.size()) {
        spawnTimer -= dt;
        if (spawnTimer <= 0.0f) {
            std::uniform_int_distribution<int> laneDist(0, NUM_LANES - 1);
            spawnEnemy(waveQueue[spawnedInWave++], laneDist(rng));

            std::uniform_real_distribution<float> gap(kWaves[waveIndex].minGap, kWaves[waveIndex].maxGap);
            spawnTimer = gap(rng);
        }
    }
    else if (enemies.empty()) {
        // wave fully spawned and cleared -> next wave
        ++waveIndex;
        spawnedInWave = 0;
        spawnTimer = WAVE_BREAK;
        buildWaveQueue();
    }
}

Unit* Game::blockingEnemy(const Unit& p)
{
    Unit* best = nullptr;
    for (Unit& e : enemies)
        if (e.hp > 0 && touching(p, e) && (!best || e.x < best->x)) best = &e;
    return best;
}

Unit* Game::blockingPlayer(const Unit& e)
{
    Unit* best = nullptr;   // the front-most player it touches
    for (Unit& p : players)
        if (p.hp > 0 && touching(p, e) && (!best || p.x > best->x)) best = &p;
    return best;
}

void Game::updatePlayers(float dt)
{
    for (Unit& p : players) {
        const CharacterDef& d = g_chars[p.def];
        p.animTime += dt;

        // jump / duck (defense players ignore both)
        if (d.role != Role::Defense) {
            if (p.jumpY > 0.0f || p.jumpVel > 0.0f) {
                p.jumpY += p.jumpVel * dt;
                p.jumpVel -= GRAVITY * dt;
                if (p.jumpY <= 0.0f) { p.jumpY = 0.0f; p.jumpVel = 0.0f; }
            }
            if (p.duckTimer > 0.0f) p.duckTimer -= dt;
        }

        // movement: stop while touching an enemy
        Unit* target = blockingEnemy(p);
        p.fighting = (target != nullptr);
        if (d.speed > 0.0f && !p.fighting) {
            p.x += d.speed * dt;
            p.x = std::min(p.x, GRID_RIGHT - unitHalfWidth(p.def));
        }

        // attacking starts as soon as the player is placed
        if (p.swingTime > 0.0f) p.swingTime -= dt;
        p.attackTimer -= dt;
        if (d.damage > 0 && p.attackTimer <= 0.0f) {
            p.attackTimer = d.attackCooldown;
            if (d.role == Role::Melee) {
                p.swingTime = 0.18f;
                if (target) target->hp -= d.damage;
            }
            else if (d.role == Role::Ranged) {
                Projectile pr;
                pr.lane = p.lane;
                pr.x = p.x + unitHalfWidth(p.def);
                pr.yOff = GROUND_PAD + p.jumpY + unitHeight(p.def) * 0.80f;   // barrel height
                pr.vx = PLAYER_BOLT_SPEED;
                pr.damage = d.damage;
                projectiles.push_back(pr);
            }
        }

        // pick up collectibles by walking over them
        if (d.role != Role::Defense) {
            for (Collectible& c : collectibles) {
                if (c.alive && c.lane == p.lane && std::fabs(c.x - p.x) < unitHalfWidth(p.def)) {
                    c.alive = false;
                    coins += COIN_DROP;
                }
            }
        }
    }

    // coins nobody walked over are collected automatically:
// they hover for a moment, fly to the coin counter, then count
    for (Collectible& c : collectibles) {
        c.age += dt;
        if (c.alive && c.age >= COIN_HOVER + COIN_FLY) {
            c.alive = false;
            coins += COIN_DROP;
        }
    }
}



void Game::updateEnemies(float dt)
{
    for (Unit& e : enemies) {
        const CharacterDef& d = g_chars[e.def];
        e.animTime += dt;

        Unit* target = blockingPlayer(e);
        e.fighting = (target != nullptr);
        if (!e.fighting) {
            e.x -= d.speed * dt;
            if (e.x - unitHalfWidth(e.def) <= GRID_LEFT) {   // reached the home base
                st = GameState::Lost;
                return;
            }
        }

        if (e.swingTime > 0.0f) e.swingTime -= dt;
        e.attackTimer -= dt;
        if (e.attackTimer > 0.0f) continue;
        e.attackTimer = d.attackCooldown;

        if (d.role == Role::EnemyMelee) {
            e.swingTime = 0.2f;
            if (target) target->hp -= d.damage;
        }
        else {
            Projectile pr;
            pr.lane = e.lane;
            pr.x = e.x - unitHalfWidth(e.def);
            pr.vx = -ENEMY_BOLT_SPEED;
            pr.fromEnemy = true;
            pr.high = (d.role == Role::EnemyUpper);
            pr.yOff = pr.high ? GROUND_PAD + 70.0f : GROUND_PAD + 2.0f;
            pr.damage = d.damage;
            projectiles.push_back(pr);
        }
    }
}

void Game::updateProjectiles(float dt)
{
    for (Projectile& pr : projectiles) {
        if (!pr.alive) continue;
        pr.x += pr.vx * dt;

        if (!pr.fromEnemy) {
            for (Unit& e : enemies) {
                if (e.lane == pr.lane && e.hp > 0 && std::fabs(pr.x - e.x) < unitHalfWidth(e.def)) {
                    e.hp -= pr.damage;
                    pr.alive = false;
                    break;
                }
            }
            if (pr.x > WORLD_W) pr.alive = false;
            continue;
        }

        // enemy projectile: a defense player soaks it first, which is what
        // protects every player standing behind it
        Unit* hit = nullptr;
        for (Unit& p : players) {
            if (p.lane == pr.lane && p.hp > 0 && g_chars[p.def].role == Role::Defense &&
                std::fabs(pr.x - p.x) < unitHalfWidth(p.def)) {
                hit = &p; break;
            }
        }
        if (!hit) {
            for (Unit& p : players) {
                if (p.lane != pr.lane || p.hp <= 0) continue;
                if (std::fabs(pr.x - p.x) >= unitHalfWidth(p.def)) continue;
                bool dodged = pr.high ? (p.duckTimer > 0.0f)   // duck under high attacks
                    : (p.jumpY > 12.0f);     // jump over low attacks
                if (dodged) continue;
                hit = &p;
                break;
            }
        }
        if (hit) {
            hit->hp -= pr.damage;
            pr.alive = false;
        }
        if (pr.x < GRID_LEFT - 10.0f) pr.alive = false;
    }
}

void Game::cleanup()
{
    // defeated enemies drop a collectible where they fell
    for (const Unit& e : enemies)
        if (e.hp <= 0) {
            Collectible c;
            c.lane = e.lane;
            c.x = e.x;
            collectibles.push_back(c);
        }

    auto dead = [](const Unit& u) { return u.hp <= 0; };
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), dead), enemies.end());
    players.erase(std::remove_if(players.begin(), players.end(), dead), players.end());
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
        [](const Projectile& p) { return !p.alive; }), projectiles.end());
    collectibles.erase(std::remove_if(collectibles.begin(), collectibles.end(),
        [](const Collectible& c) { return !c.alive; }), collectibles.end());
}



// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void Game::onMouseMove(float wx, float wy)
{
    hoveredLane = laneAt(wx, wy);
    int c = cards.hitTest(wx, wy);
    hoveredCard = isUnlocked(c) ? c : -1;   // locked cards ignore hover
}

void Game::onMouseButton(int button, float wx, float wy)
{
    if (st != GameState::Playing) return;

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {   // cancel the picked card
        armedCard = -1;
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    int card = cards.hitTest(wx, wy);
    if (card >= 0) {
        if (isUnlocked(card)) armedCard = (armedCard == card) ? -1 : card;
        return;
    }

    int lane = laneAt(wx, wy);
    if (lane < 0) return;
    if (armedCard >= 0) {
        placePlayer(armedCard, lane);        // card picked -> place on this lane
        armedCard = -1;
    }
    else {
        selectedLanes[lane] = !selectedLanes[lane];   // toggle for jump / duck
    }
}

void Game::onKey(int key)
{
    if (key == GLFW_KEY_ESCAPE) {
        if (st == GameState::Playing)     st = GameState::Paused;
        else if (st == GameState::Paused) st = GameState::Playing;
        return;
    }
    if (key == GLFW_KEY_R && st != GameState::Playing) {
        reset();
        return;
    }
    if (st != GameState::Playing) return;

    bool jump = (key == GLFW_KEY_W || key == GLFW_KEY_UP);
    bool duck = (key == GLFW_KEY_S || key == GLFW_KEY_DOWN);
    if (!jump && !duck) return;

    for (Unit& p : players) {
        if (!selectedLanes[p.lane] || g_chars[p.def].role == Role::Defense) continue;
        if (jump && p.jumpY <= 0.0f) p.jumpVel = JUMP_SPEED;
        if (duck) p.duckTimer = DUCK_TIME;
    }
}



// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void Game::drawUnit(Renderer& r, const Unit& u, bool enemy, float alpha) const
{
    const CharacterDef& d = g_chars[u.def];

    // pick the animation frame
    SpriteId id = d.frameA;
    bool walking = d.speed > 0.0f && !u.fighting;
    if (d.role == Role::Melee)
        id = (u.swingTime > 0.0f) ? d.frameB : d.frameA;            // B = arm shoots out on a punch
    else if (d.role == Role::EnemyMelee && u.fighting)
        id = (u.swingTime > 0.0f) ? d.frameA : d.frameB;            // weapon strike          
    else if (walking)
        id = ((int)(u.animTime * 5.0f) % 2 == 0) ? d.frameA : d.frameB;   // walk cycle

    const SpriteFrame& f = g_sprites[id];
    float s = unitScale(u.def);
    float sy = (u.duckTimer > 0.0f) ? s * 0.6f : s;
    float y = laneBottom(u.lane) + GROUND_PAD + u.jumpY;

    DrawFx fx;
    fx.red = (1.0f - std::max(0, u.hp) / (float)d.maxHp) * 0.75f;   // redder as health drops
    fx.alpha = alpha;
    r.drawSprite(f, u.x, y, s, sy, enemy, fx);

    if (alpha >= 1.0f)
        drawHealthBar(r, u.x, y + f.h * sy + 6.0f, 56.0f, u.hp, d.maxHp, enemy);
}

// health bar centred on cx, bottom edge at y (world is y-up)
static void drawHealthBar(Renderer& r, float cx, float y, int hp, int maxHp)
{
    const float W = 50.0f, H = 7.0f, B = 1.5f;
    float frac = maxHp > 0 ? std::clamp((float)hp / maxHp, 0.0f, 1.0f) : 0.0f;
    float left = cx - W / 2.0f;

    r.drawRect(left - B, y - B, W + 2 * B, H + 2 * B, { 0.05f, 0.05f, 0.05f });  // outline
    r.drawRect(left, y, W, H, { 0.30f, 0.05f, 0.05f });                         // empty part

    // green -> yellow -> red as health drops
    float red = frac > 0.5f ? (1.0f - frac) * 2.0f : 1.0f;
    float green = frac > 0.5f ? 1.0f : frac * 2.0f;
    r.drawRect(left, y, W * frac, H, { red, green, 0.10f });

    // a tick between each life so you can count them
    for (int i = 1; i < maxHp; ++i)
        r.drawRect(left + W * i / maxHp - 0.5f, y, 1.0f, H, { 0.0f, 0.0f, 0.0f });
}

void Game::render(Renderer& r) const
{
    drawBattlefield(r, st == GameState::Playing ? hoveredLane : -1, selectedLanes, clock);

    // ghost preview of where the picked card will appear
    if (st == GameState::Playing && armedCard >= 0 && hoveredLane >= 0) {
        Unit ghost;
        ghost.def = armedCard;
        ghost.lane = hoveredLane;
        ghost.x = GRID_LEFT + unitHalfWidth(armedCard);
        ghost.hp = g_chars[armedCard].maxHp;
        drawUnit(r, ghost, false, 0.45f);
    }

    for (const Collectible& c : collectibles) {
        // start where the enemy fell, end at the coin counter (82, 655)
        float startY = laneBottom(c.lane) + GROUND_PAD + 6.0f + std::sin(c.age * 8.0f) * 1.0f;
        float t = std::clamp((c.age - COIN_HOVER) / COIN_FLY, 0.0f, 1.0f);
        t = t * t;                                   // ease in: starts slow, speeds up
        float x = c.x + (82.0f - c.x) * t;
        float y = startY + (655.0f - startY) * t;
        float fcs = 0.8f / g_sprites[SPR_COIN].h;  // field coin size - lower = smaller
        r.drawSprite(g_sprites[SPR_COIN], x, y, fcs, fcs, false);
    }

    const float BAR_Y = 95.0f;   // height above the lane floor, tune this

    for (const Unit& p : players) {
        drawUnit(r, p, false);
        drawHealthBar(r, p.x, laneBottom(p.lane) + GROUND_PAD + BAR_Y, p.hp, g_chars[p.def].maxHp);
    }
    for (const Unit& e : enemies) {
        drawUnit(r, e, true);
        drawHealthBar(r, e.x, laneBottom(e.lane) + GROUND_PAD + BAR_Y, e.hp, g_chars[e.def].maxHp);
    }

    for (const Projectile& p : projectiles) {
        float y = laneBottom(p.lane) + p.yOff;
        if (!p.fromEnemy) {
            // rocket flying right: flickering flame, red body, steel fins, white nose
            float flick = (std::fmod(p.x, 20.0f) < 10.0f) ? 4.0f : 0.0f;
            r.drawRect(p.x - 24 - flick, y - 2, 8 + flick, 4, { 1.00f, 0.75f, 0.20f });
            r.drawRect(p.x - 16, y - 4, 22, 8, { 0.76f, 0.26f, 0.17f });
            r.drawRect(p.x - 16, y - 7, 6, 14, { 0.35f, 0.38f, 0.42f });
            r.drawRect(p.x + 6, y - 3, 5, 6, { 0.85f, 0.84f, 0.76f });
            r.drawRect(p.x + 11, y - 1, 3, 2, { 0.85f, 0.84f, 0.76f });
        }
        else if (p.high) {
            r.drawRect(p.x - 7, y - 7, 14, 14, { 0.20f, 0.60f, 0.15f });
            r.drawRect(p.x - 4, y - 4, 8, 8, { 0.60f, 1.00f, 0.40f });
        }
        else {
            r.drawRect(p.x - 10, y, 20, 6, { 0.20f, 0.60f, 0.15f });
            r.drawRect(p.x - 6, y + 2, 12, 2, { 0.60f, 1.00f, 0.40f });
        }
    }

    // HUD
    drawCoinPanel(r, coins);
    cards.draw(r, coins, armedCard, hoveredCard);

    std::string wave = "WAVE " + std::to_string(std::min(waveIndex + 1, NUM_WAVES)) + "/" + std::to_string(NUM_WAVES);
    r.drawText(wave, WORLD_W - r.textWidth(wave, 4.0f) - 30.0f, 660.0f, 4.0f, { 0.90f, 0.95f, 1.00f });
    r.drawText("CLICK CARD THEN LANE.  CLICK LANE TO SELECT.", 490, 650, 2.0f, { 0.60f, 0.70f, 0.80f });
    r.drawText("W JUMP  S DUCK  ESC PAUSE  RIGHT CLICK CANCEL", 490, 630, 2.0f, { 0.60f, 0.70f, 0.80f });

    // overlays
    if (st != GameState::Playing) {
        r.drawRect(0, 0, WORLD_W, WORLD_H, { 0.0f, 0.0f, 0.0f }, 0.6f);
        if (st == GameState::Paused) {
            drawCenteredText(r, "PAUSED", 400, 10.0f, { 1, 1, 1 });
            drawCenteredText(r, "ESC RESUME   R RESTART", 320, 4.0f, { 0.8f, 0.85f, 0.9f });
        }
        else if (st == GameState::Won) {
            drawCenteredText(r, "VICTORY!", 400, 10.0f, { 0.4f, 1.0f, 0.5f });
            drawCenteredText(r, "R RESTART", 320, 4.0f, { 0.8f, 0.85f, 0.9f });
        }
        else {
            drawCenteredText(r, "BASE OVERRUN", 400, 10.0f, { 1.0f, 0.35f, 0.3f });
            drawCenteredText(r, "R RESTART", 320, 4.0f, { 0.8f, 0.85f, 0.9f });
        }
    }
}