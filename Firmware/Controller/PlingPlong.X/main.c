/* 
 * File:   main.c
 * Author: Mats
 *
 * Created on October 28, 2013, 1:16 PM
 */

#include <stdlib.h>
#include <stdint.h>         // uint8_t definitions
#include <stdbool.h>        // true/false definitions

#include <xc.h>
#include <timers.h>
#include <usart.h>

#include "config.h"

#include "fuses.h"          // PIC Fuse settings
#include "rotaryenc.h"      // Quadrature Rotary Encoder
#include "dispbut.h"        // Refresh leds and read buttons
#include "touch.h"          // Handling of the touch pads
#include "easteregg.h"

/*
 * RA0  PAD19
 * RA1  PAD20
 * RA2  PAD21
 * RA3  PAD22
 * RA4  spare
 * RA5  PAD23
 * RA6  LATCH
 * RA7  KEY1
 *
 * RB0  PAD13
 * RB1  PAD14
 * RB2  PAD15
 * RB3  PAD16
 * RB4  PAD17
 * RB5  PAD18
 * RB6  SCLK
 * RB7  SDA
 *
 * RC0  QUAD2
 * RC1  QUAD1
 * RC2  PAD1
 * RC3  PAD2
 * RC4  PAD7
 * RC5  PAD8
 * RC6  PAD9
 * RC7  PAD10
 *
 * RD0  PAD3
 * RD1  PAD4
 * RD2  PAD5
 * RD3  PAD6
 * RD4  PAD11
 * RD5  PAD12
 * RD6  TX
 * RD7  RX
 *
 * RE0  PAD24
 * RE1  KEY2
 * RE2  KEY3
 *

 */


#define QUAD1           PORTCbits.RC1
#define QUAD2           PORTCbits.RC0

#define LATCH595tris    TRISAbits.RA6
#define CLK595tris      TRISBbits.RB6
#define DATA595tris     TRISBbits.RB7
#define KEYS    16


volatile uint16_t   encValue;
volatile uint16_t   encMin;
volatile uint16_t   encMax;
volatile uint8_t    octave;


//  HTTP://HACKADAY.COM
//
//  X�X XXX XXX XXX ��� ��X ��X X�X XXX XXX X�X XXX XX� XXX X�X ��� XXX XXX XXX
//  X�X �X� �X� X�X �X� ��X ��X X�X X�X X�� X�X X�X X�X X�X X�X ��� X�� X�X XXX
//  XXX �X� �X� XXX ��� �X� �X� XXX XXX X�� XX� XXX X�X XXX �X� ��� X�� X�X X�X
//  X�X �X� �X� X�� �X� X�� X�� X�X X�X X�� X�X X�X X�X X�X �X� ��� X�� X�X X�X
//  X�X �X� �X� X�� ��� X�� X�� X�X X�X XXX X�X X�X XX� X�X �X� �X� XXX XXX X�X





void SetupBoard() {
    OSCCON=0b01110010; // Sleep, 16MHz, IntOsc

    //Set PADS as Analog pins
    ANSELA=0b00101111;  // RA0..RA3 + RA5 Analog
    ANSELB=0b00111111;  // RB0..RB5 Analog
    ANSELC=0b11111100;  // RC2..RC7 Analog
    ANSELD=0b00111111;  // RD0..RD5 Analog
    ANSELE=0b00000001;  // RE0 Analog

    // Most ports are Inputs
    TRISA=0xFF;
    TRISB=0xFF;
    TRISC=0xFF;
    TRISD=0xFF;
    TRISE=0xFF;
    // Set the pins for the shift registers as Outputs
    LATCH595tris=0;
    CLK595tris=0;
    DATA595tris=0;

    // Set all pins low
    LATA=0x00;
    LATB=0x00;
    LATC=0x00;

    Open2USART(
        USART_TX_INT_OFF & USART_RX_INT_OFF &
        USART_ASYNCH_MODE &
        USART_EIGHT_BIT &
        USART_CONT_RX &
        USART_BRGH_HIGH,
//        103); // 9600 bps @ 16MHz
        31); // 31250 bps @ 16MHz

}











