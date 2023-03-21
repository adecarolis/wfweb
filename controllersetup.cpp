#include "controllersetup.h"
#include "ui_controllersetup.h"
#include "logcategories.h"

controllerSetup::controllerSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::controllerSetup)
{
    ui->setupUi(this);
    ui->tabWidget->clear();
    ui->tabWidget->hide();
    noControllersText = new QLabel("No USB controller found");
    noControllersText->setStyleSheet("QLabel { color : gray; }");
    ui->hboxLayout->addWidget(noControllersText);
    this->resize(this->sizeHint());
}

controllerSetup::~controllerSetup()
{

    if (onEventProxy != Q_NULLPTR) {
        delete onEventProxy;
        delete onEvent;
    }

    if (offEventProxy != Q_NULLPTR) {
        delete offEventProxy;
        delete offEvent;
    }

    if (knobEventProxy != Q_NULLPTR) {
        delete knobEventProxy;
        delete knobEvent;
    }

    delete ui;
}

void controllerSetup::mousePressed(controllerScene* scene, QPoint p)
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

        if (ui->tabWidget->currentWidget()->objectName() == b.devicePath && b.pos.contains(p))
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
            if (ui->tabWidget->currentWidget()->objectName() == k.devicePath && k.pos.contains(p))
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

    if(found)
    {
        found=false;
        foreach (QGraphicsItem *item, scene->items())
        {
            QGraphicsProxyWidget *node = dynamic_cast<QGraphicsProxyWidget *>(item);
            if (node) {
                found=true;
                break;
            }
        }
        if (!found) {
            scene->addItem(offEvent->graphicsProxyWidget());
            scene->addItem(onEvent->graphicsProxyWidget());
            scene->addItem(knobEvent->graphicsProxyWidget());
        }
    }
    else
    {
        onEvent->hide();
        offEvent->hide();
        knobEvent->hide();
    }
}

void controllerSetup::onEventIndexChanged(int index) {
    Q_UNUSED(index);
    // If command is changed, delete current command and deep copy the new command
    if (currentButton != Q_NULLPTR && onEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        if (currentButton->onCommand)
            delete currentButton->onCommand;
        currentButton->onCommand = new COMMAND(commands->at(onEvent->currentData().toInt()));
        currentButton->onText->setPlainText(currentButton->onCommand->text);
        currentButton->onText->setPos(currentButton->pos.center().x() - currentButton->onText->boundingRect().width() / 2,
            (currentButton->pos.center().y() - currentButton->onText->boundingRect().height() / 2)-6);
        // Signal that any button programming on the device should be completed.
        emit programButton(currentButton->devicePath,currentButton->num, currentButton->onCommand->text);
    }
}


void controllerSetup::offEventIndexChanged(int index) {
    Q_UNUSED(index);
    // If command is changed, delete current command and deep copy the new command
    if (currentButton != Q_NULLPTR && offEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        if (currentButton->offCommand)
            delete currentButton->offCommand;
        currentButton->offCommand = new COMMAND(commands->at(offEvent->currentData().toInt()));
        currentButton->offText->setPlainText(currentButton->offCommand->text);
        currentButton->offText->setPos(currentButton->pos.center().x() - currentButton->offText->boundingRect().width() / 2,
            (currentButton->pos.center().y() - currentButton->offText->boundingRect().height() / 2)+6);
    }
}

void controllerSetup::knobEventIndexChanged(int index) {
    Q_UNUSED(index);
    // If command is changed, delete current command and deep copy the new command
    if (currentKnob != Q_NULLPTR && knobEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        if (currentKnob->command)
            delete currentKnob->command;
        currentKnob->command = new COMMAND(commands->at(knobEvent->currentData().toInt()));
        currentKnob->text->setPlainText(currentKnob->command->text);
        currentKnob->text->setPos(currentKnob->pos.center().x() - currentKnob->text->boundingRect().width() / 2,
            (currentKnob->pos.center().y() - currentKnob->text->boundingRect().height() / 2));

    }
}

void controllerSetup::removeDevice(USBDEVICE* dev)
{
    QMutexLocker locker(mutex);

    int remove = -1;

    for (int i = 0; i < ui->tabWidget->count(); i++) {
        auto widget = ui->tabWidget->widget(i);
        if (widget->objectName() == dev->path) {
            qDeleteAll(widget->findChildren<QWidget *>("", Qt::FindDirectChildrenOnly));
            remove = i;
        }
    }

    if (remove != -1) {
        qInfo(logUsbControl()) << "Removing tab" << dev->product;
        ui->tabWidget->removeTab(remove);
    }
    if (ui->tabWidget->count() == 0)
    {
        ui->tabWidget->hide();
        noControllersText->show();
        this->adjustSize();
    }
}

