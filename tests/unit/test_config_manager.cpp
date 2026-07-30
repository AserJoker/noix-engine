#include <gtest/gtest.h>
#include "runtime/ConfigManager.h"
#include <filesystem>
#include <fstream>

using namespace noix::core;
using namespace noix::runtime;

class ConfigManagerTest : public ::testing::Test {
protected:
    std::filesystem::path _tempDir;

    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-cfg-test";
        std::filesystem::remove_all(_tempDir);
        std::filesystem::create_directories(_tempDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(_tempDir);
    }

    void writeJsonFile(const std::string& ns, const std::string& name,
                       const std::string& content) {
        auto dir = _tempDir / ns;
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / (name + ".json"));
        file << content;
    }
};

TEST_F(ConfigManagerTest, LoadNonexistentReturnsFalse) {
    ConfigManager mgr(_tempDir);
    EXPECT_FALSE(mgr.load(NamespacedId("debug", "server")));
}

TEST_F(ConfigManagerTest, LoadAllEmptyDirReturnsZero) {
    ConfigManager mgr(_tempDir);
    EXPECT_EQ(mgr.loadAll(), 0);
}

TEST_F(ConfigManagerTest, LoadAllMissingDirReturnsZero) {
    ConfigManager mgr(_tempDir / "nonexistent");
    EXPECT_EQ(mgr.loadAll(), 0);
}

TEST_F(ConfigManagerTest, LoadValidJson) {
    writeJsonFile("debug", "server", R"({"port":9900,"host":"localhost"})");
    ConfigManager mgr(_tempDir);
    EXPECT_TRUE(mgr.load(NamespacedId("debug", "server")));

    Value cfg = mgr.get(NamespacedId("debug", "server"));
    ASSERT_TRUE(cfg.isObject());
    EXPECT_EQ(cfg["port"].asInt(), 9900);
    EXPECT_EQ(cfg["host"].asString(), "localhost");
}

TEST_F(ConfigManagerTest, LoadAllDiscoversFiles) {
    writeJsonFile("debug", "server", R"({"port":9900})");
    writeJsonFile("mymod", "settings", R"({"enabled":true})");

    ConfigManager mgr(_tempDir);
    EXPECT_EQ(mgr.loadAll(), 2);

    EXPECT_TRUE(mgr.has(NamespacedId("debug", "server")));
    EXPECT_TRUE(mgr.has(NamespacedId("mymod", "settings")));
}

TEST_F(ConfigManagerTest, LoadAllIgnoresNonJson) {
    writeJsonFile("debug", "server", R"({"port":9900})");
    auto dir = _tempDir / "debug";
    std::ofstream file(dir / "notes.txt");
    file << "not a config";

    ConfigManager mgr(_tempDir);
    EXPECT_EQ(mgr.loadAll(), 1);
}

TEST_F(ConfigManagerTest, SetAndGetConfig) {
    ConfigManager mgr(_tempDir);
    Value cfg = Value::object();
    cfg.asObject()["port"] = 8080;
    cfg.asObject()["host"] = "0.0.0.0";
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));

    Value retrieved = mgr.get(NamespacedId("debug", "server"));
    ASSERT_TRUE(retrieved.isObject());
    EXPECT_EQ(retrieved["port"].asInt(), 8080);
    EXPECT_EQ(retrieved["host"].asString(), "0.0.0.0");
}

TEST_F(ConfigManagerTest, GetMissingReturnsNullValue) {
    ConfigManager mgr(_tempDir);
    Value cfg = mgr.get(NamespacedId("missing", "item"));
    EXPECT_TRUE(cfg.isNull());
}

