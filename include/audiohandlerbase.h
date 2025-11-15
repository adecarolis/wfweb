#ifndef AUDIOHANDLERBASE_H
#define AUDIOHANDLERBASE_H

// =============================
// audiohandler_base.h/.cpp (renamed to lowercase)
// =============================
#pragma once

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QAudio>
#include <QAudioFormat>
#include <QIODevice>
#include <atomic>
#include <cstring>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioInput>
#include <QAudioOutput>
#include <QAudioDeviceInfo>
#else
#include <QAudioSource>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#endif

#include "packettypes.h"
#include "audiotaper.h"
#include "audioconverter.h"
#include "logcategories.h"

class audioHandlerBase : public QObject
{
    Q_OBJECT
public:
    explicit audioHandlerBase(QObject* parent = nullptr);
    ~audioHandlerBase() override;

    int latencyMs() const noexcept { return static_cast<int>(currentLatency.load(std::memory_order_relaxed)); }
    quint16 amplitudePeak() const noexcept { return static_cast<quint16>(amplitude.load(std::memory_order_relaxed) * 255.0f); }

    void dispose();
    virtual void start();
    virtual void stop();

signals:
    void audioMessage(QString message);
    void haveAudioData(const audioPacket& data);
    void haveLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 configuredLatency, quint16 measuredLatency, bool underrun, bool overrun);
    void setupConverter(QAudioFormat in, codecType codecIn, QAudioFormat out, codecType codecOut, quint8 opusApp, quint8 resampQuality);
    void sendToConverter(audioPacket audio);

public slots:
    bool init(const audioSetup& setup);
    void setVolume(quint8 volumeIdx);
    void changeLatency(quint16 newLatencyMs);
    void stateChanged(QAudio::State state);
    void clearUnderrun();

protected:
    virtual bool openDevice() noexcept = 0;

    virtual void closeDevice() = 0;
    virtual QString role() const = 0;

    bool negotiateFormat(int minSampleRate = 48000);

    virtual QAudioFormat getNativeFormat() { qCritical(logAudio()) << "No getNativeFormat()!!"; return QAudioFormat(); };
    virtual bool isFormatSupported(QAudioFormat f) { Q_UNUSED(f) qCritical(logAudio()) << "No isFormatSupported()!"; return false; };

    void reportError(const QString& msg);

protected:
    audioSetup       setupData{};
    QAudioFormat     radioFormat;
    QAudioFormat     nativeFormat;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo deviceInfo;
#else
    QAudioDevice     deviceInfo;
#endif

    audioConverter*  converter {nullptr};
    QThread*         converterThread {nullptr};
    QTimer*          underTimer {nullptr};

    codecType        codec {codecType::LPCM};

    std::atomic<int>   currentLatency {0};
    std::atomic<float> amplitude {0.0f};
    std::atomic<bool>  isUnderrun {false};
    std::atomic<bool>  isOverrun  {false};

    QElapsedTimer    lastReceived;

    qreal            volume {1.0};

    bool             initialized {false};

    QMutex           devMutex;
    std::atomic<bool> disposed{false};
    audioPacket      tempBuf;

};


#endif // AUDIOHANDLERBASE_H
