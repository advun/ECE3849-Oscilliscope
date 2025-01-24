/*
 * ECE 3849 Lab2 starter project
 *
 * Gene Bogdanov    9/13/2017
 */
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/cfg/global.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
//#include <ti/sysbios/knl/Clock.xdc>

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "Crystalfontz128x128_ST7735.h"
#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "driverlib/pwm.h"
#include "driverlib/timer.h"
#include "driverlib/udma.h"
#include "buttons.h"
#include "sampling.h"
#include "draw.h"
#include "waveform.h"
#include "kiss_fft.h"
#include "_kiss_fft_guts.h"


#define ADC_OFFSET 2047  //2047 is middle of 4095
#define PIXELS_PER_DIV 20 // LCD pixels per voltage division = 20
#define VIN_RANGE 3.3  //total V ADC range in volts = 3.3V
#define ADC_BITS 12 //the adc is 12 bit
#define SPEC_SAMPLE 1024 //the number of samples for spectrum mode
#define PI 3.14159265358979f
#define NFFT 1024 // FFT length
#define KISS_FFT_CFG_SIZE (sizeof(struct kiss_fft_state)+sizeof(kiss_fft_cpx)*(NFFT-1))


volatile uint32_t gTime = 8345; // time in hundredths of a second
volatile uint16_t gDisplayBuffer[LCD_HORIZONTAL_MAX]; // display buffer
volatile int32_t out_db[LCD_HORIZONTAL_MAX]; // display buffer for spec
volatile int32_t gSpecDisplayBuffer[SPEC_SAMPLE]; // display buffer for Spec Mode
uint8_t pause = 0; //if 1, pause, if 0 unpause
bool unsync = 0;  //if 1, unsynced, if 0 synced
bool trigRise = 1;  //trigger rising flag
uint8_t voltscalestage = 3; //which voltage scale, starts out at 1 v/div
//volatile int32_t gADCBufferIndex = ADC_BUFFER_SIZE - 1; // latest sample index
volatile uint16_t gADCBuffer[ADC_BUFFER_SIZE]; // circular buffer
uint32_t gSystemClock = 120000000; // [Hz] system clock frequency
volatile int yScaleBuff[LCD_HORIZONTAL_MAX]; // print buffer
uint8_t specMode = 0;  //spectrum mode flag, starts at 0 = oscilloscope mode
int count_unloaded;  //cpuload variable, called before interrupts
int count_loaded;  //cpuload variable, called during interrupts
float cpuload;
uint32_t timerPeriod;
uint32_t pwmPeriod;


void initTimer3(void){
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER3);
    TimerDisable(TIMER3_BASE, TIMER_BOTH);
    TimerConfigure(TIMER3_BASE, TIMER_CFG_ONE_SHOT);
    TimerLoadSet(TIMER3_BASE, TIMER_A, ((gSystemClock/100) - 1)); // 10ms interval Timer Load Value=(Desired Time Interval)×(System Clock Frequency)−1, desired time = 1/100th of a second
}

uint32_t cpu_load_count(void)
{
    uint32_t i = 0;
    TimerIntClear(TIMER3_BASE, TIMER_TIMA_TIMEOUT);
    TimerEnable(TIMER3_BASE, TIMER_A); // start one-shot timer
    while (!(TimerIntStatus(TIMER3_BASE, false) & TIMER_TIMA_TIMEOUT))
        i++;
    return i;
}


/*
 *  ======== main ========
 */
int main(void)
{
    IntMasterDisable();

    // hardware initialization

    Crystalfontz128x128_Init(); // Initialize the LCD display driver
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP); // set screen orientation

    pwmPeriod = roundf((float)gSystemClock/PWM_FREQUENCY);
    initSignal();
    initADC();
    initDMA();
    initSpeaker();
    ButtonInit();
    initTimer3();
    initTimer0A();


    /* Start BIOS */
    BIOS_start();

    return (0);
}


void process_task(UArg arg1, UArg arg2)  //lowest prio
{
    //locals
    float fVoltsPerDiv[] = {0.1, 0.2, 0.5, 1, 2};  //volts per division, default is 1 V/Div

    static char kiss_fft_cfg_buffer[KISS_FFT_CFG_SIZE]; // Kiss FFT config memory
    size_t buffer_size = KISS_FFT_CFG_SIZE;
    kiss_fft_cfg cfg; // Kiss FFT config
    static kiss_fft_cpx in[NFFT], out[NFFT]; // complex waveform and spectrum buffers
    int i;
    cfg = kiss_fft_alloc(NFFT, 0, kiss_fft_cfg_buffer, &buffer_size); // init Kiss FFT

    while (true) {
        Semaphore_pend(process_sem, BIOS_WAIT_FOREVER);//pend on semaphore

        if (specMode == 0){ //if in oscilliscope mode
            //recalc fScale, as checkButtons may have changed
            float fScale = (VIN_RANGE * PIXELS_PER_DIV)/((1 << ADC_BITS) * fVoltsPerDiv[voltscalestage]);

            for (i=0; i < LCD_HORIZONTAL_MAX; i++){
                yScaleBuff[i] = LCD_VERTICAL_MAX/2 - (int)roundf(fScale * (gDisplayBuffer[i] - ADC_OFFSET));  // scale to y coordinate PROCESSING TASK
            }
        }

        if (specMode == 1){ //if in spectrum mode
            for (i = 0; i < NFFT; i++) { // generate an input waveform
                in[i].r = gSpecDisplayBuffer[i]/4096.0f;
                in[i].i = 0; // imaginary part of waveform
            }

            kiss_fft(cfg, in, out); // compute FFT
            // convert first 128 bins of out[] to dB for display
            for (i=0; i < LCD_HORIZONTAL_MAX; i++){
            out_db[i] = -(10 * log10f(out[i].r * out[i].r + out[i].i * out[i].i))+85;
            }
        }

        Semaphore_post(display_sem); //post to DISPLAY
        Semaphore_post(waveform_sem); //post to WAVEFORM
    }
}

