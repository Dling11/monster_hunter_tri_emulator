#pragma once
struct SDL_Window;
void RowellTouch_InitializeUserDir();
void RowellTouch_Attach(SDL_Window* window);
bool RowellTouch_WaitForRestartParent(int& argc,char** argv);
bool RowellTouch_RestartGame();
