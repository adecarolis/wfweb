#include "repeatersetup.h"
#include "ui_repeatersetup.h"

repeaterSetup::repeaterSetup(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::repeaterSetup)
{
    ui->setupUi(this);

    // populate the CTCSS combo box:
    populateTones();

    // populate the DCS combo box:
    populateDTCS();
}

repeaterSetup::~repeaterSetup()
{
    delete ui;
}

void repeaterSetup::populateTones()
{
    ui->rptToneCombo->addItem("67.0", quint16(670));
    ui->rptToneCombo->addItem("69.3", quint16(693));
    ui->rptToneCombo->addItem("71.9", quint16(719));
    ui->rptToneCombo->addItem("74.4", quint16(744));
    ui->rptToneCombo->addItem("77.0", quint16(770));
    ui->rptToneCombo->addItem("79.7", quint16(797));
    ui->rptToneCombo->addItem("82.5", quint16(825));
    ui->rptToneCombo->addItem("85.4", quint16(854));
    ui->rptToneCombo->addItem("88.5", quint16(885));
    ui->rptToneCombo->addItem("91.5", quint16(915));
    ui->rptToneCombo->addItem("94.8", quint16(948));
    ui->rptToneCombo->addItem("97.4", quint16(974));
    ui->rptToneCombo->addItem("100.0", quint16(1000));
    ui->rptToneCombo->addItem("103.5", quint16(1035));
    ui->rptToneCombo->addItem("107.2", quint16(1072));
    ui->rptToneCombo->addItem("110.9", quint16(1109));
    ui->rptToneCombo->addItem("114.8", quint16(1148));
    ui->rptToneCombo->addItem("118.8", quint16(1188));
    ui->rptToneCombo->addItem("123.0", quint16(1230));
    ui->rptToneCombo->addItem("127.3", quint16(1273));
    ui->rptToneCombo->addItem("131.8", quint16(1318));
    ui->rptToneCombo->addItem("136.5", quint16(1365));
    ui->rptToneCombo->addItem("141.3", quint16(1413));
    ui->rptToneCombo->addItem("146.2", quint16(1462));
    ui->rptToneCombo->addItem("151.4", quint16(1514));
    ui->rptToneCombo->addItem("156.7", quint16(1567));
    ui->rptToneCombo->addItem("159.8", quint16(1598));
    ui->rptToneCombo->addItem("162.2", quint16(1622));
    ui->rptToneCombo->addItem("165.5", quint16(1655));
    ui->rptToneCombo->addItem("167.9", quint16(1679));
    ui->rptToneCombo->addItem("171.3", quint16(1713));
    ui->rptToneCombo->addItem("173.8", quint16(1738));
    ui->rptToneCombo->addItem("177.3", quint16(1773));
    ui->rptToneCombo->addItem("179.9", quint16(1799));
    ui->rptToneCombo->addItem("183.5", quint16(1835));
    ui->rptToneCombo->addItem("186.2", quint16(1862));
    ui->rptToneCombo->addItem("189.9", quint16(1899));
    ui->rptToneCombo->addItem("192.8", quint16(1928));
    ui->rptToneCombo->addItem("196.6", quint16(1966));
    ui->rptToneCombo->addItem("199.5", quint16(1995));
    ui->rptToneCombo->addItem("203.5", quint16(2035));
    ui->rptToneCombo->addItem("206.5", quint16(2065));
    ui->rptToneCombo->addItem("210.7", quint16(2107));
    ui->rptToneCombo->addItem("218.1", quint16(2181));
    ui->rptToneCombo->addItem("225.7", quint16(2257));
    ui->rptToneCombo->addItem("229.1", quint16(2291));
    ui->rptToneCombo->addItem("233.6", quint16(2336));
    ui->rptToneCombo->addItem("241.8", quint16(2418));
    ui->rptToneCombo->addItem("250.3", quint16(2503));
    ui->rptToneCombo->addItem("254.1", quint16(2541));
}

