#ifndef BANDBUTTONS_H
#define BANDBUTTONS_H

#include <QWidget>
#include <QPushButton>

#include "cachingqueue.h"
#include "logcategories.h"
#include "wfviewtypes.h"
#include "rigidentities.h"

namespace Ui {
class bandbuttons;
}

class bandbuttons : public QWidget
{
    Q_OBJECT

public:
    explicit bandbuttons(QWidget *parent = nullptr);
    ~bandbuttons();

    int getBSRNumber(); // returns the BSR combo box position
    void setBand(availableBands band) { bandStackBtnClick(band);};
    availableBands currentBand() {return requestedBand;};

    // flow:
    // User presses button
    // this sends request for BSR information and flags waitingForBSR=true
    // When BSR data is received, and if we were waiting for it,
    // then we select the frequency and mode and act upon it,
    // by sending out a gotoFreqMode(freq, mode).
    // Or we could grant access to the command queue and not have to do these
    // custom functions.

    // The BSR can presumably show up without having been invited.

    // Most of the connections shall be to rigCommander and
    // to the command queue in wfmain (soon to be broken out).
    // One connection may also be made to update the UI when we change frequency and mode.

signals:
    // look at what cmdSetModeFilter does, can we depreciate it?
    //void issueCmdUniquePriority(cmds cmd, char c); // to go to a mode
    //void issueDelayedCommand(cmds cmd); // for data mode and filter
    //void issueCmdF(cmds cmd, freqt f); // go to frequency
    //void issueCmd(cmds, char); // to get BSR of a specific band

    // void getBandStackReg(char band, char regCode); // no, use the queue.
    // void gotoFreqMode(); // TODO, arguments will contain BSR data

public slots:
    void receiveRigCaps(rigCapabilities* rc);
    void receiveCache(cacheItem item);

private slots:
    void on_band2200mbtn_clicked();
    void on_band630mbtn_clicked();
    void on_band160mbtn_clicked();
    void on_band80mbtn_clicked();
    void on_band60mbtn_clicked();
    void on_band40mbtn_clicked();
    void on_band30mbtn_clicked();
    void on_band20mbtn_clicked();
    void on_band17mbtn_clicked();
    void on_band15mbtn_clicked();
    void on_band12mbtn_clicked();
    void on_band10mbtn_clicked();
    void on_band6mbtn_clicked();
    void on_band4mbtn_clicked();
    void on_band2mbtn_clicked();
    void on_band70cmbtn_clicked();
    void on_band23cmbtn_clicked();
    void on_band13cmbtn_clicked();
    void on_band6cmbtn_clicked();
    void on_band3cmbtn_clicked();
    void on_bandWFMbtn_clicked();
    void on_bandAirbtn_clicked();
    void on_bandGenbtn_clicked();

private:
    Ui::bandbuttons *ui;
    void bandStackBtnClick(availableBands band);
    void jumpToBandWithoutBSR(availableBands band);
    void setUIToRig();
    void showButton(QPushButton *b);
    void hideButton(QPushButton *b);
    char bandStkRegCode;

    bool waitingForBSR = false;
    rigCapabilities* rigCaps=Q_NULLPTR;
    bool haveRigCaps = false;
    cachingQueue* queue;
    availableBands requestedBand = bandUnknown;
};

#endif // BANDBUTTONS_H