TEST_F(ConfigManagerTest, SaveWritesFile) {
    ConfigManager mgr(_tempDir);
    Value cfg = Value::object();
    cfg.asObject()["port"] = 9900;
    cfg.asObject()["host"] = "localhost";
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));

    EXPECT_TRUE(mgr.save(NamespacedId("debug", "server")));

    auto path = _tempDir / "debug" / "server.json";
    ASSERT_TRUE(std::filesystem::exists(path));

    ConfigManager mgr2(_tempDir);
    EXPECT_TRUE(mgr2.load(NamespacedId("debug", "server")));
    Value loaded = mgr2.get(NamespacedId("debug", "server"));
    EXPECT_EQ(loaded["port"].asInt(), 9900);
    EXPECT_EQ(loaded["host"].asString(), "localhost");
}

TEST_F(ConfigManagerTest, RemoveEntry) {
    ConfigManager mgr(_tempDir);
    Value cfg = Value::object();
    cfg.asObject()["port"] = 9900;
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));

    EXPECT_TRUE(mgr.remove(NamespacedId("debug", "server")));
    EXPECT_FALSE(mgr.has(NamespacedId("debug", "server")));
    EXPECT_FALSE(mgr.remove(NamespacedId("debug", "server")));
}

TEST_F(ConfigManagerTest, LoadInvalidJsonReturnsFalse) {
    writeJsonFile("debug", "bad", "{invalid json}");
    ConfigManager mgr(_tempDir);
    EXPECT_FALSE(mgr.load(NamespacedId("debug", "bad")));
}

TEST_F(ConfigManagerTest, ConfigDir) {
    ConfigManager mgr(_tempDir);
    EXPECT_EQ(mgr.configDir(), _tempDir);
}

TEST_F(ConfigManagerTest, GetOrDefaultCreatesEntry) {
    ConfigManager mgr(_tempDir);
    Value defaults = Value::object();
    defaults.asObject()["port"] = 9900;
    defaults.asObject()["host"] = "localhost";

    Value cfg = mgr.getOrDefault(NamespacedId("debug", "server"), defaults);
    ASSERT_TRUE(cfg.isObject());
    EXPECT_EQ(cfg["port"].asInt(), 9900);
    EXPECT_TRUE(mgr.has(NamespacedId("debug", "server")));
}

TEST_F(ConfigManagerTest, GetOrDefaultReturnsExisting) {
    ConfigManager mgr(_tempDir);
    writeJsonFile("debug", "server", R"({"port":8080})");
    mgr.load(NamespacedId("debug", "server"));

    Value defaults = Value::object();
    defaults.asObject()["port"] = 9900;

    Value cfg = mgr.getOrDefault(NamespacedId("debug", "server"), defaults);
    EXPECT_EQ(cfg["port"].asInt(), 8080);
}

TEST_F(ConfigManagerTest, GetOrDefaultWritesToDisk) {
    ConfigManager mgr(_tempDir);
    Value defaults = Value::object();
    defaults.asObject()["port"] = 9900;

    mgr.getOrDefault(NamespacedId("debug", "server"), defaults);

    ConfigManager mgr2(_tempDir);
    EXPECT_TRUE(mgr2.load(NamespacedId("debug", "server")));
    Value loaded = mgr2.get(NamespacedId("debug", "server"));
    EXPECT_EQ(loaded["port"].asInt(), 9900);
}

TEST_F(ConfigManagerTest, ListReturnsAllEntries) {
    ConfigManager mgr(_tempDir);
    Value cfg1 = Value::object();
    cfg1.asObject()["port"] = 9900;
    Value cfg2 = Value::object();
    cfg2.asObject()["enabled"] = true;
    mgr.set(NamespacedId("debug", "server"), std::move(cfg1));
    mgr.set(NamespacedId("mymod", "settings"), std::move(cfg2));

    auto entries = mgr.list();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_NE(std::find(entries.begin(), entries.end(), NamespacedId("debug", "server")),
              entries.end());
    EXPECT_NE(std::find(entries.begin(), entries.end(), NamespacedId("mymod", "settings")),
              entries.end());
}

TEST_F(ConfigManagerTest, ListEmpty) {
    ConfigManager mgr(_tempDir);
    EXPECT_TRUE(mgr.list().empty());
}
