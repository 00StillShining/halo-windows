#include <doctest/doctest.h>

#include "Core/Ep40EntityNames.h"
#include "Core/Ep40MidiMapping.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace halo;

TEST_CASE ("entity names: 43 unique contract names plus the root")
{
    std::set<std::string_view> unique (ep40::entityNames.begin(), ep40::entityNames.end());
    CHECK (ep40::entityNames.size() == 43);
    CHECK (unique.size() == 43);
    CHECK (unique.count (ep40::rootEntityName) == 0);
    CHECK (ep40::isContractName ("ep40_root"));
    CHECK (ep40::isContractName ("pad_enter"));
    CHECK_FALSE (ep40::isContractName ("status_lamp_0_0"));
}

TEST_CASE ("entity names: the MIDI mapping's part names are all in the contract")
{
    for (auto name : ep40::gridPartNames)
        CHECK (ep40::isContractName (name));
    for (auto name : ep40::groupPartNames)
        CHECK (ep40::isContractName (name));
}

TEST_CASE ("entity names: match the Mac app's EP40EntityNames.swift")
{
#ifdef HALO_MAC_REFERENCE_DIR
    const std::string path = std::string (HALO_MAC_REFERENCE_DIR) + "/Halo/Reality/EP40EntityNames.swift";
    std::ifstream in (path);
    if (! in)
    {
        MESSAGE ("EP40EntityNames.swift not found at " << path << "; cross-check skipped");
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const auto text = buffer.str();

    // case chassisBase     = "chassis_base"
    const std::regex pattern (R"re(case\s+\w+\s*=\s*"([^"]+)")re");
    std::vector<std::string> swift;
    for (auto it = std::sregex_iterator (text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
        swift.push_back ((*it)[1].str());

    REQUIRE (swift.size() == 44);
    CHECK (swift.front() == ep40::rootEntityName);
    swift.erase (swift.begin());
    for (size_t i = 0; i < swift.size(); ++i)
        CHECK (swift[i] == ep40::entityNames[i]);
#else
    MESSAGE ("HALO_MAC_REFERENCE_DIR not set; cross-check skipped");
#endif
}
