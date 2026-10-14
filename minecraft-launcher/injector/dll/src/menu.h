#pragma once
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <windows.h>

struct ModuleState {
    bool keystrokesEnabled;
    bool fpsEnabled;
    bool motionBlurEnabled;
    float motionBlurStrength;
};

void Menu_Init(ID2D1RenderTarget* rt, IDWriteFactory3* dwrite);
void Menu_Draw(ID2D1RenderTarget* rt, float screenW, float screenH);
void Menu_HandleKey(int vkKey);
void Menu_HandleMouse(float x, float y, bool clicked, bool held);
void Menu_HandleWheel(float delta);
bool Menu_IsOpen();
ModuleState Menu_GetState();
void Menu_SetKeybind(int vkKey);
