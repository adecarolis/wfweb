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
    }
    // Signal that any button programming on the device should be completed.
    emit programButton(onEvent->currentData().toInt(), currentButton->onCommand->text); 
}


void controllerSetup::offEventIndexChanged(int index) {
    Q_UNUSED(index);
    if (currentButton != Q_NULLPTR && offEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentButton->offCommand = &commands->at(offEvent->currentData().toInt());
        currentButton->offText->setPlainText(currentButton->offCommand->text);
    }
}

void controllerSetup::knobEventIndexChanged(int index) {
    Q_UNUSED(index);
    if (currentKnob != Q_NULLPTR && knobEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentKnob->command = &commands->at(knobEvent->currentData().toInt());
        currentKnob->text->setPlainText(currentKnob->command->text);
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
        default:
            textItem->show();
            ui->graphicsView->setSceneRect(scene->itemsBoundingRect());
            this->adjustSize();
            return;
            break;
    }
    
    textItem->hide();
    bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    scene->addItem(bgImage);

    ui->graphicsView->setMinimumSize(bgImage->boundingRect().width() + 100, bgImage->boundingRect().height() + 2);
    currentDevice = devType;

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
            b.onText->setPos(b.pos.center().x() - b.onText->boundingRect().width() / 2, b.pos.y());
            emit programButton(b.num, b.onCommand->text); // Program the button with ontext if supported

            b.offText = new QGraphicsTextItem(b.offCommand->text);
            b.offText->setDefaultTextColor(b.textColour);
            scene->addItem(b.offText);
            b.offText->setPos(b.pos.center().x() - b.offText->boundingRect().width() / 2, b.pos.y() + 15);
        }
    }

    // Set knob text

    for (KNOB& k : *knobs)
    {
        if (k.dev == currentDevice) {
            k.text = new QGraphicsTextItem(k.command->text);
            k.text->setDefaultTextColor(k.textColour);
            scene->addItem(k.text);
            k.text->setPos(k.pos.center().x() - k.text->boundingRect().width() / 2, k.pos.y());
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