void controllerSetup::newDevice(USBDEVICE* dev, CONTROLLER* cntrl, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut)
{
    buttons = but;
    knobs = kb;
    commands = cmd;
    mutex = mut;

    QMutexLocker locker(mutex);

    qDebug(logUsbControl()) << "Adding new tab for" << dev->product;
    noControllersText->hide();

    QWidget* tab = new QWidget();
    tab->setObjectName(dev->path);

    ui->tabWidget->addTab(tab,dev->product);
    ui->tabWidget->show();

    QVBoxLayout* mainlayout = new QVBoxLayout();
    mainlayout->setContentsMargins(0,0,0,0);
    tab->setLayout(mainlayout);


    QHBoxLayout* toplayout = new QHBoxLayout();
    mainlayout->addLayout(toplayout);
    toplayout->setContentsMargins(0,0,0,0);

    QWidget* widget = new QWidget();
    mainlayout->addWidget(widget);
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0,0,0,0);

    QCheckBox* disabled = new QCheckBox();
    disabled->setText("Disable");
    toplayout->addWidget(disabled);
    dev->message = new QLabel();
    if (dev->connected) {
        dev->message->setStyleSheet("QLabel { color : green; }");
        dev->message->setText("Connected");
    } else {
        dev->message->setStyleSheet("QLabel { color : red; }");
        dev->message->setText("Not Connected");
    }

    toplayout->addWidget(dev->message);
    dev->message->setAlignment(Qt::AlignRight);

    connect(disabled, qOverload<bool>(&QCheckBox::clicked),
        [dev,this,widget](bool checked) { this->disableClicked(dev->path,checked,widget); });

    disabled->setChecked(dev->disabled);

    QGraphicsView *view = new QGraphicsView();
    layout->addWidget(view);

    QHBoxLayout* senslayout = new QHBoxLayout();
    layout->addLayout(senslayout);
    QLabel* senslabel = new QLabel("Sensitivity");
    senslayout->addWidget(senslabel);
    QSlider *sens = new QSlider();
    sens->setMinimum(1);
    sens->setMaximum(21);
    sens->setOrientation(Qt::Horizontal);
    sens->setInvertedAppearance(true);
    senslayout->addWidget(sens);
    sens->setValue(cntrl->sensitivity);
    connect(sens, &QSlider::valueChanged,
        [dev,this](int val) { this->sensitivityMoved(dev->path,val); });

    QImage image;

    switch (dev->usbDevice) {
        case shuttleXpress:
            image.load(":/resources/shuttlexpress.png");
            break;
        case shuttlePro2:
            image.load(":/resources/shuttlepro.png");
            break;
        case RC28:
            image.load(":/resources/rc28.png");
            break;
        case xBoxGamepad:
            image.load(":/resources/xbox.png");
            break;
        case eCoderPlus:
            image.load(":/resources/ecoder.png");
            break;
        case QuickKeys:
            image.load(":/resources/quickkeys.png");
            break;
        default:
            //ui->graphicsView->setSceneRect(scene->itemsBoundingRect());
            this->adjustSize();
            break;
    }

    QGraphicsItem* bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    view->setMinimumSize(bgImage->boundingRect().width() + 2, bgImage->boundingRect().height() + 2);

    // This command causes the window to disappear in Linux?
#if !defined(Q_OS_LINUX)
    this->setMinimumSize(bgImage->boundingRect().width() + 2, bgImage->boundingRect().height() + 250);
