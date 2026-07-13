#include <gba_video.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_dma.h>
#include <gba_sprites.h>
#include <maxmod.h>
//================ASSETS================
#include "../data/assets/ui/logo-bump0.h"
#include "../data/assets/ui/menuBG.h"
#include "../data/assets/ui/freeplay-btn-l.h"
#include "../data/assets/ui/freeplay-btn-r.h"
#include "../data/assets/ui/freeplaySelect-btn-l.h"
#include "../data/assets/ui/freeplaySelect-btn-r.h"
#include "soundbank.h"
#include "soundbank_bin.h"

#define OBJ_TILE_BASE ((u16*)0x06010000)
#define OBJ_PALETTE_MEM ((u16*)0x05000200)
#define FRAME_TILES 64
#define PIECE_TILES (3 * FRAME_TILES)
#define SELECT_BASE (2 * PIECE_TILES)

volatile OBJATTR* oam = (OBJATTR*)0x07000000;

typedef enum { STATE_LOGO, STATE_MAIN_MENU } GameState;
GameState currentState = STATE_LOGO;
GameState lastState = (GameState)-1;

int cyanPaletteIndex = -1;

int pulsePhase = 0;
int pulseDir = 1;
const int PULSE_STEPS = 40;
const int PULSE_FRAME_DIV = 2;
int pulseFrameCounter = 0;

#define PULSE_CYAN_G 31
#define PULSE_CYAN_B 31
#define PULSE_DARKBLUE_G 4
#define PULSE_DARKBLUE_B 16

bool startConfirmed = false;
int confirmTimer = 0;
const int CONFIRM_DURATION = 90;

int blinkCounter = 0;
const int BLINK_INTERVAL = 8;
bool blinkShowWhite = false;

bool isSelected = false;

int bopFrameBtn = 0;
int bopCounterBtn = 0;
const int BOP_INTERVAL = 8;

bool isConfirming = false;
int confirmBtnTimer = 0;
const int CONFIRM_BTN_DURATION = 60;

int flickerCounter = 0;
const int FLICKER_INTERVAL = 4; // era 6 — mais rápido agora
bool flickerVisible = true;

int btnX, btnY;

int findCyanIndex(const u16* pal, int count)
{
    u16 cyan = RGB5(0, 31, 31);
    for (int i = 0; i < count; i++)
        if (pal[i] == cyan) return i;
    return -1;
}

void updateButtonSprites()
{
    bool visible = isConfirming ? flickerVisible : true;
    int selectOffset = isSelected ? SELECT_BASE : 0;

    for (int i = 0; i < 2; i++)
    {
        if (!visible) { oam[i].attr0 = 0x0200; continue; }

        oam[i].attr0 = (btnY & 0xFF) | 0x6000;
        oam[i].attr1 = ((btnX + i * 64) & 0x1FF) | 0xC000;
        oam[i].attr2 = selectOffset + i * PIECE_TILES + bopFrameBtn * FRAME_TILES;
    }
}

void updateMenuButtonLogic()
{
    bopCounterBtn++;
    if (bopCounterBtn >= BOP_INTERVAL) { bopCounterBtn = 0; bopFrameBtn = (bopFrameBtn + 1) % 3; }

    if (isConfirming)
    {
        confirmBtnTimer++;
        flickerCounter++;
        if (flickerCounter >= FLICKER_INTERVAL) { flickerCounter = 0; flickerVisible = !flickerVisible; }
        if (confirmBtnTimer >= CONFIRM_BTN_DURATION) isConfirming = false;
    }

    updateButtonSprites();
}

void resetToLogoState()
{
    startConfirmed = false;
    confirmTimer = 0;
    blinkCounter = 0;
    blinkShowWhite = false;
    pulsePhase = 0;
    pulseDir = 1;
    pulseFrameCounter = 0;
    isSelected = false;
    isConfirming = false;

    for (int i = 0; i < 128; i++) oam[i].attr0 = 0x0200; // esconde sprites do menu

    DMA3COPY(logo_bump0Pal, BG_PALETTE, logo_bump0PalLen / 2);
    DMA3COPY(logo_bump0Bitmap, (u16*)0x06000000, logo_bump0BitmapLen / 2);
    cyanPaletteIndex = findCyanIndex(BG_PALETTE, 256);
}