void repeaterSetup::populateDTCS()
{
    ui->rptDTCSCombo->addItem("023", quint16(23));
    ui->rptDTCSCombo->addItem("025", quint16(25));
    ui->rptDTCSCombo->addItem("026", quint16(26));
    ui->rptDTCSCombo->addItem("031", quint16(31));
    ui->rptDTCSCombo->addItem("032", quint16(32));
    ui->rptDTCSCombo->addItem("036", quint16(36));
    ui->rptDTCSCombo->addItem("043", quint16(43));
    ui->rptDTCSCombo->addItem("047", quint16(47));
    ui->rptDTCSCombo->addItem("051", quint16(51));
    ui->rptDTCSCombo->addItem("053", quint16(53));
    ui->rptDTCSCombo->addItem("054", quint16(54));
    ui->rptDTCSCombo->addItem("065", quint16(65));
    ui->rptDTCSCombo->addItem("071", quint16(71));
    ui->rptDTCSCombo->addItem("072", quint16(72));
    ui->rptDTCSCombo->addItem("073", quint16(73));
    ui->rptDTCSCombo->addItem("074", quint16(74));

    ui->rptDTCSCombo->addItem("114", quint16(114));
    ui->rptDTCSCombo->addItem("115", quint16(115));
    ui->rptDTCSCombo->addItem("116", quint16(116));
    ui->rptDTCSCombo->addItem("122", quint16(122));
    ui->rptDTCSCombo->addItem("125", quint16(125));
    ui->rptDTCSCombo->addItem("131", quint16(131));
    ui->rptDTCSCombo->addItem("132", quint16(132));
    ui->rptDTCSCombo->addItem("134", quint16(134));
    ui->rptDTCSCombo->addItem("143", quint16(143));
    ui->rptDTCSCombo->addItem("145", quint16(145));
    ui->rptDTCSCombo->addItem("152", quint16(152));
    ui->rptDTCSCombo->addItem("155", quint16(155));
    ui->rptDTCSCombo->addItem("156", quint16(156));
    ui->rptDTCSCombo->addItem("162", quint16(162));
    ui->rptDTCSCombo->addItem("165", quint16(165));
    ui->rptDTCSCombo->addItem("172", quint16(172));
    ui->rptDTCSCombo->addItem("174", quint16(174));

    ui->rptDTCSCombo->addItem("205", quint16(205));
    ui->rptDTCSCombo->addItem("212", quint16(212));
    ui->rptDTCSCombo->addItem("223", quint16(223));
    ui->rptDTCSCombo->addItem("225", quint16(225));
    ui->rptDTCSCombo->addItem("226", quint16(226));
    ui->rptDTCSCombo->addItem("243", quint16(243));
    ui->rptDTCSCombo->addItem("244", quint16(244));
    ui->rptDTCSCombo->addItem("245", quint16(245));
    ui->rptDTCSCombo->addItem("246", quint16(246));
    ui->rptDTCSCombo->addItem("251", quint16(251));
    ui->rptDTCSCombo->addItem("252", quint16(252));
    ui->rptDTCSCombo->addItem("255", quint16(255));
    ui->rptDTCSCombo->addItem("261", quint16(261));
    ui->rptDTCSCombo->addItem("263", quint16(263));
    ui->rptDTCSCombo->addItem("265", quint16(265));
    ui->rptDTCSCombo->addItem("266", quint16(266));
    ui->rptDTCSCombo->addItem("271", quint16(271));
    ui->rptDTCSCombo->addItem("274", quint16(274));

    ui->rptDTCSCombo->addItem("306", quint16(306));
    ui->rptDTCSCombo->addItem("311", quint16(311));
    ui->rptDTCSCombo->addItem("315", quint16(315));
    ui->rptDTCSCombo->addItem("325", quint16(325));
    ui->rptDTCSCombo->addItem("331", quint16(331));
    ui->rptDTCSCombo->addItem("332", quint16(332));
    ui->rptDTCSCombo->addItem("343", quint16(343));
    ui->rptDTCSCombo->addItem("346", quint16(346));
    ui->rptDTCSCombo->addItem("351", quint16(351));
    ui->rptDTCSCombo->addItem("356", quint16(356));
    ui->rptDTCSCombo->addItem("364", quint16(364));
    ui->rptDTCSCombo->addItem("365", quint16(365));
    ui->rptDTCSCombo->addItem("371", quint16(371));

    ui->rptDTCSCombo->addItem("411", quint16(411));
    ui->rptDTCSCombo->addItem("412", quint16(412));
    ui->rptDTCSCombo->addItem("413", quint16(413));
    ui->rptDTCSCombo->addItem("423", quint16(423));
    ui->rptDTCSCombo->addItem("431", quint16(431));
    ui->rptDTCSCombo->addItem("432", quint16(432));
    ui->rptDTCSCombo->addItem("445", quint16(445));
    ui->rptDTCSCombo->addItem("446", quint16(446));
    ui->rptDTCSCombo->addItem("452", quint16(452));
    ui->rptDTCSCombo->addItem("454", quint16(454));
    ui->rptDTCSCombo->addItem("455", quint16(455));
    ui->rptDTCSCombo->addItem("462", quint16(462));
    ui->rptDTCSCombo->addItem("464", quint16(464));
    ui->rptDTCSCombo->addItem("465", quint16(465));
    ui->rptDTCSCombo->addItem("466", quint16(466));

    ui->rptDTCSCombo->addItem("503", quint16(503));
    ui->rptDTCSCombo->addItem("506", quint16(506));
    ui->rptDTCSCombo->addItem("516", quint16(516));
    ui->rptDTCSCombo->addItem("523", quint16(512));
    ui->rptDTCSCombo->addItem("526", quint16(526));
    ui->rptDTCSCombo->addItem("532", quint16(532));
    ui->rptDTCSCombo->addItem("546", quint16(546));
    ui->rptDTCSCombo->addItem("565", quint16(565));

    ui->rptDTCSCombo->addItem("606", quint16(606));
    ui->rptDTCSCombo->addItem("612", quint16(612));
    ui->rptDTCSCombo->addItem("624", quint16(624));
    ui->rptDTCSCombo->addItem("627", quint16(627));
    ui->rptDTCSCombo->addItem("631", quint16(631));
    ui->rptDTCSCombo->addItem("632", quint16(632));
    ui->rptDTCSCombo->addItem("654", quint16(654));
    ui->rptDTCSCombo->addItem("662", quint16(662));
    ui->rptDTCSCombo->addItem("664", quint16(664));

    ui->rptDTCSCombo->addItem("703", quint16(703));
    ui->rptDTCSCombo->addItem("712", quint16(712));
    ui->rptDTCSCombo->addItem("723", quint16(723));
    ui->rptDTCSCombo->addItem("731", quint16(731));
    ui->rptDTCSCombo->addItem("732", quint16(732));
    ui->rptDTCSCombo->addItem("734", quint16(734));
    ui->rptDTCSCombo->addItem("746", quint16(746));
    ui->rptDTCSCombo->addItem("754", quint16(754));
}

