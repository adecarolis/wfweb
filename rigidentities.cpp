#include "rigidentities.h"
#include "logcategories.h"

// Copytight 2017-2021 Elliott H. Liggett

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
        case modelR8600:
            rig = modelR8600;
            break;
        case model7000:
            rig = model7000;
            break;
        case model7410:
            rig = model7410;
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
        case model9700:
            rig = model9700;
            break;
        case model706:
            rig = model706;
            break;
        case model705:
            rig = model705;
            break;
        case model718:
            rig = model718;
            break;
        case model736:
            rig = model736;
            break;
        case model746:
            rig = model746;
            break;
        case model756pro:
            rig = model756pro;
            break;
        case model756proii:
            rig = model756proii;
            break;
        case model756proiii:
            rig = model756proiii;
            break;
        case model910h:
            rig = model910h;
            break;
        case model9100:
            rig = model9100;
            break;
        default:
            rig = modelUnknown;
            break;
    }

    return rig;
}





