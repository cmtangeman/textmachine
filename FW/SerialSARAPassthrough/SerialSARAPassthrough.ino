/*
   SerialSARAPassthrough sketch

   This sketch allows you to send AT commands from the USB CDC serial port
   of the MKR NB 1500 board to the onboard ublox SARA-R410 cellular module.

   For a list of supported AT commands see:
   https://www.u-blox.com/sites/default/files/u-blox-CEL_ATCommands_%28UBX-13002752%29.pdf

   Circuit:
   - MKR NB 1500 board
   - Antenna
   - SIM card

   Make sure the Serial Monitor's line ending is set to "Both NL and CR" or "Carriage Return"

   created 11 December 2017
   Sandeep Mistry
   
   Modified: auto-registers on T-Mobile/Tello and configures
   MNO profile + eDRX for power saving testing
*/

// baud rate used for both Serial ports
unsigned long baud = 115200;

void setup() {
    // NEVER EVER use RESET_N
    pinMode(SARA_RESETN, OUTPUT);
    digitalWrite(SARA_RESETN, LOW);

    // Send Poweron pulse
    pinMode(SARA_PWR_ON, OUTPUT);
    digitalWrite(SARA_PWR_ON, HIGH);
    delay(150);
    digitalWrite(SARA_PWR_ON, LOW);

    Serial.begin(baud);
    SerialSARA.begin(baud);

    // wait for modem to boot
    delay(3000);

    // ── Set T-Mobile MNO profile if not already set ──────────
    SerialSARA.println("AT+UMNOPROF?");
    delay(500);
    String prof = "";
    while (SerialSARA.available()) prof += (char)SerialSARA.read();
    
    if (prof.indexOf("+UMNOPROF: 90") == -1) {
        Serial.println("Setting T-Mobile profile...");
        SerialSARA.println("AT+UMNOPROF=90");
        delay(500);
        SerialSARA.println("AT+CFUN=15");  // restart to apply
        delay(10000);  // wait for restart
        Serial.println("Modem restarted with T-Mobile profile");
    } else {
        Serial.println("T-Mobile profile already set");
    }

    // ── Register on network ───────────────────────────────────
    Serial.println("Waiting for network registration...");
    SerialSARA.println("AT+CFUN=1");   // full functionality
    delay(500);
    SerialSARA.println("AT+COPS=0");   // automatic operator selection
    delay(500);

    // wait up to 60 seconds for registration
    unsigned long start = millis();
    bool registered = false;
    while (millis() - start < 60000) {
        SerialSARA.println("AT+CEREG?");
        delay(1000);
        String resp = "";
        while (SerialSARA.available()) resp += (char)SerialSARA.read();
        if (resp.indexOf("+CEREG: 0,1") != -1 || 
            resp.indexOf("+CEREG: 0,5") != -1) {
            registered = true;
            break;
        }
        Serial.print(".");
    }

    if (registered) {
        Serial.println("\nRegistered!");
        
        // show what RAT we connected on
        SerialSARA.println("AT+COPS?");
        delay(500);
        while (SerialSARA.available()) Serial.write(SerialSARA.read());

        // ── Configure power saving ────────────────────────────
        SerialSARA.println("AT+UPSV=2");
        delay(300);
        SerialSARA.println("AT+CEDRXS=1,4,\"0101\"");
        delay(500);

        // check what network actually granted
        Serial.println("eDRX negotiated values:");
        SerialSARA.println("AT+CEDRXRDP");
        delay(500);
        while (SerialSARA.available()) Serial.write(SerialSARA.read());

    } else {
        Serial.println("\nRegistration failed — manual AT commands available");
    }

    Serial.println("\n=== Passthrough ready ===");
}

void loop() {
    if (Serial.available()) {
        SerialSARA.write(Serial.read());
    }
    if (SerialSARA.available()) {
        Serial.write(SerialSARA.read());
    }
}