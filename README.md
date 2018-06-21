# wfview
Open Source IC-7300 Visualizer and Controller, including waterfall view

### Requirements:
1. gcc / g++ / make
2. qmake
3. qt5 (proably the package named "qt5-default")
4. libqt5serialport5-dev
5. libqcustomplot-dev 

### Recommended:
* Debian-based Linux system
* QT Creator for building, designing, and debugging

### Rig setting:
1. Use default CI-V address (0x94)
2. Baud rate 115200
3. Turn on the bandscope on the rig screen

* Note: The program currently assumes the device is on /dev/ttyUSB0. Make sure the port is writable by your username. 

### Features:
1. Plot bandscope and bandscope waterfall. Optionally, also plot a "peak hold". 
2. Double-elick anywhere on the bandscope or waterfall to tune the radio. 
3. Entry of frequency is permitted under the "Frequency" tab
4. Bandscope parameters (span and mode) are adjustable. 

### TODO:
1. Carefully build comm port traffic into messages. Reject corrupted messages earlier in the chain. 
2. Impliment "dark mode" in plot and UI elements. 
3. Impliment band presets per the "Band" tab (currently does nothing)
4. Impliment STO and RCL buttons under Freq tab
5. Save settings to text file
6. Automatically poll transceiver and bandscope state on startup. 
7. Change the delayedCommand so that it can accept a vector of queued commands. Or change the signal/slot paradigm to automatically queue requests. 
8. Automatically poll the Data Mode when noting the Mode. Also allow data mode setting. 

