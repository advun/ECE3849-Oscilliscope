/*
 * draw.c
 *
 *  Created on: Nov 19, 2024
 *      Author: cwilding
 */


#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include "Crystalfontz128x128_ST7735.h"
#include "draw.h"

const char * const TimeScaleStr[] = {
        "20 us"
        };

const char * const VoltageScaleStr[] = {
    "100 mV", "200 mV", "500 mV", " 1 V", " 2 V"
    };

const char * const FreqScaleStr[] = {
        "20 kHz"
        };

const char * const dBScaleStr[] = {
        "20 dB"
        };

char str1[50];   // string buffer
uint16_t r;  //counting variable


// full-screen rectangle
    tRectangle rectPause = {0, 103, 39, 113};  //pause rectangle


    void drawBackground(tContext *Context, tRectangle *rect){
    GrContextForegroundSet(Context, ClrBlack);
    GrRectFill(Context, rect); // fill screen with black
    }

    void drawGrid(tContext *Context){
        GrContextForegroundSet(Context, ClrBlue);
        for (r =0; r < 7; r++){  //draw grid
            GrLineDrawH(Context, 0, LCD_HORIZONTAL_MAX, (4+20*r));  //20x20 grid, starting at 4 as the screen is 128x128
            GrLineDrawV(Context, (4+20*r), 0, LCD_VERTICAL_MAX);
            }
    }

    void drawSpecGrid(tContext *Context){
            GrContextForegroundSet(Context, ClrBlue);
            for (r =0; r < 7; r++){  //draw grid
                GrLineDrawH(Context, 0, LCD_HORIZONTAL_MAX, (20*r));  //20x20 grid, starting at 0, cooresponding to 0 frequency
                GrLineDrawV(Context, (20*r), 0, LCD_VERTICAL_MAX);
                }
        }

    void drawTrigger(tContext *Context){
        GrContextForegroundSet(Context, ClrLightSteelBlue); //Draw Trigger Lines
        GrLineDrawV(Context, (LCD_HORIZONTAL_MAX/2), 0, LCD_VERTICAL_MAX);
        GrLineDrawH(Context, 0, LCD_HORIZONTAL_MAX, (LCD_VERTICAL_MAX/2));
    }


    void drawRiseFall(tContext *Context, uint16_t risefall){
        GrContextForegroundSet(Context, ClrWhite);
        if (risefall == 1){  //draw picture if trigRise is true (i.e. trig is rising)
                GrLineDrawH(Context, 110, 115, 7);
                GrLineDrawH(Context, 115, 120, 0);
                }

        if (risefall == 0){//draw picture if trigRise is false (i.e. trig is falling)
                GrLineDrawH(Context, 110, 115, 0);
                GrLineDrawH(Context, 115, 120, 7);
                }

         //final parts of trigRise drawing
         GrLineDrawV(Context, 115, 0, 7);
         GrCircleDraw(Context, /*x*/ 115, /*y*/ 4, /*radius*/ 1);
    }


    void drawTimeScale(tContext *Context, uint16_t timeindex){
        GrContextForegroundSet(Context, ClrWhite);
        GrStringDraw(Context, TimeScaleStr[timeindex], /*length*/ -1, /*x*/ 0, /*y*/ 0, /*opaque*/ false); //write timescale to screen

    }

    void drawVoltScale(tContext *Context, uint16_t voltindex){
        GrContextForegroundSet(Context, ClrWhite);
        GrStringDraw(Context, VoltageScaleStr[voltindex], /*length*/ -1, /*x*/ 50, /*y*/ 0, /*opaque*/ false); //write voltage scale to screen
    }

    void drawUnsync(tContext *Context){ //draws red circle with outline in right bottom corner if missed trigger
        GrContextForegroundSet(Context, ClrWhite);
        GrCircleDraw(Context, /*x*/ 121, /*y*/ 121, /*radius*/ 6);
        GrContextForegroundSet(Context, ClrRed);
        GrCircleFill(Context, /*x*/ 121, /*y*/ 121, /*radius*/ 5); //draw red circle if unsynched
    }


    void drawPause(tContext *Context){//pause indicator
        GrContextForegroundSet(Context, ClrWhite);
        GrRectFill(Context, &rectPause); // draw background of pause
        GrContextForegroundSet(Context, ClrRed);
        snprintf(str1, sizeof(str1), "Paused");
        GrStringDraw(Context, str1, /*length*/ -1, /*x*/ 2, /*y*/ 105, /*opaque*/ false); //write cpu load to screen
    }

    void drawFreqScale(tContext *Context, uint16_t freqindex){ //draw frequency scale
        GrContextForegroundSet(Context, ClrWhite);
        GrStringDraw(Context, FreqScaleStr[freqindex], /*length*/ -1, /*x*/ 0, /*y*/ 0, /*opaque*/ false);

     }

    void drawdBScale(tContext *Context, uint16_t dbindex){ //draw dB scale
        GrContextForegroundSet(Context, ClrWhite);
        GrStringDraw(Context, dBScaleStr[dbindex], /*length*/ -1, /*x*/ 50, /*y*/ 0, /*opaque*/ false);
    }

    void drawCPULoad(tContext *Context, float load){
        GrContextForegroundSet(Context, ClrWhite);
        snprintf(str1, sizeof(str1), "CPU Load = %.1f%%", load);
        GrStringDraw(Context, str1, /*length*/ -1, /*x*/ 2, /*y*/ 120, /*opaque*/ false);
    }

    void drawFrequency(tContext *Context, int frequency){ //draw measured frequency
        GrContextForegroundSet(Context, ClrWhite);
        snprintf(str1, sizeof(str1), "f = %d Hz", frequency);
        GrStringDraw(Context, str1, /*length*/ -1, /*x*/ 2, /*y*/ 110, /*opaque*/ false); //write cpu load to screen
    }

    void drawPeriod(tContext *Context, int period){
        GrContextForegroundSet(Context, ClrWhite);
        snprintf(str1, sizeof(str1), "T = %d", period);
        GrStringDraw(Context, str1, /*length*/ -1, /*x*/ 80, /*y*/ 110, /*opaque*/ false);
    }
