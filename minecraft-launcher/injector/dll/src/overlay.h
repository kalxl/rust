#pragma once
#include <d2d1_3.h>
#include <dwrite_3.h>

void Overlay_Init(ID2D1RenderTarget* rt, IDWriteFactory3* dwrite);
void Overlay_Draw(ID2D1RenderTarget* rt);
