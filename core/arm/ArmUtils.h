#pragma once

#include "IArmController.h"

namespace arm {

/// Move all 5 axes to the specified position values
inline bool moveAllAxes(IArmController& arm, int x, int y, int z, int a, int b) {
    bool ok = true;
    if (!arm.moveToPosition(0, x)) ok = false;
    if (!arm.moveToPosition(1, y)) ok = false;
    if (!arm.moveToPosition(2, z)) ok = false;
    if (!arm.moveToPosition(3, a)) ok = false;
    if (!arm.moveToPosition(4, b)) ok = false;
    return ok;
}

/// Move all 5 axes to the values in a SMovementPoint
inline bool moveToPoint(IArmController& arm, const SMovementPoint& pt) {
    return moveAllAxes(arm, pt.x, pt.y, pt.z, pt.a, pt.b);
}

} // namespace arm
