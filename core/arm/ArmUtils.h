#pragma once

#include "IArmController.h"

namespace arm {

/// Move all 3 axes concurrently (X/Y/Z move simultaneously; A/B disabled)
inline bool moveAllAxes(IArmController& arm, int x, int y, int z) {
    return arm.moveAxesConcurrent(x, y, z);
}

/// Move only X and Y axes (concurrent: both axes move simultaneously)
inline bool moveXYAxes(IArmController& arm, int x, int y) {
    return arm.moveXYAxes(x, y);
}

/// Move X/Y/Z to the values in a SMovementPoint (A/B disabled)
inline bool moveToPoint(IArmController& arm, const SMovementPoint& pt) {
    return moveAllAxes(arm, pt.x, pt.y, pt.z);
}

} // namespace arm
