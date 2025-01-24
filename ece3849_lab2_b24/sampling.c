/*
 * sampling.c
 *
 *  Created on: Nov 5, 2024
 *      Author: cwilding
 */

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "inc/hw_memmap.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/adc.h"
#include "sysctl_pll.h"
#include "inc/tm4c1294ncpdt.h"
#include "sampling.h"
#include "Crystalfontz128x128_ST7735.h"
#include "driverlib/pin_map.h"
#include "driverlib/pwm.h"
#include <xdc/cfg/global.h> //needed for gate object
#include "audio_waveform.h"
#include "driverlib/fpu.h"

extern volatile int32_t gADCBufferIndex; // latest sample index
extern volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE]; // circular buffer
volatile uint32_t gADCErrors = 0; // number of missed ADC deadlines
uint32_t gADCSamplingRate;      // [Hz] actual ADC sampling rate
extern uint32_t gSystemClock;
volatile bool gDMAPrimary = true; // is DMA occurring in the primary channel?
uint32_t countPrev; //previous count value
extern uint32_t timerPeriod;
uint32_t pwmPeriod;
uint32_t countCurr;
uint32_t gPWMSample = 0; // PWM sample counter
float gSamplingRateDivider; // sampling rate divider

//DMA setup
#pragma DATA_ALIGN(gDMAControlTable, 1024) // address alignment required
tDMAControlTable gDMAControlTable[64]; // uDMA control table (global)


    void initADC(void){
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE); //enable peripheral for port e
    GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_0); // GPIO setup for analog input AIN3, Port E pin 0

   // initialize ADC0+1 peripherals
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC1);

    uint32_t pll_frequency = SysCtlFrequencyGet(CRYSTAL_FREQUENCY);
    uint32_t pll_divisor = (pll_frequency - 1) / (16 * ADC_SAMPLING_RATE) + 1; // round divisor up
    gADCSamplingRate = pll_frequency / (16 * pll_divisor); // actual sampling rate may differ from ADC_SAMPLING_RATE
    ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, pll_divisor); // only ADC0 has PLL clock divisor control
    ADCClockConfigSet(ADC1_BASE, ADC_CLOCK_SRC_PLL | ADC_CLOCK_RATE_FULL, pll_divisor);


    // initialize ADC sampling sequence
    ADCSequenceDisable(ADC0_BASE, 0);
    ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH13);                             // Joystick HOR(X)
    ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH17 | ADC_CTL_IE | ADC_CTL_END);  // Joystick VER(Y)
    ADCSequenceEnable(ADC0_BASE, 0);

    ADCSequenceDisable(ADC1_BASE, 0); // choose ADC1 sequence 0; disable before configuring
    ADCSequenceConfigure(ADC1_BASE, 0, ADC_TRIGGER_ALWAYS, 0); // specify the "Always" trigger
    ADCSequenceStepConfigure(ADC1_BASE, 0, 0, ADC_CTL_CH3|ADC_CTL_END|ADC_CTL_IE);// in the 0th step, sample channel 3 (AIN3)

    // enable interrupt, and make it the end of sequence
    ADCSequenceEnable(ADC1_BASE, 0); // enable the sequence. it is now sampling
    //ADCIntEnable(ADC1_BASE, 0); // enable sequence 0 interrupt in the ADC1 peripheral, needed for single sample
    }



    void ADC_ISR(void)
    { /* Single sample code
        ADC1_ISC_R = ADC_ISC_IN0; // clear ADC1 sequence0 interrupt flag in the ADCISC register by writing a 1 to bit 0
    if (ADC1_OSTAT_R & ADC_OSTAT_OV0) { // check for ADC FIFO overflow
        gADCErrors++; // count errors
        ADC1_OSTAT_R = ADC_OSTAT_OV0; // clear overflow condition
    }
    gADCBuffer[gADCBufferIndex = ADC_BUFFER_WRAP(gADCBufferIndex + 1)] = ADC1_SSFIFO0_R; // read sample from the ADC1 sequence 0 FIFO
     */
        //DMA code
        ADCIntClearEx(ADC1_BASE, ADC_INT_DMA_SS0); // clear the ADC1 sequence 0 DMA interrupt flag
        // Check the primary DMA channel for end of transfer, and restart if needed.
        if (uDMAChannelModeGet(UDMA_SEC_CHANNEL_ADC10 | UDMA_PRI_SELECT) ==
        UDMA_MODE_STOP) {
        uDMAChannelTransferSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_PRI_SELECT,
        UDMA_MODE_PINGPONG, (void*)&ADC1_SSFIFO0_R,
        (void*)&gADCBuffer[0], ADC_BUFFER_SIZE/2); // restart the primary channel (same as setup)
        gDMAPrimary = false; // DMA is currently occurring in the alternate buffer
        }


        // Check the alternate DMA channel for end of transfer, and restart if needed.
        if (uDMAChannelModeGet(UDMA_SEC_CHANNEL_ADC10|UDMA_ALT_SELECT) ==  //if the mode of the alternate channel of ADC1 SS0 =
        UDMA_MODE_STOP) {
            uDMAChannelTransferSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_ALT_SELECT,
                    UDMA_MODE_PINGPONG, (void*)&ADC1_SSFIFO0_R,
                    (void*)&gADCBuffer[ADC_BUFFER_SIZE/2], ADC_BUFFER_SIZE/2); // restart the alternate channel (same as setup)
            gDMAPrimary = true; // DMA is currently occurring in the primary buffer
        }


        // The DMA channel may be disabled if the CPU is paused by the debugger.
        if (!uDMAChannelIsEnabled(UDMA_SEC_CHANNEL_ADC10)) {
        uDMAChannelEnable(UDMA_SEC_CHANNEL_ADC10); // re-enable the DMA channel
        }

    }


    void initSignal(void){
        // configure M0PWM2, at GPIO PF2, which is BoosterPack 1 header C1 (2nd from right) pin 2
        // configure M0PWM3, at GPIO PF3, which is BoosterPack 1 header C1 (2nd from right) pin 3
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
        GPIOPinTypePWM(GPIO_PORTF_BASE, GPIO_PIN_2 | GPIO_PIN_3); // PF2 = M0PWM2, PF3 = M0PWM3
        GPIOPinConfigure(GPIO_PF2_M0PWM2);
        GPIOPinConfigure(GPIO_PF3_M0PWM3);
        GPIOPadConfigSet(GPIO_PORTF_BASE, GPIO_PIN_2 | GPIO_PIN_3, GPIO_STRENGTH_2MA,
        GPIO_PIN_TYPE_STD);
        // configure the PWM0 peripheral, gen 1, outputs 2 and 3
        SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
        PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_1); // use system clock without division
        PWMGenConfigure(PWM0_BASE, PWM_GEN_1, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
        PWMGenPeriodSet(PWM0_BASE, PWM_GEN_1, pwmPeriod);
        PWMPulseWidthSet(PWM0_BASE, PWM_OUT_2,
        roundf((float)pwmPeriod*0.4f)); // 40% duty cycle
        PWMPulseWidthSet(PWM0_BASE, PWM_OUT_3,
        roundf((float)pwmPeriod*0.4f));
        PWMOutputState(PWM0_BASE, PWM_OUT_2_BIT | PWM_OUT_3_BIT, true);
        PWMGenEnable(PWM0_BASE, PWM_GEN_1);
    }

    void initSpeaker(void){
        //GPIO pin PG1 as M0PWM5
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);
        GPIOPinTypePWM(GPIO_PORTG_BASE, GPIO_PIN_1);
        GPIOPinConfigure(GPIO_PG1_M0PWM5);
        GPIOPadConfigSet(GPIO_PORTG_BASE,  GPIO_PIN_1,
        GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
        // initialize the PWM0 generator 2, output 5
        PWMIntDisable(PWM0_BASE, PWM_INT_GEN_2);
        SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
        PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_1); // use system clock without division
        PWMGenConfigure(PWM0_BASE, PWM_GEN_2, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC);
        PWMGenPeriodSet(PWM0_BASE, PWM_GEN_2, speakerPeriod);
        PWMPulseWidthSet(PWM0_BASE, PWM_OUT_5, (speakerPeriod*0.5)); //50% duty cycle
        PWMOutputState(PWM0_BASE, PWM_OUT_5_BIT, true);
        PWMGenEnable(PWM0_BASE, PWM_GEN_2);
        PWMGenIntTrigEnable(PWM0_BASE, PWM_GEN_2, PWM_INT_CNT_ZERO);


        gSamplingRateDivider = roundf((float)gSystemClock/(speakerPeriod*AUDIO_SAMPLING_RATE));
    }

    void initDMA(void){
        SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
        uDMAEnable();
        uDMAControlBaseSet(gDMAControlTable);
        uDMAChannelAssign(UDMA_CH24_ADC1_0); // assign DMA channel 24 to ADC1 sequence 0
        uDMAChannelAttributeDisable(UDMA_SEC_CHANNEL_ADC10, UDMA_ATTR_ALL);

        // primary DMA channel = first half of the ADC buffer
        uDMAChannelControlSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_PRI_SELECT,
        UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_4);
        uDMAChannelTransferSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_PRI_SELECT,
        UDMA_MODE_PINGPONG, (void*)&ADC1_SSFIFO0_R,
        (void*)&gADCBuffer[0], ADC_BUFFER_SIZE/2);

        // alternate DMA channel = second half of the ADC buffer
        uDMAChannelControlSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_ALT_SELECT,
        UDMA_SIZE_16 | UDMA_SRC_INC_NONE | UDMA_DST_INC_16 | UDMA_ARB_4);
        uDMAChannelTransferSet(UDMA_SEC_CHANNEL_ADC10 | UDMA_ALT_SELECT,
        UDMA_MODE_PINGPONG, (void*)&ADC1_SSFIFO0_R,
        (void*)&gADCBuffer[ADC_BUFFER_SIZE/2], ADC_BUFFER_SIZE/2);
        uDMAChannelEnable(UDMA_SEC_CHANNEL_ADC10);

        ADCSequenceDMAEnable(ADC1_BASE, 0); // enable DMA for ADC1 sequence 0
        ADCIntEnableEx(ADC1_BASE, ADC_INT_DMA_SS0); // enable ADC1 sequence 0 DMA interrupt
    }



