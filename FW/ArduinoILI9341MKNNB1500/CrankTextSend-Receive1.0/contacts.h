#ifndef CONTACTS_H
#define CONTACTS_H

#include <Arduino.h>

#define MAX_CONTACTS 50
#define MAX_NAME_LEN 20
#define MAX_PHONE_LEN 20


struct Contact {
  char name[MAX_NAME_LEN];
  char phone[MAX_PHONE_LEN];
};

void contactsInit();
void contactsMenu();
void addContact();
void viewContacts();
int findContactPhone(const char* name);
int findContactName(const char* phone);
// accessor : 
const char* getContactPhone(int index);


#endif
