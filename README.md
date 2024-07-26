# wfview


[wfview](https://gitlab.com/eliggett/wfview) is an open-source front-end application for the 

- [Icom IC-705 ](https://www.icomamerica.com/en/products/amateur/hf/705/default.aspx) HF portable SDR Amateur Radio
- [Icom IC-805 ](https://www.icomamerica.com/lineup/products/IC-905/) Multiband VHF/UHF/SHF portable SDR Amateur Radio
- [Icom IC-7300](https://www.icomamerica.com/en/products/amateur/hf/7300/default.aspx) HF SDR Amateur Radio
- [Icom IC-7610](https://www.icomamerica.com/en/products/amateur/hf/7610/default.aspx) HF SDR Amateur Radio
- [Icom IC-7850](https://www.icomamerica.com/en/products/amateur/hf/7850/default.aspx) HF Hybrid SDR Amateur Radio
- [Icom IC-7851](https://www.icomamerica.com/en/products/amateur/hf/7851/default.aspx) HF Hybrid SDR Amateur Radio
- [Icom IC-9700](https://www.icomamerica.com/en/products/amateur/hf/9700/default.aspx) VHF/UHF SDR Amateur Radio


website - [WFVIEW](https://wfview.org/) wfview.org

wfview supports modern and vintage radios using the CI-V protocol. Core features include a visual display of the waterfall data, simple point-and-click operation, basic type-and-send CW, and remote operation over wifi and Ethernet. For advanced users, wfview can also operate as a radio server, enabling remote access to USB-connected radios like the IC-7300. wfview allows other programs, such as logging and digital mode applications, to share control of the radio. 

wfview is unique in the radio control ecosystem in that it is free and open-source software and can take advantage of modern radio features (such as the waterfall). wfview also does not "eat the serial port", and can allow a second program, such as fldigi, access to the radio via a pseudo-terminal device. 

**For screenshots, documentation, User FAQ, Programmer FAQ, and more, please [see the project's website, wfview.org](https://wfview.org/).**

Links for Users: 
- [Getting Started](https://wfview.org/wfview-user-manual/getting-started/)
- [FAQ](https://wfview.org/wfview-user-manual/faq/)
- [User Manual](https://wfview.org/wfview-user-manual/)

Links for Developers: 
- [Developer's Corner](https://wfview.org/developers/)
- [Compiling for Linux](https://gitlab.com/eliggett/wfview/-/blob/master/INSTALL.md)
- [Compiler Script for Debian-based Linux](https://gitlab.com/eliggett/scripts/-/blob/master/fullbuild-wfview.sh)
- [Public Automated Builds](https://wfview.org/developers/)


wfview is copyright 2017-2024 Elliott H. Liggett (W6EL) and Phil Taylor (M0VSE). All rights reserved. wfview source code is licensed via the GNU GPLv3.

## Special Thanks: 
- ICOM for their well designed rigs

see ICOM Japan (https://www.icomjapan.com/)

- ICOM for their well written RS-BA1 software

see ICOM JAPAN products page (https://www.icomjapan.com/lineup/options/RS-BA1_Version2/)

- kappanhang which inspired us to enhance the original wfview project:

  Akos Marton           ES1AKOS
  Elliot Liggett W6EL    (passcode algorithm)
  Norbert Varga HA2NON  nonoo@nonoo.hu

see for their fine s/w here [kappanhang](https://github.com/nonoo/kappanhang)

- resampling code from the opus project:
  [Xiph.Org Foundation] (https://xiph.org/)

see [sources] (https://github.com/xiph/opus/tree/master/silk)

- QCP: the marvellous qt custom plot code
  
  Emanuel Eichhammer

see [QCP] (https://www.qcustomplot.com/)



If you feel that we forgot anyone, just drop a mail.

