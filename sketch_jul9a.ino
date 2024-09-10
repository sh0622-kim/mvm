#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <mbed.h>
#include "KVStore.h"
#include "kvstore_global_api.h"
#include <ArduinoBLE.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define WIRE Wire
// Declaration for SSD1306 display connected using software SPI (default case):
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &WIRE);

#define BUTTON_UP_PIN 6
#define BUTTON_DOWN_PIN 3
#define BUTTON_LEFT_PIN 7
#define BUTTON_RIGHT_PIN 2
#define BUTTON_SELECT_PIN 5

// 버튼 핀 정의
const int BUTTON_PINS[] = {BUTTON_UP_PIN, BUTTON_DOWN_PIN, BUTTON_LEFT_PIN, BUTTON_RIGHT_PIN, BUTTON_SELECT_PIN};
const int BUTTON_COUNT = 5;

#define MAIN_SCREEN 0
#define SETTINGS_SCREEN 1

#define MAX_DEVICES 5

// 아이콘 정의 (8x8 픽셀)
const unsigned char PROGMEM icon_temperature[] = {
    B00111100,
    B01000010,
    B01000010,
    B01000010,
    B01000010,
    B01111110,
    B00111100,
    B00011000
};

const unsigned char PROGMEM icon_humidity[] = {
    B00001000,
    B00011100,
    B00111110,
    B01111111,
    B01111111,
    B00111110,
    B00011100,
    B00001000
};

const char* deviceServiceUuid = "19b10000-e8f2-537e-4f6c-d104768a1214";
const char* distanceCharacteristicUuid = "19b10001-e8f2-537e-4f6c-d104768a1214";
const char* temperatureCharacteristicUuid = "19b10002-e8f2-537e-4f6c-d104768a1214";
const char* humidityCharacteristicUuid = "19b10003-e8f2-537e-4f6c-d104768a1214";
const char* pressureCharacteristicUuid = "19b10004-e8f2-537e-4f6c-d104768a1214";

struct DeviceSettings {
    float minHeight;
    float diameter;
    float targetCapacity;
};

struct PeripheralDevice {
    BLEDevice device;
    bool isConnected;
    float distance;
    float temperature;
    float humidity;
    float pressure;
    char name[20];
    DeviceSettings settings;
    unsigned long lastUpdateTime;
};

PeripheralDevice peripherals[MAX_DEVICES];
int connectedPeripherals = 0;
int currentDevice = 0;
int currentScreen = MAIN_SCREEN;
int settingsMenuItem = 0;

unsigned long lastScanTime = 0;
const unsigned long scanInterval = 5000; // 5초마다 스캔
bool isScanning = false;

unsigned long lastReconnectAttempt[MAX_DEVICES] = {0};
const unsigned long RECONNECT_INTERVAL = 5000; // 5초마다 재연결 시도

// 버튼 상태 구조체
struct ButtonState {
    bool isPressed;
    bool wasPressed;
    unsigned long lastDebounceTime;
    unsigned long holdTime;
    unsigned long lastRepeatTime;
};

ButtonState buttonStates[BUTTON_COUNT];

// 디바운스 및 반복 설정
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long REPEAT_DELAY = 300;
const unsigned long REPEAT_INTERVAL = 100;

// 설정 조정을 위한 변수
float adjustmentSpeed = 1.0;
unsigned long lastAdjustmentTime = 0;

void setupButtons() {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(BUTTON_PINS[i]), buttonInterrupt, CHANGE);
    }
}

void buttonInterrupt() {
    // 인터럽트 처리는 최소한으로 유지
    // 실제 처리는 main loop에서 수행
}

void setup() {
    Serial.begin(9600);
    //while (!Serial);
    Serial.println("Remote Control Setup Started");

    pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
    pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);

    for (int i = 0; i < 5; i++) {
        buttonStates[i] = {HIGH, HIGH, 0};
    }

    Serial.println("Button pins initialized");

    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        //while (1);
    }
    display.clearDisplay();
    display.display();
    Serial.println("Display initialized");

    if (!BLE.begin()) {
        Serial.println("Failed to initialize BLE!");
        while (1);
    }
    Serial.println("BLE initialized");

    BLE.setConnectionInterval(0x0006, 0x0c80);

    loadSettings();
    initializeDevices();

    BLE.setEventHandler(BLEDiscovered, blePeripheralDiscoveredCallback);
    BLE.setEventHandler(BLEConnected, blePeripheralConnectedCallback);
    BLE.setEventHandler(BLEDisconnected, blePeripheralDisconnectedCallback);
    Serial.println("BLE Central device ready");
    Serial.println("Scanning for peripherals...");
    startScan();
}

