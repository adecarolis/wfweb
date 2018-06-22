# wfview
Open Source Icom IC-7300 Visualizer and Controller, including waterfall view. With this program, you can see your waterfall display on a large screen, or launch from a VNC session and remotely control your radio. wfview does not use hamlib or any other common radio control libraries. That may chance, but at this time, it is not clear how to impliment a continuous stream of spectrum data through either hamlib or flrig. The code should be easily adaptable to the IC-7610 and any other SDR-based Icom radios. 

### Features:
1. Plot bandscope and bandscope waterfall. Optionally, also plot a "peak hold". A splitter lets the user adjust the space used for the waterfall and bandscope plots.
2. Double-elick anywhere on the bandscope or waterfall to tune the radio. 
3. Entry of frequency is permitted under the "Frequency" tab. Buttons are provided for touch-screen control
4. Bandscope parameters (span and mode) are adjustable. 

### Build Requirements:
1. gcc / g++ / make
2. qmake
3. qt5 (proably the package named "qt5-default")
4. libqt5serialport5-dev
5. libqcustomplot-dev 

### Recommended:
* Debian-based Linux system (Debian Linux, Linux Mint, Ubuntu, etc).
* QT Creator for building, designing, and debugging w/gdb

### Build directions:
1. clone this repository into a new folder
2. make a directory to build the code in (mkdir build)
3. Run qmake to configure the build:
  * cd build
  * qmake ../wfview/wfview.pro
4. Compile by running make.

### Rig setting:
1. Use default CI-V address (0x94)
2. Baud rate 115200
3. Turn on the bandscope on the rig screen

* Note: The program currently assumes the device is on /dev/ttyUSB0. Make sure the port is writable by your username. 

### TODO:
1. Carefully build comm port traffic into messages. Reject corrupted messages earlier in the chain. 
2. Impliment "dark mode" in plot and UI elements. 
3. Impliment band presets per the "Band" tab (currently does nothing)
4. Impliment STO and RCL buttons under Freq tab
5. Save settings to text file
6. Automatically poll transceiver and bandscope state on startup. 
7. Change the delayedCommand so that it can accept a vector of queued commands. Or change the signal/slot paradigm to automatically queue requests. 
8. Automatically query the Data Mode when we query the Mode. Also allow data mode setting. 
9. Fix the indexes on the waterfall display. Currently we take the mouse coordinates and intrepret frequency from the bandscope plot. 
10. Enable the band scope display in addition to the band scope serial data output. 


