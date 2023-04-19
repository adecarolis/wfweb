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
    qInfo(logUsbControl()) << "Deleting controllerSetup() window";
    delete noControllersText;
    delete updateDialog;
    delete ui; // Will delete all content in all tabs
    tabs.clear();
    // Step through ALL buttons and knobs setting their text to NULL (just in case)
    for (auto b = buttons->begin(); b != buttons->end(); b++)
    {
        b->text=Q_NULLPTR;
        b->bgRect=Q_NULLPTR;
    }

    for (auto k= knobs->begin(); k != knobs->end(); k++)
    {
        k->text=Q_NULLPTR;
    }
}

void controllerSetup::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event)
    qDebug(logUsbControl()) << "Controller window hideEvent()";
    updateDialog->hide();
}

void controllerSetup::on_tabWidget_currentChanged(int index)
{
    // We may get the indexChanged event before the tabWidget has been initialized
    if (ui->tabWidget->widget(index) != Q_NULLPTR) {
        QString path = ui->tabWidget->widget(index)->objectName();
        auto tab = tabs.find(path);
        if (tab != tabs.end())
        {
            this->resize(this->sizeHint());
        }
    }

    if (updateDialog != Q_NULLPTR)
    {
        updateDialog->hide();
    }

}

void controllerSetup::init(usbDevMap* dev, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut)
{
    // Store pointers to all current settings
    devices = dev;
    buttons = but;
    knobs = kb;
    commands = cmd;
    mutex = mut;

    updateDialog = new QDialog(this);
    // Not sure if I like it Frameless or not?
    updateDialog->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);

    QGridLayout* udLayout = new QGridLayout(updateDialog);

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

    ledNumber = new QSpinBox();
    ledNumber->setPrefix("LED: ");
    ledNumber->setMinimum(0);
    udLayout->addWidget(ledNumber,3,1);

    buttonLatch = new QCheckBox();
    buttonLatch->setText("Toggle");
    udLayout->addWidget(buttonLatch,4,0);

    QHBoxLayout* colorLayout = new QHBoxLayout();
    udLayout->addLayout(colorLayout,4,1);

    buttonOnColor = new QPushButton("Color");
    colorLayout->addWidget(buttonOnColor);

    buttonOffColor = new QPushButton("Pressed");
    colorLayout->addWidget(buttonOffColor);

    buttonIcon = new QPushButton("Icon");
    udLayout->addWidget(buttonIcon,5,0);
    iconLabel = new QLabel("<None>");
    udLayout->addWidget(iconLabel,5,1);
    udLayout->setAlignment(iconLabel,Qt::AlignCenter);
    udLayout->setSizeConstraint(QLayout::SetFixedSize);

    updateDialog->hide();

    onEvent->clear();
    offEvent->clear();
    knobEvent->clear();

    for (COMMAND& c : *commands) {
        if (c.cmdType == commandButton || c.cmdType == commandAny) {
            if (c.command == cmdSeparator) {
                onEvent->insertSeparator(onEvent->count());
                offEvent->insertSeparator(offEvent->count());

            } else {
                onEvent->addItem(c.text, c.index);
                offEvent->addItem(c.text, c.index);
            }
        }
        if (c.cmdType == commandKnob || c.cmdType == commandAny) {
            if (c.command == cmdSeparator) {
                knobEvent->insertSeparator(knobEvent->count());
            } else {
                knobEvent->addItem(c.text, c.index);
            }
        }
    }

    connect(offEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(offEventIndexChanged(int)));
    connect(onEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(onEventIndexChanged(int)));
    connect(knobEvent, SIGNAL(currentIndexChanged(int)), this, SLOT(knobEventIndexChanged(int)));
    connect(buttonOnColor, SIGNAL(clicked()), this, SLOT(buttonOnColorClicked()));
    connect(buttonOffColor, SIGNAL(clicked()), this, SLOT(buttonOffColorClicked()));
    connect(buttonIcon, SIGNAL(clicked()), this, SLOT(buttonIconClicked()));
    connect(buttonLatch, SIGNAL(stateChanged(int)), this, SLOT(latchStateChanged(int)));
    connect(ledNumber, SIGNAL(valueChanged(int)), this, SLOT(ledNumberChanged(int)));
}

