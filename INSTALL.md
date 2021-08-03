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
sudo apt-get install libqt5multimedia5
sudo apt-get install libqt5multimedia5-plugins
sudo apt-get install qtmultimedia5-dev
sudo apt-get install git 
~~~
Now you need to install qcustomplot. There are two versions that are commonly found in linux distros: 1.3 and 2.0. Either will work fine. If you are not sure which version your linux install comes with, simply run both commands. One will work and the other will fail, and that's fine!

qcustomplot1.3 for older linux versions (Linux Mint 19.x, Ubuntu 18.04): 

~~~
sudo apt-get install libqcustomplot1.3 libqcustomplot-doc libqcustomplot-dev
~~~

qcustomplot2 for newer linux versions (Linux Mint 20, Ubuntu 19, Rasbian V?, Debian 10):

~~~
sudo apt-get install libqcustomplot2.0 libqcustomplot-doc libqcustomplot-dev
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
If you want to change the default install path from `/usr/local` to a different prefix (e.g. `/opt`), you must call `qmake ../wfview/wfview.pro PREFIX=/opt`

~~~
mkdir build
cd build
qmake ../wfview/wfview.pro
make -j
sudo make install
~~~

### 4. You can now launch wfview, either from the terminal or from your desktop environment. If you encounter issues using the serial port, run the following command: 
~~~

if you are using the wireless 705 or any networked rig like the 7610, 7800, 785x, there is no need to use USB so below is not needed.

sudo chown $USER /dev/ttyUSB*

Note, on most linux systems, you just need to add your user to the dialout group, which is persistent and more secure:

~~~
sudo usermod -aG dialout $USER 
~~~
(don't forget to log out and log in)

~~~

### opensuse/sles/tumbleweed install
---

install wfview on suse 15.x sles 15.x or tumbleweed; this was done on a clean install/updated OS. 

we need to add packages to be able to build the stuff.

- sudo zypper in --type pattern devel_basis
- sudo zypper in libQt5Widgets-devel libqt5-qtbase-common-devel libqt5-qtserialport-devel libQt5SerialPort5 qcustomplot-devel libqcustomplot2 libQt5PrintSupport-devel libqt5-qtmultimedia-devel 

optional (mainly for development specifics): get and install qt5:

- wget http://download.qt.io/official_releases/online_installers/qt-unified-linux-x64-online.run
- chmod +x qt-unified-linux-x64-online.run


- kdesu ./qt-unified-linux-x64-online.run

install Qt 5.15.2 for GCC for desktop application development

when done, create the place where to build:

in this case, use your homedir:

- mkdir -p ~/src/build && cd src
- git clone https://gitlab.com/eliggett/wfview.git
- cd build
- qmake-qt5 ../wfview/wfview.pro
- make -j
- sudo ./install.sh

wfview is now installed in /usr/local/bin

---

### Fedora install ###
---

Tested under Fedora 33/34.

Install qt5 dependencies:
- sudo dnf install qt5-qtbase-common qt5-qtbase qt5-qtbase-gui qt5-qtserialport qt5-qtmultimedia mingw64-qt5-qmake qt5-qtbase-devel qt5-qtserialport-devel qt5-qtmultimedia-devel 

Install qcustomplot:
- sudo dnf install qcustomplot qcustomplot-devel

When done, create a build area, clone the repo, build and install:

- mkdir -p ~/src/build && cd src
- git clone https://gitlab.com/eliggett/wfview.git
- cd build
- qmake-qt5 ../wfview/wfview.pro
- make -j
- sudo ./install.sh

wfview is now installed in /usr/local/bin

---
