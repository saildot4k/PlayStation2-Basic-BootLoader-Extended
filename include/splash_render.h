#ifndef SPLASH_RENDER_H
#define SPLASH_RENDER_H

#include <tamtypes.h>

int SplashRenderBegin(int logo_disp, int is_psx_desr);
void SplashRenderDrawTextPx(int x, int y, u32 color, const char *text);
int SplashRenderIsActive(void);
void SplashRenderEnd(void);
int SplashRenderGetScreenWidth(void);
int SplashRenderGetScreenHeight(void);
int SplashRenderGetScreenCenterX(void);
int SplashRenderGetScreenCenterY(void);
int SplashRenderGetHotkeysX(void);
int SplashRenderGetHotkeysY(void);

#endif
