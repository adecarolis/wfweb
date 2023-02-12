#include "controllersetup.h"
#include "ui_controllersetup.h"
#include "logcategories.h"

controllerSetup::controllerSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::controllerSetup)
{
    ui->setupUi(this);
    scene = new controllerScene();
    connect(scene, SIGNAL(mousePressed(QPoint)), this, SLOT(mousePressed(QPoint)));
    ui->graphicsView->setScene(scene);
    ui->qkBrightCombo->setVisible(false);
    ui->qkOrientCombo->setVisible(false);
    ui->qkSpeedCombo->setVisible(false);
    ui->qkColorButton->setVisible(false);
    ui->qkTimeoutSpin->setVisible(false);
    ui->qkTimeoutLabel->setVisible(false);
    textItem = scene->addText("No USB controller found");
    textItem->setDefaultTextColor(Qt::gray);
    this->resize(this->sizeHint());
}

controllerSetup::~controllerSetup()
{
    /*
    // Remove any existing button text:
    for (QGraphicsItem* item : scene->items())
    {
        QGraphicsTextItem* txt = qgraphicsitem_cast<QGraphicsTextItem*>(item);
        if (!txt || txt == textItem)
            continue;
        scene->removeItem(txt);
        delete txt;
    }

    if (onEventProxy != Q_NULLPTR) {
        scene->removeItem(onEventProxy);
        onEventProxy = Q_NULLPTR;
        delete onEvent;
        onEvent = Q_NULLPTR;
    }
    if (offEventProxy != Q_NULLPTR) {
        scene->removeItem(offEventProxy);
        offEventProxy = Q_NULLPTR;
        delete offEvent;
        offEvent = Q_NULLPTR;
    }
    if (knobEventProxy != Q_NULLPTR) {
        scene->removeItem(knobEventProxy);
        knobEventProxy = Q_NULLPTR;
        delete knobEvent;
        knobEvent = Q_NULLPTR;
    }
    */

    if (bgImage != Q_NULLPTR) {
        scene->removeItem(bgImage);
        delete bgImage;
        bgImage = Q_NULLPTR;
    }

    delete textItem;
    delete scene;
    delete ui;
}

void controllerSetup::mousePressed(QPoint p) 
{
    // Receive mouse event from the scene
    qDebug() << "Looking for button Point x=" << p.x() << " y=" << p.y();
    if (onEvent == Q_NULLPTR|| offEvent == Q_NULLPTR|| knobEvent == Q_NULLPTR)
    {
        qInfo(logUsbControl()) << "Event missing, cannot continue...";
        return;
    }
        
    bool found = false;
    QMutexLocker locker(mutex);


    for (BUTTON& b : *buttons)
    {
        if (b.dev == currentDevice && b.pos.contains(p))
        {
            found = true;
            currentButton = &b;
            qDebug() << "Button" << currentButton->num << "On Event" << currentButton->onCommand->text << "Off Event" << currentButton->offCommand->text;
            onEvent->blockSignals(true);
            onEvent->move(p);
            onEvent->setCurrentIndex(onEvent->findData(currentButton->onCommand->index));
            onEvent->show();
            onEvent->blockSignals(false);

            p.setY(p.y() + 40);
            offEvent->blockSignals(true);
            offEvent->move(p);
            offEvent->setCurrentIndex(offEvent->findData(currentButton->offCommand->index));
            offEvent->show();
            offEvent->blockSignals(false);
            knobEvent->hide();
            break;
        }
    }

    if (!found) {
        for (KNOB& k : *knobs)
        {
            if (k.dev == currentDevice && k.pos.contains(p))
            {
                found = true;
                currentKnob = &k;
                qDebug() << "Knob" << currentKnob->num << "Event" << currentKnob->command->text;
                knobEvent->blockSignals(true);
                knobEvent->move(p);
                knobEvent->setCurrentIndex(knobEvent->findData(currentKnob->command->index));
                knobEvent->show();
                knobEvent->blockSignals(false);
                onEvent->hide();
                offEvent->hide();
                break;
            }
        }
    }

    if (!found) {
        onEvent->hide();
        offEvent->hide();
        knobEvent->hide();
    }
}

