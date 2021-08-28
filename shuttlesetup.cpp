#include "shuttlesetup.h"
#include "ui_shuttlesetup.h"
#include "logcategories.h"

shuttleSetup::shuttleSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::shuttleSetup)
{
    ui->setupUi(this);
    scene = new QGraphicsScene();
    ui->graphicsView->setScene(scene);
    textItem = scene->addText("No USB controller found");
    textItem->setDefaultTextColor(Qt::gray);
}

shuttleSetup::~shuttleSetup()
{
    delete textItem;
    delete scene;
    delete ui;

    if (bgImage != Q_NULLPTR) {
        delete bgImage;
    }
}

void shuttleSetup::newDevice(unsigned char devType)
{
    if (bgImage != Q_NULLPTR) {
        scene->removeItem(bgImage);
        delete bgImage;
        bgImage = Q_NULLPTR;
    }
    QImage image;

    switch (devType) {
        case shuttleXpress:
            image.load(":/resources/shuttlexpress.png");
            break;
        case shuttlePro2:
            image.load(":/resources/shuttlepro.png");
            break;
        case RC28:
            image.load(":/resources/rc28.png");
            break;
        default:
            textItem->show();
            return;
    }
    textItem->hide();
    bgImage = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    scene->addItem(bgImage);

    ui->graphicsView->setMinimumSize(bgImage->boundingRect().width() + 2, bgImage->boundingRect().height() + 2);
    this->resize(this->sizeHint());
}
