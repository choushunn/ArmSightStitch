#pragma once

#include "IArmController.h"

namespace arm {

/// Move all 5 axes concurrently (all axes move simultaneously)
inline bool moveAllAxes(IArmController& arm, int x, int y, int z, int a, int b) {
    return arm.moveAxesConcurrent(x, y, z, a, b);
}

/// Move only X and Y axes (concurrent: both axes move simultaneously)
inline bool moveXYAxes(IArmController& arm, int x, int y) {
    return arm.moveXYAxes(x, y);
}

/// Move all 5 axes to the values in a SMovementPoint
inline bool moveToPoint(IArmController& arm, const SMovementPoint& pt) {
    return moveAllAxes(arm, pt.x, pt.y, pt.z, pt.a, pt.b);
}

} // namespace arm