void controllerSetup::onEventIndexChanged(int index) {
    Q_UNUSED(index);

    if (currentButton != Q_NULLPTR && onEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentButton->onCommand = &commands->at(onEvent->currentData().toInt());
        currentButton->onText->setPlainText(currentButton->onCommand->text);
        currentButton->onText->setPos(currentButton->pos.center().x() - currentButton->onText->boundingRect().width() / 2,
            (currentButton->pos.center().y() - currentButton->onText->boundingRect().height() / 2)-6);
        // Signal that any button programming on the device should be completed.
        emit programButton(currentButton->num, currentButton->onCommand->text);
    }
}


void controllerSetup::offEventIndexChanged(int index) {
    Q_UNUSED(index);
    if (currentButton != Q_NULLPTR && offEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentButton->offCommand = &commands->at(offEvent->currentData().toInt());
        currentButton->offText->setPlainText(currentButton->offCommand->text);
        currentButton->offText->setPos(currentButton->pos.center().x() - currentButton->offText->boundingRect().width() / 2,
            (currentButton->pos.center().y() - currentButton->offText->boundingRect().height() / 2)+6);
    }
}

void controllerSetup::knobEventIndexChanged(int index) {
    Q_UNUSED(index);
    if (currentKnob != Q_NULLPTR && knobEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentKnob->command = &commands->at(knobEvent->currentData().toInt());
        currentKnob->text->setPlainText(currentKnob->command->text);
        currentKnob->text->setPos(currentKnob->pos.center().x() - currentKnob->text->boundingRect().width() / 2,
            (currentKnob->pos.center().y() - currentKnob->text->boundingRect().height() / 2));

    }
}