void interrupt TimerOverflow() {
    uint8_t dir;

    if(TMR2IF==1) {
        TMR2IF=0;
        RefreshDisp();
    }

    if(TMR4IF==1) {
        TMR4IF=0;
        dir=PollRotaryEncoder(QUAD1, QUAD2);
        if (dir==DIR_CCW) {
            if (encValue<encMax) {
                encValue++;
            } else {
                encValue=encMax;
            }
        }
        if (dir==DIR_CW) {
            if (encValue>encMin) {
                encValue--;
            } else {
                encValue=encMin;
            }
        }
    }
}







SendNoteOn(uint8_t no) {
    while (Busy2USART());
    Write2USART(0x90|CHANNEL);
    while (Busy2USART());
    Write2USART(no);
    while (Busy2USART());
    Write2USART(VELOCITY);
    setbit(leds,no-octave*12);
    __delay_ms(1);
}

void SendNoteOff(uint8_t no) {
    while (Busy2USART());
    Write2USART(0x80|CHANNEL);
    while (Busy2USART());
    Write2USART(no);
    while (Busy2USART());
    Write2USART(VELOCITY);
    clrbit(leds,no-octave*12);
    __delay_ms(1);
}


void SendCC(uint8_t no, uint8_t v) {
    while (Busy2USART());
    Write2USART(0xB0|CHANNEL);
    while (Busy2USART());
    Write2USART(no);
    while (Busy2USART());
    Write2USART(v);
    __delay_ms(1);
}


/*

    OCT Keyboard octave         0-8
10  SND wavetable selection     there are 16 tables, table 1 is PWM
11  ATT attack
12  DEC decay
13  FRE frequency sweep         64 is off, 0 - 63 is down, 65 - 127 is up
14  FMD fm depth                0 is off
15  FMF fm frequency
16  BIT bit crush               0 is off, 127 is 1 bit
17  REV reverb depth            0 is off
18  PCO PWM "cut-off"
19  PCD PWM "cut-off" decay     64 is off, 0 - 63 is down, 65 - 127 is up
20  AMF am frequency            0 is off
 */


typedef struct {
    char    txt[4];
    uint8_t cc;
    uint16_t min;
    uint16_t max;
    uint8_t stepSize;
    uint16_t value;
} button_t;

button_t butSettings[13] = {
    {"OCT",127,  0,  8, 1, 3},
    {"SND", 10,  0, 13, 8, 1},
    {"ATT", 11,  0,127, 1,64},
    {"DEC", 12,  0,127, 1,64},
    {"FRE", 13,  0,127, 1,64},
    {"FMD", 14,  0,127, 1,64},
    {"FMF", 15,  0,127, 1,64},
    {"BIT", 16,  0,127, 1,64},
    {"REV", 17,  0,127, 1, 0},
    {"PCO", 18,  0,127, 1,64},
    {"PCD", 19,  0,127, 1,64},
    {"AMF", 20,  0,127, 1, 0},
    {"SEn",126,  0,999, 1,99}
};


