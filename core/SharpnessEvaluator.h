#pragma once

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

#include <vector>
#include <algorithm>
#include <cmath>

namespace core {

/// Single Z-stack sample: Z height → sharpness score.
struct ZFocusSample {
    int    z;        // Z-axis height (pulses)
    double score;    // focus measure (higher = sharper)
};

/// Result of a Z-stack autofocus scan.
struct ZFocusResult {
    int              optimalZ = 0;       // Z with highest sharpness
    double           peakScore = 0.0;    // sharpness at optimal Z
    std::vector<ZFocusSample> samples;   // complete Z→score curve
};

/// Fast, header-only image sharpness evaluator.
///
/// Primary metric: variance of Laplacian (well-established focus measure).
/// High variance → many edges → sharp image.
/// Low variance  → few edges   → blurry image.
class SharpnessEvaluator {
public:
    /// Compute Laplacian variance on a grayscale ROI.
    /// Returns a score where higher = sharper.
    /// @param gray  8-bit single-channel image (CV_8UC1)
    /// @param roi   region to evaluate (empty Rect = full image)
    static double laplacianVariance(const cv::Mat& gray,
                                    cv::Rect roi = cv::Rect())
    {
        if (gray.empty()) return 0.0;

        cv::Mat work;
        if (roi.empty() || roi == cv::Rect(0, 0, gray.cols, gray.rows)) {
            work = gray;
        } else {
            // Clamp ROI to image bounds
            roi &= cv::Rect(0, 0, gray.cols, gray.rows);
            if (roi.width <= 0 || roi.height <= 0) return 0.0;
            work = gray(roi);
        }

        cv::Mat lap;
        cv::Laplacian(work, lap, CV_64F);
        cv::Scalar mean, stddev;
        cv::meanStdDev(lap, mean, stddev);
        // Variance = stddev² — directly proportional to edge energy
        double var = stddev[0] * stddev[0];
        return var;
    }

    /// Convenience: convert BGR → grayscale → evaluate.
    static double laplacianVarianceBGR(const cv::Mat& bgr,
                                       cv::Rect roi = cv::Rect())
    {
        if (bgr.empty()) return 0.0;
        cv::Mat gray;
        if (bgr.channels() == 1) {
            gray = bgr;
        } else {
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        }
        return laplacianVariance(gray, roi);
    }

    /// Run a Z-stack autofocus scan.
    ///
    /// Caller provides:
    ///  - startZ / endZ: Z sweep range (pulses)
    ///  - zStep:        Z increment per sample (pulses, must be non-zero)
    ///  - captureFn:    callback(int z, cv::Mat& frame) → bool
    ///                  Called at each Z height; returns true on success.
    ///
    /// Returns the sample with the highest sharpness score.
    /// If no samples succeeded, optimalZ = 0 and peakScore = 0.
    template<typename CaptureFn>
    static ZFocusResult zStackFocus(int startZ, int endZ, int zStep,
                                    CaptureFn&& captureFn)
    {
        ZFocusResult result;
        if (zStep == 0) return result;

        // Ensure ascending order
        int lo = std::min(startZ, endZ);
        int hi = std::max(startZ, endZ);

        for (int z = lo; z <= hi; z += zStep) {
            cv::Mat frame;
            if (!captureFn(z, frame) || frame.empty()) continue;

            double score = laplacianVarianceBGR(frame);
            result.samples.push_back({z, score});

            if (score > result.peakScore) {
                result.peakScore = score;
                result.optimalZ  = z;
            }
        }

        return result;
    }

    /// Find peak Z by simple maximum search (already done inline during scan).
    /// Exposed as a standalone utility for post-hoc analysis.
    static ZFocusSample findPeak(const std::vector<ZFocusSample>& samples) {
        if (samples.empty()) return {0, 0.0};
        return *std::max_element(samples.begin(), samples.end(),
            [](const ZFocusSample& a, const ZFocusSample& b) {
                return a.score < b.score;
            });
    }
};

} // namespace core
