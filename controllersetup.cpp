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
    delete ui;
}

void controllerSetup::hideEvent(QHideEvent *event)
{
    qDebug(logUsbControl()) << "Controller window hideEvent()";
    updateDialog->hide();
}

void controllerSetup::on_tabWidget_currentChanged(int index)
{
    if (updateDialog != Q_NULLPTR)
        updateDialog->hide();
}


void controllerSetup::init()
{

    updateDialog = new QDialog(this);
    // Not sure if I like it Frameless or not?
    updateDialog->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);

    QGridLayout* udLayout = new QGridLayout;
    updateDialog->setLayout(udLayout);
    updateDialog->setBaseSize(1, 1);

    onLabel = new QLabel("On");
    udLayout->addWidget(onLabel,0,0);
    onEvent = new QComboBox();
    udLayout->addWidget(onEvent,0,1);
    onLabel->setBuddy(onEvent);

    offLabel = new QLabel("Off");
    udLayout->addWidget(offLabel,1,0);
    offEvent = new QComboBox();
    udLayout->addWidget(offEvent,1,1);
    offLabel->setBuddy(offEvent);

    knobLabel = new QLabel("Knob");
    udLayout->addWidget(knobLabel,2,0);
    knobEvent = new QComboBox();
    udLayout->addWidget(knobEvent,2,1);
    knobLabel->setBuddy(knobEvent);

    buttonLatch = new QCheckBox();
    buttonLatch->setText("Toggle");
    udLayout->addWidget(buttonLatch,3,0);

    QHBoxLayout* colorLayout = new QHBoxLayout();
    udLayout->addLayout(colorLayout,3,1);

    buttonOnColor = new QPushButton("Color");
    colorLayout->addWidget(buttonOnColor);

    buttonOffColor = new QPushButton("Pressed");
    colorLayout->addWidget(buttonOffColor);

    buttonIcon = new QPushButton("Icon");
    udLayout->addWidget(buttonIcon,4,0);
    iconLabel = new QLabel("<None>");
    udLayout->addWidget(iconLabel,4,1);
    udLayout->setAlignment(iconLabel,Qt::AlignCenter);

    udLayout->setSizeConstraint(QLayout::SetFixedSize);

    updateDialog->hide();

    connect(offEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(offEventIndexChanged(int)));
    connect(onEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(onEventIndexChanged(int)));
    connect(knobEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(knobEventIndexChanged(int)));
    connect(buttonOnColor, SIGNAL(clicked()), this, SLOT(buttonOnColorClicked()));
    connect(buttonOffColor, SIGNAL(clicked()), this, SLOT(buttonOffColorClicked()));
    connect(buttonIcon, SIGNAL(clicked()), this, SLOT(buttonIconClicked()));
    connect(buttonLatch, SIGNAL(stateChanged(int)), this, SLOT(latchStateChanged(int)));

}
void controllerSetup::mousePressed(controllerScene* scene, QPoint p)
{
    Q_UNUSED (scene) // We might want it in the future?

    // Receive mouse event from the scene
    qDebug() << "Looking for knob or button at Point x=" << p.x() << " y=" << p.y();
        
    QPoint gp = this->mapToGlobal(p);

    // Did the user click on a button?
    auto b = std::find_if(buttons->begin(), buttons->end(), [p, this](BUTTON& b)
    { return (b.parent != Q_NULLPTR && b.pos.contains(p) && b.page == b.parent->currentPage && ui->tabWidget->currentWidget()->objectName() == b.path ); });

    if (b != buttons->end())
    {
        currentButton = b;
        currentKnob = Q_NULLPTR;
        qDebug() << "Button" << currentButton->num << "On Event" << currentButton->onCommand->text << "Off Event" << currentButton->offCommand->text;

        updateDialog->setWindowTitle(QString("Update button %0").arg(b->num));

        onEvent->blockSignals(true);
        onEvent->setCurrentIndex(onEvent->findData(currentButton->onCommand->index));
        onEvent->show();
        onLabel->show();
        onEvent->blockSignals(false);

        offEvent->blockSignals(true);
        offEvent->setCurrentIndex(offEvent->findData(currentButton->offCommand->index));
        offEvent->show();
        offLabel->show();
        offEvent->blockSignals(false);
        knobEvent->hide();
        knobLabel->hide();

        buttonLatch->blockSignals(true);
        buttonLatch->setChecked(currentButton->toggle);
        buttonLatch->blockSignals(false);
        buttonLatch->show();

        switch (currentButton->parent->type.model)
        {
        case StreamDeckMini:
        case StreamDeckMiniV2:
        case StreamDeckOriginal:
        case StreamDeckOriginalV2:
        case StreamDeckOriginalMK2:
        case StreamDeckXL:
        case StreamDeckXLV2:
        case StreamDeckPlus:
            buttonOnColor->setStyleSheet(QString("background-color: %1").arg(currentButton->backgroundOn.name(QColor::HexArgb)));
            buttonOffColor->setStyleSheet(QString("background-color: %1").arg(currentButton->backgroundOff.name(QColor::HexArgb)));
            buttonOnColor->show();
            buttonOffColor->show();
            buttonIcon->show();
            iconLabel->setText(currentButton->iconName);
            iconLabel->show();
            break;
        default:
            buttonOnColor->hide();
            buttonOffColor->hide();
            buttonIcon->hide();
            iconLabel->hide();
            break;
        }

        updateDialog->show();
        updateDialog->move(gp);
        updateDialog->adjustSize();
        updateDialog->raise();
    } else {
        // It wasn't a button so was it a knob?
        auto k = std::find_if(knobs->begin(), knobs->end(), [p, this](KNOB& k)
        { return (k.parent != Q_NULLPTR && k.pos.contains(p) && k.page == k.parent->currentPage && ui->tabWidget->currentWidget()->objectName() == k.path ); });

        if (k != knobs->end())
        {
            currentKnob = k;
            currentButton = Q_NULLPTR;
            qDebug() << "Knob" << currentKnob->num << "Event" << currentKnob->command->text;

            updateDialog->setWindowTitle(QString("Update knob %0").arg(k->num));

            knobEvent->blockSignals(true);
            knobEvent->setCurrentIndex(knobEvent->findData(currentKnob->command->index));
            knobEvent->show();
            knobLabel->show();
            knobEvent->blockSignals(false);
            onEvent->hide();
            offEvent->hide();
            onLabel->hide();
            offLabel->hide();
            buttonLatch->hide();
            buttonOnColor->hide();
            buttonOffColor->hide();
            buttonIcon->hide();
            iconLabel->hide();

            updateDialog->show();
            updateDialog->move(gp);
            updateDialog->adjustSize();
            updateDialog->raise();
        }
        else
        {
            // It wasn't either so hide the updateDialog();
            updateDialog->hide();
            currentButton = Q_NULLPTR;
            currentKnob = Q_NULLPTR;
        }
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
        emit sendRequest(currentButton->parent,usbFeatureType::featureButton,currentButton->num,currentButton->onCommand->text,Q_NULLPTR,&currentButton->backgroundOn);
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


void controllerSetup::buttonOnColorClicked()
{
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, false);
    options.setFlag(QColorDialog::DontUseNativeDialog, false);
    QColor selColor = QColorDialog::getColor(currentButton->backgroundOn, this, "Select Color to use for unpressed button", options);

    if(selColor.isValid() && currentButton != Q_NULLPTR)
    {
        QMutexLocker locker(mutex);
        currentButton->backgroundOn = selColor;
        buttonOnColor->setStyleSheet(QString("background-color: %1").arg(currentButton->backgroundOn.name(QColor::HexArgb)));
        emit sendRequest(currentButton->parent,usbFeatureType::featureButton,currentButton->num,currentButton->onCommand->text,currentButton->icon,&currentButton->backgroundOn);
    }
}

void controllerSetup::buttonOffColorClicked()
{
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, false);
    options.setFlag(QColorDialog::DontUseNativeDialog, false);
    QColor selColor = QColorDialog::getColor(currentButton->backgroundOff, this, "Select Color to use for pressed button", options);

    if(selColor.isValid() && currentButton != Q_NULLPTR)
    {
        QMutexLocker locker(mutex);
        currentButton->backgroundOff = selColor;
        buttonOffColor->setStyleSheet(QString("background-color: %1").arg(currentButton->backgroundOff.name(QColor::HexArgb)));
    }
}

