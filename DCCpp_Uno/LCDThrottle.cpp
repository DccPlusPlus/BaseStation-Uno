#include <Arduino.h>
#include "SerialCommand.h"
#include "LCDThrottle.h"

//#define SPEED_UP_INCREMENT   1 /* 13 */
//#define SPEED_DOWN_INCREMENT 1 /* 13 */
#define MAX_SPEED           60 /*126 */
#define MAX_NOTCH_NORMAL     15
#define MAX_NOTCH_SWITCHER    7

#define THROTTLE_STATE_RUN      0
#define THROTTLE_STATE_DEBOUNCE 1

// MAX_COMMAND_LENGTH is defined in SerialCommand.h
#define MAX_COMMAND_LENGTH 30
char command[MAX_COMMAND_LENGTH];

static char display[2][17];

static LCDThrottle *lcdThrottle = NULL;

static LCDThrottle *LCDThrottle::getThrottle(int r, int c) {
  if (lcdThrottle == NULL) {
    lcdThrottle = new LCDThrottle(r, c);
  }

  return(lcdThrottle);
}

LCDThrottle::LCDThrottle(int reg, int cab) {
  lcd = new LCD();
  lcd->begin();
  throttleState = THROTTLE_STATE_RUN;
  this->reg = reg;
  this->cab = cab;
  notch = 0;
  speed = 0;
  dir = FORWARD;
  displayMode = DISPLAY_MODE;
  power_state = false;
  updateDisplay();
}

void LCDThrottle::run() {
  lcd->run();
  int button = lcd->getButtons();
  switch(throttleState) {
  case THROTTLE_STATE_RUN:
    switch(button) {
    case KEYS_RIGHT:
      increaseSpeed();
      sendThrottleCommand();
      updateDisplay();
      break;
    case KEYS_LEFT:
      decreaseSpeed();
      sendThrottleCommand();
      updateDisplay();
      break;
    case KEYS_UP:
    case KEYS_DOWN:
      // For now, dumbly toggle direction with either left or right key.
      // Maybe make this smarter or repurpose later.
      dir = (dir == FORWARD ? REVERSE : FORWARD);
      sendThrottleCommand();
      updateDisplay();
      break;
    case KEYS_SELECT:
      // For now, this is emergency stop.
      speed = -1;
      notch = 0;
      sendThrottleCommand();
      // reset speed to Zero after sending "Emergency Stop" special value of -1
      // This won't hurt b/c the Base Station will set the loco's speed to zero as well.
      speed = 0; 
      updateDisplay();
      break;
    case KEYS_LONG_SELECT:
      // This will toggle power.
      power_state = !power_state;
      sendPowerCommand(power_state);
      break;
        
    default:
      break;
      // Do nothing.
    } // switch(button)
    break;

  case THROTTLE_STATE_DEBOUNCE:
    // ???
    break;
  } // switch(throttleState)
}

void LCDThrottle::sendPowerCommand(bool on) {
  sprintf(command, "");
  sprintf(command, on == true ? "1" : "0");
  Serial.println(command);
  SerialCommand::parse(command);
  
}

void LCDThrottle::increaseSpeed() {
  int tmp_notch;
  if (displayMode == DISPLAY_MODE_NORMAL) {
    // in DISPLAY_MODE_NORMAL, notch is 0->maxnotch.
    // since this is the "increase" function we never have to worry about
    // flipping the direction bit.
    notch = (notch == MAX_NOTCH_NORMAL ? MAX_NOTCH_NORMAL : notch + 1);
    speed = notch * (MAX_SPEED / MAX_NOTCH_NORMAL);
  } else {
    // in DISPLAY_MODE_SWITCHER the direction can change when "increasing"
    // the throttle, it depends.  Have to deal with absolute value.
    // First, get the signed version of "notch" and increment it.
    tmp_notch = (dir == REVERSE ? -notch : notch);
    tmp_notch = (tmp_notch == MAX_NOTCH_SWITCHER ? MAX_NOTCH_SWITCHER : tmp_notch + 1);
    // Now handle the possible sign change by setting the direction and 
    // storing notch = abs(tmp_notch)
    if (tmp_notch >= 0) {
      dir = FORWARD;
      notch = tmp_notch;    
    } else {
      dir = REVERSE;
      notch = -tmp_notch;
    }
    speed = notch * (MAX_SPEED / MAX_NOTCH_SWITCHER);
  } // if(displayMode)

  Serial.println("inc: N= " + String(notch) + " S=" + String(speed));
}