void display_task(UArg arg1, UArg arg2)  //low prio
{

    tContext sContext;
    GrContextInit(&sContext, &g_sCrystalfontz128x128); // Initialize the grlib graphics context
    GrContextFontSet(&sContext, &g_sFontFixed6x8); // select font

    //locals
    // full-screen rectangle
    tRectangle rectFullScreen = {0, 0, GrContextDpyWidthGet(&sContext)-1, GrContextDpyHeightGet(&sContext)-1};
    uint16_t i; //loop variable

    while (true) {

        Semaphore_pend(display_sem, BIOS_WAIT_FOREVER);//pend on semaphore

        //Display Code
        drawBackground(&sContext, &rectFullScreen);


        if (specMode == 0){ //if in oscilliscope mode
            drawGrid(&sContext);
            drawTrigger(&sContext);
            drawRiseFall(&sContext, trigRise);
            drawTimeScale(&sContext, 0);
            drawVoltScale(&sContext, voltscalestage);
        }

        if (specMode == 1){ //if in spectrum mode
            drawSpecGrid(&sContext);
            drawFreqScale(&sContext, 0);
            drawdBScale(&sContext, 0);
        }


        drawFrequency(&sContext, (gSystemClock/timerPeriod));
        drawPeriod(&sContext, pwmPeriod);


        GrContextForegroundSet(&sContext, ClrYellow);
        if (specMode == 0){  //if ocilliscope mode
            for (i=0; i < LCD_HORIZONTAL_MAX; i++){
                if (i != 0){  //draw function uses previous value of yScaleBuff, so you can't use 0.  still draws the line from 0-1
                    GrLineDraw(&sContext, i-1, yScaleBuff[i-1], i, yScaleBuff[i]); //draws line from previous point to current point
                }
            }
        }

        if (specMode == 1){  //if spectrum mode
            for (i=0; i < LCD_HORIZONTAL_MAX; i++){
                if (i != 0){  //draw function uses previous value of out_db, so you can't use 0.  still draws the line from 0-1
                   GrLineDraw(&sContext, i-1, out_db[i-1], i, out_db[i]); //draws line from previous point to current point
                  }
            }
        }

        if (unsync == 1){ //draws warning if missed trigger
            drawUnsync(&sContext);
           }

        count_loaded = cpu_load_count();  //check counts loaded currently
        cpuload = (1.0f - (float)count_loaded/count_unloaded)*100;  //calculate CPU load, multiply by 100 to get percent

        drawCPULoad(&sContext, cpuload);


        if (pause == 1){
             drawPause(&sContext);
            }

        GrFlush(&sContext); // flush the frame buffer to the LCD

    }
}

void userinput_task(UArg arg1, UArg arg2)  //medium prio
{
    while (true) {
        checkButtons(); //pends waiting for a button to be read from button_mailbox
        Semaphore_post(display_sem); //post to display to update screen
        }
}


void button_task(UArg arg1, UArg arg2)  //high prio
{
    while (true) {
        Semaphore_pend(button_sem, BIOS_WAIT_FOREVER);//pend on semaphore that is posted to every 5ms by button_task
        readButtons();
    }
}


void waveform_task(UArg arg1, UArg arg2)  //highest prio
{
    uint16_t i; //loop variable
    count_unloaded = cpu_load_count(); //check count unloaded once before interrupts enabled
    IntMasterEnable();


    while (true) {
      Semaphore_pend(waveform_sem, BIOS_WAIT_FOREVER);//pend on semaphore, initial count = 1 so it runs first time

      if (specMode == 0){ //if in oscilliscope mode
          int trigger = FindTrigger(); //call find trigger once

          if (pause == 0){ //stops updating buffer if pause = 1
              for (i=0; i < LCD_HORIZONTAL_MAX; i++){
                  //gDisplayBuffer[i] = gADCBuffer[ADC_BUFFER_WRAP(trigger+i-(LCD_HORIZONTAL_MAX/2))]; //single sample code
                  gDisplayBuffer[i] = gADCBuffer[ADC_BUFFER_WRAP(trigger+i-(LCD_HORIZONTAL_MAX/2))]; //DMA code
              }
          }
      }


      if (specMode == 1){ //if in spec mode

          if (pause == 0){ //stops updating buffer if pause = 1
             for (i=0; i < SPEC_SAMPLE; i++){
                 //gSpecDisplayBuffer[i] = gADCBuffer[ADC_BUFFER_WRAP(gADCBufferIndex+i-(SPEC_SAMPLE+1))]; //single sample code
                 gSpecDisplayBuffer[i] = gADCBuffer[ADC_BUFFER_WRAP(getADCBufferIndex()+i-(SPEC_SAMPLE+1))]; //DMA code
               }
          }

      }


      Semaphore_post(process_sem); //post to process task
    }
}