void controllerSetup::buttonIconClicked()
{
    QString file = QFileDialog::getOpenFileName(this,"Select Icon Filename",".","Images (*png *.jpg)");
    if (!file.isEmpty()) {
        QFileInfo info = QFileInfo(file);
        currentButton->iconName = info.fileName();
        iconLabel->setText(currentButton->iconName);
        QImage image;
        image.load(file);
        if (currentButton->icon != Q_NULLPTR)
            delete currentButton->icon;
        currentButton->icon = new QImage(image.scaled(currentButton->parent->type.iconSize,currentButton->parent->type.iconSize));
        emit sendRequest(currentButton->parent,usbFeatureType::featureButton,currentButton->num,currentButton->onCommand->text,currentButton->icon, &currentButton->backgroundOn);
    }
}

void controllerSetup::latchStateChanged(int state)
{
    if (currentButton != Q_NULLPTR) {
        QMutexLocker locker(mutex);
        currentButton->toggle=(int)state;
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
        [dev,this,widget](bool checked) { this->disableClicked(dev,checked,widget); });

    disabled->setChecked(dev->disabled);

    QGraphicsView *view = new QGraphicsView();
    layout->addWidget(view);

    QSpinBox *page = new QSpinBox();
    page->setValue(1);
    page->setMinimum(1);
    page->setMaximum(dev->pages);
    page->setToolTip("Select current page to edit");
    layout->addWidget(page,0,Qt::AlignBottom | Qt::AlignRight);
    dev->pageSpin = page;


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
        [dev,this](int val) { this->sensitivityMoved(dev,val); });

    QImage image;

    switch (dev->type.model) {
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
        case StreamDeckOriginal:
        case StreamDeckOriginalV2:
        case StreamDeckOriginalMK2:
            image.load(":/resources/streamdeck.png");
            break;
        case StreamDeckMini:
        case StreamDeckMiniV2:
            image.load(":/resources/streamdeckmini.png");
            break;
        case StreamDeckXL:
        case StreamDeckXLV2:
            image.load(":/resources/streamdeckxl.png");
            break;
        case StreamDeckPlus:
            image.load(":/resources/streamdeckplus.png");
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
    dev->scene = scene;
    view->setScene(scene);
    connect(scene, SIGNAL(mousePressed(controllerScene*,QPoint)), this, SLOT(mousePressed(controllerScene*,QPoint)));
    scene->addItem(bgImage);


    QGridLayout* grid = new QGridLayout();
    layout->addLayout(grid);

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
        [dev,this](int index) { this->brightnessChanged(dev,index); });

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
        [dev,this](int index) { this->speedChanged(dev,index); });

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
        [dev,this](int index) { this->orientationChanged(dev,index); });

    QLabel* colorlabel = new QLabel("Color");
    grid->addWidget(colorlabel,0,3);
    QPushButton* color = new QPushButton("Select");
    grid->addWidget(color,1,3);
    connect(color, &QPushButton::clicked,
        [dev,this]() { this->colorPicker(dev); });

    QLabel* timeoutlabel = new QLabel("Timeout");
    grid->addWidget(timeoutlabel,0,4);
    QSpinBox *timeout = new QSpinBox();
    timeout->setValue(cntrl->timeout);
    grid->addWidget(timeout,1,4);
    connect(timeout, qOverload<int>(&QSpinBox::valueChanged),
        [dev,this](int index) { this->timeoutChanged(dev,index); });

    QLabel* pageslabel = new QLabel("Pages");
    grid->addWidget(pageslabel,0,5);
    QSpinBox *pages = new QSpinBox();
    pages->setValue(dev->pages);
    pages->setMinimum(1);
    grid->addWidget(pages,1,5);
    connect(pages, qOverload<int>(&QSpinBox::valueChanged),
        [dev,this](int index) { this->pagesChanged(dev,index); });
    for (int i=0;i<6;i++)
        grid->setColumnStretch(i,1);

    QLabel *helpText = new QLabel();
    helpText->setText("<p><b>Button configuration:</b> Right-click on each button to configure it.</p>");
    helpText->setAlignment(Qt::AlignCenter);
    layout->addWidget(helpText);

    onEvent->blockSignals(true);
    offEvent->blockSignals(true);
    knobEvent->blockSignals(true);

    onEvent->clear();
    offEvent->clear();
    knobEvent->clear();

    for (COMMAND& c : *commands) {
        if (c.cmdType == commandButton || c.text == "None") {
            if (c.index == -1) {
                onEvent->insertSeparator(onEvent->count());
                offEvent->insertSeparator(offEvent->count());

            } else {
                onEvent->addItem(c.text, c.index);
                offEvent->addItem(c.text, c.index);
            }
        }
        else if (c.cmdType == commandKnob || c.text == "None") {
            if (c.index == -1) {
                knobEvent->insertSeparator(knobEvent->count());
            } else {
                knobEvent->addItem(c.text, c.index);
            }
        }
    }

    onEvent->blockSignals(false);
    offEvent->blockSignals(false);
    knobEvent->blockSignals(false);

    locker.unlock();
    pageChanged(dev,1);
    locker.relock();

    view->setSceneRect(scene->itemsBoundingRect());

    // Add comboboxes to scene after everything else.

    // Attach pageChanged() here so we have access to all necessary vars
    connect(page, qOverload<int>(&QSpinBox::valueChanged),
        [dev, this](int index) { this->pageChanged(dev, index); });


    this->adjustSize();

    numTabs++;

    // Finally update the device with the default values
    emit sendRequest(dev,usbFeatureType::featureSensitivity,cntrl->sensitivity);
    emit sendRequest(dev,usbFeatureType::featureBrightness,cntrl->brightness);
    emit sendRequest(dev,usbFeatureType::featureOrientation,cntrl->orientation);
    emit sendRequest(dev,usbFeatureType::featureSpeed,cntrl->speed);
    emit sendRequest(dev,usbFeatureType::featureTimeout,cntrl->timeout);
    emit sendRequest(dev,usbFeatureType::featureColor,1,cntrl->color.name(QColor::HexArgb));

}

