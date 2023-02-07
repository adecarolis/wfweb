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

    connect(&onEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(onEventIndexChanged(int)));
    connect(&offEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(offEventIndexChanged(int)));
}

controllerSetup::~controllerSetup()
{
    delete textItem;
    delete scene;
    delete ui;

    if (bgImage != Q_NULLPTR) {
        delete bgImage;
    }
}

void controllerSetup::mousePressed(QPoint p) 
{
    // Receive mouse event from the scene
    qDebug() << "Looking for button Point x=" << p.x() << " y=" << p.y();
    bool found = false;
    for (BUTTON& b : *buttons)
    {
        if (b.dev == currentDevice && b.pos.contains(p))
        {
            found = true;
            currentButton = &b;
            qDebug() << "Button" << currentButton->num << "On Event" << currentButton->onCommand->text << "Off Event" << currentButton->offCommand->text;
            // Add off event first so it doesn't obscure on event.
            if (offEventProxy == Q_NULLPTR) {
                offEventProxy = scene->addWidget(&offEvent);
            }
            if (onEventProxy == Q_NULLPTR) {
                onEventProxy = scene->addWidget(&onEvent);
            }
            onEvent.blockSignals(true);
            onEvent.move(p);
            onEvent.setCurrentIndex(currentButton->onCommand->index);
            onEvent.show();
            onEvent.blockSignals(false);

            p.setY(p.y() + 40);
            offEvent.blockSignals(true);
            offEvent.move(p);
            offEvent.setCurrentIndex(currentButton->offCommand->index);
            offEvent.show();
            offEvent.blockSignals(false);

        }
    }

    if (!found) {
        onEvent.hide();
        offEvent.hide();
    }

}

void controllerSetup::onEventIndexChanged(int index) {
    qDebug() << "On Event for button" << currentButton->num << "Event" << index;
    if (currentButton != Q_NULLPTR && index < commands->size()) {
        currentButton->onCommand = &commands->at(index);
        currentButton->onText->setPlainText(currentButton->onCommand->text);
    }
}


void controllerSetup::offEventIndexChanged(int index) {
    qDebug() << "Off Event for button" << currentButton->num << "Event" << index;
    if (currentButton != Q_NULLPTR && index < commands->size()) {
        currentButton->offCommand = &commands->at(index);
        currentButton->offText->setPlainText(currentButton->offCommand->text);
    }
}


void controllerSetup::newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<COMMAND>* cmd)
{
    buttons = but;
    commands = cmd;

    // Remove any existing button text:
    for (QGraphicsItem* item : scene->items())
    {
        QGraphicsTextItem* txt = qgraphicsitem_cast<QGraphicsTextItem*>(item);
        if (!txt || txt==textItem)
            continue;
        scene->removeItem(txt);
    }

    if (bgImage != Q_NULLPTR) {
        scene->removeItem(bgImage);
        delete bgImage;
        bgImage = Q_NULLPTR;
        if (onEventProxy != Q_NULLPTR)
            scene->removeItem(onEventProxy);
        if (offEventProxy != Q_NULLPTR)
            scene->removeItem(offEventProxy);
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
    }

    textItem->hide();
    bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    scene->addItem(bgImage);

    ui->graphicsView->setMinimumSize(bgImage->boundingRect().width() + 100, bgImage->boundingRect().height() + 2);
    currentDevice = devType;

    onEvent.blockSignals(true);
    offEvent.blockSignals(true);
    onEvent.clear();
    offEvent.clear();

    onEvent.setMaxVisibleItems(5);
    offEvent.setMaxVisibleItems(5);
    onEvent.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    offEvent.view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    onEvent.setStyleSheet("combobox-popup: 0;");
    offEvent.setStyleSheet("combobox-popup: 0;");

    for (COMMAND &c : *commands) {
        onEvent.addItem(c.text);
        offEvent.addItem(c.text);
    }
    onEvent.blockSignals(false);
    offEvent.blockSignals(false);

    // Set button text
    for (BUTTON& b : *buttons)
    {

        if (b.dev == currentDevice) {
            //b.onCommand = &commands->at(0);
            b.onText = new QGraphicsTextItem(b.onCommand->text);
            b.onText->setDefaultTextColor(b.textColour);
            scene->addItem(b.onText);
            b.onText->setPos(b.pos.x(), b.pos.y());

            //b.offCommand = &commands->at(0);
            b.offText = new QGraphicsTextItem(b.offCommand->text);
            b.offText->setDefaultTextColor(b.textColour);
            scene->addItem(b.offText);
            b.offText->setPos(b.pos.x(), b.pos.y() + 10);
        }
    }

    ui->graphicsView->setSceneRect(scene->itemsBoundingRect());
    ui->graphicsView->resize(ui->graphicsView->sizeHint());
    //this->resize(this->sizeHint());
    this->adjustSize();
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
