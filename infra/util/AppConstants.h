#pragma once

namespace infra::constants {

// ── UI sizing ──
inline constexpr int kThumbnailSize   = 50;     // grid cell thumbnail (px)
inline constexpr int kMinPreviewWidth  = 320;
inline constexpr int kMinPreviewHeight = 240;
inline constexpr int kMinDialogWidth   = 500;

// ── Slider conversion ──
inline constexpr float kExposureGainSliderMultiplier = 100.0f;  // slider int ↔ float

// ── Camera hardware ranges ──
inline constexpr int kExposureSliderMin = 10;       // 0.1 ms
inline constexpr int kExposureSliderMax = 35000;     // 350 ms
inline constexpr int kSharpeningMax     = 500;

// ── Arm zero tolerance (pulses) ──
inline constexpr double kArmZeroTolerance = 200.0;

// ── Z-axis range (pulses) ──
inline constexpr int kZAxisMax = 80000;

// ── Crop offset range (px) ──
inline constexpr int kCropOffsetMax = 500;

}  // namespace infra::constants
