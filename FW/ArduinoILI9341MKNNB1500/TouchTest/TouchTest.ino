#include <XPT2046_Touchscreen.h>
#include <SPI.h>

#define TFT_DC 6
#define TFT_CS 7
#define TFT_RST -1

#define Touch_CS 5
// MOSI=11, MISO=12, SCK=13

//XPT2046_Touchscreen ts(CS_PIN);

// #define TIRQ_PIN  2

XPT2046_Touchscreen ts(Touch_CS);  // Param 2 - NULL - No interrupts
//XPT2046_Touchscreen ts(CS_PIN, 255);  // Param 2 - 255 - No interrupts

// XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);  // Param 2 - Touch IRQ Pin - interrupt enabled polling

void setup() {
  Serial.begin(38400);

  // Make sure both chip selects are set as high so there are no contention issues
  pinMode(TFT_CS, OUTPUT);
  pinMode(Touch_CS ,OUTPUT);
  digitalWrite(TFT_CS,HIGH);
  digitalWrite(Touch_CS, HIGH);
  //


  ts.begin();
  // ts.begin(SPI1); // use alternate SPI port
  ts.setRotation(1);
  while (!Serial && (millis() <= 1000));
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    Serial.print("Pressure = ");
    Serial.print(p.z);
    Serial.print(", x = ");
    Serial.print(p.x);
    Serial.print(", y = ");
    Serial.print(p.y);
    delay(30);
    Serial.println();
  }
}

