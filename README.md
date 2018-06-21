# wfview
Open Source IC-7300 Visualizer and Controller, including waterfall view

Requirements:
gcc
qmake
qt5 (proably the package named "qt5-default")
libqt5serialport5-dev
libqcustomplot-dev 

Rig setting:
Use default CI-V address (0x94)
Baud rate 115200
Turn on the bandscope on the rig screen

The program currently assumes the device is on /dev/ttyUSB0. Make sure the port is writable by your username. 

Features:
plot bandscope and bandscope waterfall. Optionally, also plot a "peak hold". 
Double-elick anywhere on the bandscope or waterfall to tune the radio. 
Entry of frequency is permitted under the "Frequency" tab
Bandscope parameters (span and mode) are adjustable. 

TODO:
Carefully build comm port traffic into messages. Reject corrupted messages earlier in the chain. 
Impliment "dark mode" in plot and UI elements. 
Impliment band presets per the "Band" tab (currently does nothing)
Impliment STO and RCL buttons under Freq tab
Save settings to text file
Automatically poll transceiver and bandscope state on startup. 
Change the delayedCommand so that it can accept a vector of queued commands. Or change the signal/slot paradigm to automatically queue requests. 

