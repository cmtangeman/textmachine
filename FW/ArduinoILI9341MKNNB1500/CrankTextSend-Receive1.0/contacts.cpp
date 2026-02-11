#include "Contacts.h"
#include "Messages.h"
#include <string.h> 

 Contact contactList[MAX_CONTACTS];
static int contactCount = 0;

// Forward declaration (Serial helper from main sketch)
extern int readSerial(char result[]);
extern int readInt();

void contactsInit() {
  contactCount = 0;
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

const char* getContactPhone(int index) {
  if (index < 0 || index >= contactCount) {
    return nullptr;
  }
  return contactList[index].phone;
}