void controllerSetup::newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut)
{
    buttons = but;
    knobs = kb;
    commands = cmd;
    mutex = mut;

    QMutexLocker locker(mutex);

    // Remove any existing button text:
    for (QGraphicsItem* item : scene->items())
    {
        QGraphicsTextItem* txt = qgraphicsitem_cast<QGraphicsTextItem*>(item);
        if (!txt || txt==textItem)
            continue;
        scene->removeItem(txt);
        delete txt;
    }

    if (bgImage != Q_NULLPTR) {
        scene->removeItem(bgImage);
        delete bgImage;
        bgImage = Q_NULLPTR;
    }
    if (onEventProxy != Q_NULLPTR) {
        scene->removeItem(onEventProxy);
        onEventProxy = Q_NULLPTR;
        delete onEvent;
        onEvent = Q_NULLPTR;
    }
    if (offEventProxy != Q_NULLPTR) {
        scene->removeItem(offEventProxy);
        offEventProxy = Q_NULLPTR;
        delete offEvent;
        offEvent = Q_NULLPTR;
    }
    if (knobEventProxy != Q_NULLPTR) {
        scene->removeItem(knobEventProxy);
        knobEventProxy = Q_NULLPTR;
        delete knobEvent;
        knobEvent = Q_NULLPTR;
    }

    QImage image;

    switch (devType) {
        case shuttleXpress:
            image.load(":/resources/shuttlexpress.png");
            deviceName = "shuttleXpress";
            break;
        case shuttlePro2:
            image.load(":/resources/shuttlepro.png");
            deviceName = "shuttlePro2";
            break;
        case RC28:
            image.load(":/resources/rc28.png");
            deviceName = "RC28";
            break;
        case xBoxGamepad:
            image.load(":/resources/xbox.png");
            deviceName = "XBox";
            break;
        case eCoderPlus:
            image.load(":/resources/ecoder.png");
            deviceName = "eCoderPlus";
            break;
        case QuickKeys:
            image.load(":/resources/quickkeys.png");
            deviceName = "QuickKeys";
            break;
        default:
            textItem->show();
            ui->graphicsView->setSceneRect(scene->itemsBoundingRect());
            this->adjustSize();
            break;
    }
    
    textItem->hide();
    bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    scene->addItem(bgImage);

    ui->graphicsView->setMinimumSize(bgImage->boundingRect().width() + 100, bgImage->boundingRect().height() + 2);
    currentDevice = devType;

    if (currentDevice == QuickKeys) {
        ui->qkBrightCombo->setVisible(true);
        ui->qkOrientCombo->setVisible(true);
        ui->qkSpeedCombo->setVisible(true);
        ui->qkColorButton->setVisible(true);
        ui->qkTimeoutSpin->setVisible(true);
        ui->qkTimeoutLabel->setVisible(true);
    }
    else
    {
        ui->qkBrightCombo->setVisible(false);
        ui->qkOrientCombo->setVisible(false);
        ui->qkSpeedCombo->setVisible(false);
        ui->qkColorButton->setVisible(false);
        ui->qkTimeoutSpin->setVisible(false);
        ui->qkTimeoutLabel->setVisible(false);
    }

    if (currentDevice != usbNone)
    {
        offEvent = new QComboBox;
        onEvent = new QComboBox;
        knobEvent = new QComboBox;

        onEvent->blockSignals(true);
        offEvent->blockSignals(true);
        knobEvent->blockSignals(true);

        onEvent->clear();
        offEvent->clear();
        knobEvent->clear();

        onEvent->setMaxVisibleItems(5);
        offEvent->setMaxVisibleItems(5);
        knobEvent->setMaxVisibleItems(5);

        onEvent->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        offEvent->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        knobEvent->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

        onEvent->setStyleSheet("combobox-popup: 0;");
        offEvent->setStyleSheet("combobox-popup: 0;");
        knobEvent->setStyleSheet("combobox-popup: 0;");

        for (COMMAND& c : *commands) {
            if (c.cmdType == commandButton || c.text == "None") {
                onEvent->addItem(c.text, c.index);
                offEvent->addItem(c.text, c.index);
            }

            if (c.cmdType == commandKnob || c.text == "None") {
                knobEvent->addItem(c.text, c.index);
            }
        }

        onEvent->blockSignals(false);
        offEvent->blockSignals(false);
        knobEvent->blockSignals(false);

        onEvent->hide();
        offEvent->hide();
        knobEvent->hide();

        // Set button text
        for (BUTTON& b : *buttons)
        {

            if (b.dev == currentDevice) {
                b.onText = new QGraphicsTextItem(b.onCommand->text);
                b.onText->setDefaultTextColor(b.textColour);
                scene->addItem(b.onText);
                b.onText->setPos(b.pos.center().x() - b.onText->boundingRect().width() / 2,
                    (b.pos.center().y() - b.onText->boundingRect().height() / 2) - 6);
                emit programButton(b.num, b.onCommand->text); // Program the button with ontext if supported

                b.offText = new QGraphicsTextItem(b.offCommand->text);
                b.offText->setDefaultTextColor(b.textColour);
                scene->addItem(b.offText);
                b.offText->setPos(b.pos.center().x() - b.offText->boundingRect().width() / 2,
                    (b.pos.center().y() - b.onText->boundingRect().height() / 2) + 6);
            }
        }

        // Set knob text

        for (KNOB& k : *knobs)
        {
            if (k.dev == currentDevice) {
                k.text = new QGraphicsTextItem(k.command->text);
                k.text->setDefaultTextColor(k.textColour);
                scene->addItem(k.text);
                k.text->setPos(k.pos.center().x() - k.text->boundingRect().width() / 2,
                    (k.pos.center().y() - k.text->boundingRect().height() / 2));
            }
        }
        ui->graphicsView->setSceneRect(scene->itemsBoundingRect());
        ui->graphicsView->resize(ui->graphicsView->sizeHint());
        //this->resize(this->sizeHint());
        this->adjustSize();

        // Add comboboxes to scene after everything else.
        offEventProxy = scene->addWidget(offEvent);
        connect(offEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(offEventIndexChanged(int)));
        onEventProxy = scene->addWidget(onEvent);
        connect(onEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(onEventIndexChanged(int)));
        knobEventProxy = scene->addWidget(knobEvent);
        connect(knobEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(knobEventIndexChanged(int)));

        if (currentDevice == QuickKeys) {
            // Finally update the device with the default values
            emit programBrightness((quint8)ui->qkBrightCombo->currentIndex() - 1);
            emit programOrientation((quint8)ui->qkOrientCombo->currentIndex());
            emit programSpeed((quint8)ui->qkSpeedCombo->currentIndex());
            emit programTimeout((quint8)ui->qkTimeoutSpin->value());
            emit programWheelColour((quint8)initialColor.red(), (quint8)initialColor.green(), (quint8)initialColor.blue());
        }
    }
}

