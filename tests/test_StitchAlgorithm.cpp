#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include "core/stitch/GridStitchAlgorithm.h"
#include "core/stitch/SeamFeatherStitchAlgorithm.h"
#include "core/stitch/IStitchAlgorithm.h"

// Pure-logic tests — no Qt dependency

TEST(GridStitchAlgorithmTest, EmptyInputReturnsEmpty) {
    stitch::GridStitchAlgorithm algo;
    std::vector<cv::Mat> images;
    cv::Mat result = algo.stitch(images, cv::Size(2, 2));
    EXPECT_TRUE(result.empty());
}

TEST(GridStitchAlgorithmTest, SingleImageReturnsSameSize) {
    stitch::GridStitchAlgorithm algo;
    cv::Mat img(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    std::vector<cv::Mat> images = {img};
    cv::Mat result = algo.stitch(images, cv::Size(1, 1));
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.rows, 100);
    EXPECT_EQ(result.cols, 100);
}

TEST(GridStitchAlgorithmTest, TwoByTwoGridProducesValidOutput) {
    stitch::GridStitchAlgorithm algo;
    cv::Mat img(50, 50, CV_8UC3, cv::Scalar(255, 0, 0));
    std::vector<cv::Mat> images(4, img);
    cv::Mat result = algo.stitch(images, cv::Size(2, 2));
    ASSERT_FALSE(result.empty());
    // 2x2 grid of 50x50 images = 100x100
    EXPECT_EQ(result.rows, 100);
    EXPECT_EQ(result.cols, 100);
}

TEST(SeamFeatherStitchAlgorithmTest, EmptyInputReturnsEmpty) {
    stitch::SeamFeatherStitchAlgorithm algo;
    std::vector<cv::Mat> images;
    cv::Mat result = algo.stitch(images, cv::Size(2, 2));
    EXPECT_TRUE(result.empty());
}

TEST(SeamFeatherStitchAlgorithmTest, SingleImageReturnsSameSize) {
    stitch::SeamFeatherStitchAlgorithm algo;
    cv::Mat img(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));
    std::vector<cv::Mat> images = {img};
    cv::Mat result = algo.stitch(images, cv::Size(1, 1));
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.rows, 100);
    EXPECT_EQ(result.cols, 100);
}
