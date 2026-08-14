#pragma once
// Пины актуальны для НОВОЙ версии платы Waveshare ESP32-S3-Touch-LCD-1.69
// (на задней стороне платы напечатано название модели — это признак новой версии).
// Если вдруг у тебя окажется старая версия без надписи — поменяй значения
// BUZZER_PIN / RTC_INT / SYS_EN / SYS_OUT по таблице из справочника.

// ====== Экран ST7789V2 (SPI) ======
#define LCD_WIDTH   240
#define LCD_HEIGHT  280
#define LCD_DC      4
#define LCD_CS      5
#define LCD_SCK     6   // CLK
#define LCD_MOSI    7   // DIN (DOUT не подключен, экран write-only)
#define LCD_RST     8
#define LCD_BL      15  // подсветка, ШИМ

// ====== I2C шина (общая для тача, IMU и RTC) ======
#define IIC_SDA     11
#define IIC_SCL     10

// ====== Тач CST816T ======
#define TP_RST      13
#define TP_INT      14
#define CST816T_DEVICE_ADDRESS 0x15

// ====== RTC PCF85063 ======
#define RTC_ADDRESS 0x51

// ====== IMU QMI8658 (пока не используется, зарезервировано) ======
#define QMI8658_ADDRESS 0x6B
#define QMI_INT     38

// ====== Силовая цепь / кнопка PWR (НОВАЯ версия платы) ======
#define SYS_OUT     40   // читаем как кнопку PWR: HIGH = отпущена, LOW = нажата
#define SYS_EN      41   // держим HIGH, иначе плата выключится сразу после отпускания кнопки
#define BUZZER_PIN  42
#define RTC_INT     39

// ====== Прочее ======
#define BOOT_PIN    0
#define BAT_ADC_PIN 1
