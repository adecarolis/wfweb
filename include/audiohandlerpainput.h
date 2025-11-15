#ifndef AUDIOHANDLERPAINPUT_H
#define AUDIOHANDLERPAINPUT_H

#include "audiohandlerbase.h"
#include <portaudio.h>
#include <memory>
#include "bytering.h"

class audioHandlerPaInput : public audioHandlerBase {
    Q_OBJECT

public:
    explicit audioHandlerPaInput(QObject* parent=nullptr);
    ~audioHandlerPaInput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Input"); }

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private slots:
    void onReadyRead();
    void onConverted(audioPacket audio);

private:
    static int paCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags statusFlags, void* user);
    void handleCallbackInput(const void* input, unsigned long frameCount, PaStreamCallbackFlags statusFlags);

    PaStream* stream=nullptr;
    unsigned bytesPerSample{2};
    unsigned bytesPerFrame{2};
    std::unique_ptr<ByteRing> inRB;

};
#endif // AUDIOHANDLERPAINPUT_H
