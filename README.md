<div align="center">

# TinyML-Based Intrusion Detection System for Electric Vehicles
*A lightweight, offline, privacy-preserving IDS that detects DoS and Fuzzy attacks on EV CAN networks entirely on-device.*

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/Platform-ESP32%20|%20ESP8266-green)
![Framework](https://img.shields.io/badge/Framework-TensorFlow%20Lite-orange)
![Language](https://img.shields.io/badge/Language-C++%20|%20Python-yellow)

</div>

---

## 📖 Table of Contents

- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Hardware Requirements](#-hardware-requirements)
- [Repository Structure](#-repository-structure)
- [Dataset](#-dataset)
- [Model Pipeline](#-model-pipeline)
- [Getting Started](#-getting-started)
- [How It Works](#-how-it-works)
- [Results](#-results)
- [Sample Data](#-sample-data)
- [Scope & Limitations](#-scope--limitations)
- [Related Publication](#-related-publication)
- [License](#-license)
- [Contributors](#-contributors)

---

## 🔍 Overview

As Electric Vehicles evolve towards autonomous driving through **V2X (Vehicle-to-Everything)** communication, they become increasingly vulnerable to cyberattacks, particularly on the **CAN (Controller Area Network) network**, which is the backbone of in-vehicle communication.

Most existing Intrusion Detection Systems (IDS) rely on **cloud-based architectures** that introduce latency, require frequent internet connectivity, and raise privacy concerns - all of which are critical for such automotive systems.

This project is a **proof-of-concept** demonstrating that an IDS of this class fits
within microcontroller resource limits. It is an **edge-based IDS** built with **TinyML** that:

- Runs entirely on an **ESP32** microcontroller (~$5 hardware).
- Detects **DoS** and **Fuzzy attacks** on-device.
- Operates **fully offline** - no cloud dependency.
- Preserves **data privacy** - all processing happens on-device.
- Achieves **87.05% test accuracy** with a 48.68 KB model and a 30 KB tensor arena.

---

## 📄 System Architecture

```
      ┌─────────────────────────────┐        WiFi (TCP)          ┌──────────────────────────────────┐
      │     ESP8266 (Attacker)      │ ────────────────────────>  │       ESP32 (Vehicle/IDS)        │
      │                             │    CAN frames over WiFi    │                                  │
      │  • Reads CAN data via       │                            │  • Receives CAN frames           │
      │    Serial from laptop       │                            │  • Preprocesses (hex→numeric,    │
      │  • Simulates attack         │                            │    MinMax scaling)               │
      │    traffic (DoS / Fuzzy)    │                            │  • Runs TFLite MLP inference     │
      │  • Transmits to ESP32       │                            │  • Classifies: Attack_Free /     │
      │    via TCP socket           │                            │    DoS / Fuzzy                   │
      │                             │                            │  • Displays on 16x2 LCD          │
      │  Hardware: NodeMCU/         │                            │  • Logs to SD card               │
      │  Wemos D1 Mini              │                            │  • RGB LED alert indicator       │
      └─────────────────────────────┘                            └──────────────────────────────────┘
```

Both devices connect to the **same WiFi network**. Recorded CAN frames are replayed
over TCP rather than transmitted on a physical CAN bus - **no CAN transceiver
(e.g. MCP2515/TJA1050) is used anywhere in this project**.

This bench setup exercises the complete on-device path - parsing, preprocessing,
inference, alerting and logging - which is what the proof of concept sets out to show.
It does **not** reproduce CAN bus electrical behaviour, arbitration, or real bus timing,
so it cannot stand in for validation on a live vehicle network.

---

## 🪛 Hardware Requirements

### ESP32 - Receiver / Vehicle Node (IDS)

| Component | Connection | ESP32 Pin |
|---|---|---|
| **SD Card Module** | VCC | VIN |
| | GND | GND |
| | MISO | GPIO 19 |
| | MOSI | GPIO 23 |
| | SCK | GPIO 18 |
| | CS | GPIO 5 |
| **I2C LCD Module (16×2)** | VCC | 3.3V |
| | GND | GND |
| | SDA | GPIO 21 |
| | SCL | GPIO 22 |
| **RGB LED** | Red | GPIO 25 |
| | Green | GPIO 26 |
| | Common Anode | 3.3V |

### ESP8266 - Transmitter / Attacker Node

| Component | Connection |
|---|---|
| **ESP8266** | USB to Laptop (Serial input) |

---

## 📁 Repository Structure

```
📦 TinyML-Based-Intrusion-Detection-System-for-EVs
├── 📂 01_Dataset/
│   ├── Merged_CAN_Dataset_Shuffled.csv    # ~3.5M CAN frames (gitignored — large file)
│   └── dataset_prep.ipynb                 # Data preparation & preprocessing notebook
│
├── 📂 02_Model/
│   ├── 01_IDS_Train_Model.ipynb           # Model training notebook (MLP)
│   ├── 02_confusion_matrix.png            # Confusion matrix visualization
│   ├── 03_training_history.png            # Training accuracy & loss curves
│   ├── ids_model.h5                       # Keras model (gitignored)
│   ├── ids_model.keras                    # Keras model (gitignored)
│   ├── ids_model.tflite                   # TensorFlow Lite model (gitignored)
│   └── model_data.h                       # TFLite model as C byte array (gitignored)
│
├── 📂 04_Reciever_ESP32_Code/
│   ├── 04_Reciever_ESP32_Code.ino         # ESP32 Arduino sketch (IDS main code)
│   └── model_data.h                       # TFLite model header for deployment
│
├── 📂 05_Transmitter_ESP8266_Code/
│   ├── 05_Transmitter_ESP8266_Code.ino    # ESP8266 Arduino sketch (attacker transmitter)
│   └── Sample CAN Data.txt               # Sample CAN frames for testing
│
├── hardware_pin_connections.png           # Hardware wiring diagram
├── .gitignore
├── LICENSE                                # MIT License
└── README.md
```

> ⚠️ Large files (`.csv`, `.h5`, `.keras`, `.tflite`, `model_data.h`) are excluded from the repo via `.gitignore`. Run the notebooks to regenerate them.

---

## 📊 Dataset

- **Source:** OTIDS CAN intrusion dataset - **4,500,479 raw frames**
- **Frames used:** **3,505,007** after excluding the 995,472 `Impersonation` frames
- **Classes:** 3 - `Attack_Free` (2,268,519), `DoS` (656,579), `Fuzzy` (579,909)
- **Split:** 80/20 stratified train/test (`random_state=42`) - 701,002 test frames
- **Features per frame:** 10 - `CAN_ID` (numeric), `DLC`, `DATA[0]` through `DATA[7]`
- **Preprocessing:**
  - CAN IDs converted from hex to numeric
  - Data bytes parsed from hex
  - MinMaxScaler normalization applied
  - `Impersonation` class dropped (not modelled in this work)
  - Dataset shuffled for training

The `01_Dataset/dataset_prep.ipynb` notebook handles all data preparation steps.

---

## 🧠 Model Pipeline

### 1. Primary Model - MLP (Multi-Layer Perceptron)
- **Architecture:** 10 → 128 → 64 → 32 → 3, ReLU hidden layers, softmax output
- **Regularization:** Dropout (0.3, 0.2), EarlyStopping, ReduceLROnPlateau
- **Training:** Adam (lr=0.001), batch size 128, up to 150 epochs with early stopping,
  20% validation split
- **Input:** 10 features - `CAN_ID` (numeric), `DLC`, `DATA[0]`–`DATA[7]`
- **Output:** 3-class softmax (`Attack_Free`, `DoS`, `Fuzzy`)

### 2. Reference Baseline - XGBoost
- A non-deployable reference point, trained **separately from this notebook** and not
  included in this repository
- Achieves **90.79% test accuracy** on the identical split - 3.74 points above the MLP,
  at roughly **55× the model size** (2.61 MB vs 48.68 KB)
- Shows the *same* DoS/Fuzzy recall weakness as the MLP, indicating the shortfall is a
  limitation of the 10-feature representation rather than of model capacity

### 3. Deployment - TensorFlow Lite
- Keras model → `.tflite` conversion
- `.tflite` → C byte array (`model_data.h`) for ESP32 embedding
- **Float32** weights - no post-training quantization is applied (`optimizations = []`)
- Model size: **48.68 KB** (`ids_model.tflite`)
- Tensor arena: **30 KB** statically allocated on ESP32
- Ops registered: `FullyConnected`, `Softmax`, `Relu`, `Quantize`, `Dequantize`
- Conversion is **lossless**: the `.tflite` model reproduces the Keras predictions on
  100% of the 701,002 test frames (max output difference 1.55e-06)

---

## 🚀 Getting Started

### Prerequisites

- [Arduino IDE](https://www.arduino.cc/en/software) (v1.8+ or v2.x)
- ESP32 Board Package installed in Arduino IDE
- ESP8266 Board Package installed in Arduino IDE
- Python 3.x with Jupyter Notebook (for dataset prep & model training)

### Arduino Libraries Required

<div align="center">

| Library | Purpose |
|---|---|
| `TensorFlowLite_ESP32` | TFLite Micro runtime for ESP32 |
| `LiquidCrystal_I2C` | I2C LCD display driver |
| `SD` | SD card read/write |
| `SPI` | SPI communication |
| `WiFi` (ESP32) / `ESP8266WiFi` | WiFi connectivity |

</div>

### Step-by-Step Setup

#### 1️⃣ Prepare the Dataset
```bash
# Open and run the dataset preparation notebook
jupyter notebook 01_Dataset/dataset_prep.ipynb
```

#### 2️⃣ Train the Model
```bash
# Open and run the model training notebook
jupyter notebook 02_Model/01_IDS_Train_Model.ipynb
```
This generates `ids_model.tflite` and `model_data.h`.

#### 3️⃣ Flash the ESP32 (Receiver / IDS Node)
1. Open `04_Reciever_ESP32_Code/04_Reciever_ESP32_Code.ino` in Arduino IDE
2. Ensure `model_data.h` is in the same folder
3. Update WiFi credentials:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   ```
4. Select board: **ESP32 Dev Module**
5. Upload to ESP32
6. Note the **IP address** printed on Serial Monitor

#### 4️⃣ Flash the ESP8266 (Transmitter / Attacker Node)
1. Open `05_Transmitter_ESP8266_Code/05_Transmitter_ESP8266_Code.ino` in Arduino IDE
2. Update WiFi credentials and **ESP32's IP address**:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* ESP32_SERVER_IP = "192.168.x.x";  // ESP32's IP from step 3
   ```
3. Select board: **NodeMCU 1.0 (ESP-12E Module)** or equivalent
4. Upload to ESP8266

#### 5️⃣ Run the System
1. Power on both devices (same WiFi network)
2. ESP32 starts a TCP server on **port 8888**
3. On ESP8266's Serial Monitor, paste sample CAN data rows:
   ```
   0165000,8,0e,e8,7f,00,00,00,07,9e
   ```
4. ESP32 receives, preprocesses, runs inference, and displays the result

---

## ⚙️ How It Works

1. **Data Input** - CAN frames are fed to the ESP8266 via Serial Monitor (simulating attack/normal traffic)
2. **Wireless Transmission** - ESP8266 transmits each frame to the ESP32 over WiFi (TCP socket on port 8888)
3. **On-Device Preprocessing** - ESP32 parses the raw CAN frame, converts hex values to numeric, and applies MinMaxScaler normalization using pre-computed scaler parameters
4. **TFLite Inference** - The preprocessed 10-feature vector is fed into the MLP model running on TensorFlow Lite Micro
5. **Classification** - Output softmax probabilities determine the class: `Attack_Free`, `DoS`, or `Fuzzy`
6. **Alerting & Logging:**
   - **LCD Display:** Shows prediction and confidence percentage
   - **RGB LED:** Green blink = normal, Red = attack detected
   - **SD Card:** Logs all detections to `/intrusion_log.csv`
   - **Serial Monitor:** Detailed output with probabilities and inference time

---

## 📈 Results

### Model Performance (Offline Validation)

> All figures below are **offline** metrics computed on the 701,002-frame held-out test
> set. On-device latency, throughput, RAM/CPU utilization and power draw have **not**
> been measured - see [Scope & Limitations](#-scope--limitations).

<div align="center">

| Metric | XGBoost (Reference) | MLP (TFLite Deployed) |
|---|---|---|
| **Test Accuracy** | 90.79% | **87.05%** |
| **Macro F1** | 0.868 | **0.815** |
| **MCC** | 0.819 | **0.743** |
| **ROC-AUC** (OvR macro) | 0.965 | **0.937** |
| **Model Size** | 2.61 MB | **48.68 KB** (.tflite) |
| **Tensor Arena** | N/A | **30 KB** |
| **Target Hardware** | PC (not deployable on MCU) | **ESP32** |

</div>

### Confusion Matrix

<div align="center">

| | Predicted: Attack_Free | Predicted: DoS | Predicted: Fuzzy |
|---|---|---|---|
| **Actual: Attack_Free** | 436,942 | 2,988 | 13,774 |
| **Actual: DoS** | 30,073 | 88,961 | 12,282 |
| **Actual: Fuzzy** | 26,198 | 5,457 | 84,327 |

</div>

### Per-Class Performance (MLP, deployed TFLite model)

<div align="center">

| Class | Precision | Recall | F1 | FPR | FNR |
|---|---|---|---|---|---|
| **Attack_Free** | 0.886 | 0.963 | 0.923 | 0.228 | 0.037 |
| **DoS** | 0.913 | 0.677 | 0.778 | 0.015 | 0.323 |
| **Fuzzy** | 0.764 | 0.727 | 0.745 | 0.045 | 0.273 |

</div>

> **Known limitation.** DoS recall (0.68) is the weakest result. The XGBoost reference,
> despite being ~55× larger, shows the same weakness (0.749 DoS / 0.745 Fuzzy recall).
> This points to the per-frame 10-feature representation, which carries no inter-frame
> timing information, rather than to model capacity. DoS on CAN is a timing-preserving
> attack, so adding inter-arrival-time features is the clearest path to improvement.

### Training

Trained with Adam (lr=0.001), batch size 128, up to 150 epochs with EarlyStopping and
ReduceLROnPlateau on a 20% validation split.

> 📊 See `02_Model/02_confusion_matrix.png` and `02_Model/03_training_history.png` for visual plots.

---

## 🧪 Sample Data

Use these sample CAN frames to test the system:

- Attack_Free samples
```
0080000,8,00,17,1c,0a,1d,17,1d,94
0165000,8,0f,e8,7f,00,00,00,06,9e
0370000,8,ff,20,00,80,ff,00,00,ec
```

- DoS attack samples
```
0000000,8,00,00,00,00,00,00,00,00
01f1000,8,00,00,00,00,00,00,00,00
02a0000,8,62,00,88,9d,bc,0c,b7,02
```

- Fuzzy attack samples
```
0220000,8,c2,03,ef,03,0c,00,45,10
0164000,8,50,b6,bb,ff,1a,30,a8,47
01f1000,8,1a,30,1f,4f,30,62,79,91
```

**Format:** `CAN_ID(decimal), DLC, DATA[0](hex), ..., DATA[7](hex)`

---

## 🔬 Scope & Limitations

This is a feasibility study, and the following are **not** claimed or demonstrated:

- **No on-device performance measurements.** Inference latency, throughput, RAM/CPU
  utilization, power draw and jitter were never instrumented. Only the static footprint
  (48.68 KB flash, 30 KB tensor arena) is known.
- **No physical CAN bus.** Frames are replayed over WiFi/TCP. There is no CAN
  transceiver, no arbitration, and no bus-accurate timing.
- **Weak DoS recall (0.68).** See the note in [Results](#-results). Not addressable
  without inter-frame timing features.
- **Two attack classes only.** `Impersonation` was excluded; replay, masquerade and
  bus-off attacks are out of scope.
- **Single dataset.** Trained and evaluated on OTIDS only, so generalization to other
  vehicles or bus configurations is untested.
- **No real-time guarantees.** The system is not evaluated against the deadlines a
  safety-critical deployment would require.

---

## 📄 Related Publication

This work is described in a paper accepted at **IFIP IoT 2026**, which reports the
same figures as this README along with a fuller evaluation and discussion of
limitations. Where the two differ, the paper is authoritative.

---

## 📜 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

## 👥 Contributors

This project was developed as a **Minor Project (Semester 3)** at **IIIT Naya Raipur**.

<table>
  <tr>
    <td align="center"><a href="https://github.com/akshita24101"><b>Akshita Sondhi</b></a></td>
    <td align="center"><a href="https://github.com/exorev07"><b>Ekansh Arohi</b></a></td>
    <td align="center"><a href="https://github.com/karishmasroy"><b> Karishma Singha Roy</b></a></td>
  </tr>
</table>

---

<div align="center">

**If you found this project useful, consider giving it a star!**

</div>
