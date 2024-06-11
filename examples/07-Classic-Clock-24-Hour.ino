/*-----------------------------------------------------------------------------------------------*
 * 7-Segment Flip-disc Clock by Marcin Saj https://flipo.io                                      *
 * https://github.com/marcinsaj/Flipo-Clock-4x7-Segment-Flip-Disc-Display                        *
 *                                                                                               *
 * Classic 24-hour Clock                                                                         *
 * This example is used to test clock displays                                                   *
 *                                                                                               *
 * Time Setting Instructions:                                                                    *
 *                                                                                               *
 * Setup:                                                                                        *
 * Assembly Instructions - https://bit.ly/Flip-Disc-Clock-Assembly                               *
 * Clock diagram - https://bit.ly/3RhW7H2                                                        *
 * Dedicated Controller - https://bit.ly/AC1-FD                                                  *
 * 7-Segment Flip-disc Display - https://bit.ly/7SEG-FD                                          *
 * 3x1 Flip-disc Display - https://bit.ly/3x1DOT-FD                                              *
 * Arduino Nano Every - https://bit.ly/ARD-EVERY                                                 *
 * RTC real Time Clock RX8025T - https://bit.ly/RX8025T                                          *
 * Temperature Sensor DHT22 - https://bit.ly/DHT22                                               *
 *-----------------------------------------------------------------------------------------------*/

#include <FlipDisc.h>         // https://github.com/marcinsaj/FlipDisc
#include <RTC_RX8025T.h>      // https://github.com/marcinsaj/RTC_RX8025T
#include <TimeLib.h>          // https://github.com/PaulStoffregen/Time
#include <Wire.h>             // https://arduino.cc/en/Reference/Wire (included with Arduino IDE) 

/************************************************************************************************/
/* Set the delay effect between flip discs. Recommended delay range: 0 - 100ms, max 255ms       */
int flip_disc_delay_time = 50;
/************************************************************************************************/

/* Attention: do not change! Changing these settings may physical damage the flip-disc displays.
Pin declaration for a dedicated controller */
#define EN_PIN A7  // Start & End SPI transfer data
#define CH_PIN A2  // Charging PSPS module - turn ON/OFF
#define PL_PIN A3  // Release the current pulse - turn ON/OFF 

/* Buttons - counting from the top */
#define B1_PIN 10  // Incerement button
#define B2_PIN 9   // Settings button
#define B3_PIN 8   // Decrement button

/* RTC */
#define RTC_PIN A1 // Interrupt input

/* Declare structure that allows convenient access to the time elements:
- tm.Hour - hours
- tm.Minute - minutes */
tmElements_t tm;

/* The flag stores the status of pressing the middle button */
volatile bool setButtonPressedStatus = false;
/* The flag stores the status of long pressing the middle button */
volatile bool setButtonPressedLongStatus = false;
/* Variable for storing the time the middle button was pressed */
volatile unsigned long setButtonPressStartTime = 0;
/* Long press time in milliseconds */
const unsigned long longPressTime = 2000;

/* The variable stores the current level of time settings 
level 0 - normal operation
level 1 - settings of tenths of hours
level 2 - setting of hour units
level 3 - setting of tenths of minutes
level 4 - setting of minute units */
volatile uint8_t settingsLevel = 0; 

/* The flag to store status of RTC interrupt */
volatile bool rtcInterruptStatus = false;

/* A flag that stores the time display status, if the flag is set, 
the current time will be displayed */
volatile bool timeDisplayStatus = false;

/* The flag to store status of the settings status,
if the flag is set, the time setting option is active */
volatile bool timeSetStatus = false;

/* An array to store individual digits for display */
int digit[4] = {0, 0, 0, 0};