void repeaterSetup::receiveDuplexMode(duplexMode dm)
{
    currentdm = dm;
    switch(dm)
    {
        case dmSimplex:
        case dmSplitOff:
            ui->rptSimplexBtn->setChecked(true);
            break;
        case dmDupPlus:
            ui->rptDupPlusBtn->setChecked(true);
            break;
        case dmDupMinus:
            ui->rptDupMinusBtn->setChecked(true);
            break;
        default:
            break;
    }
}

void repeaterSetup::handleRptAccessMode(rptAccessTxRx tmode)
{
    switch(tmode)
    {
        case ratrNN:
            ui->toneNone->setChecked(true);
            break;
        case ratrTT:
            ui->toneTSQL->setChecked(true);
            break;
        case ratrTN:
            ui->toneTone->setChecked(true);
            break;
        case ratrDD:
            ui->toneDTCS->setChecked(true);
            break;
        default:
            break;
    }

    (void)tmode;
}

void repeaterSetup::handleTone(quint16 tone)
{
    int tindex = ui->rptToneCombo->findData(tone);
    ui->rptToneCombo->setCurrentIndex(tindex);
}

void repeaterSetup::handleTSQL(quint16 tsql)
{
    // TODO: Consider a second combo box for the TSQL
    int tindex = ui->rptToneCombo->findData(tsql);
    ui->rptToneCombo->setCurrentIndex(tindex);
}