void controllerSetup::sensitivityMoved(USBDEVICE* dev, int val)
{
    qInfo(logUsbControl()) << "Setting sensitivity" << val <<"for device" << dev->product;
    emit sendRequest(dev,usbFeatureType::featureSensitivity,val);
}

void controllerSetup::brightnessChanged(USBDEVICE* dev, int index)
{
    emit sendRequest(dev,usbFeatureType::featureBrightness,index);
}

void controllerSetup::orientationChanged(USBDEVICE* dev, int index)
{
    emit sendRequest(dev,usbFeatureType::featureOrientation,index);
}

void controllerSetup::speedChanged(USBDEVICE* dev, int index)
{
    emit sendRequest(dev,usbFeatureType::featureSpeed,index);
}

void controllerSetup::colorPicker(USBDEVICE* dev)
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
    emit sendRequest(dev,usbFeatureType::featureColor,1,selColor.name(QColor::HexArgb));
}

void controllerSetup::timeoutChanged(USBDEVICE* dev, int val)
{
    emit sendRequest(dev,usbFeatureType::featureTimeout,val);
    emit sendRequest(dev,usbFeatureType::featureOverlay,val,QString("Sleep timeout set to %0 minutes").arg(val));
}

void controllerSetup::pagesChanged(USBDEVICE* dev, int val)
{
    emit programPages(dev,val);
    dev->pageSpin->setMaximum(val); // Update pageSpin
}