void setup() 
{
  /* Attention: do not change! Changing these settings may physical damage the flip-disc displays.
  Flip.Pin(...); it is the most important function and first to call before everything else. 
  The function is used to declare pin functions. */
  Flip.Pin(EN_PIN, CH_PIN, PL_PIN);
  
  pinMode(B1_PIN, INPUT);
  pinMode(B2_PIN, INPUT);
  pinMode(B3_PIN, INPUT);
  pinMode(RTC_PIN, INPUT_PULLUP);

  /* RTC RX8025T initialization */
  RTC_RX8025T.init();

  /* Time update interrupt initialization. Interrupt generated by RTC (INT output): 
  "INT_SECOND" - every second,
  "INT_MINUTE" - every minute. */
  RTC_RX8025T.initTUI(INT_MINUTE);

  /* "INT_ON" - turn ON interrupt generated by RTC (INT output),
  "INT_OFF" - turn OFF interrupt. */
  RTC_RX8025T.statusTUI(INT_ON);

  /* Assigning the interrupt handler to the button responsible for time settings */
  attachInterrupt(digitalPinToInterrupt(B2_PIN), setButtonPressedISR, RISING);
  
  /* Assign an interrupt handler to the RTC output, 
  an interrupt will be generated every minute to display the time */
  attachInterrupt(digitalPinToInterrupt(RTC_PIN), rtcInterruptISR, FALLING);

  /* Attention: do not change! Changing these settings may physical damage the flip-disc displays. 
  Flip.Init(...); it is the second most important function. Initialization function for a series 
  of displays. The function also prepares SPI to control displays. Correct initialization requires 
  code names of the serially connected displays:
  - D7SEG - 7-segment display
  - D3X1 - 3x1 display */
  Flip.Init(D7SEG, D7SEG, D3X1, D7SEG, D7SEG);

  /* This function allows you to display numbers and symbols
  Flip.Matrix_7Seg(data1,data2,data3,data4); */ 
  Flip.Matrix_7Seg(T,I,M,E);

  /* Function allows you to control one, two or three discs of the selected D3X1 display.
  - Flip.Display_3x1(module_number, disc1, disc2, disc3); */
  Flip.Display_3x1(1, 1,1,0);

  Serial.begin(9600);
  delay(3000);

  DisplayTime();

  /* Resetting the flags if any interrupts occurred in the meantime */
  timeDisplayStatus = false;
  timeSetStatus = false;
}


void loop()
{
  /* The buttons handling function called here only supports the middle button 
  responsible for entering the time settings */
  HandlingButtons();

  /* Display the time if an RTC interrupt has been triggered 
  and the flag has been set */
  if(timeDisplayStatus == true) 
  {
    DisplayTime();
    timeDisplayStatus = false;
  }

  /* Enter setting mode if the middle button is pressed 
  for more than 3 seconds */  
  if(timeSetStatus == true) 
  {
    SetTime();
    timeSetStatus = false;
  }
}


/* Buttons operating function
- top button - incrementation +
- middle button - settings
- bottom button - decrementation - */
void HandlingButtons(void) 
{
  /* Enter setup mode if the middle button is pressed for more than 3 seconds */
  if(setButtonPressedStatus == true) 
  {
    /* Record the current time */
    unsigned long currentTime = millis();
    
    /* Calculate how long the button is pressed */
    unsigned long pressDuration = currentTime - setButtonPressStartTime;

    /* If the button is pressed for more than longPressTime - 3 seconds, 
    go to the next setting level */
    if(pressDuration >= longPressTime) 
    {
      settingsLevel = settingsLevel + 1;

      /* If from normal clock operation you entered here for the first time 
      it means that you activated the time settings */
      if(settingsLevel == 1) timeSetStatus = true;

      /* If you were already at the setting level 4, 
      it means that you have exited the setting option */
      if(settingsLevel > 4) settingsLevel = 0;

      /* Display the currently set digit according to the setting level.
      HLM - horizontal middle line - for more details see FlipDisc.h library. */
      if(settingsLevel == 0) Flip.Matrix_7Seg(digit[0],digit[1],digit[2],digit[3]);
      if(settingsLevel == 1) Flip.Matrix_7Seg(digit[0],HLM,HLM,HLM);
      if(settingsLevel == 2) Flip.Matrix_7Seg(HLM,digit[1],HLM,HLM);
      if(settingsLevel == 3) Flip.Matrix_7Seg(HLM,HLM,digit[2],HLM);
      if(settingsLevel == 4) Flip.Matrix_7Seg(HLM,HLM,HLM,digit[3]);

      // Reset the button press flag to handle a new press
      setButtonPressedStatus = false;
    }

    /* If the button has been released then clear the button press flag. 
    This logic is required to correctly detect a long button press. */
    if(digitalRead(B2_PIN) == LOW) setButtonPressedStatus = false;
  }

  /* Entering settings mode */
  if(settingsLevel != 0)
  {
    if(incButtonPressed() == true)
    {
      /* The digit[] array is addressed from zero so we need to correct the address [...-1]. 
      Each press of the button increments the displayed digit */
      digit[settingsLevel - 1] = digit[settingsLevel - 1] + 1;

      /* If the first digit is 2 then the second digit 
      cannot be greater than 3 because the largest possible hour is 23 */
      if((digit[0] == 2) && (settingsLevel == 2) && (digit[1] > 3)) digit[1] = 0;
      if(digit[0] > 2) digit[0] = 0;

      /* If the first digit is 1 then the second digit can be from 0 to 9 */
      if(digit[1] > 9) digit[1] = 0;

      /* Third digit always from 0 to 5 */
      if(digit[2] > 5) digit[2] = 0;

      /* Third digit always from 0 to 9 */
      if(digit[3] > 9) digit[3] = 0;

      /* Delay for easiest correct button operation */
      delay(500);

      /* Update the display for only the currently set digit. 
      for more details see FlipDisc.h library */
      Flip.Display_7Seg(settingsLevel, digit[settingsLevel - 1]);
    }

    if(decButtonPressed() == true)
    {
      /* The digit[] array is addressed from zero so we need to correct the address [...-1]. 
      Each press of the button decrements the displayed digit */
      digit[settingsLevel - 1] = digit[settingsLevel - 1] - 1;

      /* If the first digit is 2 then the second digit 
      cannot be greater than 3 because the largest possible hour is 23 */      
      if((digit[0] == 2) && (settingsLevel == 2) && (digit[1] < 0)) digit[1] = 3;
      if(digit[0] < 0) digit[0] = 2;

      /* If the first digit is 1 then the second digit can be from 0 to 9 */
      if(digit[1] < 0) digit[1] = 9;

      /* Third digit always from 0 to 5 */
      if(digit[2] < 0) digit[2] = 5;

      /* Third digit always from 0 to 9 */
      if(digit[3] < 0) digit[3] = 9;      

      /* Delay for easiest correct button operation */
      delay(500);

      /* Update the display for only the currently set digit. 
      for more details see FlipDisc.h library */
      Flip.Display_7Seg(settingsLevel, digit[settingsLevel - 1]);
    } 
  }
}

