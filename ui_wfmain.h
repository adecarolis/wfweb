/********************************************************************************
** Form generated from reading UI file 'wfmain.ui'
**
** Created by: Qt User Interface Compiler version 5.15.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_WFMAIN_H
#define UI_WFMAIN_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDial>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_wfmain
{
public:
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout;
    QTabWidget *tabWidget;
    QWidget *mainTab;
    QVBoxLayout *verticalLayout_2;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout_3;
    QSplitter *splitter;
    QCustomPlot *plot;
    QCustomPlot *waterfall;
    QHBoxLayout *horizontalLayout_13;
    QCheckBox *scopeCenterModeChk;
    QLabel *label_6;
    QComboBox *scopeBWCombo;
    QLabel *label_7;
    QComboBox *scopeEdgeCombo;
    QPushButton *toFixedBtn;
    QPushButton *clearPeakBtn;
    QCheckBox *scopeEnableWFBtn;
    QSpacerItem *horizontalSpacer_2;
    QHBoxLayout *horizontalLayout_2;
    QLabel *freqLabel;
    QLabel *label_3;
    QLabel *label_2;
    QComboBox *modeSelectCombo;
    QDial *freqDial;
    QVBoxLayout *verticalLayout_8;
    QSlider *rfGainSlider;
    QLabel *label_10;
    QVBoxLayout *verticalLayout_9;
    QSlider *afGainSlider;
    QLabel *label_11;
    QSpacerItem *horizontalSpacer_3;
    QWidget *bandTab;
    QVBoxLayout *verticalLayout_7;
    QGroupBox *groupBox_3;
    QVBoxLayout *verticalLayout_6;
    QHBoxLayout *horizontalLayout_8;
    QPushButton *band6mbtn;
    QPushButton *band10mbtn;
    QPushButton *band12mbtn;
    QHBoxLayout *horizontalLayout_9;
    QPushButton *band15mbtn;
    QPushButton *band17mbtn;
    QPushButton *band20mbtn;
    QHBoxLayout *horizontalLayout_10;
    QPushButton *band30mbtn;
    QPushButton *band40mbtn;
    QPushButton *band60mbtn;
    QHBoxLayout *horizontalLayout_11;
    QPushButton *band80mbtn;
    QPushButton *band160mbtn;
    QPushButton *bandGenbtn;
    QGroupBox *groupBox_4;
    QHBoxLayout *horizontalLayout_12;
    QRadioButton *bandStkLastUsedBtn;
    QLabel *label;
    QComboBox *bandStkPopdown;
    QRadioButton *bandStkVoiceBtn;
    QRadioButton *bandStkDataBtn;
    QRadioButton *bandStkCWBtn;
    QSpacerItem *horizontalSpacer;
    QWidget *freqTab;
    QVBoxLayout *verticalLayout_4;
    QHBoxLayout *horizontalLayout_3;
    QLabel *label_8;
    QLineEdit *freqMhzLineEdit;
    QPushButton *goFreqBtn;
    QGroupBox *groupBox_2;
    QGridLayout *gridLayout_3;
    QPushButton *f5btn;
    QPushButton *fRclBtn;
    QPushButton *f6btn;
    QPushButton *f3btn;
    QPushButton *fCEbtn;
    QPushButton *f4btn;
    QPushButton *fStoBtn;
    QPushButton *f9btn;
    QPushButton *fBackbtn;
    QPushButton *fEnterBtn;
    QPushButton *f0btn;
    QPushButton *fDotbtn;
    QPushButton *f1btn;
    QPushButton *f2btn;
    QPushButton *f7btn;
    QPushButton *f8btn;
    QWidget *settingsTab;
    QVBoxLayout *verticalLayout_5;
    QHBoxLayout *horizontalLayout_4;
    QCheckBox *drawPeakChk;
    QCheckBox *drawTracerChk;
    QCheckBox *fullScreenChk;
    QCheckBox *useDarkThemeChk;
    QSpacerItem *horizontalSpacer_4;
    QHBoxLayout *horizontalLayout_14;
    QCheckBox *tuningFloorZerosChk;
    QCheckBox *pttEnableChk;
    QSpacerItem *horizontalSpacer_7;
    QHBoxLayout *horizontalLayout_15;
    QPushButton *pttOnBtn;
    QPushButton *pttOffBtn;
    QPushButton *aboutBtn;
    QPushButton *saveSettingsBtn;
    QPushButton *connectBtn;
    QPushButton *debugBtn;
    QSpacerItem *horizontalSpacer_5;
    QHBoxLayout *horizontalLayout_16;
    QCheckBox *tuneEnableChk;
    QPushButton *tuneNowBtn;
    QSpacerItem *tuneSpacer;
    QHBoxLayout *horizontalLayout_5;
    QCheckBox *lanEnableChk;
    QSpacerItem *horizontalSpacer_8;
    QHBoxLayout *horizontalLayout_6;
    QLabel *label_4;
    QLineEdit *ipAddressTxt;
    QLabel *label_5;
    QLineEdit *controlPortTxt;
    QLabel *label_9;
    QLineEdit *serialPortTxt;
    QLabel *label_12;
    QLineEdit *audioPortTxt;
    QHBoxLayout *horizontalLayout_7;
    QLabel *label_15;
    QLineEdit *usernameTxt;
    QLabel *label_14;
    QLineEdit *passwordTxt;
    QHBoxLayout *horizontalLayout_18;
    QLabel *label_16;
    QSlider *audioBufferSizeSlider;
    QLabel *bufferValue;
    QLabel *label_19;
    QComboBox *audioRXCodecCombo;
    QLabel *label_20;
    QComboBox *audioTXCodecCombo;
    QHBoxLayout *horizontalLayout_17;
    QLabel *label_17;
    QComboBox *audioSampleRateCombo;
    QLabel *label_13;
    QComboBox *audioOutputCombo;
    QLabel *label_18;
    QComboBox *audioInputCombo;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer_6;
    QPushButton *exitBtn;
    QSpacerItem *verticalSpacer_2;
    QMenuBar *menuBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *wfmain)
    {
        if (wfmain->objectName().isEmpty())
            wfmain->setObjectName(QString::fromUtf8("wfmain"));
        wfmain->resize(810, 582);
        centralWidget = new QWidget(wfmain);
        centralWidget->setObjectName(QString::fromUtf8("centralWidget"));
        verticalLayout = new QVBoxLayout(centralWidget);
        verticalLayout->setSpacing(6);
        verticalLayout->setContentsMargins(11, 11, 11, 11);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        tabWidget = new QTabWidget(centralWidget);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        mainTab = new QWidget();
        mainTab->setObjectName(QString::fromUtf8("mainTab"));
        verticalLayout_2 = new QVBoxLayout(mainTab);
        verticalLayout_2->setSpacing(6);
        verticalLayout_2->setContentsMargins(11, 11, 11, 11);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        groupBox = new QGroupBox(mainTab);
        groupBox->setObjectName(QString::fromUtf8("groupBox"));
        verticalLayout_3 = new QVBoxLayout(groupBox);
        verticalLayout_3->setSpacing(6);
        verticalLayout_3->setContentsMargins(11, 11, 11, 11);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        splitter = new QSplitter(groupBox);
        splitter->setObjectName(QString::fromUtf8("splitter"));
        splitter->setOrientation(Qt::Vertical);
        plot = new QCustomPlot(splitter);
        plot->setObjectName(QString::fromUtf8("plot"));
        splitter->addWidget(plot);
        waterfall = new QCustomPlot(splitter);
        waterfall->setObjectName(QString::fromUtf8("waterfall"));
        splitter->addWidget(waterfall);

        verticalLayout_3->addWidget(splitter);


        verticalLayout_2->addWidget(groupBox);

        horizontalLayout_13 = new QHBoxLayout();
        horizontalLayout_13->setSpacing(6);
        horizontalLayout_13->setObjectName(QString::fromUtf8("horizontalLayout_13"));
        horizontalLayout_13->setContentsMargins(-1, 0, -1, -1);
        scopeCenterModeChk = new QCheckBox(mainTab);
        scopeCenterModeChk->setObjectName(QString::fromUtf8("scopeCenterModeChk"));

        horizontalLayout_13->addWidget(scopeCenterModeChk);

        label_6 = new QLabel(mainTab);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        horizontalLayout_13->addWidget(label_6);

        scopeBWCombo = new QComboBox(mainTab);
        scopeBWCombo->setObjectName(QString::fromUtf8("scopeBWCombo"));

        horizontalLayout_13->addWidget(scopeBWCombo);

        label_7 = new QLabel(mainTab);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        horizontalLayout_13->addWidget(label_7);

        scopeEdgeCombo = new QComboBox(mainTab);
        scopeEdgeCombo->setObjectName(QString::fromUtf8("scopeEdgeCombo"));

        horizontalLayout_13->addWidget(scopeEdgeCombo);

        toFixedBtn = new QPushButton(mainTab);
        toFixedBtn->setObjectName(QString::fromUtf8("toFixedBtn"));

        horizontalLayout_13->addWidget(toFixedBtn);

        clearPeakBtn = new QPushButton(mainTab);
        clearPeakBtn->setObjectName(QString::fromUtf8("clearPeakBtn"));

        horizontalLayout_13->addWidget(clearPeakBtn);

        scopeEnableWFBtn = new QCheckBox(mainTab);
        scopeEnableWFBtn->setObjectName(QString::fromUtf8("scopeEnableWFBtn"));
        scopeEnableWFBtn->setChecked(true);

        horizontalLayout_13->addWidget(scopeEnableWFBtn);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_13->addItem(horizontalSpacer_2);


        verticalLayout_2->addLayout(horizontalLayout_13);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(6);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        freqLabel = new QLabel(mainTab);
        freqLabel->setObjectName(QString::fromUtf8("freqLabel"));
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(freqLabel->sizePolicy().hasHeightForWidth());
        freqLabel->setSizePolicy(sizePolicy);
        freqLabel->setMinimumSize(QSize(190, 0));
        freqLabel->setMaximumSize(QSize(145, 30));
        QFont font;
        font.setFamily(QString::fromUtf8("DejaVu Sans"));
        font.setPointSize(20);
        freqLabel->setFont(font);

        horizontalLayout_2->addWidget(freqLabel);

        label_3 = new QLabel(mainTab);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        label_3->setMaximumSize(QSize(16777215, 30));
        label_3->setFont(font);

        horizontalLayout_2->addWidget(label_3);

        label_2 = new QLabel(mainTab);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setMaximumSize(QSize(16777215, 30));

        horizontalLayout_2->addWidget(label_2);

        modeSelectCombo = new QComboBox(mainTab);
        modeSelectCombo->setObjectName(QString::fromUtf8("modeSelectCombo"));

        horizontalLayout_2->addWidget(modeSelectCombo);

        freqDial = new QDial(mainTab);
        freqDial->setObjectName(QString::fromUtf8("freqDial"));
        sizePolicy.setHeightForWidth(freqDial->sizePolicy().hasHeightForWidth());
        freqDial->setSizePolicy(sizePolicy);
        freqDial->setMinimumSize(QSize(50, 1));
        freqDial->setMaximumSize(QSize(60, 60));
        freqDial->setWrapping(true);
        freqDial->setNotchesVisible(true);

        horizontalLayout_2->addWidget(freqDial);

        verticalLayout_8 = new QVBoxLayout();
        verticalLayout_8->setSpacing(6);
        verticalLayout_8->setObjectName(QString::fromUtf8("verticalLayout_8"));
        verticalLayout_8->setContentsMargins(-1, -1, 0, -1);
        rfGainSlider = new QSlider(mainTab);
        rfGainSlider->setObjectName(QString::fromUtf8("rfGainSlider"));
        rfGainSlider->setMaximumSize(QSize(16777215, 60));
        rfGainSlider->setMaximum(255);
        rfGainSlider->setOrientation(Qt::Vertical);

        verticalLayout_8->addWidget(rfGainSlider);

        label_10 = new QLabel(mainTab);
        label_10->setObjectName(QString::fromUtf8("label_10"));
        label_10->setMaximumSize(QSize(16777215, 15));

        verticalLayout_8->addWidget(label_10);


        horizontalLayout_2->addLayout(verticalLayout_8);

        verticalLayout_9 = new QVBoxLayout();
        verticalLayout_9->setSpacing(6);
        verticalLayout_9->setObjectName(QString::fromUtf8("verticalLayout_9"));
        verticalLayout_9->setContentsMargins(-1, -1, 0, -1);
        afGainSlider = new QSlider(mainTab);
        afGainSlider->setObjectName(QString::fromUtf8("afGainSlider"));
        afGainSlider->setMaximumSize(QSize(16777215, 60));
        afGainSlider->setMaximum(255);
        afGainSlider->setOrientation(Qt::Vertical);

        verticalLayout_9->addWidget(afGainSlider);

        label_11 = new QLabel(mainTab);
        label_11->setObjectName(QString::fromUtf8("label_11"));
        label_11->setMaximumSize(QSize(16777215, 15));

        verticalLayout_9->addWidget(label_11);


        horizontalLayout_2->addLayout(verticalLayout_9);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer_3);


        verticalLayout_2->addLayout(horizontalLayout_2);

        tabWidget->addTab(mainTab, QString());
        bandTab = new QWidget();
        bandTab->setObjectName(QString::fromUtf8("bandTab"));
        verticalLayout_7 = new QVBoxLayout(bandTab);
        verticalLayout_7->setSpacing(6);
        verticalLayout_7->setContentsMargins(11, 11, 11, 11);
        verticalLayout_7->setObjectName(QString::fromUtf8("verticalLayout_7"));
        groupBox_3 = new QGroupBox(bandTab);
        groupBox_3->setObjectName(QString::fromUtf8("groupBox_3"));
        verticalLayout_6 = new QVBoxLayout(groupBox_3);
        verticalLayout_6->setSpacing(6);
        verticalLayout_6->setContentsMargins(11, 11, 11, 11);
        verticalLayout_6->setObjectName(QString::fromUtf8("verticalLayout_6"));
        horizontalLayout_8 = new QHBoxLayout();
        horizontalLayout_8->setSpacing(6);
        horizontalLayout_8->setObjectName(QString::fromUtf8("horizontalLayout_8"));
        band6mbtn = new QPushButton(groupBox_3);
        band6mbtn->setObjectName(QString::fromUtf8("band6mbtn"));
        QSizePolicy sizePolicy1(QSizePolicy::Minimum, QSizePolicy::MinimumExpanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(band6mbtn->sizePolicy().hasHeightForWidth());
        band6mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_8->addWidget(band6mbtn);

        band10mbtn = new QPushButton(groupBox_3);
        band10mbtn->setObjectName(QString::fromUtf8("band10mbtn"));
        sizePolicy1.setHeightForWidth(band10mbtn->sizePolicy().hasHeightForWidth());
        band10mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_8->addWidget(band10mbtn);

        band12mbtn = new QPushButton(groupBox_3);
        band12mbtn->setObjectName(QString::fromUtf8("band12mbtn"));
        sizePolicy1.setHeightForWidth(band12mbtn->sizePolicy().hasHeightForWidth());
        band12mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_8->addWidget(band12mbtn);


        verticalLayout_6->addLayout(horizontalLayout_8);

        horizontalLayout_9 = new QHBoxLayout();
        horizontalLayout_9->setSpacing(6);
        horizontalLayout_9->setObjectName(QString::fromUtf8("horizontalLayout_9"));
        band15mbtn = new QPushButton(groupBox_3);
        band15mbtn->setObjectName(QString::fromUtf8("band15mbtn"));
        sizePolicy1.setHeightForWidth(band15mbtn->sizePolicy().hasHeightForWidth());
        band15mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_9->addWidget(band15mbtn);

        band17mbtn = new QPushButton(groupBox_3);
        band17mbtn->setObjectName(QString::fromUtf8("band17mbtn"));
        sizePolicy1.setHeightForWidth(band17mbtn->sizePolicy().hasHeightForWidth());
        band17mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_9->addWidget(band17mbtn);

        band20mbtn = new QPushButton(groupBox_3);
        band20mbtn->setObjectName(QString::fromUtf8("band20mbtn"));
        sizePolicy1.setHeightForWidth(band20mbtn->sizePolicy().hasHeightForWidth());
        band20mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_9->addWidget(band20mbtn);


        verticalLayout_6->addLayout(horizontalLayout_9);

        horizontalLayout_10 = new QHBoxLayout();
        horizontalLayout_10->setSpacing(6);
        horizontalLayout_10->setObjectName(QString::fromUtf8("horizontalLayout_10"));
        band30mbtn = new QPushButton(groupBox_3);
        band30mbtn->setObjectName(QString::fromUtf8("band30mbtn"));
        sizePolicy1.setHeightForWidth(band30mbtn->sizePolicy().hasHeightForWidth());
        band30mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_10->addWidget(band30mbtn);

        band40mbtn = new QPushButton(groupBox_3);
        band40mbtn->setObjectName(QString::fromUtf8("band40mbtn"));
        sizePolicy1.setHeightForWidth(band40mbtn->sizePolicy().hasHeightForWidth());
        band40mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_10->addWidget(band40mbtn);

        band60mbtn = new QPushButton(groupBox_3);
        band60mbtn->setObjectName(QString::fromUtf8("band60mbtn"));
        sizePolicy1.setHeightForWidth(band60mbtn->sizePolicy().hasHeightForWidth());
        band60mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_10->addWidget(band60mbtn);


        verticalLayout_6->addLayout(horizontalLayout_10);

        horizontalLayout_11 = new QHBoxLayout();
        horizontalLayout_11->setSpacing(6);
        horizontalLayout_11->setObjectName(QString::fromUtf8("horizontalLayout_11"));
        band80mbtn = new QPushButton(groupBox_3);
        band80mbtn->setObjectName(QString::fromUtf8("band80mbtn"));
        sizePolicy1.setHeightForWidth(band80mbtn->sizePolicy().hasHeightForWidth());
        band80mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_11->addWidget(band80mbtn);

        band160mbtn = new QPushButton(groupBox_3);
        band160mbtn->setObjectName(QString::fromUtf8("band160mbtn"));
        sizePolicy1.setHeightForWidth(band160mbtn->sizePolicy().hasHeightForWidth());
        band160mbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_11->addWidget(band160mbtn);

        bandGenbtn = new QPushButton(groupBox_3);
        bandGenbtn->setObjectName(QString::fromUtf8("bandGenbtn"));
        sizePolicy1.setHeightForWidth(bandGenbtn->sizePolicy().hasHeightForWidth());
        bandGenbtn->setSizePolicy(sizePolicy1);

        horizontalLayout_11->addWidget(bandGenbtn);


        verticalLayout_6->addLayout(horizontalLayout_11);


        verticalLayout_7->addWidget(groupBox_3);

        groupBox_4 = new QGroupBox(bandTab);
        groupBox_4->setObjectName(QString::fromUtf8("groupBox_4"));
        QSizePolicy sizePolicy2(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(groupBox_4->sizePolicy().hasHeightForWidth());
        groupBox_4->setSizePolicy(sizePolicy2);
        horizontalLayout_12 = new QHBoxLayout(groupBox_4);
        horizontalLayout_12->setSpacing(6);
        horizontalLayout_12->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_12->setObjectName(QString::fromUtf8("horizontalLayout_12"));
        bandStkLastUsedBtn = new QRadioButton(groupBox_4);
        bandStkLastUsedBtn->setObjectName(QString::fromUtf8("bandStkLastUsedBtn"));
        bandStkLastUsedBtn->setIconSize(QSize(16, 16));
        bandStkLastUsedBtn->setChecked(true);

        horizontalLayout_12->addWidget(bandStkLastUsedBtn);

        label = new QLabel(groupBox_4);
        label->setObjectName(QString::fromUtf8("label"));

        horizontalLayout_12->addWidget(label);

        bandStkPopdown = new QComboBox(groupBox_4);
        bandStkPopdown->addItem(QString());
        bandStkPopdown->addItem(QString());
        bandStkPopdown->addItem(QString());
        bandStkPopdown->setObjectName(QString::fromUtf8("bandStkPopdown"));

        horizontalLayout_12->addWidget(bandStkPopdown);

        bandStkVoiceBtn = new QRadioButton(groupBox_4);
        bandStkVoiceBtn->setObjectName(QString::fromUtf8("bandStkVoiceBtn"));

        horizontalLayout_12->addWidget(bandStkVoiceBtn);

        bandStkDataBtn = new QRadioButton(groupBox_4);
        bandStkDataBtn->setObjectName(QString::fromUtf8("bandStkDataBtn"));

        horizontalLayout_12->addWidget(bandStkDataBtn);

        bandStkCWBtn = new QRadioButton(groupBox_4);
        bandStkCWBtn->setObjectName(QString::fromUtf8("bandStkCWBtn"));

        horizontalLayout_12->addWidget(bandStkCWBtn);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_12->addItem(horizontalSpacer);


        verticalLayout_7->addWidget(groupBox_4);

        tabWidget->addTab(bandTab, QString());
        freqTab = new QWidget();
        freqTab->setObjectName(QString::fromUtf8("freqTab"));
        verticalLayout_4 = new QVBoxLayout(freqTab);
        verticalLayout_4->setSpacing(6);
        verticalLayout_4->setContentsMargins(11, 11, 11, 11);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setSpacing(6);
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        label_8 = new QLabel(freqTab);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        horizontalLayout_3->addWidget(label_8);

        freqMhzLineEdit = new QLineEdit(freqTab);
        freqMhzLineEdit->setObjectName(QString::fromUtf8("freqMhzLineEdit"));
        QFont font1;
        font1.setFamily(QString::fromUtf8("DejaVu Sans Mono"));
        font1.setPointSize(14);
        font1.setBold(true);
        font1.setWeight(75);
        freqMhzLineEdit->setFont(font1);

        horizontalLayout_3->addWidget(freqMhzLineEdit);

        goFreqBtn = new QPushButton(freqTab);
        goFreqBtn->setObjectName(QString::fromUtf8("goFreqBtn"));

        horizontalLayout_3->addWidget(goFreqBtn);


        verticalLayout_4->addLayout(horizontalLayout_3);

        groupBox_2 = new QGroupBox(freqTab);
        groupBox_2->setObjectName(QString::fromUtf8("groupBox_2"));
        gridLayout_3 = new QGridLayout(groupBox_2);
        gridLayout_3->setSpacing(6);
        gridLayout_3->setContentsMargins(11, 11, 11, 11);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        f5btn = new QPushButton(groupBox_2);
        f5btn->setObjectName(QString::fromUtf8("f5btn"));
        sizePolicy1.setHeightForWidth(f5btn->sizePolicy().hasHeightForWidth());
        f5btn->setSizePolicy(sizePolicy1);
        f5btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f5btn, 1, 1, 1, 1);

        fRclBtn = new QPushButton(groupBox_2);
        fRclBtn->setObjectName(QString::fromUtf8("fRclBtn"));
        sizePolicy1.setHeightForWidth(fRclBtn->sizePolicy().hasHeightForWidth());
        fRclBtn->setSizePolicy(sizePolicy1);
        fRclBtn->setMinimumSize(QSize(0, 30));

        gridLayout_3->addWidget(fRclBtn, 1, 3, 1, 1);

        f6btn = new QPushButton(groupBox_2);
        f6btn->setObjectName(QString::fromUtf8("f6btn"));
        sizePolicy1.setHeightForWidth(f6btn->sizePolicy().hasHeightForWidth());
        f6btn->setSizePolicy(sizePolicy1);
        f6btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f6btn, 1, 2, 1, 1);

        f3btn = new QPushButton(groupBox_2);
        f3btn->setObjectName(QString::fromUtf8("f3btn"));
        sizePolicy1.setHeightForWidth(f3btn->sizePolicy().hasHeightForWidth());
        f3btn->setSizePolicy(sizePolicy1);
        f3btn->setMinimumSize(QSize(30, 30));
        f3btn->setFlat(false);

        gridLayout_3->addWidget(f3btn, 0, 2, 1, 1);

        fCEbtn = new QPushButton(groupBox_2);
        fCEbtn->setObjectName(QString::fromUtf8("fCEbtn"));
        sizePolicy1.setHeightForWidth(fCEbtn->sizePolicy().hasHeightForWidth());
        fCEbtn->setSizePolicy(sizePolicy1);
        fCEbtn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(fCEbtn, 3, 2, 1, 1);

        f4btn = new QPushButton(groupBox_2);
        f4btn->setObjectName(QString::fromUtf8("f4btn"));
        sizePolicy1.setHeightForWidth(f4btn->sizePolicy().hasHeightForWidth());
        f4btn->setSizePolicy(sizePolicy1);
        f4btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f4btn, 1, 0, 1, 1);

        fStoBtn = new QPushButton(groupBox_2);
        fStoBtn->setObjectName(QString::fromUtf8("fStoBtn"));
        sizePolicy1.setHeightForWidth(fStoBtn->sizePolicy().hasHeightForWidth());
        fStoBtn->setSizePolicy(sizePolicy1);
        fStoBtn->setMinimumSize(QSize(0, 30));

        gridLayout_3->addWidget(fStoBtn, 0, 3, 1, 1);

        f9btn = new QPushButton(groupBox_2);
        f9btn->setObjectName(QString::fromUtf8("f9btn"));
        sizePolicy1.setHeightForWidth(f9btn->sizePolicy().hasHeightForWidth());
        f9btn->setSizePolicy(sizePolicy1);
        f9btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f9btn, 2, 2, 1, 1);

        fBackbtn = new QPushButton(groupBox_2);
        fBackbtn->setObjectName(QString::fromUtf8("fBackbtn"));
        sizePolicy1.setHeightForWidth(fBackbtn->sizePolicy().hasHeightForWidth());
        fBackbtn->setSizePolicy(sizePolicy1);
        fBackbtn->setMinimumSize(QSize(0, 30));

        gridLayout_3->addWidget(fBackbtn, 3, 3, 1, 1);

        fEnterBtn = new QPushButton(groupBox_2);
        fEnterBtn->setObjectName(QString::fromUtf8("fEnterBtn"));
        sizePolicy1.setHeightForWidth(fEnterBtn->sizePolicy().hasHeightForWidth());
        fEnterBtn->setSizePolicy(sizePolicy1);
        fEnterBtn->setMinimumSize(QSize(0, 30));

        gridLayout_3->addWidget(fEnterBtn, 2, 3, 1, 1);

        f0btn = new QPushButton(groupBox_2);
        f0btn->setObjectName(QString::fromUtf8("f0btn"));
        sizePolicy1.setHeightForWidth(f0btn->sizePolicy().hasHeightForWidth());
        f0btn->setSizePolicy(sizePolicy1);
        f0btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f0btn, 3, 1, 1, 1);

        fDotbtn = new QPushButton(groupBox_2);
        fDotbtn->setObjectName(QString::fromUtf8("fDotbtn"));
        sizePolicy1.setHeightForWidth(fDotbtn->sizePolicy().hasHeightForWidth());
        fDotbtn->setSizePolicy(sizePolicy1);
        fDotbtn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(fDotbtn, 3, 0, 1, 1);

        f1btn = new QPushButton(groupBox_2);
        f1btn->setObjectName(QString::fromUtf8("f1btn"));
        sizePolicy1.setHeightForWidth(f1btn->sizePolicy().hasHeightForWidth());
        f1btn->setSizePolicy(sizePolicy1);
        f1btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f1btn, 0, 0, 1, 1);

        f2btn = new QPushButton(groupBox_2);
        f2btn->setObjectName(QString::fromUtf8("f2btn"));
        sizePolicy1.setHeightForWidth(f2btn->sizePolicy().hasHeightForWidth());
        f2btn->setSizePolicy(sizePolicy1);
        f2btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f2btn, 0, 1, 1, 1);

        f7btn = new QPushButton(groupBox_2);
        f7btn->setObjectName(QString::fromUtf8("f7btn"));
        sizePolicy1.setHeightForWidth(f7btn->sizePolicy().hasHeightForWidth());
        f7btn->setSizePolicy(sizePolicy1);
        f7btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f7btn, 2, 0, 1, 1);

        f8btn = new QPushButton(groupBox_2);
        f8btn->setObjectName(QString::fromUtf8("f8btn"));
        sizePolicy1.setHeightForWidth(f8btn->sizePolicy().hasHeightForWidth());
        f8btn->setSizePolicy(sizePolicy1);
        f8btn->setMinimumSize(QSize(30, 30));

        gridLayout_3->addWidget(f8btn, 2, 1, 1, 1);


        verticalLayout_4->addWidget(groupBox_2);

        tabWidget->addTab(freqTab, QString());
        settingsTab = new QWidget();
        settingsTab->setObjectName(QString::fromUtf8("settingsTab"));
        verticalLayout_5 = new QVBoxLayout(settingsTab);
        verticalLayout_5->setSpacing(6);
        verticalLayout_5->setContentsMargins(11, 11, 11, 11);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setSpacing(6);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        drawPeakChk = new QCheckBox(settingsTab);
        drawPeakChk->setObjectName(QString::fromUtf8("drawPeakChk"));

        horizontalLayout_4->addWidget(drawPeakChk);

        drawTracerChk = new QCheckBox(settingsTab);
        drawTracerChk->setObjectName(QString::fromUtf8("drawTracerChk"));
        drawTracerChk->setChecked(true);

        horizontalLayout_4->addWidget(drawTracerChk);

        fullScreenChk = new QCheckBox(settingsTab);
        fullScreenChk->setObjectName(QString::fromUtf8("fullScreenChk"));

        horizontalLayout_4->addWidget(fullScreenChk);

        useDarkThemeChk = new QCheckBox(settingsTab);
        useDarkThemeChk->setObjectName(QString::fromUtf8("useDarkThemeChk"));

        horizontalLayout_4->addWidget(useDarkThemeChk);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_4);


        verticalLayout_5->addLayout(horizontalLayout_4);

        horizontalLayout_14 = new QHBoxLayout();
        horizontalLayout_14->setSpacing(6);
        horizontalLayout_14->setObjectName(QString::fromUtf8("horizontalLayout_14"));
        horizontalLayout_14->setContentsMargins(-1, 0, -1, -1);
        tuningFloorZerosChk = new QCheckBox(settingsTab);
        tuningFloorZerosChk->setObjectName(QString::fromUtf8("tuningFloorZerosChk"));
        tuningFloorZerosChk->setChecked(true);

        horizontalLayout_14->addWidget(tuningFloorZerosChk);

        pttEnableChk = new QCheckBox(settingsTab);
        pttEnableChk->setObjectName(QString::fromUtf8("pttEnableChk"));

        horizontalLayout_14->addWidget(pttEnableChk);

        horizontalSpacer_7 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_14->addItem(horizontalSpacer_7);


        verticalLayout_5->addLayout(horizontalLayout_14);

        horizontalLayout_15 = new QHBoxLayout();
        horizontalLayout_15->setSpacing(6);
        horizontalLayout_15->setObjectName(QString::fromUtf8("horizontalLayout_15"));
        horizontalLayout_15->setContentsMargins(-1, 0, -1, -1);
        pttOnBtn = new QPushButton(settingsTab);
        pttOnBtn->setObjectName(QString::fromUtf8("pttOnBtn"));

        horizontalLayout_15->addWidget(pttOnBtn);

        pttOffBtn = new QPushButton(settingsTab);
        pttOffBtn->setObjectName(QString::fromUtf8("pttOffBtn"));

        horizontalLayout_15->addWidget(pttOffBtn);

        aboutBtn = new QPushButton(settingsTab);
        aboutBtn->setObjectName(QString::fromUtf8("aboutBtn"));

        horizontalLayout_15->addWidget(aboutBtn);

        saveSettingsBtn = new QPushButton(settingsTab);
        saveSettingsBtn->setObjectName(QString::fromUtf8("saveSettingsBtn"));

        horizontalLayout_15->addWidget(saveSettingsBtn);

        connectBtn = new QPushButton(settingsTab);
        connectBtn->setObjectName(QString::fromUtf8("connectBtn"));

        horizontalLayout_15->addWidget(connectBtn);

        debugBtn = new QPushButton(settingsTab);
        debugBtn->setObjectName(QString::fromUtf8("debugBtn"));

        horizontalLayout_15->addWidget(debugBtn);

        horizontalSpacer_5 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_15->addItem(horizontalSpacer_5);


        verticalLayout_5->addLayout(horizontalLayout_15);

        horizontalLayout_16 = new QHBoxLayout();
        horizontalLayout_16->setSpacing(6);
        horizontalLayout_16->setObjectName(QString::fromUtf8("horizontalLayout_16"));
        horizontalLayout_16->setContentsMargins(-1, 0, -1, -1);
        tuneEnableChk = new QCheckBox(settingsTab);
        tuneEnableChk->setObjectName(QString::fromUtf8("tuneEnableChk"));

        horizontalLayout_16->addWidget(tuneEnableChk);

        tuneNowBtn = new QPushButton(settingsTab);
        tuneNowBtn->setObjectName(QString::fromUtf8("tuneNowBtn"));

        horizontalLayout_16->addWidget(tuneNowBtn);

        tuneSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_16->addItem(tuneSpacer);


        verticalLayout_5->addLayout(horizontalLayout_16);

        horizontalLayout_5 = new QHBoxLayout();
        horizontalLayout_5->setSpacing(6);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        lanEnableChk = new QCheckBox(settingsTab);
        lanEnableChk->setObjectName(QString::fromUtf8("lanEnableChk"));

        horizontalLayout_5->addWidget(lanEnableChk);

        horizontalSpacer_8 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_8);


        verticalLayout_5->addLayout(horizontalLayout_5);

        horizontalLayout_6 = new QHBoxLayout();
        horizontalLayout_6->setSpacing(6);
        horizontalLayout_6->setObjectName(QString::fromUtf8("horizontalLayout_6"));
        label_4 = new QLabel(settingsTab);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        horizontalLayout_6->addWidget(label_4);

        ipAddressTxt = new QLineEdit(settingsTab);
        ipAddressTxt->setObjectName(QString::fromUtf8("ipAddressTxt"));

        horizontalLayout_6->addWidget(ipAddressTxt);

        label_5 = new QLabel(settingsTab);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        horizontalLayout_6->addWidget(label_5);

        controlPortTxt = new QLineEdit(settingsTab);
        controlPortTxt->setObjectName(QString::fromUtf8("controlPortTxt"));

        horizontalLayout_6->addWidget(controlPortTxt);

        label_9 = new QLabel(settingsTab);
        label_9->setObjectName(QString::fromUtf8("label_9"));

        horizontalLayout_6->addWidget(label_9);

        serialPortTxt = new QLineEdit(settingsTab);
        serialPortTxt->setObjectName(QString::fromUtf8("serialPortTxt"));

        horizontalLayout_6->addWidget(serialPortTxt);

        label_12 = new QLabel(settingsTab);
        label_12->setObjectName(QString::fromUtf8("label_12"));

        horizontalLayout_6->addWidget(label_12);

        audioPortTxt = new QLineEdit(settingsTab);
        audioPortTxt->setObjectName(QString::fromUtf8("audioPortTxt"));

        horizontalLayout_6->addWidget(audioPortTxt);


        verticalLayout_5->addLayout(horizontalLayout_6);

        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setSpacing(6);
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        label_15 = new QLabel(settingsTab);
        label_15->setObjectName(QString::fromUtf8("label_15"));

        horizontalLayout_7->addWidget(label_15);

        usernameTxt = new QLineEdit(settingsTab);
        usernameTxt->setObjectName(QString::fromUtf8("usernameTxt"));

        horizontalLayout_7->addWidget(usernameTxt);

        label_14 = new QLabel(settingsTab);
        label_14->setObjectName(QString::fromUtf8("label_14"));

        horizontalLayout_7->addWidget(label_14);

        passwordTxt = new QLineEdit(settingsTab);
        passwordTxt->setObjectName(QString::fromUtf8("passwordTxt"));
        passwordTxt->setInputMethodHints(Qt::ImhNoAutoUppercase|Qt::ImhNoPredictiveText|Qt::ImhSensitiveData);
        passwordTxt->setEchoMode(QLineEdit::PasswordEchoOnEdit);

        horizontalLayout_7->addWidget(passwordTxt);


        verticalLayout_5->addLayout(horizontalLayout_7);

        horizontalLayout_18 = new QHBoxLayout();
        horizontalLayout_18->setSpacing(6);
        horizontalLayout_18->setObjectName(QString::fromUtf8("horizontalLayout_18"));
        label_16 = new QLabel(settingsTab);
        label_16->setObjectName(QString::fromUtf8("label_16"));

        horizontalLayout_18->addWidget(label_16);

        audioBufferSizeSlider = new QSlider(settingsTab);
        audioBufferSizeSlider->setObjectName(QString::fromUtf8("audioBufferSizeSlider"));
        audioBufferSizeSlider->setMinimumSize(QSize(0, 0));
        audioBufferSizeSlider->setMaximum(65536);
        audioBufferSizeSlider->setOrientation(Qt::Horizontal);

        horizontalLayout_18->addWidget(audioBufferSizeSlider);

        bufferValue = new QLabel(settingsTab);
        bufferValue->setObjectName(QString::fromUtf8("bufferValue"));

        horizontalLayout_18->addWidget(bufferValue);

        label_19 = new QLabel(settingsTab);
        label_19->setObjectName(QString::fromUtf8("label_19"));

        horizontalLayout_18->addWidget(label_19);

        audioRXCodecCombo = new QComboBox(settingsTab);
        audioRXCodecCombo->setObjectName(QString::fromUtf8("audioRXCodecCombo"));

        horizontalLayout_18->addWidget(audioRXCodecCombo);

        label_20 = new QLabel(settingsTab);
        label_20->setObjectName(QString::fromUtf8("label_20"));

        horizontalLayout_18->addWidget(label_20);

        audioTXCodecCombo = new QComboBox(settingsTab);
        audioTXCodecCombo->setObjectName(QString::fromUtf8("audioTXCodecCombo"));

        horizontalLayout_18->addWidget(audioTXCodecCombo);


        verticalLayout_5->addLayout(horizontalLayout_18);

        horizontalLayout_17 = new QHBoxLayout();
        horizontalLayout_17->setSpacing(6);
        horizontalLayout_17->setObjectName(QString::fromUtf8("horizontalLayout_17"));
        label_17 = new QLabel(settingsTab);
        label_17->setObjectName(QString::fromUtf8("label_17"));

        horizontalLayout_17->addWidget(label_17);

        audioSampleRateCombo = new QComboBox(settingsTab);
        audioSampleRateCombo->addItem(QString());
        audioSampleRateCombo->addItem(QString());
        audioSampleRateCombo->addItem(QString());
        audioSampleRateCombo->addItem(QString());
        audioSampleRateCombo->setObjectName(QString::fromUtf8("audioSampleRateCombo"));

        horizontalLayout_17->addWidget(audioSampleRateCombo);

        label_13 = new QLabel(settingsTab);
        label_13->setObjectName(QString::fromUtf8("label_13"));

        horizontalLayout_17->addWidget(label_13);

        audioOutputCombo = new QComboBox(settingsTab);
        audioOutputCombo->setObjectName(QString::fromUtf8("audioOutputCombo"));

        horizontalLayout_17->addWidget(audioOutputCombo);

        label_18 = new QLabel(settingsTab);
        label_18->setObjectName(QString::fromUtf8("label_18"));

        horizontalLayout_17->addWidget(label_18);

        audioInputCombo = new QComboBox(settingsTab);
        audioInputCombo->setObjectName(QString::fromUtf8("audioInputCombo"));

        horizontalLayout_17->addWidget(audioInputCombo);


        verticalLayout_5->addLayout(horizontalLayout_17);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        horizontalLayout->setContentsMargins(-1, 0, -1, -1);
        horizontalSpacer_6 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer_6);

        exitBtn = new QPushButton(settingsTab);
        exitBtn->setObjectName(QString::fromUtf8("exitBtn"));
        QFont font2;
        font2.setBold(true);
        font2.setWeight(75);
        exitBtn->setFont(font2);

        horizontalLayout->addWidget(exitBtn);


        verticalLayout_5->addLayout(horizontalLayout);

        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_5->addItem(verticalSpacer_2);

        tabWidget->addTab(settingsTab, QString());

        verticalLayout->addWidget(tabWidget);

        wfmain->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(wfmain);
        menuBar->setObjectName(QString::fromUtf8("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 810, 22));
        wfmain->setMenuBar(menuBar);
        statusBar = new QStatusBar(wfmain);
        statusBar->setObjectName(QString::fromUtf8("statusBar"));
        wfmain->setStatusBar(statusBar);

        retranslateUi(wfmain);

        tabWidget->setCurrentIndex(0);
        goFreqBtn->setDefault(true);


        QMetaObject::connectSlotsByName(wfmain);
    } // setupUi

    void retranslateUi(QMainWindow *wfmain)
    {
        wfmain->setWindowTitle(QCoreApplication::translate("wfmain", "wfmain", nullptr));
        groupBox->setTitle(QCoreApplication::translate("wfmain", "Spectrum", nullptr));
        scopeCenterModeChk->setText(QCoreApplication::translate("wfmain", "Center Mode", nullptr));
        label_6->setText(QCoreApplication::translate("wfmain", "Span:", nullptr));
        label_7->setText(QCoreApplication::translate("wfmain", "Edge", nullptr));
#if QT_CONFIG(tooltip)
        toFixedBtn->setToolTip(QCoreApplication::translate("wfmain", "<html><head/><body><p>Press button to convert center mode spectrum to fixed mode, preserving the range. This allows you to tune without the spectrum moving, in the same currently-visible range that you see now. </p><p><br/></p><p>The currently-selected edge slot will be overriden.</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        toFixedBtn->setText(QCoreApplication::translate("wfmain", "ToFixed", nullptr));
        clearPeakBtn->setText(QCoreApplication::translate("wfmain", "Clear Peaks", nullptr));
        scopeEnableWFBtn->setText(QCoreApplication::translate("wfmain", "Enable WF", nullptr));
        freqLabel->setText(QCoreApplication::translate("wfmain", "0000.000000", nullptr));
        label_3->setText(QCoreApplication::translate("wfmain", "MHz", nullptr));
        label_2->setText(QCoreApplication::translate("wfmain", "Mode:", nullptr));
#if QT_CONFIG(tooltip)
        rfGainSlider->setToolTip(QCoreApplication::translate("wfmain", "RX RF Gain", nullptr));
#endif // QT_CONFIG(tooltip)
        label_10->setText(QCoreApplication::translate("wfmain", "RF", nullptr));
#if QT_CONFIG(tooltip)
        afGainSlider->setToolTip(QCoreApplication::translate("wfmain", "RX AF Gain", nullptr));
#endif // QT_CONFIG(tooltip)
        label_11->setText(QCoreApplication::translate("wfmain", "AF", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(mainTab), QCoreApplication::translate("wfmain", "View", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("wfmain", "Band", nullptr));
        band6mbtn->setText(QCoreApplication::translate("wfmain", "6M", nullptr));
#if QT_CONFIG(shortcut)
        band6mbtn->setShortcut(QCoreApplication::translate("wfmain", "6", nullptr));
#endif // QT_CONFIG(shortcut)
        band10mbtn->setText(QCoreApplication::translate("wfmain", "10M", nullptr));
#if QT_CONFIG(shortcut)
        band10mbtn->setShortcut(QCoreApplication::translate("wfmain", "1", nullptr));
#endif // QT_CONFIG(shortcut)
        band12mbtn->setText(QCoreApplication::translate("wfmain", "12M", nullptr));
#if QT_CONFIG(shortcut)
        band12mbtn->setShortcut(QCoreApplication::translate("wfmain", "T", nullptr));
#endif // QT_CONFIG(shortcut)
        band15mbtn->setText(QCoreApplication::translate("wfmain", "15M", nullptr));
#if QT_CONFIG(shortcut)
        band15mbtn->setShortcut(QCoreApplication::translate("wfmain", "5", nullptr));
#endif // QT_CONFIG(shortcut)
        band17mbtn->setText(QCoreApplication::translate("wfmain", "17M", nullptr));
#if QT_CONFIG(shortcut)
        band17mbtn->setShortcut(QCoreApplication::translate("wfmain", "7", nullptr));
#endif // QT_CONFIG(shortcut)
        band20mbtn->setText(QCoreApplication::translate("wfmain", "20M", nullptr));
#if QT_CONFIG(shortcut)
        band20mbtn->setShortcut(QCoreApplication::translate("wfmain", "2", nullptr));
#endif // QT_CONFIG(shortcut)
        band30mbtn->setText(QCoreApplication::translate("wfmain", "30M", nullptr));
#if QT_CONFIG(shortcut)
        band30mbtn->setShortcut(QCoreApplication::translate("wfmain", "3", nullptr));
#endif // QT_CONFIG(shortcut)
        band40mbtn->setText(QCoreApplication::translate("wfmain", "40M", nullptr));
#if QT_CONFIG(shortcut)
        band40mbtn->setShortcut(QCoreApplication::translate("wfmain", "4", nullptr));
#endif // QT_CONFIG(shortcut)
        band60mbtn->setText(QCoreApplication::translate("wfmain", "60M", nullptr));
#if QT_CONFIG(shortcut)
        band60mbtn->setShortcut(QCoreApplication::translate("wfmain", "S", nullptr));
#endif // QT_CONFIG(shortcut)
        band80mbtn->setText(QCoreApplication::translate("wfmain", "80M", nullptr));
#if QT_CONFIG(shortcut)
        band80mbtn->setShortcut(QCoreApplication::translate("wfmain", "8", nullptr));
#endif // QT_CONFIG(shortcut)
        band160mbtn->setText(QCoreApplication::translate("wfmain", "160M", nullptr));
#if QT_CONFIG(shortcut)
        band160mbtn->setShortcut(QCoreApplication::translate("wfmain", "L", nullptr));
#endif // QT_CONFIG(shortcut)
        bandGenbtn->setText(QCoreApplication::translate("wfmain", "Gen", nullptr));
#if QT_CONFIG(shortcut)
        bandGenbtn->setShortcut(QCoreApplication::translate("wfmain", "G", nullptr));
#endif // QT_CONFIG(shortcut)
        groupBox_4->setTitle(QCoreApplication::translate("wfmain", "Segment", nullptr));
        bandStkLastUsedBtn->setText(QCoreApplication::translate("wfmain", "&Last Used", nullptr));
        label->setText(QCoreApplication::translate("wfmain", "Band Stack Selection:", nullptr));
        bandStkPopdown->setItemText(0, QCoreApplication::translate("wfmain", "1 - Latest Used", nullptr));
        bandStkPopdown->setItemText(1, QCoreApplication::translate("wfmain", "2 - Older", nullptr));
        bandStkPopdown->setItemText(2, QCoreApplication::translate("wfmain", "3 - Oldest Used", nullptr));

        bandStkVoiceBtn->setText(QCoreApplication::translate("wfmain", "Voice", nullptr));
        bandStkDataBtn->setText(QCoreApplication::translate("wfmain", "Data", nullptr));
        bandStkCWBtn->setText(QCoreApplication::translate("wfmain", "&CW", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(bandTab), QCoreApplication::translate("wfmain", "Band", nullptr));
        label_8->setText(QCoreApplication::translate("wfmain", "Frequency:", nullptr));
        goFreqBtn->setText(QCoreApplication::translate("wfmain", "Go", nullptr));
#if QT_CONFIG(shortcut)
        goFreqBtn->setShortcut(QCoreApplication::translate("wfmain", "Return", nullptr));
#endif // QT_CONFIG(shortcut)
        groupBox_2->setTitle(QCoreApplication::translate("wfmain", "Entry", nullptr));
        f5btn->setText(QCoreApplication::translate("wfmain", "5", nullptr));
#if QT_CONFIG(shortcut)
        f5btn->setShortcut(QCoreApplication::translate("wfmain", "5", nullptr));
#endif // QT_CONFIG(shortcut)
#if QT_CONFIG(tooltip)
        fRclBtn->setToolTip(QCoreApplication::translate("wfmain", "<html><head/><body><p>To recall a preset memory:</p><p>1. Type in the preset number (0 through 99)</p><p>2. Press RCL (or use hotkey &quot;R&quot;)</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        fRclBtn->setText(QCoreApplication::translate("wfmain", "&RCL", nullptr));
#if QT_CONFIG(shortcut)
        fRclBtn->setShortcut(QCoreApplication::translate("wfmain", "R", nullptr));
#endif // QT_CONFIG(shortcut)
        f6btn->setText(QCoreApplication::translate("wfmain", "6", nullptr));
#if QT_CONFIG(shortcut)
        f6btn->setShortcut(QCoreApplication::translate("wfmain", "6", nullptr));
#endif // QT_CONFIG(shortcut)
        f3btn->setText(QCoreApplication::translate("wfmain", "3", nullptr));
#if QT_CONFIG(shortcut)
        f3btn->setShortcut(QCoreApplication::translate("wfmain", "3", nullptr));
#endif // QT_CONFIG(shortcut)
        fCEbtn->setText(QCoreApplication::translate("wfmain", "&CE", nullptr));
#if QT_CONFIG(shortcut)
        fCEbtn->setShortcut(QCoreApplication::translate("wfmain", "C", nullptr));
#endif // QT_CONFIG(shortcut)
        f4btn->setText(QCoreApplication::translate("wfmain", "4", nullptr));
#if QT_CONFIG(shortcut)
        f4btn->setShortcut(QCoreApplication::translate("wfmain", "4", nullptr));
#endif // QT_CONFIG(shortcut)
#if QT_CONFIG(tooltip)
        fStoBtn->setToolTip(QCoreApplication::translate("wfmain", "<html><head/><body><p>To store a preset:</p><p>1. Set the desired frequency and mode per normal methods</p><p>2. Type the index to to store to (0 through 99)</p><p>3. Press STO (or use hotkey &quot;S&quot;)</p></body></html>", nullptr));
#endif // QT_CONFIG(tooltip)
        fStoBtn->setText(QCoreApplication::translate("wfmain", "&STO", nullptr));
#if QT_CONFIG(shortcut)
        fStoBtn->setShortcut(QCoreApplication::translate("wfmain", "S", nullptr));
#endif // QT_CONFIG(shortcut)
        f9btn->setText(QCoreApplication::translate("wfmain", "9", nullptr));
#if QT_CONFIG(shortcut)
        f9btn->setShortcut(QCoreApplication::translate("wfmain", "9", nullptr));
#endif // QT_CONFIG(shortcut)
        fBackbtn->setText(QCoreApplication::translate("wfmain", "Back", nullptr));
#if QT_CONFIG(shortcut)
        fBackbtn->setShortcut(QCoreApplication::translate("wfmain", "Backspace", nullptr));
#endif // QT_CONFIG(shortcut)
        fEnterBtn->setText(QCoreApplication::translate("wfmain", "Enter", nullptr));
#if QT_CONFIG(shortcut)
        fEnterBtn->setShortcut(QCoreApplication::translate("wfmain", "Enter", nullptr));
#endif // QT_CONFIG(shortcut)
        f0btn->setText(QCoreApplication::translate("wfmain", "0", nullptr));
#if QT_CONFIG(shortcut)
        f0btn->setShortcut(QCoreApplication::translate("wfmain", "0", nullptr));
#endif // QT_CONFIG(shortcut)
        fDotbtn->setText(QCoreApplication::translate("wfmain", ".", nullptr));
#if QT_CONFIG(shortcut)
        fDotbtn->setShortcut(QCoreApplication::translate("wfmain", ".", nullptr));
#endif // QT_CONFIG(shortcut)
        f1btn->setText(QCoreApplication::translate("wfmain", "1", nullptr));
#if QT_CONFIG(shortcut)
        f1btn->setShortcut(QCoreApplication::translate("wfmain", "1", nullptr));
#endif // QT_CONFIG(shortcut)
        f2btn->setText(QCoreApplication::translate("wfmain", "2", nullptr));
#if QT_CONFIG(shortcut)
        f2btn->setShortcut(QCoreApplication::translate("wfmain", "2", nullptr));
#endif // QT_CONFIG(shortcut)
        f7btn->setText(QCoreApplication::translate("wfmain", "7", nullptr));
#if QT_CONFIG(shortcut)
        f7btn->setShortcut(QCoreApplication::translate("wfmain", "7", nullptr));
#endif // QT_CONFIG(shortcut)
        f8btn->setText(QCoreApplication::translate("wfmain", "8", nullptr));
#if QT_CONFIG(shortcut)
        f8btn->setShortcut(QCoreApplication::translate("wfmain", "8", nullptr));
#endif // QT_CONFIG(shortcut)
        tabWidget->setTabText(tabWidget->indexOf(freqTab), QCoreApplication::translate("wfmain", "Frequency", nullptr));
        drawPeakChk->setText(QCoreApplication::translate("wfmain", "Draw Peaks", nullptr));
        drawTracerChk->setText(QCoreApplication::translate("wfmain", "Tracer", nullptr));
        fullScreenChk->setText(QCoreApplication::translate("wfmain", "Show full screen", nullptr));
        useDarkThemeChk->setText(QCoreApplication::translate("wfmain", "Use Dark Theme", nullptr));
        tuningFloorZerosChk->setText(QCoreApplication::translate("wfmain", "When tuning, set lower digits to zero", nullptr));
        pttEnableChk->setText(QCoreApplication::translate("wfmain", "Enable PTT Controls", nullptr));
        pttOnBtn->setText(QCoreApplication::translate("wfmain", "PTT On", nullptr));
#if QT_CONFIG(shortcut)
        pttOnBtn->setShortcut(QCoreApplication::translate("wfmain", "Ctrl+S", nullptr));
#endif // QT_CONFIG(shortcut)
        pttOffBtn->setText(QCoreApplication::translate("wfmain", "PTT Off", nullptr));
        aboutBtn->setText(QCoreApplication::translate("wfmain", "About", nullptr));
        saveSettingsBtn->setText(QCoreApplication::translate("wfmain", "Save Settings", nullptr));
        connectBtn->setText(QCoreApplication::translate("wfmain", "Connect", nullptr));
        debugBtn->setText(QCoreApplication::translate("wfmain", "Debug", nullptr));
        tuneEnableChk->setText(QCoreApplication::translate("wfmain", "Enable ATU", nullptr));
        tuneNowBtn->setText(QCoreApplication::translate("wfmain", "Tune Now", nullptr));
        lanEnableChk->setText(QCoreApplication::translate("wfmain", "Enable LAN", nullptr));
        label_4->setText(QCoreApplication::translate("wfmain", "Radio IP Address", nullptr));
        label_5->setText(QCoreApplication::translate("wfmain", "Radio Control Port", nullptr));
        controlPortTxt->setPlaceholderText(QCoreApplication::translate("wfmain", "50001", nullptr));
        label_9->setText(QCoreApplication::translate("wfmain", "Radio Serial Port", nullptr));
        serialPortTxt->setPlaceholderText(QCoreApplication::translate("wfmain", "50002", nullptr));
        label_12->setText(QCoreApplication::translate("wfmain", "Radio Audio Port", nullptr));
        audioPortTxt->setPlaceholderText(QCoreApplication::translate("wfmain", "50003", nullptr));
        label_15->setText(QCoreApplication::translate("wfmain", "Username", nullptr));
        label_14->setText(QCoreApplication::translate("wfmain", "Password", nullptr));
        label_16->setText(QCoreApplication::translate("wfmain", "RX Audio Buffer Size", nullptr));
        bufferValue->setText(QCoreApplication::translate("wfmain", "0", nullptr));
        label_19->setText(QCoreApplication::translate("wfmain", "RX Codec", nullptr));
        label_20->setText(QCoreApplication::translate("wfmain", "TX Codec", nullptr));
        label_17->setText(QCoreApplication::translate("wfmain", "Sample Rate", nullptr));
        audioSampleRateCombo->setItemText(0, QCoreApplication::translate("wfmain", "48000", nullptr));
        audioSampleRateCombo->setItemText(1, QCoreApplication::translate("wfmain", "24000", nullptr));
        audioSampleRateCombo->setItemText(2, QCoreApplication::translate("wfmain", "16000", nullptr));
        audioSampleRateCombo->setItemText(3, QCoreApplication::translate("wfmain", "8000", nullptr));

        label_13->setText(QCoreApplication::translate("wfmain", "Audio Output ", nullptr));
        label_18->setText(QCoreApplication::translate("wfmain", "Audio Input", nullptr));
        exitBtn->setText(QCoreApplication::translate("wfmain", " Exit Program", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(settingsTab), QCoreApplication::translate("wfmain", "Settings", nullptr));
    } // retranslateUi

};

namespace Ui {
    class wfmain: public Ui_wfmain {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_WFMAIN_H
