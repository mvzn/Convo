/*
    Convo — a convolution audio effect plugin
    Copyright (C) 2026 mvzn

    This program is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option) any
    later version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License along
    with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>

/**
    A lightweight, once-per-process "is there a newer release?" check against the
    GitHub Releases API. It runs on a background thread, times out quickly, and fails
    silently (no network / offline / HTTPS unavailable → simply no notice). The result
    lives on a shared_ptr that outlives the fetch thread, so it is safe regardless of
    editor lifetime.

    Note: HTTP is done through juce::URL, which uses the OS stack on Windows/macOS. On
    Linux with JUCE_USE_CURL=0 there is no HTTPS support, so the check is a graceful
    no-op there (set JUCE_USE_CURL=1 + link libcurl to enable it).
*/
namespace convo
{
    struct UpdateInfo
    {
        // `available` gates the strings: written (release) after the strings, read (acquire)
        // before them, so a reader that sees `available == true` also sees valid strings.
        std::atomic<bool> available { false };
        juce::String      latestVersion;   // e.g. "1.1.0"
        juce::String      releaseUrl;      // the release's html_url
    };

    namespace detail
    {
        // "1.2.3" / "v1.2.3" → {1,2,3}; missing parts default to 0.
        inline std::array<int, 3> parseVersion (juce::String v)
        {
            v = v.trim().trimCharactersAtStart ("vV");
            juce::StringArray parts;
            parts.addTokens (v, ".", "");
            std::array<int, 3> out { 0, 0, 0 };
            for (int i = 0; i < 3 && i < parts.size(); ++i)
                out[(size_t) i] = parts[i].getIntValue();
            return out;
        }

        inline bool isNewer (const juce::String& latest, const juce::String& current)
        {
            return parseVersion (latest) > parseVersion (current);
        }

        inline void fetch (std::shared_ptr<UpdateInfo> info, juce::String currentVersion)
        {
            juce::URL url ("https://api.github.com/repos/mvzn/Convo/releases/latest");
            const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                  .withConnectionTimeoutMs (5000)
                                  .withExtraHeaders ("User-Agent: Convo\nAccept: application/vnd.github+json");

            if (auto stream = url.createInputStream (opts))
            {
                const auto json    = juce::JSON::parse (stream->readEntireStreamAsString());
                const auto tag     = json.getProperty ("tag_name", {}).toString();
                const auto htmlUrl = json.getProperty ("html_url", {}).toString();

                if (tag.isNotEmpty() && isNewer (tag, currentVersion))
                {
                    info->latestVersion = tag.trimCharactersAtStart ("vV");
                    info->releaseUrl    = htmlUrl.isNotEmpty() ? htmlUrl
                                                               : juce::String ("https://github.com/mvzn/Convo/releases/latest");
                    info->available.store (true, std::memory_order_release);
                }
            }
        }
    }

    /** Shared update state; the background check is launched on the first call. */
    inline std::shared_ptr<UpdateInfo> updateInfo()
    {
        static std::shared_ptr<UpdateInfo> info = []
        {
            auto shared = std::make_shared<UpdateInfo>();
            const juce::String current (ProjectInfo::versionString);
            juce::Thread::launch ([shared, current] { detail::fetch (shared, current); });
            return shared;
        }();
        return info;
    }
}
