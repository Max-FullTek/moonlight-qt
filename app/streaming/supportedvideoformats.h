#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <Limelight.h>
#include <SDL_assert.h>

class SupportedVideoFormatList
{
public:
    operator int() const
    {
        int value = 0;

        for (const int v : m_Formats) {
            value |= v;
        }

        return value;
    }

    void append(int format) { m_Formats.push_back(format); }
    bool empty() const { return m_Formats.empty(); }
    int front() const { return m_Formats.front(); }
    void removeFirst() { m_Formats.erase(m_Formats.begin()); }

    void removeByMask(int mask)
    {
        m_Formats.erase(std::remove_if(m_Formats.begin(), m_Formats.end(), [mask](int format) {
            return (format & mask) != 0;
        }), m_Formats.end());
    }

    void deprioritizeByMask(int mask)
    {
        std::stable_partition(m_Formats.begin(), m_Formats.end(), [mask](int format) {
            return (format & mask) == 0;
        });
    }

    int maskByServerCodecModes(int serverCodecModes) const
    {
        int mask = 0;

        static constexpr std::pair<int, int> mapping[] = {
            {SCM_H264, VIDEO_FORMAT_H264},
            {SCM_H264_HIGH8_444, VIDEO_FORMAT_H264_HIGH8_444},
            {SCM_HEVC, VIDEO_FORMAT_H265},
            {SCM_HEVC_MAIN10, VIDEO_FORMAT_H265_MAIN10},
            {SCM_HEVC_REXT8_444, VIDEO_FORMAT_H265_REXT8_444},
            {SCM_HEVC_REXT10_444, VIDEO_FORMAT_H265_REXT10_444},
            {SCM_AV1_MAIN8, VIDEO_FORMAT_AV1_MAIN8},
            {SCM_AV1_MAIN10, VIDEO_FORMAT_AV1_MAIN10},
            {SCM_AV1_HIGH8_444, VIDEO_FORMAT_AV1_HIGH8_444},
            {SCM_AV1_HIGH10_444, VIDEO_FORMAT_AV1_HIGH10_444},
        };

        for (const auto& [mode, format] : mapping) {
            if (serverCodecModes & mode) {
                mask |= format;
                serverCodecModes &= ~mode;
            }
        }

        // Make sure nobody forgets to update this for new SCM values
        SDL_assert(serverCodecModes == 0);

        return int(*this) & mask;
    }

private:
    std::vector<int> m_Formats;
};
