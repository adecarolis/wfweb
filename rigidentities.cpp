#include "rigidentities.h"
// Copytight 2017-2020 Elliott H. Liggett

model_kind determineRadioModel(unsigned char rigID)
{

    model_kind rig;

    switch(rigID)
    {
        case model7100:
            rig = model7100;
            break;
        case model7200:
            rig = model7200;
            break;
        case model7300:
            rig = model7300;
            break;
        case model7600:
            rig = model7600;
            break;
        case model7610:
            rig = model7610;
            break;
        case model7700:
            rig = model7700;
            break;
        case model7800:
            rig = model7800;
            break;
        case model7850:
            rig = model7850;
            break;
        default:
            rig = modelUnknown;
            break;
    }

    return rig;
}