#endif

    controllerScene * scene = new controllerScene();
    view->setScene(scene);
    connect(scene, SIGNAL(mousePressed(controllerScene *,QPoint)), this, SLOT(mousePressed(controllerScene *,QPoint)));
    scene->addItem(bgImage);

    QGridLayout* grid = new QGridLayout();
    layout->addLayout(grid);

    if (dev->usbDevice == QuickKeys)
    {
        // Add QuickKeys section

        QLabel* brightlabel = new QLabel("Brightness");
        grid->addWidget(brightlabel,0,0);
        QComboBox *brightness = new QComboBox();
        brightness->addItem("Off");
        brightness->addItem("Low");
        brightness->addItem("Medium");
        brightness->addItem("High");
        brightness->setCurrentIndex(cntrl->brightness);
        grid->addWidget(brightness,1,0);
        connect(brightness, qOverload<int>(&QComboBox::currentIndexChanged),
            [dev,this](int index) { this->brightnessChanged(dev->path,index); });

        QLabel* speedlabel = new QLabel("Speed");
        grid->addWidget(speedlabel,0,1);
        QComboBox *speed = new QComboBox();
        speed->addItem("Fastest");
        speed->addItem("Faster");
        speed->addItem("Normal");
        speed->addItem("Slower");
        speed->addItem("Slowest");
        speed->setCurrentIndex(cntrl->speed);
        grid->addWidget(speed,1,1);
        connect(speed, qOverload<int>(&QComboBox::currentIndexChanged),
            [dev,this](int index) { this->speedChanged(dev->path,index); });

        QLabel* orientlabel = new QLabel("Orientation");
        grid->addWidget(orientlabel,0,2);
        QComboBox *orientation = new QComboBox();
        orientation->addItem("Rotate 0");
        orientation->addItem("Rotate 90");
        orientation->addItem("Rotate 180");
        orientation->addItem("Rotate 270");
        orientation->setCurrentIndex(cntrl->orientation);
        grid->addWidget(orientation,1,2);
        connect(orientation, qOverload<int>(&QComboBox::currentIndexChanged),
            [dev,this](int index) { this->orientationChanged(dev->path,index); });

        QLabel* colorlabel = new QLabel("Dial Color");
        grid->addWidget(colorlabel,0,3);
        QPushButton* color = new QPushButton("Select");
        grid->addWidget(color,1,3);
        connect(color, &QPushButton::clicked,
            [dev,this]() { this->colorPicker(dev->path); });

        QLabel* timeoutlabel = new QLabel("Timeout");
        grid->addWidget(timeoutlabel,0,4);
        QSpinBox *timeout = new QSpinBox();
        timeout->setValue(cntrl->timeout);
        grid->addWidget(timeout,1,4);
        connect(timeout, qOverload<int>(&QSpinBox::valueChanged),
            [dev,this](int index) { this->timeoutChanged(dev->path,index); });

        // Finally update the device with the default values
        emit programSensitivity(dev->path, cntrl->sensitivity);
        emit programBrightness(dev->path,cntrl->brightness);
        emit programOrientation(dev->path,cntrl->orientation);
        emit programSpeed(dev->path,cntrl->speed);
        emit programTimeout(dev->path,cntrl->timeout);
        emit programWheelColour(dev->path, cntrl->color.red(), cntrl->color.green(), cntrl->color.blue());
    }

    QLabel *helpText = new QLabel();
    helpText->setText("<p><b>Button configuration:</b> Right-click on each button to configure it.</p><p>Top selection is command to send when button is pressed and bottom is (optional) command to send when button is released.</p>");
    helpText->setAlignment(Qt::AlignCenter);
    layout->addWidget(helpText);


    if (dev->usbDevice != usbNone)
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

            if (b.devicePath == dev->path) {
                b.onText = new QGraphicsTextItem(b.onCommand->text);
                b.onText->setDefaultTextColor(b.textColour);
                scene->addItem(b.onText);
                b.onText->setPos(b.pos.center().x() - b.onText->boundingRect().width() / 2,
                    (b.pos.center().y() - b.onText->boundingRect().height() / 2) - 6);
                emit programButton(b.devicePath,b.num, b.onCommand->text); // Program the button with ontext if supported

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
            if (k.devicePath == dev->path) {
                k.text = new QGraphicsTextItem(k.command->text);
                k.text->setDefaultTextColor(k.textColour);
                scene->addItem(k.text);
                k.text->setPos(k.pos.center().x() - k.text->boundingRect().width() / 2,
                    (k.pos.center().y() - k.text->boundingRect().height() / 2));
            }
        }
        view->setSceneRect(scene->itemsBoundingRect());

        // Add comboboxes to scene after everything else.
        connect(offEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(offEventIndexChanged(int)));
        connect(onEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(onEventIndexChanged(int)));
        connect(knobEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(knobEventIndexChanged(int)));

        onEventProxy = new QGraphicsProxyWidget();
        onEventProxy->setWidget(onEvent);
        offEventProxy = new QGraphicsProxyWidget();
        offEventProxy->setWidget(offEvent);
        knobEventProxy = new QGraphicsProxyWidget();
        knobEventProxy->setWidget(knobEvent);

        this->adjustSize();

    }


    numTabs++;

}

void controllerSetup::sensitivityMoved(QString path, int val)
{
    qInfo(logUsbControl()) << "Setting sensitivity" << val <<"for device" << path;
    emit programSensitivity(path, val);
}

void controllerSetup::brightnessChanged(QString path, int index)
{
    emit programBrightness(path, (quint8)index);
}

void controllerSetup::orientationChanged(QString path, int index)
{
    emit programOrientation(path, (quint8)index);
}

void controllerSetup::speedChanged(QString path, int index)
{
    emit programSpeed(path, (quint8)index);
}

void controllerSetup::colorPicker(QString path)
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
    emit programWheelColour(path, (quint8)selColor.red(), (quint8)selColor.green(), (quint8)initialColor.blue());
}

void controllerSetup::timeoutChanged(QString path, int val)
{
    emit programTimeout(path, (quint8)val);
    emit programOverlay(path, 3, QString("Sleep timeout set to %0 minutes").arg(val));
}

void controllerSetup::disableClicked(QString path, bool clicked, QWidget* widget)
{
    // Disable checkbox has been clicked
    emit programDisable(path,clicked);
    widget->setEnabled(!clicked);
}
