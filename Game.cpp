#include "Game.h"
#include "WaveSystem.h"
#include "Audio.h"
#include <GLFW/glfw3.h>   // key / mouse button codes

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>

using namespace cfg;

// Reset the WaveSystem globals so the game can be restarted.
void resetWaves()
{
    ::enemies.clear();
    currentWave = 1;
    spawnedInCurrentWave = 0;
    lastSpawnTime = glfwGetTime();
    gameOver = playerWon = false;
}

namespace {

    constexpr float PLAYER_BOLT_SPEED = 420.0f;
    constexpr float ENEMY_BOLT_SPEED = 260.0f;
    constexpr float COIN_HOVER = 0.5f;   // seconds a dropped coin stays put
    constexpr float COIN_FLY = 0.5f;   // seconds to fly to the counter
    constexpr float POPUP_TIME = 1.0f; // seconds a "+coins" reward popup stays on screen

    // Pause-menu buttons (world px, y up). Drawing and click-testing use the same rects.
    struct Button {
        float x, y, w, h;
        bool hit(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    };
    const Button BTN_VOL_DOWN{ 470.0f, 248.0f,  60.0f, 40.0f };
    const Button BTN_VOL_UP{ 750.0f, 248.0f,  60.0f, 40.0f };
    const Button BTN_MUTE{ 530.0f, 190.0f, 220.0f, 40.0f };

    void drawButton(Renderer& r, const Button& b, const std::string& label)
    {
        r.drawRect(b.x, b.y, b.w, b.h, { 0.20f, 0.30f, 0.42f });
        r.drawRect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, { 0.08f, 0.12f, 0.20f });
        float tx = b.x + (b.w - r.textWidth(label, 4.0f)) * 0.5f;
        float ty = b.y + (b.h - 7.0f * 4.0f) * 0.5f;
        r.drawText(label, tx, ty, 4.0f, { 0.85f, 0.92f, 1.00f });
    }

