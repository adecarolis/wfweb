#ifndef AUDIOHANDLERTCIINPUT_H
#define AUDIOHANDLERTCIINPUT_H

#include "audiohandlerbase.h"
#include "bytering.h"

#include <memory>


class audioHandlerTciInput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerTciInput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerTciInput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Input"); }

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private slots:
    void receiveTCIAudio(const QByteArray data);
    void onReadyRead();
    void onConverted(audioPacket pkt);

private:
    unsigned bytesPerSample{2};
    unsigned bytesPerFrame{2};
    std::unique_ptr<ByteRing> inRB;

};

#endif // AUDIOHANDLERTCIINPUT_H
