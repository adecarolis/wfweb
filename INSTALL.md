# How to install wfview

### 1. Install prerequisites:
(Note, some packages may have slightly different version numbers, this should be ok for minor differences.)
~~~
sudo apt-get install build-essential
sudo apt-get install qt5-qmake
sudo apt-get install qt5-default
sudo apt-get install libqt5core5a
sudo apt-get install qtbase5-dev
sudo apt-get install libqt5serialport5 libqt5serialport5-dev
sudo apt-get install git 
# for older linux versions (Linux Mint 19.x, Ubuntu 18.04): 
sudo apt-get install libqcustomplot1.3 libqcustomplot-doc libqcustomplot-dev
# for newer linux versions (Ubuntu 19, Rasbian V?):
sudo apt-get install libqcustomplot2 libqcustomplot-doc libqcustomplot-dev
~~~
optional for those that want to work on the code using the QT Creator IDE: 
~~~
sudo apt-get install qtcreator qtcreator-doc
~~~

### 2. Clone wfview to a local directory on your computer:
~~~ 
cd ~/Documents
git clone https://gitlab.com/eliggett/wfview.git
~~~

### 3. Create a build directory, compile, and install:
~~~
mkdir build
cd build
qmake ../wfview/wfview.pro
make -j
sudo ./install.sh
~~~

### 4. You can now launch wfview, either from the terminal or from your desktop environment. If you encounter issues using the serial port, run the following command: 
~~~
sudo chown $USER /dev/ttyUSB*
~~~

