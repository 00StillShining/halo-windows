#pragma once

#include "App/AppModel.h"
#include "UI/Design/EncoderBus.h"

namespace halo
{
    // PLAY's four encoders (docs/redesign/plan.md section 3.5):
    //   1 blue   LEVEL (silence floor...0 db, default -12)   press: monitor on/off
    //   2 ochre  GRAB length (3 steps: bars with a clock, else seconds)   press: grab now
    //   3 grey   ORBIT yaw (shift: tilt)   press: reset view
    //   4 orange ECHO MIX   press: engage / bypass the rack
    // ORBIT needs the stage's camera setters (3D plan section 4.4 item 4);
    // until they exist it is a dead cell that says how to orbit instead.
    std::array<std::optional<EncoderBinding>, 4> playBindings (AppModel&);
}
