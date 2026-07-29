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
        //   _tmpDir/assets/noix/textures/stone.png
        auto defaultRes = _tmpDir / "assets" / "noix" / "textures";
        std::filesystem::create_directories(defaultRes);
        writeText(defaultRes / "stone.png", "default");

        // External pack structure:
        //   _extPack/assets/noix/textures/stone.png  (override)
        //   _extPack/assets/mymod/sprites/player.png
        _extPack = _tmpDir / "ext_pack";
        auto extNoix = _extPack / "assets" / "noix" / "textures";
        auto extMod = _extPack / "assets" / "mymod" / "sprites";
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
    EXPECT_TRUE(result->string().find("assets") != std::string::npos);
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

TEST_F(ResourcePackTest, RemovePack) {
    ResourcePack pack(_tmpDir);
    pack.addPack(_extPack);
    EXPECT_EQ(pack.packCount(), 1u);

    EXPECT_TRUE(pack.removePack(_extPack));
    EXPECT_EQ(pack.packCount(), 0u);

    // After removal, should fall back to default
    auto result = pack.resolve(NamespacedId("noix", "textures/stone.png"));
    ASSERT_TRUE(result.has_value());
    std::ifstream f(*result);
    std::string content;
    std::getline(f, content);
    EXPECT_EQ(content, "default");
}

TEST_F(ResourcePackTest, RemovePackNotFound) {
    ResourcePack pack(_tmpDir);
    EXPECT_FALSE(pack.removePack(_tmpDir / "nonexistent"));
}

TEST_F(ResourcePackTest, MovePackUp) {
    // Create two external packs with the same resource
    auto pack1 = _tmpDir / "pack1";
    auto pack2 = _tmpDir / "pack2";
    auto dir1 = pack1 / "assets" / "noix" / "textures";
    auto dir2 = pack2 / "assets" / "noix" / "textures";
    std::filesystem::create_directories(dir1);
    std::filesystem::create_directories(dir2);
    writeText(dir1 / "stone.png", "pack1");
    writeText(dir2 / "stone.png", "pack2");

    ResourcePack pack(_tmpDir);
    pack.addPack(pack1);
    pack.addPack(pack2);

    // pack2 is higher priority
    auto result = pack.resolve(NamespacedId("noix", "textures/stone.png"));
    ASSERT_TRUE(result.has_value());
    std::ifstream f1(*result);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(f1), {}), "pack2");

    // Move pack1 up (swaps with pack2, now pack1 is higher)
    EXPECT_TRUE(pack.movePackUp(pack1));
    result = pack.resolve(NamespacedId("noix", "textures/stone.png"));
    ASSERT_TRUE(result.has_value());
    std::ifstream f2(*result);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(f2), {}), "pack1");
}

TEST_F(ResourcePackTest, MovePackDown) {
    auto pack1 = _tmpDir / "pack1";
    auto pack2 = _tmpDir / "pack2";
    auto dir1 = pack1 / "assets" / "noix" / "textures";
    auto dir2 = pack2 / "assets" / "noix" / "textures";
    std::filesystem::create_directories(dir1);
    std::filesystem::create_directories(dir2);
    writeText(dir1 / "stone.png", "pack1");
    writeText(dir2 / "stone.png", "pack2");

    ResourcePack pack(_tmpDir);
    pack.addPack(pack1);
    pack.addPack(pack2);

    // pack2 is higher priority; move it down
    EXPECT_TRUE(pack.movePackDown(pack2));
    auto list = pack.listPacks();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], std::filesystem::weakly_canonical(pack2));
    EXPECT_EQ(list[1], std::filesystem::weakly_canonical(pack1));
}

TEST_F(ResourcePackTest, MovePackUpAlreadyHighest) {
    auto pack1 = _tmpDir / "pack1";
    auto dir1 = pack1 / "assets" / "noix" / "textures";
    std::filesystem::create_directories(dir1);
    writeText(dir1 / "stone.png", "pack1");

    ResourcePack pack(_tmpDir);
    pack.addPack(pack1);
    EXPECT_FALSE(pack.movePackUp(pack1));
}

TEST_F(ResourcePackTest, MovePackDownAlreadyLowest) {
    auto pack1 = _tmpDir / "pack1";
    auto dir1 = pack1 / "assets" / "noix" / "textures";
    std::filesystem::create_directories(dir1);
    writeText(dir1 / "stone.png", "pack1");

    ResourcePack pack(_tmpDir);
    pack.addPack(pack1);
    EXPECT_FALSE(pack.movePackDown(pack1));
}

TEST_F(ResourcePackTest, MovePackNotFound) {
    ResourcePack pack(_tmpDir);
    EXPECT_FALSE(pack.movePackUp(_tmpDir / "nonexistent"));
    EXPECT_FALSE(pack.movePackDown(_tmpDir / "nonexistent"));
}

TEST_F(ResourcePackTest, ListPacks) {
    auto pack1 = _tmpDir / "pack1";
    auto pack2 = _tmpDir / "pack2";
    std::filesystem::create_directories(pack1 / "assets");
    std::filesystem::create_directories(pack2 / "assets");

    ResourcePack pack(_tmpDir);
    pack.addPack(pack1);
    pack.addPack(pack2);
    EXPECT_EQ(pack.packCount(), 2u);

    auto list = pack.listPacks();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0], std::filesystem::weakly_canonical(pack1));
    EXPECT_EQ(list[1], std::filesystem::weakly_canonical(pack2));
}
