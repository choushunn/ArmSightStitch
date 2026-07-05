#pragma once

#include "IStitchAlgorithm.h"
#include <spdlog/spdlog.h>

namespace stitch {

/// Algorithm 1 — position-based grid stitching
/// Images are placed at fixed grid positions in S-curve (snake) order:
/// even rows left-to-right, odd rows right-to-left.
class GridStitchAlgorithm : public IStitchAlgorithm {
public:
    cv::Mat stitch(const std::vector<cv::Mat>& images,
                  const cv::Size& gridSize) override
    {
        if (images.empty()) {
            reportStatus("No images to stitch");
            return {};
        }

        try {
            int expected = gridSize.width * gridSize.height;
            if (static_cast<int>(images.size()) < expected) {
                reportStatus("Not enough images. Expected " +
                    std::to_string(expected) + ", got " + std::to_string(images.size()));
                reportProgress(100, 100);
                return {};
            }

            reportStatus("Grid stitching " + std::to_string(images.size()) +
                         " images (" + std::to_string(gridSize.width) + "x" +
                         std::to_string(gridSize.height) + ")");
            reportProgress(0, 100);

            cv::Size imgSize = images[0].size();
            int totalW = gridSize.width * imgSize.width;
            int totalH = gridSize.height * imgSize.height;

            cv::Mat result(totalH, totalW, images[0].type(), cv::Scalar(0, 0, 0));
            reportProgress(20, 100);

            int index = 0;
            for (int row = 0; row < gridSize.height; ++row) {
                reportProgress(30 + (row * 70) / gridSize.height, 100);
                for (int i = 0; i < gridSize.width; ++i) {
                    if (index >= static_cast<int>(images.size())) break;
                    int col = (row % 2 == 0) ? i : (gridSize.width - 1 - i);
                    const cv::Mat& img = images[index];
                    cv::Rect roi(col * imgSize.width, row * imgSize.height,
                                 imgSize.width, imgSize.height);
                    img.copyTo(result(roi));
                    ++index;
                }
            }

            reportProgress(100, 100);
            reportStatus("Grid stitching completed");
            return result;
        } catch (const std::exception& e) {
            reportStatus(std::string("Grid stitching error: ") + e.what());
            reportProgress(100, 100);
            return {};
        }
    }
};

} // namespace stitch
