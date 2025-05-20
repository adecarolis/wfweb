#include "aboutbox.h"
#include "ui_aboutbox.h"

aboutbox::aboutbox(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::aboutbox)
{
    ui->setupUi(this);
    setWindowTitle("About wfview");
    setWindowIcon(QIcon(":resources/wfview.png"));

    ui->logoBtn->setIcon(QIcon(":resources/wfview.png"));
    ui->logoBtn->setStyleSheet("Text-align:left");

    ui->topText->setText("wfview version " + QString(WFVIEW_VERSION));

    QString head = QString("<html><head></head><body>");
    QString copyright = QString("Copyright 2017-2024 Elliott H. Liggett, W6EL and Phil E. Taylor, M0VSE. All rights reserved.<br/>wfview source code is <a href=\"https://gitlab.com/eliggett/wfview/-/blob/master/LICENSE\">licensed</a> under the GNU GPLv3.");
    QString scm = QString("<br/><br/>Source code and issues managed by Roeland Jansen, PA3MET");
    QString doctest = QString("<br/><br/>Testing and development mentorship from Jim Nijkamp, PA8E.");

    QString dedication = QString("<br/><br/><b>This version of wfview is dedicated to the natural pursuit of freedom by people everywhere.</b> "
                                 "<br/><br/>Special thanks to Tony Collen, N0RUA/AE0KW (SK), for his work on open890, which was the inspiration for our support of the Kenwood TS-890. "
                                 "<br/><br/>Special thanks to our translators:<br/>Siwij Cat TA1YEP (Turkish)<br/>OK2HAM (Czech)<br/>JG3HLX (Japanese)<br/>Dawid SQ6EMM (Polish)<br/>Jim PA8E (Dutch)");

#if defined(Q_OS_LINUX)
    QString ssCredit = QString("<br/><br/>Stylesheet <a href=\"https://github.com/ColinDuquesnoy/QDarkStyleSheet/tree/master/qdarkstyle\"  style=\"color: cyan;\">qdarkstyle</a> used under MIT license, stored in /usr/share/wfview/stylesheets/.");
#else
    QString ssCredit = QString("<br/><br/>Stylesheet <a href=\"https://github.com/ColinDuquesnoy/QDarkStyleSheet/tree/master/qdarkstyle\"  style=\"color: cyan;\">qdarkstyle</a> used under MIT license.");
#endif

    QString website = QString("<br/><br/>Please visit <a href=\"https://wfview.org/\"  style=\"color: cyan;\">https://wfview.org/</a> for the latest information.");

    QString donate = QString("<br/><br/>Join us on <a href=\"https://www.patreon.com/wfview\">Patreon</a> for a behind-the-scenes look at wfview development, nightly builds, and to support the software you love.");

    QString docs = QString("<br/><br/>Be sure to check the <a href=\"https://wfview.org/wfview-user-manual/\"  style=\"color: cyan;\">User Manual</a> and <a href=\"https://forum.wfview.org/\"  style=\"color: cyan;\">the Forum</a> if you have any questions.");
    QString support = QString("<br/><br/>For support, please visit <a href=\"https://forum.wfview.org/\">the official wfview support forum</a>.");
    QString gitcodelink = QString("<a href=\"https://gitlab.com/eliggett/wfview/-/tree/%1\"  style=\"color: cyan;\">").arg(GITSHORT);

    QString buildInfo = QString("<br/><br/>Build " + gitcodelink + QString(GITSHORT) + "</a> on " + QString(__DATE__) + " at " + __TIME__ + " by " + UNAME + "@" + HOST);
    QString end = QString("</body></html>");

    // Short credit strings:
    QString rsCredit = QString("<br/><br/><a href=\"https://www.speex.org/\"  style=\"color: cyan;\">Speex</a> Resample library Copyright 2003-2008 Jean-Marc Valin");
    QString rtaudiocredit = QString("<br/><br/>RT Audio, from <a href=\"https://www.music.mcgill.ca/~gary/rtaudio/index.html\">Gary P. Scavone</a>");
    QString portaudiocredit = QString("<br/><br/>Port Audio, from <a href=\"http://portaudio.com\">The Port Audio Community</a>");
    QString qcpcredit = QString("<br/><br/>The waterfall and spectrum plot graphics use QCustomPlot, from  <a href=\"https://www.qcustomplot.com/\">Emanuel Eichhammer</a>");
    QString qtcredit = QString("<br/><br/>This copy of wfview was built against Qt version %1").arg(QT_VERSION_STR);
    QString hamlibcredit = QString("<br/><br/>wfview contains our own implementation of the Hamlib rigctl protocol which uses portions of code from <a href=\"https://hamlib.github.io/\">Hamlib</a><br/>Copyright (C) 2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011,2012 The Hamlib Group");
    QString adpcmcredit = QString("<br/><br/>wfview contains the adpcm-xq audio encoder/decoder - Copyright (c) David Bryant All rights reserved.");

    // Acknowledgement:
    QString wfviewcommunityack = QString("<br/><br/>The developers of wfview wish to thank the many contributions from the wfview community at-large, including ideas, bug reports, and fixes.");
    QString kappanhangack = QString("<br/><br/>Special thanks to Norbert Varga, and the <a href=\"https://github.com/nonoo/kappanhang\">nonoo/kappanhang team</a> for their initial work on the OEM protocol.");

    QString kb3mmwCredit = QString("<br/><br/>Many thanks to KB3MMW who assisted with the reverse enginering of the Yaesu LAN protocol. Portions of his code which he has released under LGPL/GPL have been integrated within wfview <a href=\"https://forum.wfview.org/t/off-topic-yaesu-ft710-and-others/3275/46\">Forum post</a>");

    QString sxcreditcopyright = QString("Speex copyright notice: \
Copyright (C) 2003 Jean-Marc Valin\n\
Redistribution and use in source and binary forms, with or without\n\
modification, are permitted provided that the following conditions\n\
are met:\n\
- Redistributions of source code must retain the above copyright\n\
notice, this list of conditions and the following disclaimer.\n\
- Redistributions in binary form must reproduce the above copyright\n\
notice, this list of conditions and the following disclaimer in the\n\
documentation and/or other materials provided with the distribution.\n\
- Neither the name of the Xiph.org Foundation nor the names of its\n\
contributors may be used to endorse or promote products derived from\n\
this software without specific prior written permission.\n\
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n\
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n\
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n\
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR\n\
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,\n\
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,\n\
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR\n\
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF\n\
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING\n\
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n\
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\n");

QString freqCtlCredit = QString("/*"
    "* Frequency controller widget (originally from CuteSDR)\n"
    "*\n"
    "* This code is used within wfview and was modified\n"
    "* You can download the source code from here: \n"
    "* https://gitlab.com/eliggett/wfview/\n"
    "*\n"
    "* Copyright 2010 Moe Wheatley AE4JY \n"
    "* Copyright 2012-2017 Alexandru Csete OZ9AEC\n"
    "* Copyright 2024 Phil Taylor M0VSE\n"
    "* All rights reserved.\n"
    "*\n"
    "* This software is released under the \"Simplified BSD License\".\n"
    "*\n"
    "* Redistribution and use in source and binary forms, with or without\n"
    "* modification, are permitted provided that the following conditions are met:\n"
    "*\n"
    "* 1. Redistributions of source code must retain the above copyright notice,\n"
    "*    this list of conditions and the following disclaimer.\n"
    "*\n"
    "* 2. Redistributions in binary form must reproduce the above copyright notice,\n"
    "*    this list of conditions and the following disclaimer in the documentation\n"
    "*    and/or other materials provided with the distribution.\n"
    "*\n"
    "* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n"
    "* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n"
    "* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n"
    "* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE\n"
    "* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n"
    "* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n"
    "* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n"
    "* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n"
    "* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n"
    "* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n"
    "* POSSIBILITY OF SUCH DAMAGE.\n"
    "*/\n");

    QString ftdiCredit = QString("\n/**\n"
    "* FT4222 support library (for FT-710 SPI support)\n"
    "*\n"
    "* Copyright (c) 2001-2015 Future Technology Devices International Limited\n"
    "*\n"
    "* THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED \"AS IS\"\n"
    "* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n"
    "* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL\n"
    "* FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
    "* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT\n"
    "* OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)\n"
    "* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR\n"
    "* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,\n"
    "* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
    "*\n"
    "* FTDI DRIVERS MAY BE USED ONLY IN CONJUNCTION WITH PRODUCTS BASED ON FTDI PARTS.\n"
    "*\n"
    "* FTDI DRIVERS MAY BE DISTRIBUTED IN ANY FORM AS LONG AS LICENSE INFORMATION IS NOT MODIFIED.\n"
    "*/\n\n");

    // String it all together:

    QString aboutText = head + copyright + "\n" + "\n" + scm + "\n" + doctest + dedication + wfviewcommunityack;
    aboutText.append(website + "\n" + donate + "\n"+ docs + support +"\n");
    aboutText.append("\n" + ssCredit + "\n" + rsCredit +"\n");

    aboutText.append(rtaudiocredit);

    aboutText.append(portaudiocredit);

    aboutText.append(kappanhangack + kb3mmwCredit + qcpcredit + qtcredit + hamlibcredit + adpcmcredit );
    aboutText.append("<br/><br/>");
    aboutText.append("<pre>" + sxcreditcopyright + freqCtlCredit + ftdiCredit + "</pre>");
    aboutText.append("<br/><br/>");

    aboutText.append(end);
    ui->midTextBox->setText(aboutText);
    ui->bottomText->setText(buildInfo);
    ui->midTextBox->setFocus();
}

aboutbox::~aboutbox()
{
    delete ui;
}

void aboutbox::on_logoBtn_clicked()
{
    QDesktopServices::openUrl(QUrl("https://www.wfview.org/"));
}
