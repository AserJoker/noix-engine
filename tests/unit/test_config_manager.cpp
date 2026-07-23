#include <gtest/gtest.h>
#include "core/ConfigManager.h"
#include <filesystem>
#include <fstream>

using namespace noix::core;

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

    Config cfg = mgr.get(NamespacedId("debug", "server"));
    ASSERT_TRUE(static_cast<bool>(cfg));
    EXPECT_EQ(cfg.getInt("port").value_or(0), 9900);
    EXPECT_EQ(cfg.getString("host").value_or(""), "localhost");
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
    Config cfg;
    cfg.setInt("port", 8080);
    cfg.setString("host", "0.0.0.0");
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));

    Config retrieved = mgr.get(NamespacedId("debug", "server"));
    ASSERT_TRUE(static_cast<bool>(retrieved));
    EXPECT_EQ(retrieved.getInt("port").value_or(0), 8080);
    EXPECT_EQ(retrieved.getString("host").value_or(""), "0.0.0.0");
}

TEST_F(ConfigManagerTest, GetMissingReturnsEmptyConfig) {
    ConfigManager mgr(_tempDir);
    Config cfg = mgr.get(NamespacedId("missing", "item"));
    EXPECT_FALSE(static_cast<bool>(cfg));
}

TEST_F(ConfigManagerTest, SaveWritesFile) {
    ConfigManager mgr(_tempDir);
    Config cfg;
    cfg.setInt("port", 9900);
    cfg.setString("host", "localhost");
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));

    EXPECT_TRUE(mgr.save(NamespacedId("debug", "server")));

    auto path = _tempDir / "debug" / "server.json";
    ASSERT_TRUE(std::filesystem::exists(path));

    // 重新加载验证内容
    ConfigManager mgr2(_tempDir);
    EXPECT_TRUE(mgr2.load(NamespacedId("debug", "server")));
    Config loaded = mgr2.get(NamespacedId("debug", "server"));
    EXPECT_EQ(loaded.getInt("port").value_or(0), 9900);
    EXPECT_EQ(loaded.getString("host").value_or(""), "localhost");
}

TEST_F(ConfigManagerTest, SaveOnlyDirty) {
    ConfigManager mgr(_tempDir);
    writeJsonFile("debug", "server", R"({"port":9900})");
    mgr.load(NamespacedId("debug", "server"));

    // loaded entry is not dirty, save returns true but doesn't rewrite
    EXPECT_TRUE(mgr.save(NamespacedId("debug", "server")));

    // set makes it dirty
    Config cfg = mgr.get(NamespacedId("debug", "server"));
    cfg.setInt("port", 8080);
    mgr.set(NamespacedId("debug", "server"), std::move(cfg));
    EXPECT_TRUE(mgr.save(NamespacedId("debug", "server")));

    ConfigManager mgr2(_tempDir);
    mgr2.load(NamespacedId("debug", "server"));
    Config loaded = mgr2.get(NamespacedId("debug", "server"));
    EXPECT_EQ(loaded.getInt("port").value_or(0), 8080);
}

TEST_F(ConfigManagerTest, SaveAllOnlyDirty) {
    ConfigManager mgr(_tempDir);

    Config cfg1;
    cfg1.setInt("port", 9900);
    mgr.set(NamespacedId("debug", "server"), std::move(cfg1));

    Config cfg2;
    cfg2.setBool("enabled", true);
    mgr.set(NamespacedId("mymod", "settings"), std::move(cfg2));

    // set() 已经实时写入磁盘，saveAll 无 dirty 条目
    EXPECT_EQ(mgr.saveAll(), 0);

    // 验证文件已在磁盘上
    ConfigManager mgr2(_tempDir);
    mgr2.loadAll();
    EXPECT_TRUE(mgr2.has(NamespacedId("debug", "server")));
    EXPECT_TRUE(mgr2.has(NamespacedId("mymod", "settings")));
}

TEST_F(ConfigManagerTest, RemoveEntry) {
    ConfigManager mgr(_tempDir);
    Config cfg;
    cfg.setInt("port", 9900);
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
    Config defaults;
    defaults.setInt("port", 9900);
    defaults.setString("host", "localhost");

    Config cfg = mgr.getOrDefault(NamespacedId("debug", "server"), defaults);
    ASSERT_TRUE(static_cast<bool>(cfg));
    EXPECT_EQ(cfg.getInt("port").value_or(0), 9900);
    EXPECT_TRUE(mgr.has(NamespacedId("debug", "server")));
}

TEST_F(ConfigManagerTest, GetOrDefaultReturnsExisting) {
    ConfigManager mgr(_tempDir);
    writeJsonFile("debug", "server", R"({"port":8080})");
    mgr.load(NamespacedId("debug", "server"));

    Config defaults;
    defaults.setInt("port", 9900);

    Config cfg = mgr.getOrDefault(NamespacedId("debug", "server"), defaults);
    // 应返回已加载的值，不是默认值
    EXPECT_EQ(cfg.getInt("port").value_or(0), 8080);
}

TEST_F(ConfigManagerTest, GetOrDefaultWritesToDisk) {
    ConfigManager mgr(_tempDir);
    Config defaults;
    defaults.setInt("port", 9900);

    mgr.getOrDefault(NamespacedId("debug", "server"), defaults);

    // getOrDefault 已经实时写入磁盘
    ConfigManager mgr2(_tempDir);
    EXPECT_TRUE(mgr2.load(NamespacedId("debug", "server")));
    Config loaded = mgr2.get(NamespacedId("debug", "server"));
    EXPECT_EQ(loaded.getInt("port").value_or(0), 9900);
}

TEST_F(ConfigManagerTest, ListReturnsAllEntries) {
    ConfigManager mgr(_tempDir);
    Config cfg1, cfg2;
    cfg1.setInt("port", 9900);
    cfg2.setBool("enabled", true);
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
