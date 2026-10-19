#pragma once

#include "Device/Ep40Writer.h"
#include "UI/Design/Pictograms.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    class ConfirmCard;

    // What a device write needs from the device side. Production wraps
    // Ep40Writer (WriteGateDevice.cpp); tests use a fake.
    struct DeviceWrites
    {
        virtual ~DeviceWrites() = default;
        virtual void upload (const AudioData&, const juce::String& name, int slot, std::function<void (Ep40Writer::Result)> done,
                             Ep40Writer::BytesProgress onBytes) = 0;
        virtual void uploadToPads (std::vector<Ep40Writer::PadJob>, int project, std::function<void (const juce::String&)> onProgress,
                                   std::function<void (Ep40Writer::Result)> done) = 0;
        virtual void assignPad (int project, int group, int gridIndex, int slot, std::function<void (Ep40Writer::Result)> done) = 0;
        virtual void deleteSound (int slot, std::function<void (Ep40Writer::Result)> done) = 0;
        virtual bool isBusy() const = 0;
    };

    // The adapter over the real writer (the only UI code that calls its writes).
    std::unique_ptr<DeviceWrites> makeDeviceWrites (Ep40Writer&);

    // One device write, as the confirm shows it (docs/redesign/plan.md 3.13).
    struct WriteSpec
    {
        enum class Kind { send, sendToPads, assign, deleteSound };
        Kind kind = Kind::send;
        juce::String title;         // "SEND TO EP–40?"
        juce::String body;          // today's copy, verbatim
        juce::String confirmLabel;  // "send" / "assign" / "delete"
        bool destructive = false;
        juce::String from, to;      // pictogram row captions ("longsong.wav" -> "slot 912")
        double estimateSeconds = 0; // dot timeline (0 = instant)
        juce::String estimateText;  // "about 2 min"

        static juce::String headerFor (Kind);
    };

    class GatedWriter;

    // Every device write in the UI goes through here (H3). request() opens a
    // ConfirmCard over the window; while it's open the gate is re-checked at
    // 2 Hz (a closed gate kills the confirm key and prints the reason inside
    // the card); on confirm it forces a backup rescan and checks once more,
    // and only then runs the action with a GatedWriter, which checks a last
    // time at the moment of each write.
    class WriteGate
    {
    public:
        struct Environment
        {
            std::function<bool()> writesUnlocked;      // AppModel::writesUnlocked()
            std::function<bool()> connected;
            std::function<juce::String()> backupLine;  // "backup 18 oct 2026 21:04 · complete · 477 sounds", or empty
            std::function<void()> forceRescan;         // Ep40Backup::forceRescan()
            std::function<void (bool)> setStageHidden; // airspace: the GL stage hides while a card is open
            DeviceWrites* writes = nullptr;
        };

        WriteGate (juce::Component& host, Environment);
        ~WriteGate();

        bool available() const;
        juce::String reason() const;
        juce::String backupLine() const { return env.backupLine ? env.backupLine() : juce::String(); }

        void request (WriteSpec, std::function<void (GatedWriter)> action);

        ConfirmCard* openCard() const noexcept { return card.get(); }
        void closeCard();

    private:
        friend class ConfirmCard;
        friend class GatedWriter;
        void confirmed();

        juce::Component& host;
        Environment env;
        std::unique_ptr<ConfirmCard> card;
        std::vector<std::unique_ptr<ConfirmCard>> closed; // deleted on the next message, or with the gate
        std::function<void (GatedWriter)> pending;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    // Handed to a confirmed action. Each write re-checks the gate right
    // before it reaches the device; a closed gate fails it with the reason.
    class GatedWriter
    {
    public:
        explicit GatedWriter (WriteGate& g) : gate (&g) {}

        void upload (const AudioData&, const juce::String& name, int slot, std::function<void (Ep40Writer::Result)> done,
                     Ep40Writer::BytesProgress onBytes = {}) const;
        void uploadToPads (std::vector<Ep40Writer::PadJob>, int project, std::function<void (const juce::String&)> onProgress,
                           std::function<void (Ep40Writer::Result)> done) const;
        void assignPad (int project, int group, int gridIndex, int slot, std::function<void (Ep40Writer::Result)> done) const;
        void deleteSound (int slot, std::function<void (Ep40Writer::Result)> done) const;

    private:
        bool check (const std::function<void (Ep40Writer::Result)>& done) const;
        WriteGate* gate;
    };
}
