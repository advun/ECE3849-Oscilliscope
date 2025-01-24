/*
 * buttons.c
 *
 *  Created on: Aug 12, 2012, modified 9/8/2017
 *      Author: Gene Bogdanov
 *
 * ECE 3849 Lab button handling
 */
/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/cfg/global.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"
#include "driverlib/adc.h"
#include "driverlib/pwm.h"
#include "sysctl_pll.h"
#include "buttons.h"
#include "sampling.h"

// public globals
volatile uint32_t gButtons = 0; // debounced button state, one per bit in the lowest bits
// button is pressed if its bit is 1, not pressed if 0
uint32_t gJoystick[2] = { 0 };    // joystick coordinates

// imported globals
extern uint32_t gSystemClock;   // [Hz] system clock frequency
extern volatile uint32_t gTime; // time in hundredths of a second
extern uint8_t pause; //if 1, pause, if 0 unpause
extern bool trigRise;
extern uint8_t voltscalestage;
extern uint8_t specMode;
extern uint32_t pwmPeriod;


// ButtonInit: initialize all button and joystick handling hardware
void ButtonInit(void)
{
    // GPIO PJ0 and PJ1 = EK-TM4C1294XL buttons 1 and 2
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOJ);
    GPIOPinTypeGPIOInput(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTJ_BASE, GPIO_PIN_0 | GPIO_PIN_1,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);

    // analog input AIN13, at GPIO PD2 = BoosterPack Joystick HOR(X)
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2);
    // analog input AIN17, at GPIO PK1 = BoosterPack Joystick VER(Y)
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);
    GPIOPinTypeADC(GPIO_PORTK_BASE, GPIO_PIN_1);

    //S1:GPIO:PH1
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOH);
    GPIOPinTypeADC(GPIO_PORTH_BASE, GPIO_PIN_1);
    GPIOPadConfigSet(GPIO_PORTH_BASE, GPIO_PIN_1, GPIO_STRENGTH_2MA,
                     GPIO_PIN_TYPE_STD_WPU);

    //S2: GPIO:PK6
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);
    GPIOPinTypeADC(GPIO_PORTK_BASE, GPIO_PIN_6);
    GPIOPadConfigSet(GPIO_PORTK_BASE, GPIO_PIN_6, GPIO_STRENGTH_2MA,
                     GPIO_PIN_TYPE_STD_WPU);

    //Select:GPIO:PD4
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_4);
    GPIOPadConfigSet(GPIO_PORTD_BASE, GPIO_PIN_4, GPIO_STRENGTH_2MA,
                     GPIO_PIN_TYPE_STD_WPU);

}

// ButtonDebounce: update the debounced button state gButtons
void ButtonDebounce(uint32_t buttons)
{
    int32_t i, mask;
    static int32_t state[BUTTON_COUNT]; // button state: 0 = released
                                        // BUTTON_PRESSED_STATE = pressed
                                        // in between = previous state
    for (i = 0; i < BUTTON_COUNT; i++)
    {
        mask = 1 << i;
        if (buttons & mask)
        {
            state[i] += BUTTON_STATE_INCREMENT;
            if (state[i] >= BUTTON_PRESSED_STATE)
            {
                state[i] = BUTTON_PRESSED_STATE;
                gButtons |= mask; // update debounced button state
            }
        }
        else
        {
            state[i] -= BUTTON_STATE_DECREMENT;
            if (state[i] <= 0)
            {
                state[i] = 0;
                gButtons &= ~mask;
            }
        }
    }
}

// ButtonReadJoystick: sample joystick and convert to button presses
void ButtonReadJoystick(void)
{
    ADCProcessorTrigger(ADC0_BASE, 0); // trigger the ADC sample sequence for Joystick X and Y
    while (!ADCIntStatus(ADC0_BASE, 0, false))
        ;  // wait until the sample sequence has completed
    ADCSequenceDataGet(ADC0_BASE, 0, gJoystick);  // retrieve joystick data
    ADCIntClear(ADC0_BASE, 0);              // clear ADC sequence interrupt flag

    // process joystick movements as button presses using hysteresis
    if (gJoystick[0] > JOYSTICK_UPPER_PRESS_THRESHOLD)
        gButtons |= 1 << 5; // joystick right in position 5
    if (gJoystick[0] < JOYSTICK_UPPER_RELEASE_THRESHOLD)
        gButtons &= ~(1 << 5);

    if (gJoystick[0] < JOYSTICK_LOWER_PRESS_THRESHOLD)
        gButtons |= 1 << 6; // joystick left in position 6
    if (gJoystick[0] > JOYSTICK_LOWER_RELEASE_THRESHOLD)
        gButtons &= ~(1 << 6);

    if (gJoystick[1] > JOYSTICK_UPPER_PRESS_THRESHOLD)
        gButtons |= 1 << 7; // joystick up in position 7
    if (gJoystick[1] < JOYSTICK_UPPER_RELEASE_THRESHOLD)
        gButtons &= ~(1 << 7);

    if (gJoystick[1] < JOYSTICK_LOWER_PRESS_THRESHOLD)
        gButtons |= 1 << 8; // joystick down in position 8
    if (gJoystick[1] > JOYSTICK_LOWER_RELEASE_THRESHOLD)
        gButtons &= ~(1 << 8);
}

