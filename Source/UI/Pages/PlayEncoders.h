#pragma once

#include "App/AppModel.h"
#include "Stage/StageComponent.h"
#include "UI/Design/EncoderBus.h"

namespace halo
{
    // PLAY's four encoders (docs/redesign/plan.md section 3.5):
    //   1 blue   LEVEL (silence floor...0 db, default -12)   press: monitor on/off
    //   2 ochre  GRAB length (3 steps: bars with a clock, else seconds)   press: grab now
    //   3 grey   ORBIT yaw (shift: tilt)   press: reset view
    //   4 orange ECHO MIX   press: engage / bypass the rack
    // ORBIT drives the stage's camera through its small UI camera API; its
    // readout follows drags on the stage too (one camera, one readout).
    std::array<std::optional<EncoderBinding>, 4> playBindings (AppModel&, StageComponent&);

    // Shared with CAPTURE: the one monitor LEVEL and the GRAB length.
    EncoderBinding levelBinding (AppModel&);
    EncoderBinding grabBinding (AppModel&, int encoderIndex);
}
