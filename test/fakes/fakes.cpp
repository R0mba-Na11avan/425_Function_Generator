/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: state behind the fake Arduino, SPI and FreeRTOS headers
**************************************************************************/

#include "Arduino.h"
#include "SPI.h"
#include "freertos/semphr.h"

//pins
static int pinModes[FAKE_PIN_COUNT];
//the output latch: what the pin drives once it is an OUTPUT (LOW after reset,
//like the ESP32's GPIO_OUT register)
static int pinLevels[FAKE_PIN_COUNT];
//pinMode() has been called since the last reset. like ESP32 core 3.x,
//digitalWrite() on a pin that isn't claimed yet is dropped
static bool pinClaimed[FAKE_PIN_COUNT];
//the pin most recently driven LOW: the chip select of the transfer that follows.
//other outputs can sit LOW too (e.g. the waveform path switch), so "the first
//LOW output" isn't enough
static int lastLowPin = -1;
static int pinWriteFrames[FAKE_PIN_COUNT];
static int pinFirstWriteFrames[FAKE_PIN_COUNT];
static std::vector<FakePinDrive> pinDrives[FAKE_PIN_COUNT];

//what the pin puts on the wire right now (-1 = not driving)
static int drivenLevel(int pin) {
    return pinModes[pin] == OUTPUT ? pinLevels[pin] : -1;
}

//logs a change of what the pin drives
static void recordDrive(int pin, int before) {
    int now = drivenLevel(pin);
    if (now != before) pinDrives[pin].push_back(FakePinDrive{(int)SPI.frames.size(), now});
}

void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < FAKE_PIN_COUNT) {
        int before = drivenLevel(pin);
        pinModes[pin] = mode;
        pinClaimed[pin] = true;
        recordDrive(pin, before);
    }
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin < FAKE_PIN_COUNT) {
        if (!pinClaimed[pin]) return; //"IO n is not set as GPIO", nothing happens
        int before = drivenLevel(pin);
        pinLevels[pin] = value;
        pinWriteFrames[pin] = (int)SPI.frames.size();
        if (pinFirstWriteFrames[pin] < 0) pinFirstWriteFrames[pin] = pinWriteFrames[pin];
        if (value == LOW) lastLowPin = pin;
        recordDrive(pin, before);
    }
}

int fakePinDriveCount(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? (int)pinDrives[pin].size() : 0;
}

FakePinDrive fakePinDrive(uint8_t pin, int i) {
    if (pin >= FAKE_PIN_COUNT || i < 0 || i >= (int)pinDrives[pin].size()) return FakePinDrive{-1, -2}; //no such entry
    return pinDrives[pin][i];
}

int digitalRead(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? pinLevels[pin] : LOW;
}

int fakePinMode(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? pinModes[pin] : -1;
}

int fakePinLevel(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? pinLevels[pin] : -1;
}

int fakePinWriteFrame(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? pinWriteFrames[pin] : -1;
}

int fakePinFirstWriteFrame(uint8_t pin) {
    return (pin < FAKE_PIN_COUNT) ? pinFirstWriteFrames[pin] : -1;
}

void fakeResetPins() {
    for (int pin = 0; pin < FAKE_PIN_COUNT; pin++) {
        pinModes[pin] = INPUT;
        pinLevels[pin] = LOW;
        pinClaimed[pin] = false;
        pinDrives[pin].clear();
        pinWriteFrames[pin] = -1;
        pinFirstWriteFrames[pin] = -1;
    }
    lastLowPin = -1;
}

//SPI
SPIClass SPI;

SPIClass::SPIClass() {
    reset();
}

void SPIClass::reset() {
    running = false;
    inTransaction = false;
    beginCalls = 0;
    endCalls = 0;
    sck = miso = mosi = -1;
    frames.clear();
    readValue = 0;
    nestedTransactions = 0;
}

bool SPIClass::begin(int8_t sckPin, int8_t misoPin, int8_t mosiPin, int8_t ss) {
    (void)ss;
    beginCalls++;
    //same as the ESP32 core: pins are ignored while the bus is already running
    if (running) return true;
    running = true;
    sck = sckPin;
    miso = misoPin;
    mosi = mosiPin;
    return true;
}

void SPIClass::end() {
    endCalls++;
    running = false;
}

void SPIClass::beginTransaction(SPISettings settings) {
    if (inTransaction) nestedTransactions++;
    inTransaction = true;
    frames.push_back(SpiFrame{-1, settings._dataMode, settings._clock, {}});
}

void SPIClass::endTransaction() {
    inTransaction = false;
}

uint8_t SPIClass::transfer(uint8_t data) {
    if (frames.empty()) frames.push_back(SpiFrame{-1, 0, 0, {}}); //transfer outside a transaction
    SpiFrame& frame = frames.back();
    if (frame.csPin < 0 && lastLowPin >= 0 && pinModes[lastLowPin] == OUTPUT && pinLevels[lastLowPin] == LOW) {
        frame.csPin = lastLowPin;
    }
    if (frame.csPin < 0) {
        for (int pin = 0; pin < FAKE_PIN_COUNT; pin++) {
            if (pinModes[pin] == OUTPUT && pinLevels[pin] == LOW) {
                frame.csPin = pin;
                break;
            }
        }
    }
    frame.bytes.push_back(data);
    return readValue;
}

uint16_t SPIClass::transfer16(uint16_t data) {
    uint8_t hi = transfer(data >> 8);
    uint8_t lo = transfer(data & 0xFF);
    return (hi << 8) | lo;
}

//FreeRTOS
static int totalDeadlocks = 0;
static int totalHeld = 0;

int fakeMutexDeadlocks() {
    return totalDeadlocks;
}

int fakeMutexesHeld() {
    return totalHeld;
}

void fakeResetMutexStats() {
    totalDeadlocks = 0;
    totalHeld = 0;
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new FakeMutex{0, 0, 0, 0};
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
    (void)timeout;
    if (mutex->held) {
        mutex->deadlocks++;
        totalDeadlocks++;
    }
    mutex->held++;
    totalHeld++;
    mutex->takes++;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    mutex->held--;
    mutex->gives++;
    totalHeld--;
    return pdTRUE;
}
