/*
 * BarakatTime — прошивка для Waveshare ESP32-S3-Touch-LCD-1.69 (новая версия платы)
 *
 * Что уже сделано в этой версии:
 *  - безопасная инициализация buzzer (сразу LOW, чтобы плата не грелась — см. FAQ Waveshare)
 *  - удержание питания через SYS_EN (иначе плата гаснет сразу после отпускания кнопки PWR)
 *  - инициализация экрана (ST7789V2) и тача (CST816T)
 *  - инициализация RTC (PCF85063)
 *  - заставка "BarakatTime"
 *  - экран тасбиха: тап по экрану = +1 к счётчику
 *  - кнопка PWR: одиночный клик = переключить имя зикра,
 *                двойной клик = сбросить счётчик,
 *                долгое нажатие = заглушка под будущий режим настройки по WiFi AP
 *
 * ВАЖНО про шрифт: встроенный шрифт GFX-библиотеки не умеет кириллицу,
 * поэтому имена зикров временно на латинице (транслитом). Кириллицу добавим
 * отдельно через кастомный юникодный шрифт на одном из следующих шагов.
 *
 * Библиотеки: GFX Library for Arduino, Arduino_DriveBus, SensorLib (PCF85063).
 */

#include <Arduino.h>
#include <Wire.h>
#include "HWCDC.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"

HWCDC USBSerial;

// ---------- Экран ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true /* IPS */,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// ---------- Тач ----------
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> CST816T(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, TP_RST, TP_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) {
  CST816T->IIC_Interrupt_Flag = true;
}

bool touchReady = false;

// ---------- RTC ----------
SensorPCF85063 rtc;
bool rtcReady = false;

// ---------- Состояние приложения ----------
enum AppScreen { SCREEN_SPLASH, SCREEN_TASBEEH, SCREEN_SETUP_STUB };
AppScreen currentScreen = SCREEN_SPLASH;

const char *zikrNames[] = {
  "SubhanAllah",
  "Alhamdulillah",
  "AllahuAkbar",
  "LaIlahaIllaAllah"
};
const uint8_t zikrCount = sizeof(zikrNames) / sizeof(zikrNames[0]);
uint8_t currentZikrIndex = 0;
uint32_t tasbeehCount = 0;

// область счётчика/имени, чтобы перерисовывать только их, а не весь экран
const int16_t NAME_Y = 60;
const int16_t COUNT_Y = 140;

// ---------- Кнопка PWR (SYS_OUT) ----------
struct PwrButton {
  bool lastRaw = HIGH;          // HIGH = отпущена
  unsigned long pressStart = 0;
  bool longPressFired = false;
  bool waitingSecondClick = false;
  unsigned long firstClickTime = 0;
};
PwrButton pwrBtn;

const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 1000;
const unsigned long DOUBLE_CLICK_WINDOW_MS = 350;
unsigned long lastDebounceTime = 0;
bool debouncedState = HIGH;

// ---------- Прототипы ----------
void setupBuzzerSafe();
void setupPowerLatch();
void setupDisplay();
void setupTouch();
void setupRtc();
void drawSplashScreen();
void drawTasbeehScreenStatic();
void updateZikrName();
void updateCounter();
bool readTouchPoint(int16_t &x, int16_t &y);
void handleTouch();
void handlePwrButton();
void onSingleClick();
void onDoubleClick();
void onLongPress();

void setup() {
  // 1) Buzzer — ПЕРВЫМ делом в LOW, чтобы плата не грелась (см. FAQ Waveshare)
  setupBuzzerSafe();

  // 2) Удержание питания — иначе плата выключится сразу после отпускания PWR
  setupPowerLatch();

  pinMode(SYS_OUT, INPUT);

  USBSerial.begin(115200);
  delay(200);
  USBSerial.println("BarakatTime boot");

  setupDisplay();
  setupTouch();
  setupRtc();

  drawSplashScreen();
  delay(1500);

  currentScreen = SCREEN_TASBEEH;
  drawTasbeehScreenStatic();
}

void loop() {
  handleTouch();
  handlePwrButton();
  delay(5);
}

// =================== Инициализация ===================

void setupBuzzerSafe() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void setupPowerLatch() {
  pinMode(SYS_EN, OUTPUT);
  digitalWrite(SYS_EN, HIGH);
}

void setupDisplay() {
  if (!gfx->begin()) {
    USBSerial.println("gfx->begin() failed!");
  }
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(RGB565_BLACK);
}

void setupTouch() {
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    if (CST816T->begin(400000)) {
      touchReady = true;
      break;
    }
    USBSerial.println("CST816T init fail, retry...");
    delay(300);
  }
  if (touchReady) {
    CST816T->IIC_Write_Device_State(
        CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        CST816T->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    CST816T->IIC_Interrupt_Flag = false;
    USBSerial.println("Touch OK");
  } else {
    USBSerial.println("Touch NOT available, продолжаем без тача");
  }
}

void setupRtc() {
  rtcReady = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtcReady) {
    USBSerial.println("RTC NOT found, продолжаем без часов");
  } else {
    USBSerial.println("RTC OK");
  }
}

// =================== Экраны ===================

