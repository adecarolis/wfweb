#ifndef AUDIOHANDLERPAOUTPUT_H
#define AUDIOHANDLERPAOUTPUT_H

#include "audiohandlerbase.h"
#include <portaudio.h>
#include <memory>
#include "bytering.h"

class audioHandlerPaOutput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerPaOutput(QObject* parent=nullptr);
    ~audioHandlerPaOutput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Output"); }

public slots:
    void incomingAudio(audioPacket packet);

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override ;
    virtual bool isFormatSupported(QAudioFormat f) override;

private slots:
    void onConverted(audioPacket pkt);

private:
    static int paCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags statusFlags, void* user);
    void handleCallbackOutput(void* output, unsigned long frameCount, PaStreamCallbackFlags statusFlags);

    PaStream* stream=nullptr;
    unsigned bytesPerSample{2};
    unsigned bytesPerFrame{2};
    std::unique_ptr<ByteRing> outRB;

};
#endif // AUDIOHANDLERPAOUTPUT_H
