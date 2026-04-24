#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
MAX30105 particleSensor;

#define PULSE_PIN PA5
#define GSR_PIN PA3

// --- Shared Signal Processing ---
const int WINDOW_SIZE = 100;
int bufferIndex = 0;
const unsigned long REFRACTORY_PERIOD = 300;

// --- JUNIOR Variables ---
int32_t junBuffer[WINDOW_SIZE];
int32_t junMax = 0, junMin = 0, junThreshold = 0;
bool junPeakDetected = false;
unsigned long junLastPeakTime = 0;
const int HR_WINDOW = 4;
float junHrBuffer[HR_WINDOW];
int junHrIndex = 0;
int displayJunHR = 0;

// --- MAX30102 Variables (HR, SpO2, PTT, Temp) ---
int32_t irBuffer[WINDOW_SIZE];
int32_t redBuffer[WINDOW_SIZE];
int32_t irMax = 0, irMin = 0, irThreshold = 0;
int32_t redMax = 0, redMin = 0;
bool maxPeakDetected = false;
unsigned long maxLastPeakTime = 0;
float maxHrBuffer[HR_WINDOW];
float spo2Buffer[HR_WINDOW];
int maxHrIndex = 0;
int displayMaxHR = 0, displaySpO2 = 0;
float pttBuffer[HR_WINDOW];
int displaySys = 0, displayDia = 0;
float displayTemp = 0.0;

// --- ELEGANT RESPIRATORY RATE Variables ---
int32_t lastDC = 0;
int respDirection = 0; // 1 = UP, -1 = DOWN
unsigned long lastBreathTime = 0;
int brpmBuffer[3] = {0, 0, 0}; 
int brpmIdx = 0;
int displayBrPM = 0;

// --- GSR & STRESS Variables ---
int gsrValue = 0; 

enum SystemState { WAIT_FINGER, MONITORING };
SystemState currentState = WAIT_FINGER;

unsigned long stateStartTime = 0;
unsigned long lastSampleTime = 0;

// Accumulators for Baseline
long sumGSR = 0;
float sumTemp = 0.0;
long sumMaxHR = 0;
int calCount = 0;

// Calculated Baselines
int baseGSR = 0;
float baseTemp = 0.0;
int baseMaxHR = 0;

// Stress State Tracking
int stressScore = 0; 
bool tTrig = false;   // Temp Triggered
bool gTrig = false;   // GSR Triggered
bool hTrig = false;   // HR Triggered

// --- OLED Variables ---
int x = 0;
int lastx = 0;
int lastyJun = 42;
int lastyMax = 63;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); 

  Wire.begin();
  Wire.setClock(400000); 

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed"));
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
      Serial.println("MAX30102 missing.");
      while (1); 
  }

  particleSensor.setup(60, 4, 2, 100, 411, 4096);

  // Initialize buffers
  for (int i = 0; i < WINDOW_SIZE; i++) {
      junBuffer[i] = 0; irBuffer[i] = 0; redBuffer[i] = 0; 
  }
  for (int i = 0; i < HR_WINDOW; i++) {
      junHrBuffer[i] = 0; maxHrBuffer[i] = 0; spo2Buffer[i] = 0; pttBuffer[i] = 0;
  }
  
  pinMode(GSR_PIN, INPUT_ANALOG); 
}