// ButtonAutoRepeat: autorepeat button presses if a button is held long enough
uint32_t ButtonAutoRepeat(void)
{
    static int count[BUTTON_AND_JOYSTICK_COUNT] = { 0 }; // autorepeat counts
    int i;
    uint32_t mask;
    uint32_t presses = 0;
    for (i = 0; i < BUTTON_AND_JOYSTICK_COUNT; i++)
    {
        mask = 1 << i;
        if (gButtons & mask)
            count[i]++;     // increment count if button is held
        else
            count[i] = 0;   // reset count if button is let go
        if (count[i] >= BUTTON_AUTOREPEAT_INITIAL
                && (count[i] - BUTTON_AUTOREPEAT_INITIAL)
                        % BUTTON_AUTOREPEAT_NEXT == 0)
            presses |= mask;    // register a button press due to auto-repeat
    }
    return presses;
}

void checkButtons(void)
{
    char buttonMail; //button input from mailbox
    Mailbox_pend(button_mailbox, &buttonMail, BIOS_WAIT_FOREVER);

    if (buttonMail == 'A')
    { //button 1
        pause = pause ^ 1; //pauses or unpauses
    }

    if (buttonMail == 'B')
    { //button 2
        trigRise = trigRise ^ 1; //switches to rising trigger or falling trigger
    }

    if (buttonMail == 'C')
    { //button S1
        if (voltscalestage < 4)
        {  //voltage scale up
            voltscalestage++;
        }
    }

    if (buttonMail == 'D')
    { //button S2
        if (voltscalestage > 0)
        { //voltage scale down
            voltscalestage--;
        }
    }

    if (buttonMail == 'E')
    {
        specMode = specMode ^ 1; //switches to spec mode or out of spec mode
    }

    if (buttonMail == 'F')
    {
        pwmPeriod = pwmPeriod + 1; //Adds 1 to the period of the waveform
        initSignal();
    }

    if (buttonMail == 'G')
    {
        pwmPeriod = pwmPeriod - 1; //Subtracts 1 from the period of the waveform
        initSignal();
    }

    if (buttonMail == 'H')
    {
        PWMIntEnable(PWM0_BASE, PWM_INT_GEN_2);
    }
}

void readButtons(void)
{ //reads buttons to a mailbox
    // read hardware button state
    uint32_t gpio_PORTJ = ~GPIOPinRead(GPIO_PORTJ_BASE, 0xff)
            & (GPIO_PIN_1 | GPIO_PIN_0); // Read EK-TM4C1294XL buttons From PortJ 1+2
    uint32_t gpio_PORTH = ~GPIOPinRead(GPIO_PORTH_BASE, 0xff) & (GPIO_PIN_1); // Read LaunchPad button S1 from PortH1
    uint32_t gpio_PORTK = ~GPIOPinRead(GPIO_PORTK_BASE, 0xff) & (GPIO_PIN_6); // Read LaunchPad button S2 from PortK6
    uint32_t gpio_PORTD = ~GPIOPinRead(GPIO_PORTD_BASE, 0xff) & (GPIO_PIN_4); // Read Select button from PortD4

    //Assign button states by first bitshifting the button states to position 0, then shifting them to the needed position within gpio_buttons
    uint32_t gpio_buttons = (gpio_PORTJ) | (gpio_PORTH >> 1 << 2)
            | (gpio_PORTK >> 6 << 3) | (gpio_PORTD >> 4 << 4);

    uint32_t old_buttons = gButtons;    // save previous button state
    ButtonDebounce(gpio_buttons); // Run the button debouncer. The result is in gButtons.
    ButtonReadJoystick(); // Convert joystick state to button presses. The result is in gButtons.
    uint32_t presses = ~old_buttons & gButtons; // detect button presses (transitions from not pressed to pressed)
    presses |= ButtonAutoRepeat(); // autorepeat presses if a button is held long enough

    char buttonID = '0';
    if (presses & 0x01)
    { // EK-TM4C1294XL button 1 pressed
        buttonID = 'A';
    }

    if (presses & 0x02)
    { // EK-TM4C1294XL button 2 pressed
        buttonID = 'B';
    }

    if (presses & 0x04)
    { // button S1 pressed
        buttonID = 'C';
    }

    if (presses & 0x08)
    { //  button S2 pressed
        buttonID = 'D';

    }

    if (presses & 0x10)
    { //joystick button pressed
        buttonID = 'E';

    }

    if (presses & 0x20)
    { //joystick right
        buttonID = 'F';

    }

    if (presses & 0x40)
    { //joystick left
        buttonID = 'G';

    }

    if (presses & 0x80)
    { //joystick up
        buttonID = 'H';

    }

    if (buttonID != '0')
    {
        Mailbox_post(button_mailbox, &buttonID, BIOS_NO_WAIT);
    }
}

void button_func(void)
{ //posts to button_task every 5ms, called by clock0
    Semaphore_post(button_sem);
}

