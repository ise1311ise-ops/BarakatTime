/*
 * BarakatTime — прошивка для Waveshare ESP32-S3-Touch-LCD-1.69
 * Дизайн: Премиальный тёмно-зелёный интерфейс с золотыми акцентами и карточками
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "HWCDC.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"
#include "fon_data.h"
#include "brand_data.h"

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

// ---------- Настройки и WiFi ----------
Preferences preferences;
WebServer server(80);

String ssid = "";
String password = "";
String city = "Moscow";
String country = "Russia";
int gmtOffsetSec = 10800; // по умолчанию UTC+3

// ---------- Состояние приложения ----------
enum AppScreen { SCREEN_SPLASH, SCREEN_TASBEEH, SCREEN_PRAYER_TIMES, SCREEN_RAMADAN, SCREEN_SETUP_MODE };
AppScreen currentScreen = SCREEN_SPLASH;

// Цветовая палитра дизайна (RGB565)
#define COLOR_BG          0x0120 // Глубокий темно-зеленый
#define COLOR_GOLD        0xFD20 // Премиальный золотой
#define COLOR_CARD_BG     0x1A42 // Цвет полупрозрачной карточки
#define COLOR_TEXT_DIM    0x7BEF // Приглушенный серо-зеленый

// Зикры
const char *zikrNames[] = {
  "SubhanAllah",
  "Alhamdulillah",
  "AllahuAkbar",
  "LaIlahaIllaAllah"
};
const uint8_t zikrCount = sizeof(zikrNames) / sizeof(zikrNames[0]);
uint8_t currentZikrIndex = 0;
uint32_t tasbeehCount = 33; // Стартовое значение как на рендере

// Данные молитв (из API)
struct PrayerTimes {
  String fajr = "03:46";
  String dhuhr = "01:36";
  String asr = "02:49";
  String maghrib = "08:38";
  String isha = "22:15";
  String nextPrayerName = "Maghrib";
  String timeLeft = "00:45:48";
};
PrayerTimes prayers;
unsigned long lastPrayerFetch = 0;

// ---------- Кнопка PWR (SYS_OUT) ----------
struct PwrButton {
  bool lastRaw = HIGH;
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
void loadSettings();
void connectWiFiOrStartAP();
void startConfigServer();
void fetchPrayerTimes();
void updateScreenContent();
void drawSplashScreen();
void drawTasbeehScreen();
void drawPrayerScreen();
void drawRamadanScreen();
void drawSetupScreen();
void drawPageDots(uint8_t activePage);
void handleTouch();
void handlePwrButton();
void onSingleClick();
void onDoubleClick();
void onLongPress();

void setup() {
  setupBuzzerSafe();
  setupPowerLatch();

  pinMode(SYS_OUT, INPUT);

  USBSerial.begin(115200);
  delay(200);
  USBSerial.println("BarakatTime boot");

  setupDisplay();
  drawSplashScreen();

  setupTouch();
  setupRtc();
  loadSettings();

  connectWiFiOrStartAP();

  delay(1000);
  currentScreen = SCREEN_TASBEEH;
  drawTasbeehScreen();
}

void loop() {
  if (currentScreen == SCREEN_SETUP_MODE) {
    server.handleClient();
    handlePwrButton();
    delay(5);
    return;
  }

  handleTouch();
  handlePwrButton();

  if (WiFi.status() == WL_CONNECTED && (millis() - lastPrayerFetch > 3600000 || lastPrayerFetch == 0)) {
    fetchPrayerTimes();
  }

  static unsigned long lastSecTick = 0;
  if (millis() - lastSecTick >= 1000) {
    lastSecTick = millis();
    if (currentScreen == SCREEN_PRAYER_TIMES || currentScreen == SCREEN_RAMADAN) {
      updateScreenContent();
    }
  }

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
  gfx->fillScreen(COLOR_BG);
}

void setupTouch() {
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    if (CST816T->begin(400000)) {
      touchReady = true;
      break;
    }
    delay(300);
  }
  if (touchReady) {
    CST816T->IIC_Write_Device_State(
        CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        CST816T->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    CST816T->IIC_Interrupt_Flag = false;
  }
}

void setupRtc() {
  rtcReady = rtc.begin(Wire, IIC_SDA, IIC_SCL);
}

void loadSettings() {
  preferences.begin("barakat", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  city = preferences.getString("city", "Moscow");
  country = preferences.getString("country", "Russia");
  gmtOffsetSec = preferences.getInt("gmt", 10800);
  preferences.end();
}

void connectWiFiOrStartAP() {
  if (ssid.length() == 0) {
    startConfigServer();
    return;
  }

  USBSerial.print("Connecting to WiFi: ");
  USBSerial.println(ssid);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    USBSerial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    USBSerial.println("\nWiFi connected!");
    configTime(gmtOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    fetchPrayerTimes();
  } else {
    USBSerial.println("\nWiFi connection failed, running offline.");
  }
}

void startConfigServer() {
  currentScreen = SCREEN_SETUP_MODE;
  WiFi.softAP("BarakatTime-Setup", "12345678");
  IPAddress IP = WiFi.softAPIP();
  USBSerial.print("AP IP address: ");
  USBSerial.println(IP);

  server.on("/", HTTP_GET, []() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<style>body{font-family:sans-serif;padding:20px;background:#0b1a13;color:#fff;text-align:center;}"
                  "input{padding:12px;margin:8px;width:85%;max-width:300px;background:#162e22;color:#fff;border:1px solid #c5a059;border-radius:8px;}"
                  "input[type=submit]{background:#c5a059;color:#000;border:none;font-weight:bold;cursor:pointer;}"
                  "</style></head><body>"
                  "<h2>BarakatTime Setup</h2>"
                  "<form action='/save' method='POST'>"
                  "WiFi SSID:<br><input type='text' name='ssid'><br>"
                  "WiFi Password:<br><input type='password' name='pass'><br>"
                  "City:<br><input type='text' name='city' value='Moscow'><br>"
                  "Country:<br><input type='text' name='country' value='Russia'><br>"
                  "GMT Offset (sec):<br><input type='text' name='gmt' value='10800'><br><br>"
                  "<input type='submit' value='Save & Restart'>"
                  "</form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    preferences.begin("barakat", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("pass", server.arg("pass"));
    preferences.putString("city", server.arg("city"));
    preferences.putString("country", server.arg("country"));
    preferences.putInt("gmt", server.arg("gmt").toInt());
    preferences.end();

    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
  drawSetupScreen();
}

void fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED) return;
  String url = "http://api.aladhan.com/v1/timingsByCity?city=" + city + "&country=" + country + "&method=3";
  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      JsonObject timings = doc["data"]["timings"];
      prayers.fajr = timings["Fajr"].as<String>();
      prayers.dhuhr = timings["Dhuhr"].as<String>();
      prayers.asr = timings["Asr"].as<String>();
      prayers.maghrib = timings["Maghrib"].as<String>();
      prayers.isha = timings["Isha"].as<String>();
      lastPrayerFetch = millis();
    }
  }
  http.end();
}

// =================== Отрисовка премиальных экранов ===================

void drawSplashScreen() {
  // 1. Фон рисуется один раз, целиком
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)fon_rgb, FON_WIDTH, FON_HEIGHT);

  int bx = (LCD_WIDTH - BRAND_WIDTH) / 2;
  int by = (LCD_HEIGHT - BRAND_HEIGHT) / 2;

  const int STEPS = 24;            // сколько кадров в анимации
  const int FRAME_DELAY_MS = 20;   // задержка между кадрами (24 x 20мс ≈ 0.5 сек)

  static uint16_t lineBuf[BRAND_WIDTH];

  for (int step = 1; step <= STEPS; step++) {
    float t = (float)step / STEPS; // 0.0 -> 1.0, прогресс появления

    for (int y = 0; y < BRAND_HEIGHT; y++) {
      for (int x = 0; x < BRAND_WIDTH; x++) {
        uint8_t a = pgm_read_byte(&brand_alpha[y * BRAND_WIDTH + x]);
        uint16_t bgPixel = pgm_read_word(&fon_rgb[(by + y) * FON_WIDTH + (bx + x)]);

        if (a == 0) {
          lineBuf[x] = bgPixel; // прозрачный пиксель лого - оставляем фон
          continue;
        }

        uint16_t fgPixel = pgm_read_word(&brand_rgb[y * BRAND_WIDTH + x]);
        float alpha = (a / 255.0f) * t; // текущая "видимость" пикселя в этом кадре

        uint8_t br = (bgPixel >> 11) & 0x1F, bg = (bgPixel >> 5) & 0x3F, bb = bgPixel & 0x1F;
        uint8_t fr = (fgPixel >> 11) & 0x1F, fg = (fgPixel >> 5) & 0x3F, fb = fgPixel & 0x1F;

        uint8_t r = br + (uint8_t)((fr - br) * alpha);
        uint8_t g = bg + (uint8_t)((fg - bg) * alpha);
        uint8_t b = bb + (uint8_t)((fb - bb) * alpha);

        lineBuf[x] = (r << 11) | (g << 5) | b;
      }
      gfx->draw16bitRGBBitmap(bx, by + y, lineBuf, BRAND_WIDTH, 1);
    }
    delay(FRAME_DELAY_MS);
  }
}

void drawTasbeehScreen() {
  gfx->fillScreen(COLOR_BG);

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2 - 10;
  gfx->drawCircle(cx, cy, 85, COLOR_GOLD);
  gfx->drawCircle(cx, cy, 75, COLOR_GOLD);
  for (int i = 0; i < 360; i += 30) {
    float rad = i * 3.14159 / 180.0;
    int x2 = cx + cos(rad) * 85;
    int y2 = cy + sin(rad) * 85;
    gfx->drawLine(cx, cy, x2, y2, COLOR_GOLD);
  }
  gfx->fillCircle(cx, cy, 45, COLOR_BG);
  gfx->drawCircle(cx, cy, 45, COLOR_GOLD);

  gfx->setTextColor(COLOR_GOLD);
  gfx->setTextSize(4);
  char buf[12];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)tasbeehCount);
  int16_t w = strlen(buf) * 6 * 4;
  gfx->setCursor((LCD_WIDTH - w) / 2, cy - 14);
  gfx->println(buf);

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT_DIM);
  int16_t nw = strlen(zikrNames[currentZikrIndex]) * 6;
  gfx->setCursor((LCD_WIDTH - nw) / 2, 22);
  gfx->println(zikrNames[currentZikrIndex]);

  drawPageDots(0);
}

void drawPrayerScreen() {
  gfx->fillScreen(COLOR_BG);

  gfx->setTextColor(COLOR_GOLD);
  gfx->setTextSize(2);
  gfx->setCursor(20, 15);
  gfx->println("BarakatTime");

  gfx->fillRoundRect(15, 45, 210, 75, 12, COLOR_CARD_BG);
  gfx->drawRoundRect(15, 45, 210, 75, 12, COLOR_GOLD);
  
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setTextSize(1);
  gfx->setCursor(28, 55);
  gfx->print("Next Prayer - "); gfx->println(prayers.nextPrayerName);

  gfx->setTextColor(COLOR_GOLD);
  gfx->setTextSize(3);
  gfx->setCursor(28, 78);
  gfx->println(prayers.timeLeft);

  gfx->fillRoundRect(15, 130, 210, 110, 12, COLOR_CARD_BG);
  
  int y = 142;
  auto row = [&](String name, String time) {
    gfx->setTextColor(COLOR_TEXT_DIM);
    gfx->setTextSize(1);
    gfx->setCursor(28, y);
    gfx->print(name);
    gfx->setTextColor(COLOR_GOLD);
    gfx->setCursor(160, y);
    gfx->println(time);
    y += 20;
  };

  row("Fajr", prayers.fajr);
  row("Dhuhr", prayers.dhuhr);
  row("Asr", prayers.asr);
  row("Maghrib", prayers.maghrib);

  drawPageDots(1);
}

void drawRamadanScreen() {
  gfx->fillScreen(COLOR_BG);

  gfx->fillRoundRect(15, 25, 210, 185, 16, COLOR_CARD_BG);
  gfx->drawRoundRect(15, 25, 210, 185, 16, COLOR_GOLD);

  gfx->setTextColor(COLOR_GOLD);
  gfx->setTextSize(2);
  gfx->setCursor(35, 45);
  gfx->println("Ramadan");

  gfx->setTextSize(4);
  gfx->setCursor(35, 80);
  gfx->println("127");

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(140, 100);
  gfx->println("days");

  gfx->fillRoundRect(25, 145, 190, 50, 8, 0x1100);
  gfx->setTextColor(COLOR_GOLD);
  gfx->setCursor(38, 160);
  gfx->print("City: "); gfx->println(city);

  drawPageDots(2);
}

void drawSetupScreen() {
  gfx->fillScreen(COLOR_BG);
  gfx->setTextColor(COLOR_GOLD);
  gfx->setTextSize(2);
  gfx->setCursor(20, 30);
  gfx->println("WiFi Setup Mode");

  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setTextSize(1);
  gfx->setCursor(20, 80);
  gfx->println("Connect to WiFi AP:");
  gfx->setTextColor(COLOR_GOLD);
  gfx->setCursor(20, 100);
  gfx->println("BarakatTime-Setup");
  
  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(20, 130);
  gfx->println("Open in browser:");
  gfx->setTextColor(COLOR_GOLD);
  gfx->setCursor(20, 150);
  gfx->println("http://192.168.4.1");
}

void drawPageDots(uint8_t activePage) {
  int startX = LCD_WIDTH / 2 - 20;
  int y = LCD_HEIGHT - 18;
  for (int i = 0; i < 3; i++) {
    uint16_t color = (i == activePage) ? COLOR_GOLD : COLOR_TEXT_DIM;
    gfx->fillCircle(startX + (i * 20), y, 3, color);
  }
}

void updateScreenContent() {
  if (currentScreen == SCREEN_PRAYER_TIMES) {
    drawPrayerScreen();
  } else if (currentScreen == SCREEN_RAMADAN) {
    drawRamadanScreen();
  }
}

// =================== Тач ===================

void handleTouch() {
  if (!touchReady) return;
  if (!CST816T->IIC_Interrupt_Flag) return;
  CST816T->IIC_Interrupt_Flag = false;

  int32_t fingers = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
  if (fingers <= 0) return;

  if (currentScreen == SCREEN_TASBEEH) {
    tasbeehCount++;
    // Точечное обновление счетчика без мерцания
    int cx = LCD_WIDTH / 2;
    int cy = LCD_HEIGHT / 2 - 10;
    gfx->fillCircle(cx, cy, 42, COLOR_BG);
    gfx->drawCircle(cx, cy, 45, COLOR_GOLD);

    gfx->setTextColor(COLOR_GOLD);
    gfx->setTextSize(4);
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)tasbeehCount);
    int16_t w2 = strlen(buf) * 6 * 4;
    gfx->setCursor((LCD_WIDTH - w2) / 2, cy - 14);
    gfx->println(buf);
  }
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
      pwrBtn.pressStart = millis();
      pwrBtn.longPressFired = false;
    } else {
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

  if (debouncedState == LOW && !pwrBtn.longPressFired &&
      (millis() - pwrBtn.pressStart >= LONG_PRESS_MS)) {
    pwrBtn.longPressFired = true;
    pwrBtn.waitingSecondClick = false;
    onLongPress();
  }

  if (pwrBtn.waitingSecondClick &&
      (millis() - pwrBtn.firstClickTime) >= DOUBLE_CLICK_WINDOW_MS) {
    pwrBtn.waitingSecondClick = false;
    onSingleClick();
  }
}

void onSingleClick() {
  if (currentScreen == SCREEN_SETUP_MODE) return;

  if (currentScreen == SCREEN_TASBEEH) {
    currentScreen = SCREEN_PRAYER_TIMES;
    drawPrayerScreen();
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    currentScreen = SCREEN_RAMADAN;
    drawRamadanScreen();
  } else if (currentScreen == SCREEN_RAMADAN) {
    currentScreen = SCREEN_TASBEEH;
    drawTasbeehScreen();
  }
}

void onDoubleClick() {
  if (currentScreen == SCREEN_TASBEEH) {
    currentZikrIndex = (currentZikrIndex + 1) % zikrCount;
    drawTasbeehScreen();
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    fetchPrayerTimes();
    drawPrayerScreen();
  }
}

void onLongPress() {
  startConfigServer();
}
