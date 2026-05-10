#ifndef JS8_SUBMODE_HPP_
#define JS8_SUBMODE_HPP_

#include "JS8_Mode/JS8.h"

#include <QString>

#include <stdexcept>

namespace JS8::Submode {
// Exception thrown on unexpected errors, principally, handing
// us a submode that we don't understand, which seems like your
// problem, not ours.

struct error : public std::runtime_error {
    explicit error(QString const &what)
        : std::runtime_error(what.toStdString()) {}
};

// Functions that, when provided with a valid submode, return
// constant data specific to the submode. Each of those functions
// will throw if provided with an invalid JS8 submode.

// Be careful when doing arithmetic with values that are unsigned.
// Something seemingly harmless such as 3 - samplesForOneSymbol
// might end up as 4,294,965,379.

// One way to see the actual values of the various modes:
// Running JS8Call with the environment variable
// QT_LOGGING_RULES set to the value js8submode.js8=true
// will output a list of them to STDERR.

/** Name of the submode, in all uppercase letters. */
QString name(int);
unsigned int bandwidth(int);
Costas::Type costas(int);

/**
 * How long from one transmission start to the next transmission start,
 * in seconds.
 *
 * This is the usual number a typical users knows: 30 s for SLOW, 6 s for JS8 40 (formerly "Turbo").
 */
unsigned int period(int);

/** How many audio samples (at 12000 samples per second) it takes to transfer
 * one symbol. */
unsigned int samplesForOneSymbol(int);

/**
 * How many audio samples we use to transmit the symbols
 * of one period.
 *
 * As there are JS8_NUM_SYMBOLS = 79 symbols in a period,
 * this is 79 * samplesForOneSymol.
 */
unsigned int samplesForSymbols(int);

/**
 * The number of samples needed to capture the entire TX duration,
 * including the initial start delay, plus another 500 ms.
 */
unsigned int samplesNeeded(int);

/**
 * How many audio samples at 12000 samples per second are
 * needed for one period in this mode.
 */
unsigned int samplesPerPeriod(int);
int rxSNRThreshold(int);
int rxThreshold(int);

/** How long to wait after tx start before actually sending data, in
 * milliseconds. */
unsigned int startDelayMS(int);
double toneSpacing(int);

/**
 * Duration in seconds that it takes to transmit the symbols.
 *
 * This is samplesForSymbols / 12000.
 */
double dataDuration(int);

/**
 * Actual duration of the transmission, in seconds.
 *
 * This is samplesForSymbols / 12000 + startDelayMS / 1000
 */
double txDuration(int);

// Functions that, when provided with a valid submode and additional
// parametric data, compute and return results specific to the submode.
// Each of these functions will throw if provided with an invalid JS8
// submode.

int computeCycleForDecode(int, int);
int computeAltCycleForDecode(int, int, int);
double computeRatio(int, double);
} // namespace JS8::Submode

#endif // JS8_SUBMODE_HPP_