void drawSplashScreen() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(3);
  int16_t textWidth = strlen("BarakatTime") * 6 * 3;
  int16_t x = (LCD_WIDTH - textWidth) / 2;
  gfx->setCursor(x > 0 ? x : 0, LCD_HEIGHT / 2 - 10);
  gfx->println("BarakatTime");
}

void drawTasbeehScreenStatic() {
  gfx->fillScreen(RGB565_BLACK);
  updateZikrName();
  updateCounter();

  gfx->setTextColor(RGB565_DARKGREY);
  gfx->setTextSize(1);
  gfx->setCursor(10, LCD_HEIGHT - 20);
  gfx->println("tap = +1  PWR: click=zikr  2click=reset");
}

void updateZikrName() {
  gfx->fillRect(0, NAME_Y - 12, LCD_WIDTH, 20, RGB565_BLACK);
  gfx->setTextColor(RGB565_GREEN);
  gfx->setTextSize(2);
  const char *name = zikrNames[currentZikrIndex];
  int16_t textWidth = strlen(name) * 6 * 2;
  int16_t x = (LCD_WIDTH - textWidth) / 2;
  gfx->setCursor(x > 0 ? x : 0, NAME_Y - 12);
  gfx->println(name);
}

void updateCounter() {
  gfx->fillRect(0, COUNT_Y - 20, LCD_WIDTH, 40, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(4);
  char buf[12];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)tasbeehCount);
  int16_t textWidth = strlen(buf) * 6 * 4;
  int16_t x = (LCD_WIDTH - textWidth) / 2;
  gfx->setCursor(x > 0 ? x : 0, COUNT_Y - 20);
  gfx->println(buf);
}

// =================== Тач ===================

bool readTouchPoint(int16_t &x, int16_t &y) {
  if (!touchReady) return false;
  if (!CST816T->IIC_Interrupt_Flag) return false;
  CST816T->IIC_Interrupt_Flag = false;

  int32_t fingers = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  if (fingers <= 0) return false;

  int32_t rawX = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t rawY = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
  if (rawX < 0 || rawY < 0) return false;

  x = constrain((int16_t)rawX, 0, LCD_WIDTH - 1);
  y = constrain((int16_t)rawY, 0, LCD_HEIGHT - 1);
  return true;
}

void handleTouch() {
  int16_t x, y;
  if (!readTouchPoint(x, y)) return;
  if (currentScreen != SCREEN_TASBEEH) return;

  tasbeehCount++;
  updateCounter();
}

// =================== Кнопка PWR ===================

void handlePwrButton() {
  bool raw = digitalRead(SYS_OUT);

  if (raw != pwrBtn.lastRaw) {
    lastDebounceTime = millis();
  }
  pwrBtn.lastRaw = raw;

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS && raw != debouncedState) {
    debouncedState = raw;

    if (debouncedState == LOW) {
      // нажата
      pwrBtn.pressStart = millis();
      pwrBtn.longPressFired = false;
    } else {
      // отпущена
      if (!pwrBtn.longPressFired) {
        if (pwrBtn.waitingSecondClick &&
            (millis() - pwrBtn.firstClickTime) < DOUBLE_CLICK_WINDOW_MS) {
          pwrBtn.waitingSecondClick = false;
          onDoubleClick();
        } else {
          pwrBtn.waitingSecondClick = true;
          pwrBtn.firstClickTime = millis();
        }
      }
    }
  }

  // долгое нажатие — фиксируем один раз, пока кнопка ещё удерживается
  if (debouncedState == LOW && !pwrBtn.longPressFired &&
      (millis() - pwrBtn.pressStart >= LONG_PRESS_MS)) {
    pwrBtn.longPressFired = true;
    pwrBtn.waitingSecondClick = false;
    onLongPress();
  }

  // если после первого клика окно двойного клика истекло — это был одиночный клик
  if (pwrBtn.waitingSecondClick &&
      (millis() - pwrBtn.firstClickTime) >= DOUBLE_CLICK_WINDOW_MS) {
    pwrBtn.waitingSecondClick = false;
    onSingleClick();
  }
}

void onSingleClick() {
  USBSerial.println("PWR: single click -> next zikr");
  if (currentScreen != SCREEN_TASBEEH) return;
  currentZikrIndex = (currentZikrIndex + 1) % zikrCount;
  updateZikrName();
}

void onDoubleClick() {
  USBSerial.println("PWR: double click -> reset counter");
  if (currentScreen != SCREEN_TASBEEH) return;
  tasbeehCount = 0;
  updateCounter();
}

void onLongPress() {
  USBSerial.println("PWR: long press -> TODO WiFi AP setup mode");
  // TODO: следующий шаг — поднять WiFi AP + captive portal для настройки
  // геопозиции/имён зикров/статистики. Пока просто показываем заглушку.
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(10, LCD_HEIGHT / 2 - 10);
  gfx->println("Setup mode");
  gfx->setTextSize(1);
  gfx->setCursor(10, LCD_HEIGHT / 2 + 20);
  gfx->println("(coming soon)");
  delay(1500);
  currentScreen = SCREEN_TASBEEH;
  drawTasbeehScreenStatic();
}
