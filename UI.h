#pragma once
#include "Renderer.h"
#include <string>

// The bar of player cards at the top of the screen
struct CardBar {
    float x0 = 165.0f;   // left edge of the first card
    float y0 = 615.0f;   // bottom edge of the cards
    float w = 85.0f, h = 95.0f, gap = 12.0f;

    void cardRect(int i, float& x, float& y) const;
    int  hitTest(float wx, float wy) const;   // card index or -1
    void draw(Renderer& r, int coins, int armedCard, int hoveredCard) const;
};

void drawBattlefield(Renderer& r, int hoveredLane, const bool* selectedLanes, float time);
void drawCoinPanel(Renderer& r, int coins);
void drawHealthBar(Renderer& r, float centerX, float y, float width, int hp, int maxHp, bool isEnemy);
void drawCenteredText(Renderer& r, const std::string& s, float y, float px, glm::vec3 color);
