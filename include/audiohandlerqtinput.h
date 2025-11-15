#ifndef AUDIOHANDLERQTINPUT_H
#define AUDIOHANDLERQTINPUT_H
#include "audiohandlerbase.h"


class audioHandlerQtInput : public audioHandlerBase
{
    Q_OBJECT

public:
    explicit audioHandlerQtInput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerQtInput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Input"); }

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private:
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioInput*     audioInput  {nullptr};
#else
    QAudioSource*    audioInput  {nullptr};
#endif

    QIODevice*       audioDevice {nullptr};

private slots:
    void onReadyRead();
    void onConverted(audioPacket pkt);

};

#endif // AUDIOHANDLERQTINPUT_H
