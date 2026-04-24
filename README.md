# Seer: Advanced Health & Stress Monitoring System

**Seer** is an STM32-based physiological monitoring system designed to track vital health metrics and emotional stress levels in real-time. By fusing data from multiple sensors, it provides a comprehensive overview of a user's well-being.

## 🚀 Features

- **Dual Heart Rate Monitoring**: Real-time pulse detection using both an analog pulse sensor and the MAX30102 PPG sensor.
- **Pulse Oximetry (SpO2)**: Measures blood oxygen saturation levels via IR and Red light absorption.
- **Blood Pressure Estimation**: Projects Systolic and Diastolic pressure using **Pulse Transit Time (PTT)**—calculating the delay between two distinct pulse detection points.
- **Estimated Respiratory Rate (RR)**: Physiological estimation of breaths per minute based on heart rate variability trends.
- **Stress Detection Engine**:
    - **Baseline Calibration**: Automatically establishes a 30-second physiological baseline upon finger contact.
    - **Multi-Factor Analysis**: Monitors deviations in **GSR** (Galvanic Skin Response), **Skin Temperature**, and **Heart Rate**.
    - **Live Status**: Displays stress levels from `CALM` to `HIGH` based on triggered physiological markers (T: Temperature, G: GSR, H: Heart Rate).
- **Real-time OLED Visualization**: 
    - Dual-channel scrolling waveforms for pulse sensors.
    - Comprehensive dashboard for all vitals.

## 🛠 Hardware Implementation

### Components
- **Microcontroller**: STM32F103C8T6 (Bluepill)
- **Sensors**:
    - **MAX30102**: Heart Rate, SpO2, and Skin Temperature (I2C).
    - **Pulse Sensor**: Analog infrared pulse detection.
    - **GSR Sensor**: Galvanic Skin Response (Skin Conductance) for emotional arousal tracking.
- **Display**: SSD1306 128x64 OLED (I2C).

### Pin Mapping (STM32)
| Component | Pin | Note |
|-----------|-----|------|
| **Pulse Sensor** | `PA5` | Analog Input |
| **GSR Sensor** | `PA3` | Analog Input |
| **I2C SCL** | `PB6` / `SCL1` | Shared (OLED & MAX30102) |
| **I2C SDA** | `PB7` / `SDA1` | Shared (OLED & MAX30102) |
| **OLED Reset** | `N/A` | Tied to MCU Reset or Internal |

## 💻 Software & Libraries

This project is built using the **PlatformIO** IDE and the **Arduino Framework** for STM32.

### Required Libraries
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `SparkFun MAX3010x Pulse and Proximity Sensor Library`

### Build Flags
The project enables USB CDC for serial debugging:
```ini
build_flags = 
    -D PIO_FRAMEWORK_ARDUINO_ENABLE_CDC
    -D USBCON
```

## 🏗 Installation & Setup

1.  **Clone the Repository**:
    ```bash
    git clone <repository-url>
    cd Seer
    ```
2.  **Open in PlatformIO**:
    - Open VS Code.
    - Go to the PlatformIO Home and select "Open Project".
    - Navigate to the `Seer` folder.
3.  **Upload to STM32**:
    - Connect your Bluepill via an **ST-Link V2**.
    - Click the **Upload** button in the PlatformIO toolbar.

## 📈 Usage

1.  Power the device. The OLED will display `[WAIT]` and placeholders until sensors are active.
2.  Place your finger firmly on the MAX30102 and the Pulse Sensor.
3.  **Calibration**: Once a stable signal is detected, a 30-second countdown `[C:30]` begins to establish your baseline vitals.
4.  **Monitoring**: After calibration, the system enters `MONITORING` mode.
    - The top right will show `[CALM]` or your current stress level.
    - Detailed vitals (BT, SpO2, BP, RR, GSR) are updated dynamically.
    - If triggers (T, G, or H) are activated, they will appear as flags on the display.

---

*Disclaimer: Seer is an experimental project for monitoring trends and is not intended for medical diagnostic use.*
