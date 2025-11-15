#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

/* QT Headers */
#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>
#include <QThread>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QTimer>

/* QT Audio Headers */
#include <QAudioOutput>
#include <QAudioFormat>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#else
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioSink>
#endif

#include <QIODevice>


/* wfview Packet types */
#include "packettypes.h"

/* Logarithmic taper for volume control */
#include "audiotaper.h"

#include "audiohandlerbase.h"
#include "audiohandlerqtinput.h"
#include "audiohandlerqtoutput.h"
#include "audiohandlerrtinput.h"
#include "audiohandlerrtoutput.h"
#include "audiohandlerpainput.h"
#include "audiohandlerpaoutput.h"
#include "audiohandlertciinput.h"
#include "audiohandlertcioutput.h"

/* Audio converter class*/
#include "audioconverter.h"


#endif // AUDIOHANDLER_H