void loop() {
    BLE.poll();
    
    unsigned long currentTime = millis();
    
    // 연결된 장치 데이터 업데이트 및 연결 상태 확인
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (peripherals[i].isConnected) {
            if (peripherals[i].device.connected()) {
                if (currentTime - peripherals[i].lastUpdateTime > 1000) {
                    receiveData(peripherals[i].device, i);
                    peripherals[i].lastUpdateTime = currentTime;
                }
            } else {
                // 연결이 끊어진 경우 즉시 재연결 시도
                Serial.print("Connection lost with device ");
                Serial.print(i);
                Serial.println(". Attempting to reconnect...");
                peripherals[i].isConnected = false;
                connectedPeripherals--;
                attemptReconnection(i);
            }
        } else if (currentTime - lastReconnectAttempt[i] > RECONNECT_INTERVAL) {
            // 주기적으로 재연결 시도
            attemptReconnection(i);
            lastReconnectAttempt[i] = currentTime;
        }
    }

    // 연결된 장치가 없으면 스캔 시작
    if (connectedPeripherals == 0 && !isScanning) {
        startScan();
    }

    handleButtons();
    updateDisplay();
    delay(10);
}


void attemptReconnection(int index) {
    if (peripherals[index].device) {
        Serial.print("Attempting to reconnect to device at index ");
        Serial.println(index);
        
        if (connectToPeripheral(peripherals[index].device)) {
            Serial.println("Reconnection successful");
            peripherals[index].isConnected = true;
            connectedPeripherals++;
        } else {
            Serial.println("Reconnection failed. Will try again later.");
        }
    }
}

void reconnectToPeripheral(int index) {
    if (peripherals[index].device) {
        Serial.print("Attempting to reconnect to device at index ");
        Serial.println(index);
        
        if (connectToPeripheral(peripherals[index].device)) {
            Serial.println("Reconnection successful");
        } else {
            Serial.println("Reconnection failed. Will try again later.");
        }
    }
}

void handleButtons() {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        bool reading = digitalRead(BUTTON_PINS[i]) == LOW;
        if (reading != buttonStates[i].wasPressed) {
            buttonStates[i].lastDebounceTime = millis();
        }

        if ((millis() - buttonStates[i].lastDebounceTime) > DEBOUNCE_DELAY) {
            if (reading != buttonStates[i].isPressed) {
                buttonStates[i].isPressed = reading;
                if (buttonStates[i].isPressed) {
                    onButtonPressed(i);
                } else {
                    onButtonReleased(i);
                }
            }
        }

        if (buttonStates[i].isPressed) {
            unsigned long holdTime = millis() - buttonStates[i].lastDebounceTime;
            if (holdTime > REPEAT_DELAY && (millis() - buttonStates[i].lastRepeatTime) > REPEAT_INTERVAL) {
                onButtonHeld(i);
                buttonStates[i].lastRepeatTime = millis();
            }
        }

        buttonStates[i].wasPressed = reading;
    }
}

void onButtonPressed(int button) {
    switch (button) {
        case 0: // Up
            if (currentScreen == SETTINGS_SCREEN) {
                settingsMenuItem = (settingsMenuItem - 1 + 4) % 4;
            }
            break;
        case 1: // Down
            if (currentScreen == SETTINGS_SCREEN) {
                settingsMenuItem = (settingsMenuItem + 1) % 4;
            }
            break;
        case 2: // Left
            if (currentScreen == MAIN_SCREEN) {
                currentDevice = (currentDevice - 1 + MAX_DEVICES) % MAX_DEVICES;
            } else if (currentScreen == SETTINGS_SCREEN) {
                adjustSetting(-1);
            }
            break;
        case 3: // Right
            if (currentScreen == MAIN_SCREEN) {
                currentDevice = (currentDevice + 1) % MAX_DEVICES;
            } else if (currentScreen == SETTINGS_SCREEN) {
                adjustSetting(1);
            }
            break;
        case 4: // Select
            currentScreen = (currentScreen == MAIN_SCREEN) ? SETTINGS_SCREEN : MAIN_SCREEN;
            settingsMenuItem = 0;
            break;
    }
    updateDisplay();
}

