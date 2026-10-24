#include "UI.h"
#include "Characters.h"
#include "Config.h"

#include <algorithm>
#include <cmath>

using namespace cfg;

// ---------------------------------------------------------------------------
// Card bar
// ---------------------------------------------------------------------------
void CardBar::cardRect(int i, float& x, float& y) const
{
    x = x0 + i * (w + gap);
    y = y0;
}

int CardBar::hitTest(float wx, float wy) const
{
    for (int i = 0; i < NUM_HERO_TYPES; ++i) {
        float x, y;
        cardRect(i, x, y);
        if (wx >= x && wx <= x + w && wy >= y && wy <= y + h) return i;
    }
    return -1;
}

void CardBar::draw(Renderer& r, int coins, int armedCard, int hoveredCard) const
{
    for (int i = 0; i < NUM_HERO_TYPES; ++i) {
        float x, y;
        cardRect(i, x, y);
        const CharacterDef& d = g_chars[i];
        bool unlocked = coins >= d.cost;

        glm::vec3 border = { 0.30f, 0.34f, 0.40f };
        if (unlocked && hoveredCard == i) border = { 0.90f, 0.95f, 1.00f };
        if (unlocked && armedCard == i)   border = { 1.00f, 0.80f, 0.20f };

        r.drawRect(x - 3, y - 3, w + 6, h + 6, border);
        r.drawRect(x, y, w, h, unlocked ? glm::vec3(0.10f, 0.22f, 0.36f) : glm::vec3(0.16f, 0.16f, 0.16f));

        // portrait: fit the sprite into its own box, grayscale when locked
        //                          MELEE   RANGED  DEFENSE
        static const float boxH[] = { 1.5f, 2.2f, 1.90f };   // max portrait height
        static const float boxW[] = { 1.50f, 2.20f, 1.20f };   // max portrait width
        static const float feetY[] = { 1.70f, 1.80f, 1.0f };  // feet height above card bottom
        static const float offX[] = { -40.0f, -40.0f, -45.0f };   // sideways shift (negative = left)

        const SpriteFrame& f = g_sprites[d.frameA];
        float s = std::min(boxH[i] / f.h, boxW[i] / f.w);
        DrawFx fx;
        fx.gray = unlocked ? 0.0f : 1.0f;
        r.drawSprite(f, x + w / 2.0f + offX[i], y + feetY[i], s, s, false, fx);
        std::string cost = std::to_string(d.cost);
        float tw = r.textWidth(cost, 3.0f);
        r.drawText(cost, x + (w - tw) / 2.0f, y + 7.0f, 3.0f,
            unlocked ? glm::vec3(1.0f, 0.88f, 0.45f) : glm::vec3(0.55f));
    }
}

