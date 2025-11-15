#ifndef AUDIOHANDLERQTOUTPUT_H
#define AUDIOHANDLERQTOUTPUT_H
#include "audiohandlerbase.h"

class audioHandlerQtOutput : public audioHandlerBase
{
    Q_OBJECT

public:
    explicit audioHandlerQtOutput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerQtOutput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Output"); }

public slots:
    void incomingAudio(audioPacket packet);

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private:
    void writeToOutputDevice(QByteArray data, quint32 seq, float amplitudePeak, float amplitudeRms);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioOutput*    audioOutput {nullptr};
#else
    QAudioSink*      audioOutput {nullptr};
#endif

    QIODevice*       audioDevice {nullptr};

private slots:
    void onConverted(audioPacket pkt);

};

#endif // AUDIOHANDLERQTOUTPUT_H