void repeaterSetup::handleDTCS(quint16 dcode, bool tinv, bool rinv)
{
    int dindex = ui->rptDTCSCombo->findData(dcode);
    ui->rptDTCSCombo->setCurrentIndex(dindex);
    ui->rptDTCSInvertTx->setChecked(tinv);
    ui->rptDTCSInvertRx->setChecked(rinv);
}

void repeaterSetup::on_rptSimplexBtn_clicked()
{
    // Simplex
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmSimplex);
}

void repeaterSetup::on_rptDupPlusBtn_clicked()
{
    // DUP+
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmDupPlus);
}

void repeaterSetup::on_rptDupMinusBtn_clicked()
{
    // DUP-
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmDupMinus);
}

void repeaterSetup::on_rptAutoBtn_clicked()
{
    // Auto Rptr (enable this feature)
    // TODO: Hide an AutoOff button somewhere for non-US users
    emit setDuplexMode(dmDupAutoOn);
}

void repeaterSetup::on_rptReadRigBtn_clicked()
{
    emit getDuplexMode();
}

void repeaterSetup::on_rptToneCombo_activated(int tindex)
{
    quint16 tone=0;
    tone = (quint16)ui->rptToneCombo->itemData(tindex).toUInt();
    if(ui->toneTone->isChecked())
    {
        emit setTone(tone);
    } else if (ui->toneTSQL->isChecked()) {
        emit setTSQL(tone);
    }
}

void repeaterSetup::on_rptDTCSCombo_activated(int index)
{
    quint16 dcode=0;
    bool tinv = ui->rptDTCSInvertTx->isChecked();
    bool rinv = ui->rptDTCSInvertRx->isChecked();
    dcode = (quint16)ui->rptDTCSCombo->itemData(index).toUInt();
    emit setDTCS(dcode, tinv, rinv);
}

void repeaterSetup::on_toneNone_clicked()
{
    rptAccessTxRx rm;
    rm = ratrNN;
    emit setRptAccessMode(rm);
}

void repeaterSetup::on_toneTone_clicked()
{
    rptAccessTxRx rm;
    rm = ratrTN;
    emit setRptAccessMode(rm);
    emit setTone((quint16)ui->rptToneCombo->currentData().toUInt());
}

void repeaterSetup::on_toneTSQL_clicked()
{
    rptAccessTxRx rm;
    rm = ratrTT;
    emit setRptAccessMode(rm);
    emit setTSQL((quint16)ui->rptToneCombo->currentData().toUInt());
}

void repeaterSetup::on_toneDTCS_clicked()
{
    rptAccessTxRx rm;
    quint16 dcode=0;

    rm = ratrDD;
    emit setRptAccessMode(rm);

    bool tinv = ui->rptDTCSInvertTx->isChecked();
    bool rinv = ui->rptDTCSInvertRx->isChecked();
    dcode = (quint16)ui->rptDTCSCombo->currentData().toUInt();
    emit setDTCS(dcode, tinv, rinv);
}

void repeaterSetup::on_debugBtn_clicked()
{
    //emit getTone();
    //emit getTSQL();
    //emit getDTCS();
    emit getRptAccessMode();
}