int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);

    SetMode(MODE_4 | BG2_ENABLE | (1 << 12) | (1 << 6));

    DMA3COPY(logo_bump0Pal, BG_PALETTE, logo_bump0PalLen / 2);
    DMA3COPY(logo_bump0Bitmap, (u16*)0x06000000, logo_bump0BitmapLen / 2);
    cyanPaletteIndex = findCyanIndex(BG_PALETTE, 256);

    DMA3COPY(freeplay_btn_lPal, OBJ_PALETTE_MEM, freeplay_btn_lPalLen / 2);
    DMA3COPY(freeplay_btn_lTiles,       OBJ_TILE_BASE,        freeplay_btn_lTilesLen / 2);
    DMA3COPY(freeplay_btn_rTiles,       OBJ_TILE_BASE + 3072, freeplay_btn_rTilesLen / 2);
    DMA3COPY(freeplaySelect_btn_lTiles, OBJ_TILE_BASE + 6144, freeplaySelect_btn_lTilesLen / 2);
    DMA3COPY(freeplaySelect_btn_rTiles, OBJ_TILE_BASE + 9216, freeplaySelect_btn_rTilesLen / 2);

    for (int i = 0; i < 128; i++) oam[i].attr0 = 0x0200;

    btnX = (240 - 128) / 2;
    btnY = 100;

    //=========================AUDIO INIT========================
    mmInitDefault((mm_addr)soundbank_bin, 24);
    mmStart(MOD_FREAKYMENU, MM_PLAY_LOOP);

    while (1) {
        VBlankIntrWait();
        mmFrame();
        scanKeys();
        u16 keysD = keysDown();

        switch (currentState)
        {
            case STATE_LOGO:
            {
                if (!startConfirmed)
                {
                    pulseFrameCounter++;
                    if (pulseFrameCounter >= PULSE_FRAME_DIV)
                    {
                        pulseFrameCounter = 0;
                        pulsePhase += pulseDir;
                        if (pulsePhase >= PULSE_STEPS) { pulsePhase = PULSE_STEPS; pulseDir = -1; }
                        else if (pulsePhase <= 0) { pulsePhase = 0; pulseDir = 1; }

                        if (cyanPaletteIndex >= 0)
                        {
                            int g = PULSE_CYAN_G - ((PULSE_CYAN_G - PULSE_DARKBLUE_G) * pulsePhase) / PULSE_STEPS;
                            int b = PULSE_CYAN_B - ((PULSE_CYAN_B - PULSE_DARKBLUE_B) * pulsePhase) / PULSE_STEPS;
                            BG_PALETTE[cyanPaletteIndex] = RGB5(0, g, b);
                        }
                    }

                    if (keysD & KEY_START)
                    {
                        startConfirmed = true;
                        confirmTimer = 0;
                        blinkCounter = 0;
                        blinkShowWhite = false;
                    }
                }
                else
                {
                    confirmTimer++;
                    blinkCounter++;
                    if (blinkCounter >= BLINK_INTERVAL)
                    {
                        blinkCounter = 0;
                        blinkShowWhite = !blinkShowWhite;
                        if (cyanPaletteIndex >= 0)
                            BG_PALETTE[cyanPaletteIndex] = blinkShowWhite ? RGB5(31,31,31) : RGB5(0,31,31);
                    }

                    if (confirmTimer >= CONFIRM_DURATION)
                        currentState = STATE_MAIN_MENU;
                }

                break;
            }

            case STATE_MAIN_MENU:
            {
                updateMenuButtonLogic();

                if (!isSelected && (keysD & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)))
                    isSelected = true;

                if (isSelected && !isConfirming && (keysD & KEY_A))
                {
                    isConfirming = true;
                    confirmBtnTimer = 0;
                    flickerCounter = 0;
                    flickerVisible = true;
                    mmEffect(SFX_CONFIRMMENU);
                }

                if (keysD & KEY_B)
                {
                    mmEffect(SFX_CANCELMENU);
                    currentState = STATE_LOGO;
                }

                break;
            }
        }

        if (currentState != lastState)
        {
            if (currentState == STATE_MAIN_MENU)
            {
                DMA3COPY(menuBGPal, BG_PALETTE, menuBGPalLen / 2);
                DMA3COPY(menuBGBitmap, (u16*)0x06000000, menuBGBitmapLen / 2);
            }
            else if (currentState == STATE_LOGO)
            {
                resetToLogoState();
            }

            lastState = currentState;
        }
    }
}