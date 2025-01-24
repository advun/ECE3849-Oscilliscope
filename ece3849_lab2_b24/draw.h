/*
 * draw.h
 *
 *  Created on: Nov 19, 2024
 *      Author: cwilding
 */

#ifndef DRAW_H_
#define DRAW_H_

#include "Crystalfontz128x128_ST7735.h"

void drawBackground(tContext *Context, tRectangle *rect);
void drawGrid(tContext *Context);
void drawSpecGrid(tContext *Context);
void drawTrigger(tContext *Context);
void drawRiseFall(tContext *Context, uint16_t risefall);
void drawTimeScale(tContext *Context, uint16_t timeindex);
void drawVoltScale(tContext *Context, uint16_t voltindex);
void drawUnsync(tContext *Context);
void drawPause(tContext *Context);
void drawFreqScale(tContext *Context, uint16_t freqindex);
void drawdBScale(tContext *Context, uint16_t dbindex);
void drawCPULoad(tContext *Context, float load);
void drawFrequency(tContext *Context, int frequency);
void drawPeriod(tContext *Context, int period);



#endif /* DRAW_H_ */