void controllerSetup::showMenu(controllerScene* scene, QPoint p)
{
    Q_UNUSED (scene) // We might want it in the future?

    // Receive mouse event from the scene
    qDebug() << "Looking for knob or button at Point x=" << p.x() << " y=" << p.y();
        
    QPoint gp = this->mapToGlobal(p);


    // Did the user click on a button?
    auto but = std::find_if(buttons->begin(), buttons->end(), [p, this](BUTTON& b)
    { return (b.parent != Q_NULLPTR && b.pos.contains(p) && b.page == b.parent->currentPage && ui->tabWidget->currentWidget()->objectName() == b.path ); });

    if (but != buttons->end())
    {
        currentButton = &(*but);
        currentKnob = Q_NULLPTR;
        qDebug() << "Button" << currentButton->num << "On Event" << currentButton->onCommand->text << "Off Event" << currentButton->offCommand->text;

        updateDialog->setWindowTitle(QString("Update button %0").arg(but->num));

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

        ledNumber->blockSignals(true);
        ledNumber->setMaximum(currentButton->parent->type.leds);
        ledNumber->setValue(currentButton->led);
        ledNumber->blockSignals(false);

        switch (currentButton->parent->type.model)
        {
        case RC28:
        case eCoderPlus:
            ledNumber->show();
            buttonOnColor->hide();
            buttonOffColor->hide();
            buttonIcon->hide();
            iconLabel->hide();
            break;
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
            ledNumber->hide();
            break;
        }

        updateDialog->show();
        updateDialog->move(gp);
        updateDialog->adjustSize();
        updateDialog->raise();
    } else {
        // It wasn't a button so was it a knob?
        auto kb = std::find_if(knobs->begin(), knobs->end(), [p, this](KNOB& k)
        { return (k.parent != Q_NULLPTR && k.pos.contains(p) && k.page == k.parent->currentPage && ui->tabWidget->currentWidget()->objectName() == k.path ); });

        if (kb != knobs->end())
        {
            currentKnob = &(*kb);
            currentButton = Q_NULLPTR;
            qDebug() << "Knob" << currentKnob->num << "Event" << currentKnob->command->text;

            updateDialog->setWindowTitle(QString("Update knob %0").arg(kb->num));

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
            ledNumber->hide();

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
        currentButton->onCommand = &commands->at(onEvent->currentData().toInt());
        currentButton->text->setPlainText(currentButton->onCommand->text);
        currentButton->text->setPos(currentButton->pos.center().x() - currentButton->text->boundingRect().width() / 2,
            (currentButton->pos.center().y() - currentButton->text->boundingRect().height() / 2));
        // Signal that any button programming on the device should be completed.
        if (currentButton->icon == Q_NULLPTR) {
            emit sendRequest(currentButton->parent,usbFeatureType::featureButton,currentButton->num,currentButton->onCommand->text,Q_NULLPTR,&currentButton->backgroundOn);
        }
    }
}


void controllerSetup::offEventIndexChanged(int index) {
    Q_UNUSED(index);
    // If command is changed, delete current command and deep copy the new command
    if (currentButton != Q_NULLPTR && offEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentButton->offCommand = &commands->at(offEvent->currentData().toInt());
    }
}

void controllerSetup::ledNumberChanged(int index) {
    if (currentButton != Q_NULLPTR) {
        QMutexLocker locker(mutex);
        currentButton->led = index;
    }
}


void controllerSetup::knobEventIndexChanged(int index) {
    Q_UNUSED(index);
    // If command is changed, delete current command and deep copy the new command
    if (currentKnob != Q_NULLPTR && knobEvent->currentData().toInt() < commands->size()) {
        QMutexLocker locker(mutex);
        currentKnob->command = &commands->at(knobEvent->currentData().toInt());
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
        if (currentButton->graphics && currentButton->bgRect != Q_NULLPTR)
        {
            currentButton->bgRect->setBrush(currentButton->backgroundOn);
        }
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
    } else {
        if (currentButton->icon != Q_NULLPTR)
        {
            currentButton->iconName = "";
            delete currentButton->icon;
            currentButton->icon = Q_NULLPTR;
        }
    }
    iconLabel->setText(currentButton->iconName);
    emit sendRequest(currentButton->parent,usbFeatureType::featureButton,currentButton->num,currentButton->onCommand->text,currentButton->icon, &currentButton->backgroundOn);
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

    /* We need to manually delete everything that has been created for this tab */
    auto tab = tabs.find(dev->path);
    if (tab == tabs.end())
    {
        qWarning(logUsbControl()) << "Cannot find tabContent for deleted tab" << dev->path;
        return;
    }

    for (auto b = buttons->begin();b != buttons->end(); b++)
    {
        if (b->parent == dev && b->page == dev->currentPage)
        {
            if (b->text != Q_NULLPTR) {
                tab.value()->scene->removeItem(b->text);
                delete b->text;
                b->text = Q_NULLPTR;
            }
            b->offCommand = Q_NULLPTR;
            b->onCommand = Q_NULLPTR;
            if (b->icon != Q_NULLPTR) {
                delete b->icon;
                b->icon=Q_NULLPTR;
            }
            if (b->bgRect != Q_NULLPTR) {
                tab.value()->scene->removeItem(b->bgRect);
                delete b->bgRect;
                b->bgRect = Q_NULLPTR;
            }

        }
    }

    for (auto k = knobs->begin();k != knobs->end(); k++)
    {
        if (k->parent == dev && k->page == dev->currentPage)
        {
            if (k->text != Q_NULLPTR) {
                tab.value()->scene->removeItem(k->text);
                delete k->text;
                k->text = Q_NULLPTR;
                k->command = Q_NULLPTR;
            }
        }
    }


    qDebug(logUsbControl()) << "Removing tab content" << dev->product;

    if (tab.value()->bgImage != Q_NULLPTR) {
        tab.value()->scene->removeItem(tab.value()->bgImage);
        delete tab.value()->bgImage;
    }
    delete tab.value()->scene;

    // Find the tab within the tabWidget
    for (int i = 0; i < ui->tabWidget->count(); i++) {
        auto widget = ui->tabWidget->widget(i);
        if (widget->objectName() == dev->path) {
            ui->tabWidget->removeTab(i);
            break;
        }
    }

    delete tab.value();
    tabs.remove(dev->path);

    // Hide the tabWidget if no tabs exist
    if (ui->tabWidget->count() == 0)
    {
        ui->tabWidget->hide();
        noControllersText->show();
        this->adjustSize();
    }
}

void controllerSetup::newDevice(USBDEVICE* dev)
{
    QMutexLocker locker(mutex);

    for (int i=0; i<ui->tabWidget->count();i++) {
        if (ui->tabWidget->widget(i)->objectName() == dev->path)
        {
            qInfo(logUsbControl()) <<"Tab for " << dev->product << "("<< dev->path << ") Already exists!";
            return;
        }
    }

    auto tab = tabs.find(dev->path);
    if (tab != tabs.end())
    {
        qInfo(logUsbControl()) <<"Tab content for " << dev->product << "("<< dev->path << ") Already exists!";
        return;
    }


    qDebug(logUsbControl()) << "Adding new tab for" << dev->product;
    noControllersText->hide();

    tabContent* c = new tabContent();

    c->tab = new QWidget();
    c->widget = new QWidget();

    c->tab->setObjectName(dev->path);
    ui->tabWidget->addTab(c->tab,dev->product);

    // Create the different layouts required
    c->mainLayout = new QVBoxLayout(c->tab);
    c->layout = new QVBoxLayout();
    c->topLayout = new QHBoxLayout();
    c->sensLayout = new QHBoxLayout();
    c->grid = new QGridLayout();

    c->mainLayout->addLayout(c->topLayout);
    c->mainLayout->addWidget(c->widget);
    c->widget->setLayout(c->layout);

    c->disabled = new QCheckBox("Disable");
    c->topLayout->addWidget(c->disabled);
    connect(c->disabled, qOverload<bool>(&QCheckBox::clicked),
        [dev,this,c](bool checked) { this->disableClicked(dev,checked,c->widget); });
    c->disabled->setChecked(dev->disabled);

    c->message = new QLabel();
    if (dev->connected) {
        c->message->setStyleSheet("QLabel { color : green; }");
        c->message->setText("Connected");
    } else {
        c->message->setStyleSheet("QLabel { color : red; }");
        c->message->setText("Not Connected");
    }
        c->topLayout->addWidget(c->message);
    c->message->setAlignment(Qt::AlignRight);

    c->view = new QGraphicsView();
    c->layout->addWidget(c->view);

    c->layout->addLayout(c->sensLayout);
    c->sensLabel = new QLabel("Sensitivity");
    c->sensLayout->addWidget(c->sensLabel);
    c->sens = new QSlider(Qt::Horizontal);
    c->sens->setMinimum(1);
    c->sens->setMaximum(21);
    c->sens->setInvertedAppearance(true);
    c->sensLayout->addWidget(c->sens);
    c->sens->setValue(dev->sensitivity);
    connect(c->sens, &QSlider::valueChanged,
        [dev,this](int val) { this->sensitivityMoved(dev,val); });

    c->sensLayout->addStretch(0);

    c->pageLabel = new QLabel("Page:");
    c->sensLayout->addWidget(c->pageLabel);
    c->page = new QSpinBox();
    c->page->setValue(1);
    c->page->setMinimum(1);
    c->page->setMaximum(dev->pages);
    c->page->setToolTip("Select current page to edit");
    c->sensLayout->addWidget(c->page);
    dev->pageSpin = c->page;

    c->image = new QImage();
    switch (dev->type.model) {
    case shuttleXpress:
        c->image->load(":/resources/shuttlexpress.png");
        break;
    case shuttlePro2:
        c->image->load(":/resources/shuttlepro.png");
        break;
    case RC28:
        c->image->load(":/resources/rc28.png");
        break;
    case xBoxGamepad:
        c->image->load(":/resources/xbox.png");
        break;
    case eCoderPlus:
        c->image->load(":/resources/ecoder.png");
        break;
    case QuickKeys:
        c->image->load(":/resources/quickkeys.png");
        break;
    case StreamDeckOriginal:
    case StreamDeckOriginalV2:
    case StreamDeckOriginalMK2:
        c->image->load(":/resources/streamdeck.png");
        break;
    case StreamDeckMini:
    case StreamDeckMiniV2:
        c->image->load(":/resources/streamdeckmini.png");
        break;
    case StreamDeckXL:
    case StreamDeckXLV2:
        c->image->load(":/resources/streamdeckxl.png");
        break;
    case StreamDeckPlus:
        c->image->load(":/resources/streamdeckplus.png");
        break;
    case StreamDeckPedal:
        c->image->load(":/resources/streamdeckpedal.png");
        break;
    default:
        this->adjustSize();
        break;
    }

    c->bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(*c->image));
    c->view->setMinimumSize(c->bgImage->boundingRect().width() + 2, c->bgImage->boundingRect().height() + 2);
    ui->tabWidget->show();

    c->scene = new controllerScene();
    c->view->setScene(c->scene);
    connect(c->scene, SIGNAL(showMenu(controllerScene*,QPoint)), this, SLOT(showMenu(controllerScene*,QPoint)));
    c->scene->addItem(c->bgImage);


    c->layout->addLayout(c->grid);

    c->brightLabel = new QLabel("Brightness");
    c->grid->addWidget(c->brightLabel,0,0);
    c->brightness = new QComboBox();
    c->brightness->addItem("Off");
    c->brightness->addItem("Low");
    c->brightness->addItem("Medium");
    c->brightness->addItem("High");
    c->brightness->setCurrentIndex(dev->brightness);
    c->grid->addWidget(c->brightness,1,0);
    connect(c->brightness, qOverload<int>(&QComboBox::currentIndexChanged),
        [dev,this](int index) { this->brightnessChanged(dev,index); });

    c->speedLabel = new QLabel("Speed");
    c->grid->addWidget(c->speedLabel,0,1);
    c->speed = new QComboBox();
    c->speed->addItem("Fastest");
    c->speed->addItem("Faster");
    c->speed->addItem("Normal");
    c->speed->addItem("Slower");
    c->speed->addItem("Slowest");
    c->speed->setCurrentIndex(dev->speed);
    c->grid->addWidget(c->speed,1,1);
    connect(c->speed, qOverload<int>(&QComboBox::currentIndexChanged),
        [dev,this](int index) { this->speedChanged(dev,index); });

    c->orientLabel = new QLabel("Orientation");
    c->grid->addWidget(c->orientLabel,0,2);
    c->orientation = new QComboBox();
    c->orientation->addItem("Rotate 0");
    c->orientation->addItem("Rotate 90");
    c->orientation->addItem("Rotate 180");
    c->orientation->addItem("Rotate 270");
    c->orientation->setCurrentIndex(dev->orientation);
    c->grid->addWidget(c->orientation,1,2);
    connect(c->orientation, qOverload<int>(&QComboBox::currentIndexChanged),
        [dev,this](int index) { this->orientationChanged(dev,index); });

    c->color = new QPushButton("Color");
    c->color->setStyleSheet(QString("background-color: %1").arg(dev->color.name(QColor::HexArgb)));
    c->grid->addWidget(c->color,1,3);
    connect(c->color, &QPushButton::clicked,
        [dev,c,this]() { this->colorPicker(dev,c->color,dev->color); });

    c->timeoutLabel = new QLabel("Timeout");
    c->grid->addWidget(c->timeoutLabel,0,4);
    c->timeout = new QSpinBox();
    c->timeout->setValue(dev->timeout);
    c->grid->addWidget(c->timeout,1,4);
    connect(c->timeout, qOverload<int>(&QSpinBox::valueChanged),
        [dev,this](int index) { this->timeoutChanged(dev,index); });

    c->pagesLabel = new QLabel("Num Pages");
    c->grid->addWidget(c->pagesLabel,0,5);
    c->pages = new QSpinBox();
    c->pages->setValue(dev->pages);
    c->pages->setMinimum(1);
    c->grid->addWidget(c->pages,1,5);
    connect(c->pages, qOverload<int>(&QSpinBox::valueChanged),
        [dev,this](int index) { this->pagesChanged(dev,index); });

    for (int i=0;i<6;i++)
        c->grid->setColumnStretch(i,1);

    c->helpText = new QLabel();
    c->helpText->setText("<p><b>Button configuration:</b> Right-click on each button to configure it.</p>");
    c->helpText->setAlignment(Qt::AlignCenter);
    c->layout->addWidget(c->helpText);


    c->view->setSceneRect(c->scene->itemsBoundingRect());

    this->adjustSize();

    numTabs++;
    tabs.insert(dev->path,c);

    dev->uiCreated = true;

    // Finally update the device with the default values
    emit sendRequest(dev,usbFeatureType::featureSensitivity,dev->sensitivity);
    emit sendRequest(dev,usbFeatureType::featureBrightness,dev->brightness);
    emit sendRequest(dev,usbFeatureType::featureOrientation,dev->orientation);
    emit sendRequest(dev,usbFeatureType::featureSpeed,dev->speed);
    emit sendRequest(dev,usbFeatureType::featureTimeout,dev->timeout);
    emit sendRequest(dev,usbFeatureType::featureColor,0,"", Q_NULLPTR, &dev->color);



    // Attach pageChanged() here so we have access to all necessary vars
    connect(c->page, qOverload<int>(&QSpinBox::valueChanged),
            [dev, this](int index) { this->pageChanged(dev, index); });

// Hide all controls that are not relevant to this controller
// We rely on being able to fallthrough case
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

    switch (dev->type.model) {
    case QuickKeys:
        break;
    case StreamDeckPedal:
        c->sensLabel->setVisible(false);
        c->sens->setVisible(false);
    case shuttleXpress:
    case shuttlePro2:
    case RC28:
    case xBoxGamepad:
    case eCoderPlus:
        c->brightLabel->setVisible(false);
        c->speedLabel->setVisible(false);
        c->timeoutLabel->setVisible(false);
        c->orientLabel->setVisible(false);
        c->brightness->setVisible(false);
        c->speed->setVisible(false);
        c->timeout->setVisible(false);
        c->orientation->setVisible(false);
        c->color->setVisible(false);
        break;
    case StreamDeckOriginal:
    case StreamDeckOriginalV2:
    case StreamDeckOriginalMK2:
    case StreamDeckMini:
    case StreamDeckMiniV2:
    case StreamDeckXL:
    case StreamDeckXLV2:
        c->sensLabel->setVisible(false);
        c->sens->setVisible(false); // No knobs!
    case StreamDeckPlus:
        c->speedLabel->setVisible(false);
        c->timeoutLabel->setVisible(false);
        c->orientLabel->setVisible(false);
        c->speed->setVisible(false);
        c->timeout->setVisible(false);
        c->orientation->setVisible(false);
        break;
    default:
        break;
    }

// Don't allow fallthrough elsewhere in the file.
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

    // pageChanged will update the buttons/knobs for the tab (using qTimer ensures mutex is unlocked first)
    QTimer::singleShot(0, this, [=]() { pageChanged(dev,1); });

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

void controllerSetup::colorPicker(USBDEVICE* dev, QPushButton* btn, QColor current)
{
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, false);
    options.setFlag(QColorDialog::DontUseNativeDialog, false);
    QColor selColor = QColorDialog::getColor(current, this, "Select Color", options);

    if(selColor.isValid())
    {
        btn->setStyleSheet(QString("background-color: %1").arg(selColor.name(QColor::HexArgb)));
        emit sendRequest(dev,usbFeatureType::featureColor,0, "", Q_NULLPTR, &selColor);
    }
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

    QMutexLocker locker(mutex);

    auto tab = tabs.find(dev->path);
    if (tab == tabs.end())
    {
        qWarning(logUsbControl()) << "Cannot find tabContent while changing page" << dev->path;
        return;
    }


    if (val > dev->pages)
        val=1;
    if (val < 1)
        val = dev->pages;

    updateDialog->hide(); // Hide the dialog if the page changes.

    dev->currentPage=val;

    // Need to block signals so this isn't called recursively.
    dev->pageSpin->blockSignals(true);
    dev->pageSpin->setValue(val);
    dev->pageSpin->blockSignals(false);

    // (re)set button text
    for (auto b = buttons->begin();b != buttons->end(); b++)
    {
        if (b->parent == dev)
        {
            // Make sure we delete any other pages content and then update to latest.
            if (b->text != Q_NULLPTR) {
                tab.value()->scene->removeItem(b->text);
                delete b->text;
                b->text = Q_NULLPTR;
            }
            if (b->graphics && b->bgRect != Q_NULLPTR) {
                tab.value()->scene->removeItem(b->bgRect);
                delete b->bgRect;
                b->bgRect = Q_NULLPTR;
            }
            if (b->page == dev->currentPage)
            {
                if (b->graphics)
                {
                    b->bgRect = new QGraphicsRectItem(b->pos);
                    b->bgRect->setBrush(b->backgroundOn);
                    tab.value()->scene->addItem(b->bgRect);
                }
                b->text = new QGraphicsTextItem(b->onCommand->text);
                b->text->setDefaultTextColor(b->textColour);
                tab.value()->scene->addItem(b->text);
                b->text->setPos(b->pos.center().x() - b->text->boundingRect().width() / 2,
                    (b->pos.center().y() - b->text->boundingRect().height() / 2));
                emit sendRequest(dev,usbFeatureType::featureButton,b->num,b->onCommand->text,b->icon,&b->backgroundOn);
            }
        }
    }
    // Set knob text

    for (auto k = knobs->begin();k != knobs->end(); k++)
    {
        if (k->parent == dev) {
            if (k->text != Q_NULLPTR) {
                tab.value()->scene->removeItem(k->text);
                delete k->text;
                k->text = Q_NULLPTR;
            }
            if (k->page == dev->currentPage)
            {
                k->text = new QGraphicsTextItem(k->command->text);
                k->text->setDefaultTextColor(k->textColour);
                tab.value()->scene->addItem(k->text);
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
    QMutexLocker locker(mutex);


    auto tab = tabs.find(dev->path);
    if (tab != tabs.end())
    {
        if (dev->connected)
        {
            tab.value()->message->setStyleSheet("QLabel { color : green; }");
            tab.value()->message->setText("Connected");
        } else {
            tab.value()->message->setStyleSheet("QLabel { color : red; }");
            tab.value()->message->setText("Not Connected");
        }
    }
}

void controllerSetup::on_backupButton_clicked()
{
    QString file = QFileDialog::getSaveFileName(this,"Select Backup Filename",".","Backup Files (*.ini)");
    if (!file.isEmpty()) {
        auto devIt = devices->find(ui->tabWidget->currentWidget()->objectName());
        if (devIt==devices->end())
        {
            qWarning(logUsbControl) << "on_restoreButton_clicked() Cannot find existing controller, aborting!";
        }
        else
        {
            emit backup(&devIt.value(), file);
        }
    }
}

void controllerSetup::on_restoreButton_clicked()
{
    QMutexLocker locker(mutex);

    QString file = QFileDialog::getOpenFileName(this,"Select Backup Filename",".","Backup Files (*.ini)");
    if (!file.isEmpty()) {
        QString path = ui->tabWidget->currentWidget()->objectName();

        auto devIt = devices->find(path);
        if (devIt==devices->end())
        {
            qWarning(logUsbControl) << "on_restoreButton_clicked() Cannot find existing controller, aborting!";
            return;
        }
        auto dev = &devIt.value();

        QSettings* settings = new QSettings(file, QSettings::Format::IniFormat);
        QString version = settings->value("Version", "").toString();
        settings->beginGroup("Controller");
        QString model = settings->value("Model","").toString();
        delete settings;

        if (model != dev->product) {
            QMessageBox msgBox;
            msgBox.setText("Stored controller does not match");
            msgBox.setInformativeText(QString("Backup: %0 \nCurrent: %1\n\nThis will probably not work!").arg(model,dev->product));
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            int ret= msgBox.exec();
            if (ret == QMessageBox::Cancel) {
                return;
            }
        }
        if (version != QString(WFVIEW_VERSION))
        {
            QMessageBox msgBox;
            msgBox.setText("Version mismatch");
            msgBox.setInformativeText(QString("Backup was from a different version of wfview\nBackup: %0 \nCurrent: %1\n\nPlease verify compatibility").arg(version, QString(WFVIEW_VERSION)));
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            int ret= msgBox.exec();
            if (ret == QMessageBox::Cancel) {
                return;
            }
        }
        emit restore(dev, file);
    }
}