void onButtonReleased(int button) {
    adjustmentSpeed = 1.0;
}

void onButtonHeld(int button) {
    if (currentScreen == SETTINGS_SCREEN && (button == 2 || button == 3)) {
        unsigned long now = millis();
        adjustmentSpeed = min(adjustmentSpeed * 1.1, 10.0);
        if (now - lastAdjustmentTime > 50) {
            adjustSetting(button == 2 ? -adjustmentSpeed : adjustmentSpeed);
            lastAdjustmentTime = now;
        }
    }
}

void loadSettings() {
    Serial.println("Loading settings");
    char key[32];
    char value[64];
    size_t actual_size;

    for (int i = 0; i < MAX_DEVICES; i++) {
        snprintf(key, sizeof(key), "device_%d_settings", i);
        if (kv_get(key, value, sizeof(value), &actual_size) == MBED_SUCCESS) {
            sscanf(value, "%f,%f,%f,%[^,]", &peripherals[i].settings.minHeight, 
                   &peripherals[i].settings.diameter, &peripherals[i].settings.targetCapacity, 
                   peripherals[i].name);
            Serial.print("Loaded settings for device ");
            Serial.println(i);
        } else {
            // Default values if not found
            peripherals[i].settings.minHeight = 100.0;
            peripherals[i].settings.diameter = 50.0;
            peripherals[i].settings.targetCapacity = 200.0;
            snprintf(peripherals[i].name, sizeof(peripherals[i].name), "Device %d", i + 1);
            Serial.print("Using default settings for device ");
            Serial.println(i);
        }
    }
}

void saveSettings() {
    Serial.println("Saving settings");
    char key[32];
    char value[64];

    for (int i = 0; i < MAX_DEVICES; i++) {
        snprintf(key, sizeof(key), "device_%d_settings", i);
        snprintf(value, sizeof(value), "%f,%f,%f,%s", 
                 peripherals[i].settings.minHeight, 
                 peripherals[i].settings.diameter, 
                 peripherals[i].settings.targetCapacity,
                 peripherals[i].name);
        if (kv_set(key, value, strlen(value), 0) == MBED_SUCCESS) {
            Serial.print("Saved settings for device ");
            Serial.println(i);
        } else {
            Serial.print("Failed to save settings for device ");
            Serial.println(i);
        }
    }
}

void initializeDevices() {
    Serial.println("Initializing devices");
    for (int i = 0; i < MAX_DEVICES; i++) {
        peripherals[i].isConnected = false;
    }
}


void startScan() {
    Serial.println("Starting BLE scan...");
    BLE.scanForUuid(deviceServiceUuid);
    isScanning = true;
}

void blePeripheralDiscoveredCallback(BLEDevice peripheral) {
    if (peripheral.advertisedServiceUuid() == deviceServiceUuid) {
        Serial.print("Discovered device: ");
        Serial.println(peripheral.address());
        BLE.stopScan();
        isScanning = false;
        
        if (connectToPeripheral(peripheral)) {
            int index = findAvailablePeripheralSlot();
            if (index != -1) {
                peripherals[index].device = peripheral;
                peripherals[index].isConnected = true;
                strncpy(peripherals[index].name, peripheral.localName().c_str(), sizeof(peripherals[index].name) - 1);
                peripherals[index].name[sizeof(peripherals[index].name) - 1] = '\0';
                connectedPeripherals++;
                receiveData(peripheral, index);
            }
        } else {
            startScan();  // 연결 실패 시 다시 스캔 시작
        }
    }
}

bool connectToPeripheral(BLEDevice peripheral) {
    Serial.println("Attempting to connect to peripheral...");
    
    const int MAX_ATTEMPTS = 3;
    const unsigned long CONNECTION_TIMEOUT = 5000;  // 5 seconds timeout
    
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        Serial.print("Connection attempt ");
        Serial.print(attempt);
        Serial.print(" of ");
        Serial.println(MAX_ATTEMPTS);
        
        if (peripheral.connect()) {
            Serial.println("Connected to peripheral");            
            if (peripheral.discoverAttributes()) {
                Serial.println("Attributes discovered");
                return true;  // Successfully connected and attributes discovered
            } else {
                Serial.println("Attribute discovery failed");
                peripheral.disconnect();
            }
        }
        
        Serial.println("Connection attempt failed");
        delay(1000);  // Wait before next attempt
    }
    
    Serial.println("Failed to connect after maximum attempts");
    return false;
}

