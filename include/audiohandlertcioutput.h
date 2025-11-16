#ifndef AUDIOHANDLERTCIOUTPUT_H
#define AUDIOHANDLERTCIOUTPUT_H

#include "audiohandlerbase.h"

#include <memory>

class audioHandlerTciOutput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerTciOutput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerTciOutput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Output"); }

public slots:
    void incomingAudio(audioPacket packet);

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

signals:
    void sendTCIAudio(const audioPacket data);

private slots:
    void onConverted(audioPacket pkt);

private:
    unsigned bytesPerSample{2};
    unsigned bytesPerFrame{2};
    QByteArray arrayBuffer;
};

#endif // AUDIOHANDLERTCIOUTPUT_H
