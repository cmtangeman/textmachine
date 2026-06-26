#include "Contacts.h"
#include "Messages.h"
#include "buttons.h"
#include <SD.h>
#include <string.h> 

Contact contactList[MAX_CONTACTS];
static int contactCount = 0;


// Forward declaration (Serial helper from main sketch)
extern int readSerial(char result[]);
extern int readInt();


static Button contactBtns[MAX_CONTACTS];
static Button contactsBackBtn;
static Button newContactsBtn;
static bool   contactsDrawn = false;

void contactsInit() {
  contactCount = 0;

}

void contactsScreenReset() {
  contactsDrawn = false;
}

int findContactPhone(const char* name) {
    for (int i = 0; i < contactCount; i++) {
        Serial.print(i + 1);
        Serial.print(". ");

        if (strcmp(name, contactList[i].name) == 0) {
            return i; // contactList[i].phone;
        }
    }
    return -1;
}


int findContactName(const char* phone){
    for (int i = 0; i < contactCount; i++) {
        Serial.print(i + 1);
        Serial.print(". ");

        if (strcmp(phone, contactList[i].phone) == 0) {
            return i; // contactList[i].phone;
        }
    }
    return -1;
}

const char* getContactPhone(int index) {
  if (index < 0 || index >= contactCount) {
    return nullptr;
  }
  return contactList[index].phone;
}

const char* getContactName(int index) {
  if (index < 0 || index >= contactCount) {
    return nullptr;
  }
  return contactList[index].name;
}

void addContactFromUI(const char* name, const char* phone) {
    if (contactCount >= MAX_CONTACTS) {
        Serial.println("Contact list full.");
        return;
    }

    copyBounded(contactList[contactCount].name, name, MAX_NAME_LEN);
    copyBounded(contactList[contactCount].phone, phone, MAX_PHONE_LEN);
    char normalizedNumber[MAX_PHONE_LEN];

    // So no doubles of the same intended phone number 
    normalizePhoneNumber(phone, normalizedNumber, MAX_PHONE_LEN);
    contactCount++;

    saveContactToSD(normalizedNumber, phone);
}

void saveContactToSD(const char* name, const char* phone) {
    File f = SD.open("contacts.csv", FILE_WRITE);
    if (f) {
        f.print(name);
        f.print("|");
        f.println(phone);
        f.close();
    } else {
        Serial.println("Failed to open contacts.csv");
    }
}

void loadContactsFromSD() {
    File f = SD.open("contacts.csv", FILE_READ);
    if (!f) {
        Serial.println("No contacts.csv found");
        return;
    }

    contactCount = 0;
    while (f.available() && contactCount < MAX_CONTACTS) {
        String line = f.readStringUntil('\n');
        int sep = line.indexOf('|');
        if (sep == -1) continue;

        String name  = line.substring(0, sep);
        String phone = line.substring(sep + 1);
        phone.trim();  // remove \r if present

        copyBounded(contactList[contactCount].name, name.c_str(), MAX_NAME_LEN);
        copyBounded(contactList[contactCount].phone, phone.c_str(), MAX_PHONE_LEN);
        contactCount++;
    }
    f.close();
    Serial.print("Loaded contacts: ");
    Serial.println(contactCount);
}


int contactsScreen(const ScreenPoint& sp, bool justPressed) {
  const int listStartY = 50;
  const int rowH       = 40;
  const int x          = 10;
  const int w          = 220;

  if (!contactsDrawn) {
    Serial.print("contactCount = ");
    Serial.println(contactCount);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    contactsBackBtn.initButton(0, 0, 30, 30, "<");
    newContactsBtn.initButton(200, 0, 30, 30, "+");

    tft.setCursor(50, 10);
    tft.print("Contacts");

    for (int i = 0; i < contactCount; i++) {
      int y = listStartY + i * rowH;

      contactBtns[i].initButton(x, y, w, rowH, contactList[i].name);

      // phone number on the right, smaller
      tft.setTextSize(1);
      tft.setCursor(x + 130, y + 12);
      tft.print(contactList[i].phone);
      tft.setTextSize(2);
    }

    contactsDrawn = true;
  }

  if (!justPressed) return -1;

  if (contactsBackBtn.isClicked(sp)) return -2;  // caller checks for back

  if (newContactsBtn.isClicked(sp)) return -3; // Add a new contact! 

  for (int i = 0; i < contactCount; i++) {
    if (contactBtns[i].isClicked(sp)) {
      return i;  // index into contactList[]
    }
  }

  return -1;
}


void addContact() {
  if (contactCount >= MAX_CONTACTS) {
    Serial.println("Contact list full.");
    return;
  }

  Serial.print("Name: ");
  readSerial(contactList[contactCount].name);


  Serial.print("Phone: ");
  readSerial(contactList[contactCount].phone);
  saveContactToSD(contactList[contactCount].name, contactList[contactCount].phone);

  contactCount++;
  Serial.println("Contact saved.");
}

/*
Serial Based old code


void viewContacts() {
  if (contactCount == 0) {
    Serial.println("No contacts.");
    return;
  }

  for (int i = 0; i < contactCount; i++) {
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.print(contactList[i].name);
    Serial.print(" - ");
    Serial.println(contactList[i].phone);
  }
}



void contactsMenu() {
  Serial.println("\nContacts:");
  Serial.println("1. View");
  Serial.println("2. Add");
  Serial.println("3. Back");

  char choice[2];
  readSerial(choice);

  if (choice[0] == '1') {
    viewContacts();
  } else if (choice[0] == '2') {
    addContact();
  }
    else if (choice[0] == '3'){
      return;
    }
}

*/