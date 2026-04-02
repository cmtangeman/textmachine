#include <Wire.h>

#define CTP_RST      5
#define FT6336U_ADDR 0x38

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW); delay(10);
  digitalWrite(CTP_RST, HIGH); delay(50);

  Wire.begin();
  Serial.println("Touch each corner when prompted...");
  Serial.println("Touch TOP-LEFT now:");
}

void loop() {
  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(0x02);
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336U_ADDR, 1);
  if (!Wire.available()) return;
  uint8_t touches = Wire.read();
  if (touches == 0 || touches > 2) return;

  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(0x03);
  Wire.endTransmission(false);
  Wire.requestFrom(FT6336U_ADDR, 4);
  if (Wire.available() < 4) return;

  uint16_t x = ((Wire.read() & 0x0F) << 8) | Wire.read();
  uint16_t y = ((Wire.read() & 0x0F) << 8) | Wire.read();

  Serial.print("RAW -> X: "); Serial.print(x);
  Serial.print("  Y: "); Serial.println(y);
  delay(300);
}