void controllerSetup::pageChanged(USBDEVICE* dev, int val)
{
    if (dev->currentPage == val) // We haven't changed page!
        return;

    if (val > dev->pages)
        val=1;
    if (val < 1)
        val = dev->pages;

    updateDialog->hide(); // Hide the dialog if the page changes.

    QMutexLocker locker(mutex);

    int lastPage = dev->currentPage;
    dev->currentPage=val;
    dev->pageSpin->setValue(val);

    // (re)set button text
    for (auto b = buttons->begin();b != buttons->end(); b++)
    {
        if (b->parent == dev)
        {
            if (b->page == lastPage)
            {
                if (b->onText != Q_NULLPTR) {
                    dev->scene->removeItem(b->onText);
                    delete b->onText;
                    b->onText = Q_NULLPTR;
                }
                if (b->offText != Q_NULLPTR) {
                    dev->scene->removeItem(b->offText);
                    delete b->offText;
                    b->offText = Q_NULLPTR;
                }
            }
            else if (b->page == dev->currentPage)
            {
                b->onText = new QGraphicsTextItem(b->onCommand->text);
                b->onText->setDefaultTextColor(b->textColour);
                dev->scene->addItem(b->onText);
                b->onText->setPos(b->pos.center().x() - b->onText->boundingRect().width() / 2,
                    (b->pos.center().y() - b->onText->boundingRect().height() / 2) - 6);
                emit sendRequest(dev,usbFeatureType::featureButton,b->num,b->onCommand->text,b->icon,&b->backgroundOn);

                b->offText = new QGraphicsTextItem(b->offCommand->text);
                b->offText->setDefaultTextColor(b->textColour);
                dev->scene->addItem(b->offText);
                b->offText->setPos(b->pos.center().x() - b->offText->boundingRect().width() / 2,
                    (b->pos.center().y() - b->onText->boundingRect().height() / 2) + 6);
            }
        }
    }
    // Set knob text

    for (auto k = knobs->begin();k != knobs->end(); k++)
    {
        if (k->parent == dev) {
            if (k->page == lastPage)
            {
                if (k->text) {
                    dev->scene->removeItem(k->text);
                    delete k->text;
                    k->text = Q_NULLPTR;
                }
            }
            else if (k->page == dev->currentPage)
            {
                k->text = new QGraphicsTextItem(k->command->text);
                k->text->setDefaultTextColor(k->textColour);
                dev->scene->addItem(k->text);
                k->text->setPos(k->pos.center().x() - k->text->boundingRect().width() / 2,
                    (k->pos.center().y() - k->text->boundingRect().height() / 2));
            }
        }
    }
}

void controllerSetup::disableClicked(USBDEVICE* dev, bool clicked, QWidget* widget)
{
    // Disable checkbox has been clicked
    emit programDisable(dev, clicked);
    widget->setEnabled(!clicked);
}

void controllerSetup::setConnected(USBDEVICE* dev)
{
    if (dev->connected)
    {
       dev->message->setStyleSheet("QLabel { color : green; }");
       dev->message->setText("Connected");
    } else {
       dev->message->setStyleSheet("QLabel { color : red; }");
       dev->message->setText("Not Connected");
    }
}
