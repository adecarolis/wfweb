#include "rigidentities.h"
#include "logcategories.h"

// Copyright 2017-2021 Elliott H. Liggett

model_kind determineRadioModel(unsigned char rigID)
{

    model_kind rig;
    rig = (model_kind)rigID;

    return rig;
}
