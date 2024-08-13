#include "rigidentities.h"
#include "logcategories.h"

// Copyright 2017-2024 Elliott H. Liggett

model_kind determineRadioModel(quint8 rigID)
{

    model_kind rig;
    rig = (model_kind)rigID;

    return rig;
}
