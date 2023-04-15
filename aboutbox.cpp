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
    QString copyright = QString("Copyright 2017-2023 Elliott H. Liggett, W6EL and Phil E. Taylor, M0VSE. All rights reserved.<br/>wfview source code is <a href=\"https://gitlab.com/eliggett/wfview/-/blob/master/LICENSE\">licensed</a> under the GNU GPLv3.");
    QString scm = QString("<br/><br/>Source code and issues managed by Roeland Jansen, PA3MET");
    QString doctest = QString("<br/><br/>Testing and development mentorship from Jim Nijkamp, PA8E.");

    QString dedication = QString("<br/><br/>This version of wfview is dedicated to the ones we lost.");

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

    QString contact = QString("<br/>email W6EL: kilocharlie8 at gmail.com");

    QString buildInfo = QString("<br/><br/>Build " + gitcodelink + QString(GITSHORT) + "</a> on " + QString(__DATE__) + " at " + __TIME__ + " by " + UNAME + "@" + HOST);
    QString end = QString("</body></html>");

    // Short credit strings:
    QString rsCredit = QString("<br/><br/><a href=\"https://www.speex.org/\"  style=\"color: cyan;\">Speex</a> Resample library Copyright 2003-2008 Jean-Marc Valin");
#if defined(RTAUDIO)
    QString rtaudiocredit = QString("<br/><br/>RT Audio, from <a href=\"https://www.music.mcgill.ca/~gary/rtaudio/index.html\">Gary P. Scavone</a>");
#endif

#if defined(PORTAUDIO)
    QString portaudiocredit = QString("<br/><br/>Port Audio, from <a href=\"http://portaudio.com\">The Port Audio Community</a>");
#endif

    QString qcpcredit = QString("<br/><br/>The waterfall and spectrum plot graphics use QCustomPlot, from  <a href=\"https://www.qcustomplot.com/\">Emanuel Eichhammer</a>");
    QString qtcredit = QString("<br/><br/>This copy of wfview was built against Qt version %1").arg(QT_VERSION_STR);

    // Acknowledgement:
    QString wfviewcommunityack = QString("<br/><br/>The developers of wfview wish to thank the many contributions from the wfview community at-large, including ideas, bug reports, and fixes.");
    QString kappanhangack = QString("<br/><br/>Special thanks to Norbert Varga, and the <a href=\"https://github.com/nonoo/kappanhang\">nonoo/kappanhang team</a> for their initial work on the OEM protocol.");

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
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.");


    // String it all together:

    QString aboutText = head + copyright + "\n" + "\n" + scm + "\n" + doctest + dedication + wfviewcommunityack;
    aboutText.append(website + "\n" + donate + "\n"+ docs + support + contact +"\n");
    aboutText.append("\n" + ssCredit + "\n" + rsCredit + "\n");

#if defined(RTAUDIO)
    aboutText.append(rtaudiocredit);
#endif

#if defined(PORTAUDIO)
    aboutText.append(portaudiocredit);
#endif

    aboutText.append(kappanhangack + qcpcredit + qtcredit);
    aboutText.append("<br/><br/>");
    aboutText.append("<pre>" + sxcreditcopyright + "</pre>");
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
