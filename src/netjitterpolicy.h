/******************************************************************************\
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Reusable, Qt-free automatic jitter-size decision logic.
\******************************************************************************/
#pragma once
#include <cstddef>

namespace NetReceive
{
struct AutoDecision
{
    int normal;
    int emergency;
    bool noneBelowEmergency;
};
// Exact stock first-acceptable-candidate selection, independent of how errors
// are accumulated. Both policies must account for Put and Get outcomes.
inline AutoDecision SelectAutoSize (const double* errors, const int* sizes, size_t count,
                                    double normalLimit, double emergencyLimit)
{
    if (!errors || !sizes || !count) return {0, 0, true};
    int normal = sizes[count - 1], emergency = sizes[count - 1];
    bool normalFound = false, emergencyFound = false;
    for (size_t i = 0; i + 1 < count; ++i)
    {
        if (!normalFound && errors[i] <= normalLimit) { normal = sizes[i]; normalFound = true; }
        if (!emergencyFound && errors[i] <= emergencyLimit) { emergency = sizes[i]; emergencyFound = true; }
    }
    return {normal, emergency, !emergencyFound};
}
inline void UpdateSmoothedSize (double& smoothed, int& selected, int decision,
                                double weightUp, double weightDown, double hysteresis)
{
    // Preserve the MathUtils::UpDownIIR1 and DecideWithHysteresis formulas.
    const double weight = (decision > smoothed) ? weightUp : weightDown;
    smoothed = smoothed * weight + decision * (1.0 - weight);
    if (smoothed > selected + 0.5 + hysteresis) ++selected;
    else if (smoothed < selected - 0.5 - hysteresis) --selected;
}
} // namespace NetReceive