int32_t getADCBufferIndex(void){

    int32_t index;
    IArg Hwikey; // key for Hwi Gate
    Hwikey = GateHwi_enter(gateHwi0); //enters hwigate, allowing exclusive cpu access
    if (gDMAPrimary)
    { // DMA is currently in the primary channel
        index = ADC_BUFFER_SIZE / 2 - 1
                - uDMAChannelSizeGet(UDMA_SEC_CHANNEL_ADC10 | UDMA_PRI_SELECT);
    }
    else
    { // DMA is currently in the alternate channel
        index = ADC_BUFFER_SIZE - 1
                - uDMAChannelSizeGet(UDMA_SEC_CHANNEL_ADC10 | UDMA_ALT_SELECT);
    }
    GateHwi_leave(gateHwi0, Hwikey); //leaves gate, everything else can run as normal
    return index;

}

void initTimer0A(void){
    // configure GPIO PD0 as timer input T0CCP0 at BoosterPack Connector #1 pin 14
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeTimer(GPIO_PORTD_BASE, GPIO_PIN_0);
    GPIOPinConfigure(GPIO_PD0_T0CCP0);

    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
    TimerDisable(TIMER0_BASE, TIMER_BOTH);
    TimerConfigure(TIMER0_BASE, TIMER_CFG_SPLIT_PAIR | TIMER_CFG_A_CAP_TIME_UP);
    TimerControlEvent(TIMER0_BASE, TIMER_A, TIMER_EVENT_POS_EDGE); //Event is positive edge of signal
    TimerLoadSet(TIMER0_BASE, TIMER_A, 0xffff); // use maximum load value
    TimerPrescaleSet(TIMER0_BASE, TIMER_A, 0xff); // use maximum prescale value
    TimerIntEnable(TIMER0_BASE, TIMER_CAPA_EVENT);
    TimerEnable(TIMER0_BASE, TIMER_A);
}

void Timer0A_ISR(void){ //calculates timerPeriod
    TIMER0_ICR_R = TIMER_ICR_CAECINT; //set the Timer A Capture Mode Event bit high in Timer 0 isr, clearing event int

    countCurr = TimerValueGet(TIMER0_BASE, TIMER_A);

    timerPeriod = (countCurr - countPrev) & 0xFFFFFF;

    countPrev = countCurr;
}

void PWM_ISR(void){ //updates speaker duty cycle
    PWMGenIntClear(PWM0_BASE, PWM_GEN_2, PWM_INT_CNT_ZERO); // clear PWM interrupt flag

    int i = (gPWMSample++) / gSamplingRateDivider; // waveform sample index
    PWM0_2_CMPB_R = 1 + gWaveform[i]; // write directly to the PWM compare B register

    if (i >= gWaveformSize) { // if at the end of the waveform array
    PWMIntDisable(PWM0_BASE, PWM_INT_GEN_2); // disable these interrupts
    gPWMSample = 0; // reset sample index so the waveform starts from the beginning

    }
}
