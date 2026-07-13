#include <gtest/gtest.h>
#include "resource/ResourcePack.h"
#include <filesystem>
#include <fstream>

using namespace noix::core;
using namespace noix::resource;

class ResourcePackTest : public ::testing::Test {
protected:
    std::filesystem::path _tmpDir;

    void SetUp() override {
        _tmpDir = std::filesystem::temp_directory_path() / "noix-test-resourcepack";
        std::filesystem::remove_all(_tmpDir);
        std::filesystem::create_directories(_tmpDir);

        // Default pack structure:
        //   _tmpDir/resources/noix/textures/stone.png
        auto defaultRes = _tmpDir / "resources" / "noix" / "textures";
        std::filesystem::create_directories(defaultRes);
        writeText(defaultRes / "stone.png", "default");

        // External pack structure:
        //   _extPack/resources/noix/textures/stone.png  (override)
        //   _extPack/resources/mymod/sprites/player.png
        _extPack = _tmpDir / "ext_pack";
        auto extNoix = _extPack / "resources" / "noix" / "textures";
        auto extMod = _extPack / "resources" / "mymod" / "sprites";
        std::filesystem::create_directories(extNoix);
        std::filesystem::create_directories(extMod);
        writeText(extNoix / "stone.png", "override");
        writeText(extMod / "player.png", "mymod_player");
    }

    void TearDown() override {
        std::filesystem::remove_all(_tmpDir);
    }

    std::filesystem::path _extPack;

    static void writeText(const std::filesystem::path& p, const std::string& content) {
        std::ofstream f(p, std::ios::binary);
        f << content;
    }
};

TEST_F(ResourcePackTest, ResolveDefaultResource) {
    ResourcePack pack(_tmpDir);
    auto result = pack.resolve(NamespacedId("noix", "textures/stone.png"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->string().find("resources") != std::string::npos);
}

TEST_F(ResourcePackTest, ResolveUnqualified) {
    ResourcePack pack(_tmpDir);
    auto result = pack.resolve(NamespacedId("textures/stone.png"));
    ASSERT_TRUE(result.has_value());
}

TEST_F(ResourcePackTest, ResolveNotExists) {
    ResourcePack pack(_tmpDir);
    auto result = pack.resolve(NamespacedId("noix", "notexist.txt"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ResourcePackTest, AddPackAndResolve) {
    ResourcePack pack(_tmpDir);
    pack.addPack(_extPack);
    auto result = pack.resolve(NamespacedId("mymod", "sprites/player.png"));
    ASSERT_TRUE(result.has_value());
}

TEST_F(ResourcePackTest, PriorityOverlay) {
    ResourcePack pack(_tmpDir);
    pack.addPack(_extPack);
    // "noix:textures/stone.png" should resolve to the external pack's version
    auto result = pack.resolve(NamespacedId("noix", "textures/stone.png"));
    ASSERT_TRUE(result.has_value());
    // Verify it's the override by reading the file content
    std::ifstream f(*result);
    std::string content;
    std::getline(f, content);
    EXPECT_EQ(content, "override");
}

TEST_F(ResourcePackTest, DefaultPathCorrect) {
    ResourcePack pack(_tmpDir);
    EXPECT_EQ(pack.defaultPath(), _tmpDir);
}

TEST_F(ResourcePackTest, ExistsMethod) {
    ResourcePack pack(_tmpDir);
    EXPECT_TRUE(pack.exists(NamespacedId("noix", "textures/stone.png")));
    EXPECT_FALSE(pack.exists(NamespacedId("noix", "nonexistent")));
}
