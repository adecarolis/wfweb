# wfview
Open Source Icom IC-7300 Visualizer and Controller, including waterfall view. With this program, you can see your waterfall display on a large screen, or launch from a VNC session and remotely control your radio. wfview does not use hamlib or any other common radio control libraries. That may chance, but at this time, it is not clear how to impliment a continuous stream of spectrum data through either hamlib or flrig. The code should be easily adaptable to the IC-7610 and any other SDR-based Icom radios. 

**Please [see our wiki](https://gitlab.com/eliggett/wfview/-/wikis/home) for detailed information and basic operation manual.**

wfview is copyright 2017-2020 Elliott H. Liggett. 

### Features:
1. Plot bandscope and bandscope waterfall. Optionally, also plot a "peak hold". A splitter lets the user adjust the space used for the waterfall and bandscope plots.
2. Double-click anywhere on the bandscope or waterfall to tune the radio. 
3. Entry of frequency is permitted under the "Frequency" tab. Buttons are provided for touch-screen control
4. Bandscope parameters (span and mode) are adjustable. 
5. Full keyboard and mouse control. Operate in whichever way you like. Most radio functions can be operated from a numberic keypad! 

### Build Requirements:
1. gcc / g++ / make
2. qmake
3. qt5 (proably the package named "qt5-default")
4. libqt5serialport5-dev
5. libqcustomplot-dev 

### Recommended:
* Debian-based Linux system (Debian Linux, Linux Mint, Ubuntu, etc). Any recent Linux system will do though!
* QT Creator for building, designing, and debugging w/gdb

### Build directions:
1. clone this repository into a new folder
2. make a directory to build the code in (mkdir build)
3. Run qmake to configure the build:
  * cd build
  * qmake ../wfview/wfview.pro
4. Compile by running make.

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

### TODO:
1. Re-work pseudo term code into separate thread
2. Consider XML RPC to make flrig/fldigi interface easier 
3. Add hide/show for SWR, ALC, Power, S-Meter interface