void blePeripheralConnectedCallback(BLEDevice central) {
    Serial.print("Connected event, central: ");
    Serial.println(central.address());
    
    // 연결된 장치 찾기
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (peripherals[i].device == central) {
            peripherals[i].isConnected = true;
            Serial.print("Device at index ");
            Serial.print(i);
            Serial.println(" marked as connected");
            break;
        }
    }
}

void blePeripheralDisconnectedCallback(BLEDevice central) {
    Serial.print("Disconnected event, central: ");
    Serial.println(central.address());
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (peripherals[i].device == central) {
            peripherals[i].isConnected = false;
            connectedPeripherals--;
            Serial.print("Device at index ");
            Serial.print(i);
            Serial.println(" marked as disconnected");
            break;
        }
    }
}

int findAvailablePeripheralSlot() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!peripherals[i].isConnected) {
            Serial.print("Available slot found at index ");
            Serial.println(i);
            return i;
        }
    }
    Serial.println("No available slot found");
    return -1;
}

void adjustSetting(float adjustment) {
    switch (settingsMenuItem) {
        case 0:
            peripherals[currentDevice].settings.minHeight += adjustment;
            break;
        case 1:
            peripherals[currentDevice].settings.diameter += adjustment;
            if (peripherals[currentDevice].settings.diameter < 0)
                peripherals[currentDevice].settings.diameter = 0;
            break;
        case 2:
            peripherals[currentDevice].settings.targetCapacity += adjustment;
            if (peripherals[currentDevice].settings.targetCapacity < 0)
                peripherals[currentDevice].settings.targetCapacity = 0;
            break;
        case 3:
            // 이름 변경 로직 (필요한 경우 구현)
            break;
    }
    updateDisplay();
    saveSettings();
}

void updateDisplay() {
    display.clearDisplay();

    if (currentScreen == MAIN_SCREEN) {
        updateMainScreen();
    } else {
        updateSettingsScreen();
    }

    display.display();
}

void updateMainScreen() {
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);

    // 장치 이름 및 연결 상태 표시 개선
    if (peripherals[currentDevice].isConnected) {
        displaySafeName(peripherals[currentDevice].name, 12);  // 최대 12자로 제한
        display.print(" [C]");
    } else {
        display.print("Sensor ");
        display.print(currentDevice + 1);
        display.print(" [D]");
    }

    if (peripherals[currentDevice].isConnected) {
        // 볼륨 표시
        float volume = calculateVolume(peripherals[currentDevice].distance);
        display.setCursor(0, 16);
        display.setTextSize(2);
        display.print(volume, 1);
        display.setTextSize(1);
        display.print(" L");

        // 진행 바
        float targetCapacity = peripherals[currentDevice].settings.targetCapacity > 0 ? 
                               peripherals[currentDevice].settings.targetCapacity : 100.0;
        int barWidth = map(volume, 0, targetCapacity, 0, 128);
        display.drawRect(0, 36, 128, 8, WHITE);
        display.fillRect(0, 36, barWidth, 8, WHITE);

        // 센서 데이터 아이콘 및 값
        display.drawBitmap(0, 48, icon_temperature, 8, 8, WHITE);
        display.setCursor(10, 48);
        display.print(peripherals[currentDevice].temperature, 1);
        display.print("C");

        display.drawBitmap(64, 48, icon_humidity, 8, 8, WHITE);
        display.setCursor(74, 48);
        display.print(peripherals[currentDevice].humidity, 1);
        display.print("%");
    } else {
        display.setCursor(0, 32);
        display.setTextSize(2);
        display.print("Offline");
    }

    // 현재 장치 표시기
    for (int i = 0; i < MAX_DEVICES; i++) {
        display.fillRect(i * 25, 62, 20, 2, i == currentDevice ? WHITE : BLACK);
    }
}

void updateSettingsScreen() {
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("Settings");
    display.drawLine(0, 9, 128, 9, WHITE);

    const char* menuItems[] = {"Min Height", "Diameter", "Target Cap.", "Name"};
    for (int i = 0; i < 4; i++) {
        display.setCursor(0, 12 + i * 13);
        if (i == settingsMenuItem) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.print(menuItems[i]);
        display.print(": ");
        
        if (i < 3) {
            float value = getSettingValue(i);
            if (value >= 0) {
                display.print(value, 1);
            } else {
                display.print("N/A");
            }
        } else {
            if (peripherals[currentDevice].isConnected) {
                displaySafeName(peripherals[currentDevice].name, 10);  // 최대 10자로 제한
            } else {
                display.print("N/A");
            }
        }
    }
}

