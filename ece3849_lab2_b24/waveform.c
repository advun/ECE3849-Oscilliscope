/*
 * waveform.c
 *
 *  Created on: Nov 20, 2024
 *      Author: cwilding
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "waveform.h"
#include "Crystalfontz128x128_ST7735.h"
#include "sampling.h"

extern bool unsync;
extern bool trigRise;
//extern volatile int32_t gADCBufferIndex; // latest sample index
extern volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE]; // circular buffer

int FindTrigger(void) {// search for trigger
    unsync = 0;  //set unsync flag to false
    // Step 1
    //int x = gADCBufferIndex - (LCD_HORIZONTAL_MAX/2)/* half screen behind most recent sample */; //single sample code
    int x = getADCBufferIndex() - (LCD_HORIZONTAL_MAX/2)/* half screen behind most recent sample */;  //DMA code
    int initx = x; //store initial value of x
    // Step 2
    int x_stop = x - ADC_BUFFER_SIZE/2;
    for (; x > x_stop; x--) {
        if (trigRise == 1){ //check for rising trigger
            if ( gADCBuffer[ADC_BUFFER_WRAP(x)] >= ADC_OFFSET && gADCBuffer[ADC_BUFFER_WRAP(x-1)] < ADC_OFFSET){//if x is higher or equal then ADC_OFFSET, and x-1 is lower, rising
                break;
            }
        }
        if (trigRise == 0){  //check for falling trigger
            if ( gADCBuffer[ADC_BUFFER_WRAP(x)] <= ADC_OFFSET && gADCBuffer[ADC_BUFFER_WRAP(x-1)] > ADC_OFFSET){  //if x is lower or equal then ADC_OFFSET, and x-1 is higher, falling
                    break;
                }
        }
    }
    // Step 3
    if (x == x_stop) {// for loop ran to the end
        x = initx; // reset x back to how it was initialized
        unsync = 1;  //raise the unsync flag
    }

    return x;
}
