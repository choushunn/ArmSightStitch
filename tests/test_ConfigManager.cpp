#include <gtest/gtest.h>
#include "infra/config/ConfigManager.h"
#include <QCoreApplication>

class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure singleton is reset (default-constructed)
        auto& cfg = ConfigManager::instance();
        // Set known state
        cfg.setGridSizeX(10);
        cfg.setGridSizeY(10);
        cfg.setDefaultSpeed(5000);
    }
};

TEST_F(ConfigManagerTest, SingletonReturnsSameInstance) {
    auto& a = ConfigManager::instance();
    auto& b = ConfigManager::instance();
    EXPECT_EQ(&a, &b);
}

TEST_F(ConfigManagerTest, GridSizeDefaults) {
    auto& cfg = ConfigManager::instance();
    // After SetUp, we set 10x10
    EXPECT_EQ(cfg.gridSizeX(), 10);
    EXPECT_EQ(cfg.gridSizeY(), 10);
}

TEST_F(ConfigManagerTest, GridSizeSetter) {
    auto& cfg = ConfigManager::instance();
    cfg.setGridSizeX(5);
    cfg.setGridSizeY(3);
    EXPECT_EQ(cfg.gridSizeX(), 5);
    EXPECT_EQ(cfg.gridSizeY(), 3);
}

TEST_F(ConfigManagerTest, DefaultSpeed) {
    auto& cfg = ConfigManager::instance();
    EXPECT_EQ(cfg.defaultSpeed(), 5000);
}

TEST_F(ConfigManagerTest, StitchAlgorithmDefault) {
    auto& cfg = ConfigManager::instance();
    // Default should be SeamFeather (3)
    EXPECT_GE(cfg.stitchAlgorithm(), 0);
    EXPECT_LE(cfg.stitchAlgorithm(), 3);
}

TEST_F(ConfigManagerTest, RoundTripSaveLoad) {
    auto& cfg = ConfigManager::instance();
    cfg.setGridSizeX(7);
    cfg.setGridSizeY(4);
    cfg.setStepSize(200);

    std::string tmpPath = "test_config_tmp.json";
    ASSERT_TRUE(cfg.saveToFile(tmpPath));

    // Reload into same instance
    ASSERT_TRUE(cfg.loadFromFile(tmpPath));
    EXPECT_EQ(cfg.gridSizeX(), 7);
    EXPECT_EQ(cfg.gridSizeY(), 4);
    EXPECT_EQ(cfg.stepSize(), 200);

    // Cleanup
    std::remove(tmpPath.c_str());
}

