#include "Contacts.h"
#include "Messages.h"
#include "buttons.h"
#include <SD.h>
#include <string.h> 

Contact contactList[MAX_CONTACTS];
static int contactCount = 0;
static int contactScrollOffset = 0;


// Forward declaration (Serial helper from main sketch)
extern int readSerial(char result[]);
extern int readInt();


static Button contactBtns[MAX_CONTACTS];
static Button deleteContactBtns[MAX_CONTACTS];
static Button contactsBackBtn;
static Button newContactsBtn;

static Button scrollUpBtn;
static Button scrollDownBtn;

static bool   contactsDrawn = false;



static int MAX_VISIBLE = 6;

void contactsInit() {
  contactCount = 0;
}

void contactsScreenReset() {
  contactsDrawn = false;
  contactScrollOffset = 0;
}

void deleteContact(int idx);

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

    char normalizedNumber[MAX_PHONE_LEN];
    normalizePhoneNumber(phone, normalizedNumber, MAX_PHONE_LEN);

    copyBounded(contactList[contactCount].name, name, MAX_NAME_LEN);
    copyBounded(contactList[contactCount].phone, normalizedNumber, MAX_PHONE_LEN);
    
    
    contactCount++;

    saveContactToSD(name, normalizedNumber);
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
        Serial.println("Failed to open contacts.csv, reinitializing SD");
        SD.begin(3);
    
        return;
    }

   //  contactCount = 0;
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
  const int listStartY = 56;
  const int rowH       = 46;
  const int x          = 0;
  const int w          = 172;   // row width, leaving room for delete + scroll cols
  const int scrollColX = 210;

  int visibleCount = min(MAX_VISIBLE, contactCount - contactScrollOffset); // so that I never try to draw more contacts then I have

  if (!contactsDrawn) {
    Serial.print("contactCount = ");
    Serial.println(contactCount);
    tft.fillScreen(UI_BG);

    contactsBackBtn.initButton(0, 0, 32, 32, "<");
    newContactsBtn.initButton(204, 0, 32, 32, "+", UI_ACCENT);

    if (contactScrollOffset > 0) {
      scrollUpBtn.initButton(scrollColX, listStartY, 30, 30, "^");
    }

    if (contactScrollOffset + MAX_VISIBLE < contactCount) {
      scrollDownBtn.initButton(scrollColX, listStartY + MAX_VISIBLE * rowH - 30, 30, 30, "v");
    }

    uiUseTitleFont();
    tft.setTextColor(UI_TEXT);
    tft.setCursor(40, 20);
    tft.print("Contacts");
    tft.setFont(NULL);

    for (int i = 0; i < visibleCount; i++) {
      int contactIdx = i + contactScrollOffset;  // ← actual contact
      int y = listStartY + i * rowH;              // ← screen position

      // Row background is a flat tap target — name/phone/divider drawn on top.
      contactBtns[i].initButton(x, y, w, rowH, "", UI_BG);
      deleteContactBtns[i].initButton(176, y + (rowH - 28) / 2, 28, 28, "X", UI_SURFACE);

      uiUseButtonFont();
      tft.setTextColor(UI_TEXT);
      tft.setCursor(x + 10, y + 20);
      tft.print(contactList[contactIdx].name);
      tft.setFont(NULL);

      tft.setTextSize(1);
      tft.setTextColor(UI_TEXT_DIM);
      tft.setCursor(x + 10, y + 32);
      tft.print(contactList[contactIdx].phone);

      tft.drawFastHLine(8, y + rowH, w - 16, UI_BORDER);
    }

    contactsDrawn = true;
  }

  if (!justPressed) return -1;

  // ── Scroll buttons ────────────────────────────────────────
  if (scrollUpBtn.isClicked(sp)) {
      if (contactScrollOffset > 0) {
          contactScrollOffset--;
          contactsDrawn = false;
      }
        return -1;
  }

  if (scrollDownBtn.isClicked(sp)) {
        if (contactScrollOffset + MAX_VISIBLE < contactCount) {
            contactScrollOffset++;
            contactsDrawn = false;
        }
        return -1;
  }

  if (contactsBackBtn.isClicked(sp)) return -2;  // caller checks for back

  if (newContactsBtn.isClicked(sp)) return -3; // Add a new contact! 

  // Contact/ Delete contact check 
  for (int i = 0; i < visibleCount; i++) {
    if (contactBtns[i].isClicked(sp)) {
      return i + contactScrollOffset;  // index into contactList[]
    }

    if (deleteContactBtns[i].isClicked(sp)) {
        deleteContact(i+contactScrollOffset);
        contactsDrawn = false;  // force redraw
        return -1;
    }
  }

  return -1;
}

void deleteContact(int idx){
  if (idx < 0 || idx >= contactCount) return;

  for(int i = idx; i < contactCount - 1; i++){
    copyBounded(contactList[i].name, contactList[i+1].name, MAX_NAME_LEN);
    copyBounded(contactList[i].phone, contactList[i+1].phone, MAX_PHONE_LEN);
  }
  contactCount--;

  SD.remove("contacts.csv");
    File f = SD.open("contacts.csv", FILE_WRITE);
    if (f) {
        for (int i = 0; i < contactCount; i++) {
            f.print(contactList[i].name);
            f.print("|");
            f.println(contactList[i].phone);
        }
        f.close();
        Serial.println("Contact deleted, CSV updated");
    }

}



/*
Serial Based old code

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