// ---------------------------------------------------------------------------
// Battlefield: background, lanes, home base, enemy gate
// ---------------------------------------------------------------------------
void drawBattlefield(Renderer& r, int hoveredLane, const bool* selectedLanes, float time)
{
    const float gridBottom = laneBottom(NUM_LANES - 1);
    const float gridW = GRID_RIGHT - GRID_LEFT;
    const float cellW = gridW / GRID_COLS;
    const float pulse = 0.65f + 0.35f * std::sin(time * 3.0f);

    // background + top bar
    r.drawRect(0, 0, WORLD_W, WORLD_H, { 0.05f, 0.07f, 0.11f });
    r.drawRect(0, 606, WORLD_W, WORLD_H - 606, { 0.07f, 0.11f, 0.19f });
    r.drawRect(0, 604, WORLD_W, 3, { 0.20f, 0.55f, 0.75f });

    // grid frame
    r.drawRect(GRID_LEFT - 4, gridBottom - 4, gridW + 8, GRID_TOP - gridBottom + 8, { 0.02f, 0.03f, 0.05f });

    // tiles: tech floor on the hero side, mossy stone on the enemy side
    for (int lane = 0; lane < NUM_LANES; ++lane) {
        float y = laneBottom(lane);
        for (int c = 0; c < GRID_COLS; ++c) {
            float x = GRID_LEFT + c * cellW;
            bool tech = c < GRID_COLS / 2;
            bool alt = (lane + c) % 2 == 1;
            glm::vec3 col = tech
                ? (alt ? glm::vec3(0.10f, 0.30f, 0.45f) : glm::vec3(0.12f, 0.35f, 0.52f))
                : (alt ? glm::vec3(0.18f, 0.24f, 0.15f) : glm::vec3(0.21f, 0.28f, 0.17f));
            r.drawRect(x + 1, y + 1, cellW - 2, LANE_H - 2, col);
            // small detail per tile: rivets on tech tiles, cracks on stone
            glm::vec3 detail = col * 0.7f;
            if (tech) {
                r.drawRect(x + 6, y + 6, 4, 4, detail);
                r.drawRect(x + cellW - 10, y + LANE_H - 10, 4, 4, detail);
            }
            else {
                r.drawRect(x + 20, y + 30, 18, 3, detail);
                r.drawRect(x + 36, y + 26, 3, 8, detail);
            }
        }
    }

    // lane highlights
    for (int lane = 0; lane < NUM_LANES; ++lane) {
        float y = laneBottom(lane);
        if (selectedLanes[lane]) {
            r.drawRect(GRID_LEFT, y, gridW, LANE_H, { 0.30f, 0.90f, 1.00f }, 0.22f);
            r.drawRect(GRID_LEFT, y, 6, LANE_H, { 0.30f, 0.90f, 1.00f });
        }
        if (lane == hoveredLane)
            r.drawRect(GRID_LEFT, y, gridW, LANE_H, { 1.0f, 1.0f, 1.0f }, 0.12f);
    }

    // home base: wall, glowing tower, one turret per lane
    r.drawRect(0, gridBottom - 4, GRID_LEFT - 6, GRID_TOP - gridBottom + 8, { 0.22f, 0.26f, 0.32f });
    r.drawRect(10, gridBottom + 6, 60, GRID_TOP - gridBottom - 12, { 0.32f, 0.38f, 0.46f });
    r.drawRect(26, gridBottom + 20, 28, GRID_TOP - gridBottom - 40,
        { 0.20f * pulse, 0.70f * pulse, 0.95f * pulse });
    for (int lane = 0; lane < NUM_LANES; ++lane) {
        float y = laneBottom(lane) + LANE_H * 0.30f;
        r.drawRect(78, y, 44, 30, { 0.42f, 0.47f, 0.55f });
        r.drawRect(86, y + 8, 14, 14, { 0.25f * pulse, 0.85f * pulse, 1.0f * pulse });
        r.drawRect(78, y - 6, 44, 6, { 0.20f, 0.23f, 0.28f });
    }

    // enemy gate on the right edge
    r.drawRect(GRID_RIGHT + 4, gridBottom - 4, WORLD_W - GRID_RIGHT - 4, GRID_TOP - gridBottom + 8,
        { 0.12f, 0.16f, 0.10f });

    // floor strip under the grid
    r.drawRect(0, 0, WORLD_W, gridBottom - 8, { 0.04f, 0.05f, 0.08f });
}

// ---------------------------------------------------------------------------
// HUD pieces
// ---------------------------------------------------------------------------
void drawCoinPanel(Renderer& r, int coins)
{
    r.drawRect(17, 612, 131, 101, { 0.30f, 0.34f, 0.40f });
    r.drawRect(20, 615, 125, 95, { 0.06f, 0.14f, 0.24f });

    // coin icon
    const SpriteFrame& cf = g_sprites[SPR_COIN];
    const float COIN_SIZE =7.0f;              // lower = smaller
    const float COIN_X = 50.0f;             // coin position  (lower = further left)
    const float TEXT_X = 82.0f;             // number position (stays centred in the panel)
    float cs = COIN_SIZE / cf.h;
    r.drawSprite(cf, COIN_X, 660, cs, cs, false);

    // coin count
    std::string s = std::to_string(coins);
    float tw = r.textWidth(s, 4.0f);
    r.drawText(s, TEXT_X - tw / 2.0f, 625, 4.0f, { 1.0f, 0.88f, 0.45f });
}
void drawHealthBar(Renderer& r, float cx, float y, float width, int hp, int maxHp, bool isEnemy)
{
    float ratio = std::clamp((float)hp / (float)maxHp, 0.0f, 1.0f);
    float x = cx - width / 2.0f;
    r.drawRect(x - 2, y - 2, width + 4, 10, { 0.0f, 0.0f, 0.0f }, 0.85f);
    r.drawRect(x, y, width * ratio, 6,
        isEnemy ? glm::vec3(0.95f, 0.30f, 0.20f) : glm::vec3(0.30f, 0.95f, 0.45f));
    // one notch per hit point so remaining health is easy to count
    for (int i = 1; i < maxHp; ++i)
        r.drawRect(x + width * i / maxHp - 1, y, 2, 6, { 0.0f, 0.0f, 0.0f }, 0.6f);
}

void drawCenteredText(Renderer& r, const std::string& s, float y, float px, glm::vec3 color)
{
    r.drawText(s, (WORLD_W - r.textWidth(s, px)) / 2.0f, y, px, color);
}
