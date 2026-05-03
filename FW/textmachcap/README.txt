.DS_Store ?
UI.cpp/.h:  a few constants in it, alot more to be added for fonts, etc
buttons.cpp/.h:  button functions for creation,config,clicking through UI
contacts.cpp/.h: functions for contacts, very incomplete
keyboard.cpp/.h: creates keyboard, draws it using buttons
messages.cpp/.h: handles messaging storing logic.  currently saving in volative, need to change to store in SD flash
textmachcap.ino: main program that calls the above libraries

Regarding the Cap touch screen:
In a nutshell the ILI9341 chip handles screen display output, while the FT6336U handles the touch input.
Example, user touches "t" letter via FT6336U, then the FW does it's thing to then drive the ILI9431 chip which updates the screen.

As of 05/03/26: the main work in the FW to still be done:
1.  full contact integration
2.  put messages and contact onto non-volative memory
3.  AT command testing for power savings and also testing for touch power savings.
4.  General code cleanup which may also reduce some power
5.  further pretty printing the UI if we really want to.