void LCDThrottle::decreaseSpeed() {
  int tmp_notch;
  if (displayMode == DISPLAY_MODE_NORMAL) {
    notch = (notch == 0 ? 0 : notch - 1);
    speed = notch * (MAX_SPEED / MAX_NOTCH_NORMAL);
  } else {
    tmp_notch = (dir == REVERSE ? -notch : notch);
    tmp_notch = (tmp_notch == -MAX_NOTCH_SWITCHER ? -MAX_NOTCH_SWITCHER : tmp_notch - 1);
    if (tmp_notch < 0) {
      dir = REVERSE;
      notch = -tmp_notch;  
    } else {
      dir = FORWARD;
      notch = tmp_notch;
    }
    speed = notch * (MAX_SPEED / MAX_NOTCH_SWITCHER);
  }
  Serial.println("dec: N= " + String(notch) + " S=" + String(speed));
}

void LCDThrottle::sendThrottleCommand() {
  sprintf(command, "");
  sprintf(command, "t%d %d %d %d", reg, cab, speed, dir);
  Serial.println(command);
  SerialCommand::parse(command);
}

// Display Modes...

void LCDThrottle::updateDisplay() {
  switch(displayMode) {
  case DISPLAY_MODE_SWITCHER:
    // SWITCHER: Speed/Direction together
    // (something useful)
    // <------0------>
    lcd->clear();
    // Draw the line.
    sprintf(display[0], "Loco: %04d", cab);
    if (power_state == true) {
      sprintf(display[1], "<------0------>");
    } else {
      sprintf(display[1], "Track Power Off");
    }
      lcd->updateDisplay(display[0], display[1]);
      Serial.println("D0:" + String(display[0]));
      Serial.println("D1:" + String(display[1]));    
    // Figure out where to put the cursor
    if (notch == 0) {
      lcd->setCursor(7,1);
    } else {
      int tmp_notch = notch;
      if (tmp_notch == 0) {
        tmp_notch += 7;
      } else {
        tmp_notch = (dir == FORWARD ? tmp_notch + 7 : 7 - tmp_notch);
      }
      Serial.println("S=" + String(speed) + " N=" + String(notch) + " T=" + String(tmp_notch));
      lcd->setCursor(tmp_notch, 1);
    }
    if (power_state == true) {
      lcd->blink();
    } else {
      lcd->noBlink(); 
    }
    break;

  case DISPLAY_MODE_NORMAL:
  default:
    // NORMAL:  Speed + Direction
    // <--          -->
    // 0--------------+
    lcd->clear();
    if (dir == REVERSE) {
      sprintf(display[0], "<---  Loco: %04d", cab);
    } else {
      sprintf(display[0], "Loco: %04d  --->", cab);
    }
    if (power_state == true) {
      sprintf(display[1], "0               ");
      if (speed > 0) {
        for (int i = 0; i < notch-1; i++) {
          display[1][i+1] = '-';
        }
        display[1][notch] = '|';
        display[1][notch+1] = 0;
      }
    } else {
      sprintf(display[1], "Track Power Off");
    }
    lcd->updateDisplay(display[0], display[1]);
    Serial.println("D0:" + String(display[0]));
    Serial.println("D1:" + String(display[1]));    
    break;
  }
}