    // a = player, b = enemy.  signed gap prevents units from tunnelling through each other.
    bool touching(const Unit& a, const Unit& b)
    {
        if (a.lane != b.lane) return false;
        float gap = b.x - a.x;   // positive when enemy is to the right (normal case)
        float contact = unitHalfWidth(a.def) + unitHalfWidth(b.def);
        return gap <= contact && gap > -contact;
    }

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
Game::Game() : rng(std::random_device{}())
{
    srand(static_cast<unsigned int>(time(nullptr)));
    reset();
}

void Game::reset()
{
    players.clear();
    enemies.clear();
    projectiles.clear();
    collectibles.clear();
    popups.clear();

    coins = START_COINS;
    armedCard = hoveredCard = hoveredLane = -1;
    for (bool& s : selectedLanes) s = false;

    baseHp = BASE_MAX_HP;
    resetWaves();

    clock = 0.0f;
    st = GameState::Playing;
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

    // Wave system drives enemy spawning (assign lane randomly with our rng)
    {
        std::uniform_int_distribution<int> laneDist(0, NUM_LANES - 1);
        size_t prevCount = ::enemies.size();
        updateWaveSystem(glfwGetTime());
        for (size_t i = prevCount; i < ::enemies.size(); ++i) {
            const auto& we = ::enemies[i];
            int def = (we.type == EnemyType::Easy) ? CH_EASY :
                (we.type == EnemyType::Medium) ? CH_MEDIUM : CH_HARD;
            spawnEnemy(def, laneDist(rng));
        }
    }

    updatePlayers(dt);
    updateEnemies(dt);
    updateProjectiles(dt);
    cleanup();

    if (baseHp <= 0) { st = GameState::Lost; return; }
    if (::playerWon)      st = GameState::Won;
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

        // movement: stop while touching an enemy; clamp to prevent crossing
        Unit* target = blockingEnemy(p);
        p.fighting = (target != nullptr);
        if (d.speed > 0.0f && !p.fighting) {
            p.x += d.speed * dt;
            p.x = std::min(p.x, GRID_RIGHT - unitHalfWidth(p.def));
        }
        // hard clamp: never let a player overlap an enemy regardless of frame rate
        for (Unit& e : enemies) {
            if (e.lane == p.lane && e.hp > 0) {
                float contact = unitHalfWidth(p.def) + unitHalfWidth(e.def);
                if (e.x - p.x < contact) p.x = e.x - contact;
            }
        }

        // reached the end of the lane: refund 2x the card price and leave the field
        // (defense players never move, so they never get here)
        if (d.role != Role::Defense && d.speed > 0.0f &&
            p.x >= GRID_RIGHT - unitHalfWidth(p.def) - 0.01f) {
            int reward = 2 * d.cost;
            coins += reward;

            RewardPopup pop;
            pop.lane = p.lane;
            pop.x = p.x;
            pop.amount = reward;
            popups.push_back(pop);

            p.hp = 0;   // removed in cleanup(); players never drop collectibles
            continue;
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

    for (RewardPopup& pop : popups) pop.age += dt;
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
                --baseHp;
                e.hp = 0;              // mark for removal; cleanup() will do the rest
                e.reachedBase = true;  // ...but it was not defeated, so no coin drop
                continue;
            }
        }
        // hard clamp: never let an enemy cross a player
        for (Unit& p : players) {
            if (p.lane == e.lane && p.hp > 0) {
                float contact = unitHalfWidth(e.def) + unitHalfWidth(p.def);
                if (e.x - p.x < contact) e.x = p.x + contact;
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
        pr.prevX = pr.x;
        pr.x += pr.vx * dt;

        // swept-segment helpers: min/max x the bolt passed through this frame
        float segMin = std::min(pr.prevX, pr.x);
        float segMax = std::max(pr.prevX, pr.x);

        if (!pr.fromEnemy) {
            for (Unit& e : enemies) {
                if (e.lane != pr.lane || e.hp <= 0) continue;
                float bMin = e.x - unitHalfWidth(e.def);
                float bMax = e.x + unitHalfWidth(e.def);
                if (segMax >= bMin && segMin <= bMax) {
                    e.hp -= pr.damage;
                    pr.alive = false;
                    break;
                }
            }
            if (pr.x > WORLD_W + 60.0f) pr.alive = false;
            continue;
        }

        // enemy projectile: defense player soaks it first
        Unit* hit = nullptr;
        for (Unit& p : players) {
            if (p.lane != pr.lane || p.hp <= 0 || g_chars[p.def].role != Role::Defense) continue;
            float bMin = p.x - unitHalfWidth(p.def);
            float bMax = p.x + unitHalfWidth(p.def);
            if (segMax >= bMin && segMin <= bMax) { hit = &p; break; }
        }
        if (!hit) {
            for (Unit& p : players) {
                if (p.lane != pr.lane || p.hp <= 0) continue;
                float bMin = p.x - unitHalfWidth(p.def);
                float bMax = p.x + unitHalfWidth(p.def);
                if (segMax < bMin || segMin > bMax) continue;
                bool dodged = pr.high ? (p.duckTimer > 0.0f) : (p.jumpY > 12.0f);
                if (dodged) continue;
                hit = &p;
                break;
            }
        }
        if (hit) {
            hit->hp -= pr.damage;
            pr.alive = false;
        }
        if (pr.x < -60.0f) pr.alive = false;
    }
}

void Game::cleanup()
{
    // defeated enemies drop a collectible where they fell
    // (enemies that walked into the base are removed without a drop)
    for (const Unit& e : enemies)
        if (e.hp <= 0 && !e.reachedBase) {
            Collectible c;
            c.lane = e.lane;
            c.x = e.x;
            collectibles.push_back(c);
        }

    // Notify wave system of each defeated enemy (match by type only — lanes differ)
    for (const Unit& e : enemies) {
        if (e.hp <= 0) {
            EnemyType et = (e.def == CH_EASY) ? EnemyType::Easy :
                (e.def == CH_MEDIUM) ? EnemyType::Medium :
                EnemyType::Hard;
            for (auto& we : ::enemies) {
                if (we.active && we.type == et) {
                    we.active = false;
                    break;
                }
            }
        }
    }
    removeDefeatedEnemies();

    auto dead = [](const Unit& u) { return u.hp <= 0; };
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), dead), enemies.end());
    players.erase(std::remove_if(players.begin(), players.end(), dead), players.end());
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(),
        [](const Projectile& p) { return !p.alive; }), projectiles.end());
    collectibles.erase(std::remove_if(collectibles.begin(), collectibles.end(),
        [](const Collectible& c) { return !c.alive; }), collectibles.end());
    popups.erase(std::remove_if(popups.begin(), popups.end(),
        [](const RewardPopup& p) { return p.age >= POPUP_TIME; }), popups.end());
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
    // Pause-menu volume buttons (LMB only)
    if (st == GameState::Paused && button == GLFW_MOUSE_BUTTON_LEFT) {
        if (BTN_VOL_DOWN.hit(wx, wy)) { changeVolume(-10); return; }
        if (BTN_VOL_UP.hit(wx, wy)) { changeVolume(+10); return; }
        if (BTN_MUTE.hit(wx, wy)) { toggleMute();      return; }
    }

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
    // Pause-menu audio controls
    if (st == GameState::Paused) {
        if (key == GLFW_KEY_LEFT)  changeVolume(-10);
        else if (key == GLFW_KEY_RIGHT) changeVolume(+10);
        else if (key == GLFW_KEY_M)     toggleMute();
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

    for (const Unit& p : players) drawUnit(r, p, false);
    for (const Unit& e : enemies) drawUnit(r, e, true);

    // "+100" popups at the lane end: rise and fade out
    for (const RewardPopup& pop : popups) {
        float t = std::clamp(pop.age / POPUP_TIME, 0.0f, 1.0f);
        std::string txt = "+" + std::to_string(pop.amount);
        float tx = std::min(pop.x, GRID_RIGHT - r.textWidth(txt, 3.0f)) - r.textWidth(txt, 3.0f) * 0.5f;
        float ty = laneBottom(pop.lane) + LANE_H * 0.55f + t * 30.0f;
        r.drawText(txt, tx, ty, 3.0f, { 1.00f, 0.85f, 0.25f }, 1.0f - t);
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

    // Base HP bar (centred on the home-base column)
    {
        float baseMidY = (GRID_TOP + laneBottom(NUM_LANES - 1)) * 0.5f;
        drawHealthBar(r, 70.0f, baseMidY + 20.0f, 110.0f, baseHp, BASE_MAX_HP, false);
        r.drawText("BASE", 70.0f - r.textWidth("BASE", 2.5f) * 0.5f, baseMidY + 36.0f, 2.5f, { 0.80f, 0.85f, 0.90f });
    }

    // HUD
    drawCoinPanel(r, coins);
    cards.draw(r, coins, armedCard, hoveredCard);

    std::string wave = "WAVE " + std::to_string(currentWave) + "/" + std::to_string(WAVE_TOTAL);
    r.drawText(wave, WORLD_W - r.textWidth(wave, 4.0f) - 30.0f, 660.0f, 4.0f, { 0.90f, 0.95f, 1.00f });
    r.drawText("CLICK CARD THEN LANE.  CLICK LANE TO SELECT.", 490, 650, 2.0f, { 0.60f, 0.70f, 0.80f });
    r.drawText("W JUMP  S DUCK  ESC PAUSE  RIGHT CLICK CANCEL", 490, 630, 2.0f, { 0.60f, 0.70f, 0.80f });

    // overlays
    if (st != GameState::Playing) {
        r.drawRect(0, 0, WORLD_W, WORLD_H, { 0.0f, 0.0f, 0.0f }, 0.6f);
        if (st == GameState::Paused) {
            drawCenteredText(r, "PAUSED", 400, 10.0f, { 1, 1, 1 });
            drawCenteredText(r, "ESC RESUME   R RESTART", 320, 4.0f, { 0.8f, 0.85f, 0.9f });
            // Volume control: [-]  MUSIC 60%  [+]   then a MUTE / UNMUTE button
            drawButton(r, BTN_VOL_DOWN, "-");
            drawButton(r, BTN_VOL_UP, "+");
            std::string vol = musicMuted ? "MUSIC OFF" : "MUSIC " + std::to_string(musicVolPct) + "%";
            drawCenteredText(r, vol, 254, 4.0f, { 0.75f, 0.85f, 0.95f });
            drawButton(r, BTN_MUTE, musicMuted ? "UNMUTE" : "MUTE");
            drawCenteredText(r, "LEFT/RIGHT VOLUME   M MUTE", 160, 2.0f, { 0.55f, 0.65f, 0.75f });
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

void Game::applyVolume()
{
    Audio::setMusicVolume(musicMuted ? 0.0f : musicVolPct * 0.01f);
}

void Game::changeVolume(int deltaPct)
{
    musicVolPct = std::clamp(musicVolPct + deltaPct, 0, 100);
    musicMuted = false;          // touching the volume always unmutes
    applyVolume();
}

void Game::toggleMute()
{
    musicMuted = !musicMuted;    // musicVolPct is kept, so unmuting restores it
    applyVolume();
}