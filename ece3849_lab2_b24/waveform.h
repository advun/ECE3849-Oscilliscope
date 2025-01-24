/*
 * waveform.h
 *
 *  Created on: Nov 20, 2024
 *      Author: cwilding
 */

#ifndef WAVEFORM_H_
#define WAVEFORM_H_

#define ADC_OFFSET 2047  //2047 is middle of 4095
#define ADC_BUFFER_SIZE 2048 // size must be a power of 2
#define ADC_BUFFER_WRAP(i) ((i) & (ADC_BUFFER_SIZE - 1)) // index wrapping macro


int FindTrigger(void);


#endif /* WAVEFORM_H_ */