void controllerSetup::receiveSensitivity(int val)
{
    ui->sensitivitySlider->blockSignals(true);
    ui->sensitivitySlider->setValue(val);
    ui->sensitivitySlider->blockSignals(false);
}

void controllerSetup::on_sensitivitySlider_valueChanged(int val)
{
    emit sendSensitivity(val);
}

void controllerSetup::on_qkBrightCombo_currentIndexChanged(int index)
{
    if (index) {
        emit programBrightness((quint8)index - 1);
    }
    emit updateSettings((quint8)ui->qkBrightCombo->currentIndex(), (quint8)ui->qkOrientCombo->currentIndex(),
        (quint8)ui->qkSpeedCombo->currentIndex(), (quint8)ui->qkTimeoutSpin->value(), initialColor);
}

void controllerSetup::on_qkOrientCombo_currentIndexChanged(int index)
{
    if (index) {
        emit programOrientation((quint8)index);
        emit programOverlay(3, QString("Orientation set to %0").arg(ui->qkOrientCombo->currentText()));
    }
    emit updateSettings((quint8)ui->qkBrightCombo->currentIndex(), (quint8)ui->qkOrientCombo->currentIndex(),
        (quint8)ui->qkSpeedCombo->currentIndex(), (quint8)ui->qkTimeoutSpin->value(), initialColor);
}

void controllerSetup::on_qkSpeedCombo_currentIndexChanged(int index)
{
    if (index) {
        emit programSpeed((quint8)index);
        emit programOverlay(3, QString("Dial speed set to %0").arg(ui->qkSpeedCombo->currentText()));
    }
    emit updateSettings((quint8)ui->qkBrightCombo->currentIndex(), (quint8)ui->qkOrientCombo->currentIndex(),
        (quint8)ui->qkSpeedCombo->currentIndex(), (quint8)ui->qkTimeoutSpin->value(), initialColor);
}

void controllerSetup::on_qkColorButton_clicked()
{
    
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, false);
    options.setFlag(QColorDialog::DontUseNativeDialog, false);
    QColor selColor = QColorDialog::getColor(initialColor, this, "Select Color", options);

    if(!selColor.isValid())
    {
        selColor = initialColor;
    }
    initialColor = selColor;
    emit programWheelColour((quint8)selColor.red(), (quint8)selColor.green(), (quint8)initialColor.blue());
    emit updateSettings((quint8)ui->qkBrightCombo->currentIndex(), (quint8)ui->qkOrientCombo->currentIndex(),
        (quint8)ui->qkSpeedCombo->currentIndex(), (quint8)ui->qkTimeoutSpin->value(), initialColor);
}

void controllerSetup::on_qkTimeoutSpin_valueChanged(int arg1)
{
    emit programTimeout((quint8)arg1);
    emit programOverlay(3, QString("Sleep timeout set to %0 minutes").arg(arg1));
    emit updateSettings((quint8)ui->qkBrightCombo->currentIndex(), (quint8)ui->qkOrientCombo->currentIndex(), 
        (quint8)ui->qkSpeedCombo->currentIndex(), (quint8)ui->qkTimeoutSpin->value(), initialColor);
}

void controllerSetup::setDefaults(quint8 bright, quint8 orient, quint8 speed, quint8 timeout, QColor color)
{
    ui->qkBrightCombo->blockSignals(true);
    ui->qkSpeedCombo->blockSignals(true);
    ui->qkOrientCombo->blockSignals(true);
    ui->qkTimeoutSpin->blockSignals(true);

    ui->qkBrightCombo->setCurrentIndex((int)bright);
    ui->qkOrientCombo->setCurrentIndex((int)orient);
    ui->qkSpeedCombo->setCurrentIndex((int)speed);
    ui->qkTimeoutSpin->setValue((int)timeout);
    initialColor = color;

    ui->qkBrightCombo->blockSignals(false);
    ui->qkSpeedCombo->blockSignals(false);
    ui->qkOrientCombo->blockSignals(false);
    ui->qkTimeoutSpin->blockSignals(false);

}