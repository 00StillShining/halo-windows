#pragma once

#include "App/HaloPaths.h"

#include <juce_data_structures/juce_data_structures.h>

namespace halo
{
    // Small persisted preferences (port of the Mac's UserDefaults use, DD-027):
    // %APPDATA%\Halo\settings.xml (HaloPaths, so a Debug HALO_DATA_ROOT sandboxes
    // it). Only conveniences live here; nothing about the device is ever cached
    // as truth.
    class Settings
    {
    public:
        Settings()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "settings";
            options.folderName = "Halo";
            options.filenameSuffix = ".xml";
            options.osxLibrarySubFolder = "Application Support";
            options.storageFormat = juce::PropertiesFile::storeAsXML;
            file = std::make_unique<juce::PropertiesFile> (HaloPaths::settingsFile(), options);
        }

        int getInt (const juce::String& key, int fallback) const { return file->getIntValue (key, fallback); }
        double getDouble (const juce::String& key, double fallback) const { return file->getDoubleValue (key, fallback); }
        juce::String getString (const juce::String& key) const { return file->getValue (key); }

        void set (const juce::String& key, const juce::var& value)
        {
            file->setValue (key, value);
            file->saveIfNeeded();
        }

    private:
        std::unique_ptr<juce::PropertiesFile> file;
    };
}
