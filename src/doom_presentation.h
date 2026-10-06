#pragma once

#include "host_main.h"

void DoomPresentationReset(void);
void DoomPresentationBeforeFrame(void);
void DoomPresentationPrepare(int drawable_w, int drawable_h, int *width, int *height);
void DoomPresentationBegin(unsigned number);
void DoomPresentationEnd(const uint8_t *field, unsigned number);
int DoomPresentationDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                         int width, int height, double alpha);
double DoomPresentationRate(double refresh);
int DoomPresentationKeepDebt(void);
int DoomPresentationWindowWidth(int width);
void DoomPresentationViewport(int width, int height, int drawable_w, int drawable_h,
                             SnesDisplayViewport *viewport);
