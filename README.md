# wfview


[wfview](https://gitlab.com/eliggett/wfview) is an open-source front-end application for the 

- [Icom IC-705 ](https://www.icomamerica.com/en/products/amateur/hf/705/default.aspx) HF portable SDR Amateur Radio
- [Icom IC-7300](https://www.icomamerica.com/en/products/amateur/hf/7300/default.aspx) HF SDR Amateur Radio
- [Icom IC-7610](https://www.icomamerica.com/en/products/amateur/hf/7610/default.aspx) HF SDR Amateur Radio
- [Icom IC-7850](https://www.icomamerica.com/en/products/amateur/hf/7850/default.aspx) HF Hybrid SDR Amateur Radio
- [Icom IC-7851](https://www.icomamerica.com/en/products/amateur/hf/7851/default.aspx) HF Hybrid SDR Amateur Radio
- [Icom IC-9700](https://www.icomamerica.com/en/products/amateur/hf/9700/default.aspx) VHF/UHF SDR Amateur Radio

Other models to be tested/added (including the IC-705).. 

website - [WFVIEW](https://wfview.org/) wfview.org

wfview supports viewing the spectrum display waterfall and most normal radio controls. Using wfview, the radio can be operated using the mouse, or just the keyboard (great for those with visual impairments), or even a touch screen display. The gorgous waterfall spectrum can be displayed on a monitor of any size, and can even projected onto a wall for a presentation. Even a VNC session can make use of wfview for interesting remote rig possibilities. wfview runs on humble hardware, ranging from the $35 Raspberry Pi, to laptops, to desktops. wfview is designed to run on GNU Linux, but can probably be adapted to run on other operating systems. In fact we do have  working example in windows as well.



wfview is unique in the radio control ecosystem in that it is free and open-source software and can take advantage of modern radio features (such as the waterfall). wfview also does not "eat the serial port", and can allow a second program, such as fldigi, access to the radio via a pseudo-terminal device. 

**For screenshots, documentation, User FAQ, Programmer FAQ, and more, please [see the project's wiki](https://gitlab.com/eliggett/wfview/-/wikis/home).**

wfview is copyright 2017-2020 Elliott H. Liggett. All rights reserved. wfview source code is licensed via the GNU GPLv3.

### Features:
1. Plot bandscope and bandscope waterfall. Optionally, also plot a "peak hold". A splitter lets the user adjust the space used for the waterfall and bandscope plots.
2. Double-click anywhere on the bandscope or waterfall to tune the radio. 
3. Entry of frequency is permitted under the "Frequency" tab. Buttons are provided for touch-screen control
4. Bandscope parameters (span and mode) are adjustable. 
5. Full [keyboard](https://gitlab.com/eliggett/wfview/-/wikis/Keystrokes) and mouse control. Operate in whichever way you like. Most radio functions can be operated from a numeric keypad! This also enables those with visual impairments to use the IC-7300. 
6. 100 user memories stored in plain text on the computer
7. Stylable GUI using CSS
8. pseudo-terminal device, which allows for secondary program to control the radio while wfview is running
9. works for radios that support the ethernet interface with comparable waterfall speeds as on the radio itself.

### Build Requirements:
1. gcc / g++ / make
2. qmake
3. qt5 (probably the package named "qt5-default")
4. libqt5serialport5-dev
5. libqcustomplot-dev 

### Recommended:
* Debian-based Linux system (Debian Linux, Linux Mint, Ubuntu, etc) or opensuse 15.x. Any recent Linux system will do though!
* QT Creator for building, designing, and debugging w/gdb

### Build directions:

See [INSTALL.md](https://gitlab.com/eliggett/wfview/-/blob/master/INSTALL.md) for directions. 

### Rig setting:
1. CI-V Baud rate: Auto
2. CI-V address: 94h (default) 
3. CI-V Transceive ON
4. CI-V USB-> REMOTE Transceive Address: 00h
5. CI-V Output (for ANT): OFF
6. CI-V USB Port: Unlink from REMOTE
7. CI-V USB Baud Rate: 15200
8. CI-V USB Echo Back: OFF
9. Turn on the bandscope on the rig screen

* Note: The program currently assumes the radio is on a device like this: 
~~~
/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_IC-7300_02010092-if00-port0
~~~
This is symlinked to a device like /dev/ttyUSB0 typically. Make sure the port is writable by your username. You can accomplish this using udev rules, or if you are in a hurry: 
~~~
sudo chown `whoami` /dev/ttyUSB*
~~~

### TODO (for developers and contributors):
1. Re-work pseudo term code into separate thread
2. Consider XML RPC to make flrig/fldigi interface easier 
3. Add hide/show for additional controls: SWR, ALC, Power, S-Meter interface
4. Fix crash on close (order of delete operations is important)
5. Add support for other compatible CI-V radios (IC-706, IC-7100, IC-7610, etc)
6. Better settings panel (select serial port, CI-V address, more obvious exit button)
7. Add support for festival or other text-to-speech method using the computer (as apposed to the radio's speech module)


see also the wiki:

- [bugs](https://gitlab.com/eliggett/wfview/-/wikis/Bugs)
- [feature requests](https://gitlab.com/eliggett/wfview/-/wikis/Feature-requests)
- [raspberry pi server](https://gitlab.com/eliggett/wfview/-/wikis/raspi-server-functionality-for-7300,7100-etc)


### THIRD PARTY code/hardware:

the following projects we would like to thank in alphabetical order:



- ICOM for their well designed rigs

see ICOM Japan (https://www.icomjapan.com/)

- ICOM for their well written RS-BA1 software

see ICOM JAPAN products page (https://www.icomjapan.com/lineup/options/RS-BA1_Version2/)

- kappanhang which inspired us to enhance the original wfview project:

  Akos Marton           ES1AKOS
  Elliot LiggettW6EL    (passcode algorithm)
  Norbert Varga HA2NON  nonoo@nonoo.hu

see for their fine s/w here [kappanhang](https://github.com/nonoo/kappanhang)

- resampling code from the opus project:
  [Xiph.Org Foundation] (https://xiph.org/)

see [sources] (https://github.com/xiph/opus/tree/master/silk)

- QCP: the marvellous qt custom plot code
  
  Emanuel Eichhammer

see [QCP] (https://www.qcustomplot.com/)



If you feel that we forgot ayone, just drop a mail.

