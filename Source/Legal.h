#pragma once

// Legal / about text for SynSyn 1000 (AGPLv3 "Appropriate Legal Notices", GPLv3 section 5(d)).
// The source URL is a placeholder until the source is actually published.
namespace vf::legal
{
    inline constexpr const char* productName = "SynSyn 1000";
    inline constexpr const char* companyName = "DoubleDouble Instrumentations";
    inline constexpr const char* copyright   = "Copyright (C) 2026 DoubleDouble Instrumentations";
    // TODO(publish): replace with the real public repository URL when the source is published.
    inline constexpr const char* sourceUrl   = "[SOURCE URL PLACEHOLDER - not yet published]";

    inline const char* aboutText()
    {
        return "SynSyn 1000 by DoubleDouble Instrumentations\n"
               "Copyright (C) 2026 DoubleDouble Instrumentations\n\n"
               "This program is free software: you can redistribute it and/or modify it under the terms of the "
               "GNU Affero General Public License as published by the Free Software Foundation, either version 3 "
               "of the License, or (at your option) any later version.\n\n"
               "This program comes with ABSOLUTELY NO WARRANTY. See the LICENSE file for details.\n\n"
               "Corresponding source code: [SOURCE URL PLACEHOLDER - not yet published]\n\n"
               "Built with JUCE (AGPLv3) and the VST 3 SDK (MIT). See THIRD_PARTY_NOTICES.txt.\n"
               "VST is a registered trademark of Steinberg Media Technologies GmbH.";
    }
}
