# How to install wfview without building yourself on selected linux versions



We understand that downloading sources with git, selecting branches and building yourself may a bit daunting.
In the future we may at some point start distributing packages and/or images like appimage, flatpack. snap.


Instructions how to use this w/o building yourself. We are using a precompiled version that has been tested on a few 
different versions of linux in alphabetical order. Note that all are click-click-next-next-finish installs.

not supported: 

centos7			-- no qt support
debian 10 		-- outdated
devuan 3.1.1.1          -- outdated
redhat7 		-- no qt support


~~~
Debian 11  (Debian 10 is outdated)
Fedora 33
Fedora 34
mint 20.1 (and up?)
openSUSE 15.2 (outdated/deprecated) 
openSUSE 15.3/15.4)
openSUSE Tumbleweed(s)
SLES 15.x
Ubuntu 20.04.2 
mint 20.2     (see notes at the end)
~~~


### for all, the following is applicable:
~~~
download the tar.gz file here: https://wfview.org/download/test-linux-build/ 

the file below will unpack in ./dist

tar zxvf wfview-linux.tar.gz        (change the filename accordingly)          
cd dist
sudo ./install.sh
~~~
this will install the binary and a few other files to your system.


Now for the system specifics; pick your version:

### Debian 11:
~~~
sudo apt install libqcustomplot2.0 libqt5multimedia5 libqt5serialport5
sudo ln -s /usr/lib/x86_64-linux-gnu/libqcustomplot.so.2.0.1 /usr/lib/x86_64-linux-gnu/libqcustomplot.so.2
wfview
~~~

### Fedora 33/34:
~~~
sudo dnf install qcustomplot-qt5 qt5-qtmultimedia qt5-qtserialport
sudo ln  -s  /usr/lib64/libqcustomplot-qt5.so.2 /usr/lib64/libqcustomplot.so.2
wfview
~~~

### Mint 20.1
~~~
sudo apt install libqcustomplot2.0 libqt5multimedia5 libqt5serialport5
sudo ln  -s  /usr/lib64/libqcustomplot-qt5.so.2 /usr/lib64/libqcustomplot.so.2
wfview

note: if the above symlink fails, use the following line to fix the library link:

sudo ln -s /lib/x86_64-linux-gnu/libqcustomplot.so.2.0.1 /lib/x86_64-linux-gnu/libqcustomplot.so.2
~~~

### Mint 20.2
~~~

SEE THE NOTES AT THE END. You need wfview153 binary here

sudo apt install libqcustomplot2.0 libqt5multimedia5 libqt5serialport5
sudo ln  -s  /usr/lib64/libqcustomplot-qt5.so.2 /usr/lib64/libqcustomplot.so.2
wfview

note: if the above symlink fails, use the following line to fix the library link:

sudo ln -s /lib/x86_64-linux-gnu/libqcustomplot.so.2.0.1 /lib/x86_64-linux-gnu/libqcustomplot.so.2
~~~


### openSUSE/Tumbleweed/SLES based on 15.3/15.4:
~~~

sudo zypper in libqcustomplot2 libQt5SerialPort5
wfview
~~~


### UBUNTU:
~~~
sudo apt install libqcustomplot2.0 libqt5multimedia5 libqt5serialport5
sudo ln -s /usr/lib/x86_64-linux-gnu/libqcustomplot.so.2.0.1 /usr/lib/x86_64-linux-gnu/libqcustomplot.so.2
wfview


note: if the above symlink fails, use the following line to fix the library link:

sudo ln -s /lib/x86_64-linux-gnu/libqcustomplot.so.2.0.1 /lib/x86_64-linux-gnu/libqcustomplot.so.2
~~~



### notes:
~~~

openSUSE 15.2 is deprecated; old binary has been removed


~~~

 
