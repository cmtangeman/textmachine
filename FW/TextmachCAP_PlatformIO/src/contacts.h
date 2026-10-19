#ifndef CONTACTS_H
#define CONTACTS_H

#include <Arduino.h>
#include "types.h"


// TODO: Add contact and make it save to non volatile memory as a start, next up after that is to recognize numbers
// Scrolls down contacts and messages
// Fix conversation screen with respond button
// Make screen easily rotatable 


int  contactsScreen(const ScreenPoint& sp, bool justPressed);

void contactsInit();
void contactsScreenReset();

void addContactFromUI(const char* name, const char* phone);
void saveContactToSD(const char* name, const char* phone);
void loadContactsFromSD();
int  findContactPhone(const char* name);
int  findContactName(const char* phone);

const char* getContactPhone(int index);
const char* getContactName(int index);


#endif
