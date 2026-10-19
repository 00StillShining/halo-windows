#include "UI/Design/Primitives/Lane.h"
#include "UI/Design/Theme.h"

namespace halo
{
    Lane::Lane (EncoderBus& b) : bus (b)
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
        bus.addListener (this);
    }

    Lane::~Lane()
    {
        bus.removeListener (this);
    }

    int Lane::cardIndex() const
    {
        if (bus.pinnedCardIndex() >= 0)
            return bus.pinnedCardIndex();
        const int i = bus.touched();
        return i >= 0 && bus.isTouched (i) && bus.binding (i) != nullptr ? i : -1;
    }

    bool Lane::showingCard() const
    {
        return cardIndex() >= 0;
    }

    void Lane::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        if (const int i = cardIndex(); i >= 0)
        {
            if (const auto* b = bus.binding (i))
            {
                // The card cuts in and out (no fade); it covers only this lane.
                popupCard::paint (g, r, *b, i, compact);
                return;
            }
        }
        if (content != nullptr)
            content (g, r);
    }
}
