#include "Stage/ShaderLibrary.h"

#include <HaloShaders.h>

namespace halo
{
    ShaderLibrary::ShaderLibrary (StageLog& l) : log (l)
    {
       #if JUCE_DEBUG
        const auto dir = juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_SHADER_DIR", {});
        if (dir.isNotEmpty() && juce::File (dir).isDirectory())
        {
            directory = juce::File (dir);
            lastSeen = newestOnDisk();
            log.line ("shaders: hot reload from " + directory.getFullPathName());
        }
       #endif
    }

    juce::String ShaderLibrary::raw (const juce::String& file) const
    {
        if (fromDisk())
        {
            const auto f = directory.getChildFile (file);
            if (f.existsAsFile())
                return f.loadFileAsString();
        }
        for (int i = 0; i < HaloShaders::namedResourceListSize; ++i)
            if (file == HaloShaders::originalFilenames[i])
            {
                int size = 0;
                if (const auto* data = HaloShaders::getNamedResource (HaloShaders::namedResourceList[i], size))
                    return juce::String::fromUTF8 (data, size);
            }
        log.line ("shaders: missing source " + file);
        return {};
    }

    juce::String ShaderLibrary::toneConstants() const
    {
        // HALO_TC (name, value) -> const float name = value;
        juce::String out;
        for (auto line : juce::StringArray::fromLines (raw ("ToneCurveConstants.inc")))
        {
            line = line.trim();
            if (! line.startsWith ("HALO_TC"))
                continue;
            const auto inner = line.fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
            const auto name = inner.upToFirstOccurrenceOf (",", false, false).trim();
            auto value = inner.fromFirstOccurrenceOf (",", false, false).trim();
            if (! value.containsAnyOf (".eE"))
                value << ".0";
            out << "const float " << name << " = " << value << ";\n";
        }
        return out;
    }

    void ShaderLibrary::expand (const juce::String& file, juce::StringArray& included, juce::String& out) const
    {
        for (const auto& line : juce::StringArray::fromLines (raw (file)))
        {
            const auto t = line.trim();
            if (t.startsWith ("#include"))
            {
                const auto name = t.fromFirstOccurrenceOf ("\"", false, false).upToLastOccurrenceOf ("\"", false, false);
                if (! included.contains (name))
                {
                    included.add (name);
                    expand (name, included, out);
                }
                continue;
            }
            out << line << "\n";
        }
    }

    juce::String ShaderLibrary::source (const juce::String& file, bool hdr) const
    {
        juce::String out;
        out << "#version 150\n"
            << "#define HALO_HDR " << (hdr ? 1 : 0) << "\n"
            << toneConstants();
        juce::StringArray included { file };
        expand (file, included, out);
        return out;
    }

    juce::Time ShaderLibrary::newestOnDisk() const
    {
        juce::Time newest;
        for (const auto& f : directory.findChildFiles (juce::File::findFiles, false, "*.glsl;*.vert;*.frag;*.inc"))
            newest = std::max (newest, f.getLastModificationTime());
        return newest;
    }

    bool ShaderLibrary::changed()
    {
        if (! fromDisk())
            return false;
        const auto newest = newestOnDisk();
        if (newest <= lastSeen)
            return false;
        lastSeen = newest;
        return true;
    }
}
