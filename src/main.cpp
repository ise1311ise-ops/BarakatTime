/*
BarakatTime — прошивка для Waveshare ESP32-S3-Touch-LCD-1.69
LVGL-версия: экран/тач остаются на Arduino_GFX + твоём CST816T-драйвере,
LVGL только рисует виджеты поверх них.
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include <lvgl.h>
#include "HWCDC.h"
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "SensorPCF85063.hpp"
#include "pin_config.h"

HWCDC USBSerial;

// ---------- Экран (как было) ----------
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true,
                                      LCD_WIDTH, LCD_HEIGHT, 0, 20, 0, 0);

// ---------- Тач (как было) ----------
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
void Arduino_IIC_Touch_Interrupt(void);
std::unique_ptr<Arduino_IIC> CST816T(new Arduino_CST816x(
    IIC_Bus, CST816T_DEVICE_ADDRESS, TP_RST, TP_INT, Arduino_IIC_Touch_Interrupt));
void Arduino_IIC_Touch_Interrupt(void) { CST816T->IIC_Interrupt_Flag = true; }
bool touchReady = false;

// ---------- RTC ----------
SensorPCF85063 rtc;
bool rtcReady = false;

// ---------- Настройки/WiFi ----------
Preferences preferences;
WebServer server(80);
String ssid = "", password = "", city = "Moscow", country = "Russia";
int gmtOffsetSec = 10800;

// ---------- Зикры / тасбих ----------
const char *zikrNames[] = {"SubhanAllah", "Alhamdulillah", "AllahuAkbar", "LaIlahaIllaAllah"};
const uint8_t zikrCount = sizeof(zikrNames) / sizeof(zikrNames[0]);
uint8_t currentZikrIndex = 0;
uint32_t tasbeehCount = 0;

// ---------- Молитвы ----------
struct PrayerTimes {
  String fajr = "--:--", dhuhr = "--:--", asr = "--:--", maghrib = "--:--", isha = "--:--";
  String nextPrayerName = "Maghrib", timeLeft = "--:--:--";
};
PrayerTimes prayers;
unsigned long lastPrayerFetch = 0;

// ---------- Кнопка PWR (как было) ----------
struct PwrButton {
  bool lastRaw = HIGH;
  unsigned long pressStart = 0;
  bool longPressFired = false;
  bool waitingSecondClick = false;
  unsigned long firstClickTime = 0;
};
PwrButton pwrBtn;
const unsigned long DEBOUNCE_MS = 30, LONG_PRESS_MS = 1000, DOUBLE_CLICK_WINDOW_MS = 350;
unsigned long lastDebounceTime = 0;
bool debouncedState = HIGH;

// ---------- Палитра (LVGL-цвета) ----------
#define COLOR_BG          lv_color_hex(0x0B2818)
#define COLOR_CARD_BG     lv_color_hex(0x123420)
#define COLOR_CARD_BORDER lv_color_hex(0x2E5A3E)
#define COLOR_GOLD        lv_color_hex(0xE8C468)
#define COLOR_GOLD_DIM    lv_color_hex(0x5A4E2E)
#define COLOR_TEXT_DIM    lv_color_hex(0x8FAF9A)
#define COLOR_TEXT_WHITE  lv_color_hex(0xFFFFFF)

// ---------- LVGL: буфер и дисплей ----------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvbuf1[LCD_WIDTH * 40];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// ---------- LVGL: экраны ----------
lv_obj_t *scrTasbeeh, *scrPrayer, *scrRamadan, *scrSetup;
lv_obj_t *arcTasbeeh, *lblTasbeehCount, *lblZikrName;
lv_obj_t *lblNextPrayerName, *lblNextPrayerTime;
lv_obj_t *lblPrayerRows[4];
lv_obj_t *lblRamadanDays;

enum AppScreen { SCREEN_TASBEEH, SCREEN_PRAYER_TIMES, SCREEN_RAMADAN, SCREEN_SETUP_MODE };
AppScreen currentScreen = SCREEN_TASBEEH;

// ---------- Прототипы ----------
void setupBuzzerSafe();
void setupPowerLatch();
void setupDisplayAndLVGL();
void setupTouch();
void setupRtc();
void loadSettings();
void connectWiFiOrStartAP();
void startConfigServer();
void fetchPrayerTimes();
void buildTasbeehScreen();
void buildPrayerScreen();
void buildRamadanScreen();
void buildSetupScreen();
void refreshPrayerScreen();
void refreshRamadanScreen();
void handlePwrButton();
void onSingleClick();
void onDoubleClick();
void onLongPress();

// =================== LVGL мост: flush + touch ===================
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  static int32_t lastX = 0, lastY = 0;
  static bool wasPressed = false;

  if (!touchReady) { data->state = LV_INDEV_STATE_REL; return; }

  if (CST816T->IIC_Interrupt_Flag) {
    CST816T->IIC_Interrupt_Flag = false;
    int32_t fingers = CST816T->IIC_Read_Device_Value(
        CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_FINGER_NUMBER);
    if (fingers > 0) {
      // ВНИМАНИЕ: имена этих двух enum-полей нужно свериться с
      // Arduino_DriveBus_Library.h в твоей версии — если сборка ругнётся
      // на TOUCH_COORDINATE_X/Y, пришли мне этот enum и я поправлю.
      lastX = CST816T->IIC_Read_Device_Value(
          CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
      lastY = CST816T->IIC_Read_Device_Value(
          CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
      wasPressed = true;
    } else {
      wasPressed = false;
    }
  }

  data->point.x = lastX;
  data->point.y = lastY;
  data->state = wasPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// =================== setup() ===================
void setup() {
  setupBuzzerSafe();
  setupPowerLatch();
  pinMode(SYS_OUT, INPUT);

  USBSerial.begin(115200);
  delay(200);
  USBSerial.println("BarakatTime boot (LVGL)");

  setupDisplayAndLVGL();
  setupTouch();
  setupRtc();
  loadSettings();
  connectWiFiOrStartAP();

  buildTasbeehScreen();
  buildPrayerScreen();
  buildRamadanScreen();
  buildSetupScreen();

  lv_scr_load(scrTasbeeh);
  currentScreen = SCREEN_TASBEEH;
}

void loop() {
  lv_timer_handler();

  if (currentScreen == SCREEN_SETUP_MODE) {
    server.handleClient();
    handlePwrButton();
    delay(5);
    return;
  }

  handlePwrButton();

  if (WiFi.status() == WL_CONNECTED &&
      (millis() - lastPrayerFetch > 3600000 || lastPrayerFetch == 0)) {
    fetchPrayerTimes();
    refreshPrayerScreen();
  }

  static unsigned long lastSecTick = 0;
  if (millis() - lastSecTick >= 1000) {
    lastSecTick = millis();
    if (currentScreen == SCREEN_PRAYER_TIMES) refreshPrayerScreen();
    if (currentScreen == SCREEN_RAMADAN) refreshRamadanScreen();
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

void setupDisplayAndLVGL() {
  if (!gfx->begin()) USBSerial.println("gfx->begin() failed!");
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(BLACK);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvbuf1, NULL, LCD_WIDTH * 40);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
}

void setupTouch() {
  for (uint8_t attempt = 0; attempt < 5; attempt++) {
    if (CST816T->begin(400000)) { touchReady = true; break; }
    delay(300);
  }
  if (touchReady) {
    CST816T->IIC_Write_Device_State(
        CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        CST816T->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    CST816T->IIC_Interrupt_Flag = false;
  }

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);
}

void setupRtc() { rtcReady = rtc.begin(Wire, IIC_SDA, IIC_SCL); }

void loadSettings() {
  preferences.begin("barakat", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  city = preferences.getString("city", "Moscow");
  country = preferences.getString("country", "Russia");
  gmtOffsetSec = preferences.getInt("gmt", 10800);
  tasbeehCount = preferences.getUInt("tasbeeh", 0);
  preferences.end();
}

void connectWiFiOrStartAP() {
  if (ssid.length() == 0) { startConfigServer(); return; }
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    fetchPrayerTimes();
  }
}

void startConfigServer() {
  currentScreen = SCREEN_SETUP_MODE;
  WiFi.softAP("BarakatTime-Setup", "12345678");

  server.on("/", HTTP_GET, []() {
    String html =
      "<html><body style='font-family:sans-serif;background:#0b2818;color:#e8c468'>"
      "<h2>BarakatTime Setup</h2>"
      "<form method='POST' action='/save'>"
      "WiFi SSID:<br><input name='ssid'><br>"
      "WiFi Password:<br><input name='pass' type='password'><br>"
      "City:<br><input name='city'><br>"
      "Country:<br><input name='country'><br>"
      "GMT Offset (sec):<br><input name='gmt' value='10800'><br><br>"
      "<button type='submit'>Save</button>"
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
  if (scrSetup) lv_scr_load(scrSetup);
}

void fetchPrayerTimes() {
  if (WiFi.status() != WL_CONNECTED) return;
  String url = "http://api.aladhan.com/v1/timingsByCity?city=" + city + "&country=" + country + "&method=3";
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code > 0) {
    String payload = http.getString();
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      JsonObject t = doc["data"]["timings"];
      prayers.fajr = t["Fajr"].as<String>();
      prayers.dhuhr = t["Dhuhr"].as<String>();
      prayers.asr = t["Asr"].as<String>();
      prayers.maghrib = t["Maghrib"].as<String>();
      prayers.isha = t["Isha"].as<String>();
      lastPrayerFetch = millis();
    }
  }
  http.end();
}

// =================== Экран: Тасбих ===================
static void tasbeehTapCb(lv_event_t *e) {
  tasbeehCount++;
  preferences.begin("barakat", false);
  preferences.putUInt("tasbeeh", tasbeehCount);
  preferences.end();

  lv_arc_set_value(arcTasbeeh, tasbeehCount % 33);
  lv_label_set_text_fmt(lblTasbeehCount, "%lu", (unsigned long)tasbeehCount);
}

void buildTasbeehScreen() {
  scrTasbeeh = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrTasbeeh, COLOR_BG, 0);
  lv_obj_clear_flag(scrTasbeeh, LV_OBJ_FLAG_SCROLLABLE);

  arcTasbeeh = lv_arc_create(scrTasbeeh);
  lv_obj_set_size(arcTasbeeh, 176, 176);
  lv_obj_center(arcTasbeeh);
  lv_arc_set_rotation(arcTasbeeh, 270);
  lv_arc_set_bg_angles(arcTasbeeh, 0, 360);
  lv_arc_set_range(arcTasbeeh, 0, 33);
  lv_arc_set_value(arcTasbeeh, tasbeehCount % 33);
  lv_obj_set_style_arc_color(arcTasbeeh, COLOR_GOLD_DIM, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arcTasbeeh, COLOR_GOLD, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arcTasbeeh, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arcTasbeeh, 10, LV_PART_INDICATOR);
  lv_obj_remove_style(arcTasbeeh, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arcTasbeeh, LV_OBJ_FLAG_CLICKABLE);

  lblTasbeehCount = lv_label_create(scrTasbeeh);
  lv_obj_set_style_text_font(lblTasbeehCount, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lblTasbeehCount, COLOR_GOLD, 0);
  lv_label_set_text_fmt(lblTasbeehCount, "%lu", (unsigned long)tasbeehCount);
  lv_obj_align(lblTasbeehCount, LV_ALIGN_CENTER, 0, -10);

  lblZikrName = lv_label_create(scrTasbeeh);
  lv_obj_set_style_text_color(lblZikrName, COLOR_TEXT_DIM, 0);
  lv_label_set_text(lblZikrName, zikrNames[currentZikrIndex]);
  lv_obj_align(lblZikrName, LV_ALIGN_CENTER, 0, 40);

  // тап по всему экрану = +1 (вместо ручного handleTouch)
  lv_obj_add_flag(scrTasbeeh, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scrTasbeeh, tasbeehTapCb, LV_EVENT_CLICKED, NULL);
}

// =================== Экран: Молитвы ===================
void buildPrayerScreen() {
  scrPrayer = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrPrayer, COLOR_BG, 0);
  lv_obj_clear_flag(scrPrayer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *heroCard = lv_obj_create(scrPrayer);
  lv_obj_set_size(heroCard, 210, 75);
  lv_obj_align(heroCard, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_style_bg_color(heroCard, COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(heroCard, COLOR_CARD_BORDER, 0);
  lv_obj_set_style_radius(heroCard, 14, 0);

  lblNextPrayerName = lv_label_create(heroCard);
  lv_obj_set_style_text_color(lblNextPrayerName, COLOR_TEXT_DIM, 0);
  lv_obj_align(lblNextPrayerName, LV_ALIGN_TOP_MID, 0, 4);

  lblNextPrayerTime = lv_label_create(heroCard);
  lv_obj_set_style_text_font(lblNextPrayerTime, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(lblNextPrayerTime, COLOR_GOLD, 0);
  lv_obj_align(lblNextPrayerTime, LV_ALIGN_CENTER, 0, 10);

  lv_obj_t *listCard = lv_obj_create(scrPrayer);
  lv_obj_set_size(listCard, 210, 108);
  lv_obj_align(listCard, LV_ALIGN_TOP_MID, 0, 132);
  lv_obj_set_style_bg_color(listCard, COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(listCard, COLOR_CARD_BORDER, 0);
  lv_obj_set_style_radius(listCard, 14, 0);
  lv_obj_clear_flag(listCard, LV_OBJ_FLAG_SCROLLABLE);

  const char *names[4] = {"Fajr", "Dhuhr", "Asr", "Maghrib"};
  for (int i = 0; i < 4; i++) {
    lblPrayerRows[i] = lv_label_create(listCard);
    lv_obj_set_style_text_color(lblPrayerRows[i], COLOR_TEXT_DIM, 0);
    lv_obj_set_pos(lblPrayerRows[i], 10, 8 + i * 22);
    lv_label_set_text_fmt(lblPrayerRows[i], "%s   --:--", names[i]);
  }
}

void refreshPrayerScreen() {
  lv_label_set_text_fmt(lblNextPrayerName, "Next - %s", prayers.nextPrayerName.c_str());
  lv_label_set_text(lblNextPrayerTime, prayers.timeLeft.c_str());

  const char *names[4] = {"Fajr", "Dhuhr", "Asr", "Maghrib"};
  String times[4] = {prayers.fajr, prayers.dhuhr, prayers.asr, prayers.maghrib};
  for (int i = 0; i < 4; i++) {
    lv_label_set_text_fmt(lblPrayerRows[i], "%-8s%s", names[i], times[i].c_str());
    bool active = prayers.nextPrayerName == names[i];
    lv_obj_set_style_text_color(lblPrayerRows[i], active ? COLOR_GOLD : COLOR_TEXT_DIM, 0);
  }
}

// =================== Экран: Рамадан ===================
void buildRamadanScreen() {
  scrRamadan = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrRamadan, COLOR_BG, 0);
  lv_obj_clear_flag(scrRamadan, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *card = lv_obj_create(scrRamadan);
  lv_obj_set_size(card, 210, 190);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(card, COLOR_CARD_BORDER, 0);
  lv_obj_set_style_radius(card, 16, 0);

  lv_obj_t *title = lv_label_create(card);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_label_set_text(title, "RAMADAN");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lblRamadanDays = lv_label_create(card);
  lv_obj_set_style_text_font(lblRamadanDays, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lblRamadanDays, COLOR_TEXT_WHITE, 0);
  lv_label_set_text(lblRamadanDays, "127");
  lv_obj_align(lblRamadanDays, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *sub = lv_label_create(card);
  lv_obj_set_style_text_color(sub, COLOR_TEXT_DIM, 0);
  lv_label_set_text(sub, "days left");
  lv_obj_align_to(sub, lblRamadanDays, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

void refreshRamadanScreen() {
  // сюда позже подставим реальный расчёт дней до Рамадана
}

// =================== Экран: Настройка ===================
void buildSetupScreen() {
  scrSetup = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scrSetup, COLOR_BG, 0);
  lv_obj_clear_flag(scrSetup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(scrSetup);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
  lv_label_set_text(title, "WiFi Setup");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

  lv_obj_t *card = lv_obj_create(scrSetup);
  lv_obj_set_size(card, 210, 130);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
  lv_obj_set_style_border_color(card, COLOR_CARD_BORDER, 0);
  lv_obj_set_style_radius(card, 14, 0);

  lv_obj_t *l1 = lv_label_create(card);
  lv_obj_set_style_text_color(l1, COLOR_TEXT_DIM, 0);
  lv_label_set_text(l1, "Connect to:");
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *l2 = lv_label_create(card);
  lv_obj_set_style_text_color(l2, COLOR_GOLD, 0);
  lv_label_set_text(l2, "BarakatTime-Setup");
  lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 26);

  lv_obj_t *l3 = lv_label_create(card);
  lv_obj_set_style_text_color(l3, COLOR_TEXT_DIM, 0);
  lv_label_set_text(l3, "Open in browser:");
  lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 60);

  lv_obj_t *l4 = lv_label_create(card);
  lv_obj_set_style_text_font(l4, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(l4, COLOR_GOLD, 0);
  lv_label_set_text(l4, "192.168.4.1");
  lv_obj_align(l4, LV_ALIGN_TOP_MID, 0, 78);
}

// =================== Кнопка PWR (логика без изменений) ===================
void handlePwrButton() {
  bool raw = digitalRead(SYS_OUT);
  if (raw != pwrBtn.lastRaw) lastDebounceTime = millis();
  pwrBtn.lastRaw = raw;

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS && raw != debouncedState) {
    debouncedState = raw;
    if (debouncedState == LOW) {
      pwrBtn.pressStart = millis();
      pwrBtn.longPressFired = false;
    } else if (!pwrBtn.longPressFired) {
      if (pwrBtn.waitingSecondClick && (millis() - pwrBtn.firstClickTime) < DOUBLE_CLICK_WINDOW_MS) {
        pwrBtn.waitingSecondClick = false;
        onDoubleClick();
      } else {
        pwrBtn.waitingSecondClick = true;
        pwrBtn.firstClickTime = millis();
      }
    }
  }

  if (debouncedState == LOW && !pwrBtn.longPressFired &&
      (millis() - pwrBtn.pressStart >= LONG_PRESS_MS)) {
    pwrBtn.longPressFired = true;
    pwrBtn.waitingSecondClick = false;
    onLongPress();
  }

  if (pwrBtn.waitingSecondClick && (millis() - pwrBtn.firstClickTime) >= DOUBLE_CLICK_WINDOW_MS) {
    pwrBtn.waitingSecondClick = false;
    onSingleClick();
  }
}

void onSingleClick() {
  if (currentScreen == SCREEN_SETUP_MODE) return;
  if (currentScreen == SCREEN_TASBEEH) {
    currentScreen = SCREEN_PRAYER_TIMES;
    refreshPrayerScreen();
    lv_scr_load_anim(scrPrayer, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    currentScreen = SCREEN_RAMADAN;
    lv_scr_load_anim(scrRamadan, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
  } else if (currentScreen == SCREEN_RAMADAN) {
    currentScreen = SCREEN_TASBEEH;
    lv_scr_load_anim(scrTasbeeh, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
  }
}

void onDoubleClick() {
  if (currentScreen == SCREEN_TASBEEH) {
    currentZikrIndex = (currentZikrIndex + 1) % zikrCount;
    lv_label_set_text(lblZikrName, zikrNames[currentZikrIndex]);
  } else if (currentScreen == SCREEN_PRAYER_TIMES) {
    fetchPrayerTimes();
    refreshPrayerScreen();
  }
}

void onLongPress() {
  startConfigServer();
}
