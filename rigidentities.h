#ifndef RIGIDENTITIES_H
#define RIGIDENTITIES_H

// Credit:
// http://www.docksideradio.com/Icom%20Radio%20Hex%20Addresses.htm

// 7850 and 7851 have the same commands and are essentially identical

enum model_kind {
    model7100 = 0x88,
    model7200 = 0x76,
    model7300 = 0x94,
    model7600 = 0x7A,
    model7610 = 0x98,
    model7700 = 0x74,
    model7800 = 0x6A,
    model7850 = 0x8E,
    modelUnknown = 0xFF
};


model_kind determineRadioModel(unsigned char rigID);



#endif // RIGIDENTITIES_H
