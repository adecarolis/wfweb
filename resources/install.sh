#!/bin/bash
echo "This script copies the following items into your system:" 
echo ""
echo "icon: wfview.png to /usr/share/pixmaps/"
echo "wfview application to /usr/local/bin/"
echo "wfview.desktop to /usr/share/applications/"
echo "qdarkstyle stylesheet to /usr/share/wfview/stylesheets"

echo ""
echo "This script MUST be run from the build directory. Do not run it from the source directory!"
echo ""

if ! [ $(id -u) = 0 ]; then
   echo "This script must be run as root."
   echo "example: sudo $0"
   exit 1
fi


read -p "Do you wish to continue? (Y/N): " -n 1 -r
if [[ ! $REPLY =~ ^[Yy]$ ]]
then
    exit 1
fi

# Now the actual install: 

cp wfview /usr/local/bin/wfview
cp wfview.desktop /usr/share/applications/
cp wfview.png /usr/share/pixmaps/
mkdir -p /usr/share/wfview/stylesheets
cp -r qdarkstyle /usr/share/wfview/stylesheets/

echo "Done!"

