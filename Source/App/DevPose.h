#pragma once

#include <juce_core/juce_core.h>

namespace halo
{
    // Debug-only shot poses (docs/redesign/plan.md section 6.1):
    // HALO_UI_POSE=card:3,shift:1,drop:1,armed:1 freezes a popup card (encoder
    // 3), the shift layer, the drag-hover state or ASSIGN ARMED, so a state
    // can be screenshotted without a mouse. Release builds always return -1.
    struct DevPose
    {
        static int get (const char* key)
        {
           #if JUCE_DEBUG
            const auto spec = juce::SystemStats::getEnvironmentVariable ("HALO_UI_POSE", {});
            for (const auto& item : juce::StringArray::fromTokens (spec, ",", {}))
                if (item.upToFirstOccurrenceOf (":", false, false).trim() == key)
                    return item.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
           #else
            juce::ignoreUnused (key);
           #endif
            return -1;
        }
    };
}
