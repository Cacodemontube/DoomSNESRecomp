#pragma once
#include "doom_video.h"
int DoomDiagnosticBegin(void);
void DoomDiagnosticVideo(const DoomVideoSettings *settings);
void DoomDiagnosticFinish(int exit_code);
void DoomDiagnosticSimulationWork(void);