void SetTime(void)
{
  /* Enter button operation loop for time settings */
  do{HandlingButtons();}
  while(settingsLevel != 0);

  /* Convert entered individual digits to the format supported by RTC */
  uint8_t hour_time = (digit[0] * 10) + digit[1];
  uint8_t minute_time = (digit[2] * 10) + digit[3];

  /* The date is skipped (1, 1, 1) and the seconds are set by default to 0.
  We are only interested in hours and minutes */
  setTime(hour_time, minute_time, 0, 1, 1, 1);

  /* Set the RTC from the system time */
  RTC_RX8025T.set(now());
}

void DisplayTime(void)
{
  /* The function is used to set the delay effect between flip discs. 
  The default value without calling the function is 0. Can be called multiple times 
  anywhere in the code. Recommended delay range: 0 - 100ms, max 255ms */
  Flip.Delay(flip_disc_delay_time);
  
  /* Get the time from the RTC and save it to the tm structure */
  RTC_RX8025T.read(tm);
  
  /* Extract individual digits for the display */
  digit[0] = (tm.Hour / 10) % 10;
  digit[1] = (tm.Hour / 1) % 10;
  digit[2] = (tm.Minute / 10) % 10;
  digit[3] = (tm.Minute / 1) % 10;

  /* Display the current time */
  Flip.Matrix_7Seg(digit[0],digit[1],digit[2],digit[3]);
  
  /* Print time to the serial monitor */
  Serial.print("Time: ");
  if(tm.Hour < 10) Serial.print("0");
  Serial.print(tm.Hour);
  Serial.print(":");
  if(tm.Minute < 10) Serial.print("0");
  Serial.print(tm.Minute);
  Serial.print(":");
  if(tm.Second < 10) Serial.print("0");
  Serial.println(tm.Second); 

  /* The delay effect is used only when displaying time. 
  During time settings, the delay is 0. */
  Flip.Delay(0);
}

/* Function supporting the top button */
bool incButtonPressed(void)
{
  return(digitalRead(B1_PIN));
}

/* Function supporting the bottom button */
bool decButtonPressed(void)
{
  return(digitalRead(B3_PIN));
}

/* Function that handles the interrupt from pressing the middle button */
void setButtonPressedISR(void) 
{
  /* Setting the middle button press flag*/
  setButtonPressedStatus = true;

  /* Save the time when the button was pressed to determine 
  if the button was pressed for a minimum of 3 seconds */
  setButtonPressStartTime = millis();
}

void rtcInterruptISR(void)
{
  /* Set the status of the interrupt flag from the RTC 
  only if this is normal operation and we are not in time setting mode */
  if(settingsLevel == 0) timeDisplayStatus = true; 
}
