#include <ArduinoBLE.h>
#include <Arduino_LPS22HB.h>
#include <Arduino_HS300x.h>
#include "mbed.h"

#define TRIG_PIN A2
#define ECHO_PIN A3

const char* deviceServiceUuid = "19b10000-e8f2-537e-4f6c-d104768a1214";
const char* distanceCharacteristicUuid = "19b10001-e8f2-537e-4f6c-d104768a1214";
const char* temperatureCharacteristicUuid = "19b10002-e8f2-537e-4f6c-d104768a1214";
const char* humidityCharacteristicUuid = "19b10003-e8f2-537e-4f6c-d104768a1214";
const char* pressureCharacteristicUuid = "19b10004-e8f2-537e-4f6c-d104768a1214";

BLEService liquidSensorService(deviceServiceUuid);
BLEFloatCharacteristic distanceCharacteristic(distanceCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic temperatureCharacteristic(temperatureCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic humidityCharacteristic(humidityCharacteristicUuid, BLERead | BLENotify);
BLEFloatCharacteristic pressureCharacteristic(pressureCharacteristicUuid, BLERead | BLENotify);

float distance, temperature, humidity, pressure;

void setup() {
    Serial.begin(9600);
    //while (!Serial);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    if (!BLE.begin()) {
        Serial.println("Failed to initialize BLE!");
        while (1);
    }

    if (!BARO.begin()) {
        Serial.println("Failed to initialize pressure sensor!");
        while (1);
    }

    if (!HS300x.begin()) {
        Serial.println("Failed to initialize temperature and humidity sensor!");
        while (1);
    }

    BLE.setLocalName("LiquidSensor 1");
    BLE.setAdvertisedService(liquidSensorService);

    liquidSensorService.addCharacteristic(distanceCharacteristic);
    liquidSensorService.addCharacteristic(temperatureCharacteristic);
    liquidSensorService.addCharacteristic(humidityCharacteristic);
    liquidSensorService.addCharacteristic(pressureCharacteristic);

    BLE.addService(liquidSensorService);

    distanceCharacteristic.writeValue(0);
    temperatureCharacteristic.writeValue(0);
    humidityCharacteristic.writeValue(0);
    pressureCharacteristic.writeValue(0);

    BLE.advertise();

    Serial.println("Bluetooth device active, waiting for connections...");
}

void loop() {
    BLEDevice central = BLE.central();

    if (central) {
        Serial.print("Connected to central: ");
        Serial.println(central.address());

        while (central.connected()) {
            updateSensorData();
            updateCharacteristics();
            delay(50);  // Update every second
        }

        Serial.print("Disconnected from central: ");
        Serial.println(central.address());
    }
}

void updateSensorData() {
    distance = measureDistance();
    temperature = HS300x.readTemperature();
    humidity = HS300x.readHumidity();
    pressure = BARO.readPressure();

    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.print(" cm, Temp: ");
    Serial.print(temperature);
    Serial.print(" C, Humidity: ");
    Serial.print(humidity);
    Serial.print(" %, Pressure: ");
    Serial.print(pressure);
    Serial.println(" kPa");
}

void updateCharacteristics() {
    distanceCharacteristic.writeValue(distance);
    temperatureCharacteristic.writeValue(temperature);
    humidityCharacteristic.writeValue(humidity);
    pressureCharacteristic.writeValue(pressure);
}

static uint32_t newPulseIn(uint32_t pin, uint32_t state, uint32_t timeout = 300000L){
  uint32_t begin = micros();
  // wait for any previous pulse to end
  while (digitalRead(pin)){
    wait_us(1);
    if (micros() - begin >= timeout)
      return 0;
  }

  // wait for the pulse to start
  while (!digitalRead(pin)){
    wait_us(1);
    if (micros() - begin >= timeout)
      return 0;
  }
  uint32_t pulseBegin = micros();

  // wait for the pulse to stop
  while (digitalRead(pin)){
    wait_us(1);
    if (micros() - begin >= timeout)
      return 0;
  }
  uint32_t pulseEnd = micros();

  return pulseEnd - pulseBegin;
}

float measureDistance() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // 타임아웃을 30000 마이크로초로 설정하여 BLE 통신에 미치는 영향을 줄입니다.
    long duration = newPulseIn(ECHO_PIN, HIGH);
    if (duration == 0) {
        return -1.0;  // 타임아웃이 발생하면 -1.0을 반환
    }
    float distance = duration * 0.034 / 2;

    return distance;
}