void loop() {
  if(x > 127) {
    display.clearDisplay();
    x = 0; lastx = 0;
    
    // Read Temperature once per screen wipe
    if (displayMaxHR > 0) {
        displayTemp = particleSensor.readTemperature();
    } else {
        displayTemp = 0.0;
    }
  }

  // Read Sensors
  int32_t junVal = analogRead(PULSE_PIN);
  int32_t irVal = particleSensor.getIR();
  int32_t redVal = particleSensor.getRed();
  gsrValue = analogRead(GSR_PIN); 

  unsigned long currentMillis = millis();

  // --- 1. Update Fast Windows ---
  junBuffer[bufferIndex] = junVal;
  irBuffer[bufferIndex] = irVal;
  redBuffer[bufferIndex] = redVal;
  bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;

  // --- 2. Adaptive Thresholds & RESPIRATORY RATE ---
  if (bufferIndex % 10 == 0) { 
      junMax = junBuffer[0]; junMin = junBuffer[0];
      irMax = irBuffer[0]; irMin = irBuffer[0];
      redMax = redBuffer[0]; redMin = redBuffer[0];
      
      for (int i = 1; i < WINDOW_SIZE; i++) {
          if (junBuffer[i] > junMax) junMax = junBuffer[i];
          if (junBuffer[i] < junMin) junMin = junBuffer[i];
          if (irBuffer[i] > irMax) irMax = irBuffer[i];
          if (irBuffer[i] < irMin) irMin = irBuffer[i];
          if (redBuffer[i] > redMax) redMax = redBuffer[i];
          if (redBuffer[i] < redMin) redMin = redBuffer[i];
      }
      junThreshold = junMin + (junMax - junMin) * 0.6; 
      irThreshold = irMin + (irMax - irMin) * 0.6; 

// --- ESTIMATED RESPIRATORY RATE ---
  // WARNING: This is a physiological estimation, NOT a true measurement.
  if (displayMaxHR > 40 && displayMaxHR < 150) {
      // Divide HR by ~4.5 to get a normal resting adult estimation
      displayBrPM = displayMaxHR / 4.5; 
      
      // Add slight random noise (+/- 1) so it doesn't look perfectly robotic
      // displayBrPM += random(-1, 2); 
  } else {
      displayBrPM = 0;
  }
  }

  // --- Valid State Checks ---
  bool junValid = ((junMax - junMin) >= 40);
  bool maxValid = (irVal >= 50000);
  if (!junValid) displayJunHR = 0;
  if (!maxValid) { 
      displayMaxHR = 0; displaySpO2 = 0; displaySys = 0; displayDia = 0; 
      // Reset RR data if finger is removed
      displayBrPM = 0;
      lastDC = 0;
      lastBreathTime = 0;
      for(int i = 0; i < 3; i++) brpmBuffer[i] = 0;
  }

  // --- 3. JUNIOR Peak Detection ---
  if (junValid && junVal > junThreshold && !junPeakDetected) {
      if (currentMillis - junLastPeakTime > REFRACTORY_PERIOD) {
          unsigned long deltaT = currentMillis - junLastPeakTime;
          junLastPeakTime = currentMillis;
          junPeakDetected = true;

          float instHR = 60000.0 / (float)deltaT;
          if (instHR > 30 && instHR < 220) {
              junHrBuffer[junHrIndex] = instHR;
              junHrIndex = (junHrIndex + 1) % HR_WINDOW;
              float sum = 0; for (int i=0; i<HR_WINDOW; i++) sum += junHrBuffer[i];
              displayJunHR = (int)(sum / HR_WINDOW); 
          }
      }
  }
  if (junVal < junThreshold) junPeakDetected = false;

  // --- 4. MAX30102 Peak Detection, SpO2, and BP ---
  if (maxValid && irVal > irThreshold && !maxPeakDetected) {
      if (currentMillis - maxLastPeakTime > REFRACTORY_PERIOD) {
          unsigned long deltaT = currentMillis - maxLastPeakTime;
          maxLastPeakTime = currentMillis;
          maxPeakDetected = true;

          float instHR = 60000.0 / (float)deltaT;
          
          float acIR = irMax - irMin; float dcIR = irMin + (acIR / 2.0);
          float acRed = redMax - redMin; float dcRed = redMin + (acRed / 2.0);
          float instSpO2 = 0;
          if (dcIR > 0 && dcRed > 0 && acIR > 0) {
              float R = (acRed / dcRed) / (acIR / dcIR);
              instSpO2 = 104.0 - (17.0 * R); 
              if (instSpO2 > 100.0) instSpO2 = 100.0;
          }

          unsigned long currentPTT = abs((long)(maxLastPeakTime - junLastPeakTime));
          
          if (instHR > 30 && instHR < 220 && instSpO2 > 70) {
              maxHrBuffer[maxHrIndex] = instHR;
              spo2Buffer[maxHrIndex] = instSpO2;
              
              if (currentPTT > 50 && currentPTT < 500) {
                  pttBuffer[maxHrIndex] = currentPTT;
              }
              
              maxHrIndex = (maxHrIndex + 1) % HR_WINDOW;

              float sumHR = 0, sumSpO2 = 0, sumPTT = 0;
              for (int i=0; i<HR_WINDOW; i++) {
                  sumHR += maxHrBuffer[i]; sumSpO2 += spo2Buffer[i]; sumPTT += pttBuffer[i];
              }
              displayMaxHR = (int)(sumHR / HR_WINDOW); 
              displaySpO2 = (int)(sumSpO2 / HR_WINDOW);
              
              float avgPTT = sumPTT / HR_WINDOW;
              if (avgPTT > 0) {
                  displaySys = (int)(210 - (0.35 * avgPTT));
                  displayDia = (int)(140 - (0.25 * avgPTT));
              }
          }
      }
  }
  if (irVal < irThreshold) maxPeakDetected = false;

  // --- 5. STRESS STATE MACHINE ---
  switch (currentState) {
      case WAIT_FINGER:
          if (maxValid && junValid) {
              // Start timer if it's currently stopped
              if (stateStartTime == 0) {
                  stateStartTime = currentMillis;
                  lastSampleTime = currentMillis;
                  sumGSR = 0; sumTemp = 0; sumMaxHR = 0; calCount = 0;
              }
              
              if (currentMillis - lastSampleTime >= 1000) {
                  sumGSR += gsrValue;
                  sumTemp += displayTemp;
                  sumMaxHR += displayMaxHR;
                  calCount++;
                  lastSampleTime = currentMillis;
              }

              // After 30 seconds, lock in baselines
              if (currentMillis - stateStartTime >= 30000) {
                  if (calCount > 0) {
                      baseGSR = sumGSR / calCount;
                      baseTemp = sumTemp / calCount;
                      baseMaxHR = sumMaxHR / calCount;
                      currentState = MONITORING;
                  }
              }
          } else {
              // Finger Lifted: Immediately reset timer and stay in WAIT
              stateStartTime = 0; 
              tTrig = false; gTrig = false; hTrig = false;
          }
          break;

      case MONITORING:
          if (!maxValid || !junValid) {
              // Finger Lifted: Instantly drop back to WAIT and reset
              currentState = WAIT_FINGER;
              stateStartTime = 0;
              tTrig = false; gTrig = false; hTrig = false;
          } else {
              stressScore = 0; 
              
              // Evaluate conditions
              tTrig = (displayTemp <= (baseTemp - 0.2));
              gTrig = (gsrValue >= (baseGSR + 50));
              hTrig = (displayMaxHR >= (baseMaxHR + 3));

              if (tTrig) stressScore++;
              if (gTrig) stressScore++;
              if (hTrig) stressScore++;
          }
          break;
  }

  // --- 6. Draw Graphs to OLED ---
  int yJun = 42, yMax = 63;

  if (junValid) {
      yJun = map(junVal, junMin, junMax, 42, 27);
      yJun = constrain(yJun, 27, 42);
  }
  display.drawLine(lastx, lastyJun, x, yJun, SSD1306_WHITE);
  lastyJun = yJun;

  if (maxValid && (irMax - irMin > 10)) {
      yMax = map(irVal, irMin, irMax, 63, 48);
      yMax = constrain(yMax, 48, 63);
  }
  display.drawLine(lastx, lastyMax, x, yMax, SSD1306_WHITE);
  lastyMax = yMax;
  lastx = x;

  // --- 7. Draw Text to OLED ---
  display.fillRect(0, 0, 128, 25, SSD1306_BLACK); 
  
  // Line 1: Heart Rates
  display.setCursor(0, 0);
  display.print("J:");
  if (displayJunHR > 0) display.print(displayJunHR); else display.print("--");
  display.print(" M:");
  if (displayMaxHR > 0) display.print(displayMaxHR); else display.print("--");

  // Dynamic Multi-Level Stress Indicator (Top Right)
  display.setCursor(76, 0);
  if (currentState == WAIT_FINGER) {
      if (stateStartTime > 0) {
          int secondsLeft = 30 - ((currentMillis - stateStartTime) / 1000);
          if (secondsLeft < 0) secondsLeft = 0;
          display.print("[C:");
          if (secondsLeft < 10) display.print("0"); 
          display.print(secondsLeft);
          display.print("]");
      } else {
          display.print("[WAIT]");
      }
  } 
  else if (currentState == MONITORING) {
      if (stressScore == 0) {           // 0 conditions met
          display.print("[CALM]");
      } else if (stressScore == 1) {    // 1 condition met
          display.print("[S: LOW]");
      } else if (stressScore == 2) {    // 2 conditions met
          display.print("[S: MED]");
      } else if (stressScore == 3) {    // All 3 conditions met
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.print("!S:HIGH!");
          display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); 
      }
  }

  // Line 2: SpO2 and BP
  display.setCursor(0, 8);
  display.print("O2:");
  if (displaySpO2 > 0) display.print(displaySpO2); else display.print("--");
  display.print("% BP:");
  if (displaySys > 0) { display.print(displaySys); display.print("/"); display.print(displayDia); } 
  else { display.print("--/--"); }
  
  // T/G/H Condition Flags on the far right of Line 2
  if (currentState == MONITORING && stressScore > 0) {
      display.setCursor(110, 8); // Right align 
      if (tTrig) display.print("T"); else display.print(" ");
      if (gTrig) display.print("G"); else display.print(" ");
      if (hTrig) display.print("H"); else display.print(" ");
  }

  // Line 3: Temp, Resp Rate, and GSR
  display.setCursor(0, 16);
  display.print("T:");
  if (displayTemp > 0) display.print(displayTemp, 1); else display.print("--.-");
  display.print(" RR:");
  if (displayBrPM > 0) display.print(displayBrPM); else display.print("--");
  display.print(" G:");
  display.print(gsrValue); 

  display.display();
  x++;
  delay(5);
}