// 안전하게 이름을 표시하는 함수
void displaySafeName(const char* name, int maxLength) {
    if (name != NULL && name[0] != '\0') {
        char displayName[maxLength + 1];
        strncpy(displayName, name, maxLength);
        displayName[maxLength] = '\0';  // null 종료 문자 추가
        display.print(displayName);
    } else {
        display.print("Unnamed");
    }
}


float getSettingValue(int index) {
  if (!peripherals[currentDevice].isConnected) {
        return -1;  // 연결되지 않은 경우 유효하지 않은 값 반환
    }

    switch (index) {
        case 0: return peripherals[currentDevice].settings.minHeight;
        case 1: return peripherals[currentDevice].settings.diameter;
        case 2: return peripherals[currentDevice].settings.targetCapacity;
        default: return -1;
    }
}

void receiveData(BLEDevice peripheral, int deviceIndex) {
    BLECharacteristic distanceChar = peripheral.characteristic(distanceCharacteristicUuid);
    BLECharacteristic tempChar = peripheral.characteristic(temperatureCharacteristicUuid);
    BLECharacteristic humidChar = peripheral.characteristic(humidityCharacteristicUuid);
    BLECharacteristic pressChar = peripheral.characteristic(pressureCharacteristicUuid);

    if (distanceChar && tempChar && humidChar && pressChar) {
        distanceChar.read();
        tempChar.read();
        humidChar.read();
        pressChar.read();

        peripherals[deviceIndex].distance = *(float*)distanceChar.value();
        peripherals[deviceIndex].temperature = *(float*)tempChar.value();
        peripherals[deviceIndex].humidity = *(float*)humidChar.value();
        peripherals[deviceIndex].pressure = *(float*)pressChar.value();

        Serial.print("Updated data for device ");
        Serial.println(deviceIndex);
    } else {
        Serial.println("Failed to read one or more characteristics");
    }
}


float calculateVolume(float distance) {
    if (!peripherals[currentDevice].isConnected) {
        return 0;  // 연결되지 않은 경우 0 반환
    }
    float height = peripherals[currentDevice].settings.minHeight - distance;
    if (height < 0) height = 0;
    float radius = peripherals[currentDevice].settings.diameter / 2;
    if (radius <= 0) return 0;  // 잘못된 직경 설정 방지
    float volume = PI * radius * radius * height / 1000;  // 리터 단위로 변환
    return volume;
}

void printDeviceStatus() {
    Serial.println("Current Device Status:");
    for (int i = 0; i < MAX_DEVICES; i++) {
        Serial.print("Device ");
        Serial.print(i);
        Serial.print(": ");
        if (peripherals[i].isConnected) {
            Serial.print("Connected - ");
            Serial.print("Name: ");
            Serial.print(peripherals[i].name);
            Serial.print(", Distance: ");
            Serial.print(peripherals[i].distance);
            Serial.print(", Temp: ");
            Serial.print(peripherals[i].temperature);
            Serial.print(", Humidity: ");
            Serial.print(peripherals[i].humidity);
            Serial.print(", Pressure: ");
            Serial.println(peripherals[i].pressure);
        } else {
            Serial.println("Not Connected");
        }
    }
}

void checkBLEStatus() {
    Serial.print("BLE Central State: ");
    /*
    switch(BLE.central().state()) {
        case BLEState::BLE_DISCONNECTED:
            Serial.println("Disconnected");
            break;
        case BLEState::BLE_CONNECTING:
            Serial.println("Connecting");
            break;
        case BLEState::BLE_CONNECTED:
            Serial.println("Connected");
            break;
    }*/
}

// 주기적으로 상태를 확인하고 출력하는 함수
void periodicStatusCheck() {
    static unsigned long lastStatusTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastStatusTime > 10000) { // 10초마다 상태 확인
        lastStatusTime = currentTime;
        Serial.println("\n----- Periodic Status Check -----");
        printDeviceStatus();
        checkBLEStatus();
        Serial.println("-----------------------------------\n");
    }
}