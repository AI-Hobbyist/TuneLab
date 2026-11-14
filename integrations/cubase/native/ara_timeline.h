#pragma once

#include "ARA_API/ARAInterface.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace TuneLab::Cubase
{
// ARA tempo entries are sync points, not BPM samples. Preserve their song
// origin and extrapolate the first/last segment as required by ARA.
class AraTimeline
{
  public:
    explicit AraTimeline(std::vector<ARA::ARAContentTempoEntry> points,
                         std::vector<ARA::ARAContentBarSignature> signatures = {})
        : points(std::move(points)), signatures(std::move(signatures))
    {
        if (this->points.size() < 2)
            throw std::invalid_argument("ARA tempo map requires two sync points");
        bool hasOrigin = false;
        for (std::size_t index = 0; index < this->points.size(); ++index)
        {
            const auto &point = this->points[index];
            if (!std::isfinite(point.timePosition) || !std::isfinite(point.quarterPosition) ||
                (index != 0 && (point.timePosition <= this->points[index - 1].timePosition ||
                                point.quarterPosition <= this->points[index - 1].quarterPosition)))
                throw std::invalid_argument("ARA tempo sync points must be finite and increasing");
            hasOrigin = hasOrigin || point.quarterPosition == 0;
        }
        if (!hasOrigin)
            throw std::invalid_argument("ARA full tempo map requires a quarter-zero sync point");
        for (std::size_t index = 0; index < this->signatures.size(); ++index)
        {
            const auto &signature = this->signatures[index];
            if (!std::isfinite(signature.position) || signature.numerator <= 0 ||
                signature.denominator <= 0)
                throw std::invalid_argument(
                    "ARA bar signature must have finite position and positive meter");
            if (index == 0)
                continue;
            const auto &previous = this->signatures[index - 1];
            const double bars = (signature.position - previous.position) * previous.denominator /
                                (4.0 * previous.numerator);
            if (bars <= 0 || std::abs(bars - std::round(bars)) > 1e-8)
                throw std::invalid_argument(
                    "ARA bar signature changes must lie on increasing bar boundaries");
        }
    }

    double quarterAtSeconds(double seconds) const
    {
        const auto index = segment(seconds, [](const auto &point) { return point.timePosition; });
        const auto &left = points[index];
        const auto &right = points[index + 1];
        return left.quarterPosition + (seconds - left.timePosition) *
                                          (right.quarterPosition - left.quarterPosition) /
                                          (right.timePosition - left.timePosition);
    }
    double secondsAtQuarter(double quarter) const
    {
        const auto index =
            segment(quarter, [](const auto &point) { return point.quarterPosition; });
        const auto &left = points[index];
        const auto &right = points[index + 1];
        return left.timePosition + (quarter - left.quarterPosition) *
                                       (right.timePosition - left.timePosition) /
                                       (right.quarterPosition - left.quarterPosition);
    }
    const auto &syncPoints() const noexcept
    {
        return points;
    }
    const auto &barSignatures() const noexcept
    {
        return signatures;
    }

  private:
    template <typename Coordinate> std::size_t segment(double value, Coordinate coordinate) const
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("ARA timeline position must be finite");
        const auto next = std::upper_bound(
            points.begin(), points.end(), value,
            [&](double position, const auto &point) { return position < coordinate(point); });
        if (next == points.begin())
            return 0;
        return (std::min)(static_cast<std::size_t>(next - points.begin() - 1), points.size() - 2);
    }
    std::vector<ARA::ARAContentTempoEntry> points;
    std::vector<ARA::ARAContentBarSignature> signatures;
};
} // namespace TuneLab::Cubase
