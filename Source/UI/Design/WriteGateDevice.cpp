// The write gate's device side: the one place in Source/UI that calls the
// Ep40Writer's write methods (checked by tools/dev/check_write_sites.py).
// Only GatedWriter reaches it, and only after a confirmed ConfirmCard.
#include "UI/Design/WriteGate.h"

namespace halo
{
    namespace
    {
        class Ep40WriterAdapter : public DeviceWrites
        {
        public:
            explicit Ep40WriterAdapter (Ep40Writer& w) : writer (w) {}

            void upload (const AudioData& audio, const juce::String& name, int slot, std::function<void (Ep40Writer::Result)> done,
                         Ep40Writer::BytesProgress onBytes) override
            {
                writer.upload (audio, name, slot, std::move (done), std::move (onBytes));
            }

            void uploadToPads (std::vector<Ep40Writer::PadJob> jobs, int project, std::function<void (const juce::String&)> onProgress,
                               std::function<void (Ep40Writer::Result)> done) override
            {
                writer.uploadToPads (std::move (jobs), project, std::move (onProgress), std::move (done));
            }

            void assignPad (int project, int group, int gridIndex, int slot, std::function<void (Ep40Writer::Result)> done) override
            {
                writer.assignPad (project, group, gridIndex, slot, std::move (done));
            }

            void deleteSound (int slot, std::function<void (Ep40Writer::Result)> done) override
            {
                writer.deleteSound (slot, std::move (done));
            }

            bool isBusy() const override { return writer.isBusy(); }

        private:
            Ep40Writer& writer;
        };
    }

    std::unique_ptr<DeviceWrites> makeDeviceWrites (Ep40Writer& w)
    {
        return std::make_unique<Ep40WriterAdapter> (w);
    }
}
