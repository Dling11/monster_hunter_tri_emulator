#pragma once
#include "rowell_pc_graphics.h"
struct SDL_Window;
void RowellTouch_InitializeUserDir();
void RowellTouch_Attach(SDL_Window* window);
bool RowellTouch_WaitForRestartParent(int& argc,char** argv);
bool RowellTouch_RestartGame();
RowellPcGraphics::Settings RowellTouch_ReadGraphicsSettings();
void RowellTouch_SetTargetAspect(int aspect);
void RowellTouch_SetResolutionScale(float scale);
void RowellTouch_OpenDataFolder();
void RowellTouch_OpenAppFolder();
