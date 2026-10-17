#pragma once

#include "framework.h"

void DrawMainButton(const DRAWITEMSTRUCT* item);
HWND CreateMainButton(HWND parent, const wchar_t* text, int id);