void main(void) {
    uint16_t v;
    uint8_t i;
    int16_t pad;
    uint32_t padsNow=0;
    uint32_t padsLast=0;
    int8_t  lastKey;
    uint8_t cnt=0;
    uint16_t lastEncValue;
    uint8_t encStep;
    int16_t senseDelta;
    uint8_t easteregg;

    SetupBoard();
    SetupCTMU();

    encStep=1;
    encValue=0;
    lastEncValue=0;
    octave=3;
    encMin=0;
    encMax=14;
    senseDelta=99;
    OpenTimer4(TIMER_INT_ON & T4_PS_1_1 & T4_POST_1_1); // Timer4 - Rotary Encoder polling
    WriteTimer4(0xFF);

    TMR4IF=0;
    OpenTimer2(TIMER_INT_ON & T2_PS_1_4 & T2_POST_1_8); // Timer2 = Display Refresh
    WriteTimer2(0xFF);
    TMR2IF=0;

    GIE=1;
    PEIE=1;
    ei();

    disp[0]=0x00;
    disp[1]=0x00;
    disp[2]=0x00;

    keys=0;
    easteregg=0;

    leds=0x7fff;
    __delay_ms(25);
    leds=0x0000;
    CalibratePads();
    leds=0x7fff;
    __delay_ms(25);
    leds=0x0000;


//
//    encMin=1;
//    encMax=24;
//    encValue=1;
//    for (;;) {
//        pad=ReadPad(encValue);
//        DispValue(pad);
//        leds=0;
//        if ((encValue>=1) && (encValue<=9))   {setbit(leds,encValue-1);   setbit(leds,10);}
//        if ((encValue>=10) && (encValue<=19)) {setbit(leds,encValue-10);setbit(leds,11);}
//        if ((encValue>=20) && (encValue<=29)) {setbit(leds,encValue-20);setbit(leds,12);}
//        __delay_ms(25);
//    }



    for (;;) {
        // Check for tones H+A+D
        if (padsLast==0b000000000000101000000100) EasterEgg(1);
        if (padsLast==0b101000000100000000000000) EasterEgg(2);

        padsNow=0;
        for (i=0; i<PADS; i++) {
            pad=ReadPad(i);
            if (GetPadBaseValue(i)-pad>senseDelta) {
                setbit(padsNow,i);
            }
        }
        for (i=0; i<PADS; i++) {
            if (testbit(padsNow,i) != testbit(padsLast,i)) {
                if (testbit(padsNow,i)) {
                    SendNoteOn(octave*12+i);
                } else {
                    SendNoteOff(octave*12+i);
                }
            }
        }
        padsLast=padsNow;

        for (i=0; i<KEYS; i++) {
            if (testbit(keys,i)) {
                disp[0]=charmap[butSettings[i].txt[0]-32];
                disp[1]=charmap[butSettings[i].txt[1]-32];
                disp[2]=charmap[butSettings[i].txt[2]-32];
                lastKey=i;
                encMin=butSettings[i].min;
                encMax=butSettings[i].max;
                encValue=butSettings[i].value;
                encStep=butSettings[i].stepSize;
                lastEncValue=encValue;
                break;
            }
        }

        if ((encValue!=lastEncValue) && (lastKey!=-1)) {
            lastEncValue=encValue;
            if (butSettings[lastKey].cc==127) {  // OCTAVE
                DispValue(encValue*encStep);
                octave=encValue;
                butSettings[lastKey].value=encValue;
                DispValue(encValue*encStep);
            } else if (butSettings[lastKey].cc==126) {  // SENSE DELTA
                senseDelta=encValue;
                butSettings[lastKey].value=senseDelta;
                DispValue(senseDelta);
                if (senseDelta&1) setbit(leds,0); else clrbit(leds,0);
            } else {
                SendCC(butSettings[lastKey].cc, encValue*encStep);
                DispValue(encValue*encStep);
            }
        }


        __delay_ms(10);

    }
}






/*
 void mainSD(void) {
    uint16_t v;
    uint8_t i,v1,v2;
    int16_t pad;
    uint32_t padsNow=0;
    uint32_t padsLast=0;
    int8_t  lastKey;
    uint8_t cnt=0;
    uint16_t lastEncValue;
    uint8_t encStep;
    int16_t senseDelta;

    SetupBoard();

    OpenTimer4(TIMER_INT_ON & T4_PS_1_1 & T4_POST_1_1); // Timer4 - Rotary Encoder polling
    WriteTimer4(0xFF);

    TMR4IF=0;
    OpenTimer2(TIMER_INT_ON & T2_PS_1_4 & T2_POST_1_8); // Timer2 = Display Refresh
    WriteTimer2(0xFF);
    TMR2IF=0;

    GIE=1;
    PEIE=1;
    ei();

    disp[0]=0x00;
    disp[1]=0x00;
    disp[2]=0x00;
    keys=0;
    lastKey=0xff;

  //  while (Busy2USART()); Write2USART('U');

    for (;;) {
        for (i=0; i<KEYS; i++) {
            if (testbit(keys,i)) {
                if (lastKey!=i) {
                    lastKey=i;
                    if (lastKey==0) {
                        while (Busy2USART()); Write2USART('!');
                        while (Busy2USART());Write2USART('g');
                    }
                    if (lastKey==1) {
                        while (Busy2USART()); Write2USART('!');
                        while (Busy2USART()); Write2USART('h'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('0'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('2'); // Read sector 0x00000027
                        while (Busy2USART()); Write2USART('7'); // Read sector 0x00000027
                        disp[2]=0x80;
                        while (!DataRdy2USART()); v1=Read2USART();
                        while (!DataRdy2USART()); v2=Read2USART();
                        for (;;) {
                            while (!DataRdy2USART()); v1=Read2USART();
                            while (!DataRdy2USART()); v2=Read2USART();
                            disp[0]=charmap[v1-32];
                            disp[1]=charmap[v2-32];
                        }
                    }
                }
                break;
            }
        }
        __delay_ms(10);

    }
}



*/

