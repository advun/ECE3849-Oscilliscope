/*
 * sampling.h
 *
 *  Created on: Nov 5, 2024
 *      Author: cwilding
 */

#ifndef SAMPLING_H_
#define SAMPLING_H_

#include <stdint.h>
#include "driverlib/udma.h"


#define ADC_SAMPLING_RATE 1000000   // [samples/sec] desired ADC sampling rate
#define CRYSTAL_FREQUENCY 25000000  // [Hz] crystal oscillator frequency used to calculate clock rates
#define PWM_FREQUENCY 20000 // PWM frequency = 20 kHz
#define speakerPeriod 258

#define ADC_BUFFER_SIZE 2048 // size must be a power of 2
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1)) // index wrapping macro

void initADC(void);
void initSignal(void);

extern volatile int32_t gADCBufferIndex;
extern volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE];
void initDMA(void);
int32_t getADCBufferIndex(void);
void initTimer0A(void);
void initSpeaker(void);


#endif /* SAMPLING_H_ */
