#include "maliput_geopackage/geopackage/sqlite_helpers.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <sqlite3.h>

namespace maliput {
namespace geopackage {
namespace test {

class SqliteHelpersTest : public ::testing::Test {
 protected:
  // Create an in-memory test database with a simple schema.
  void SetUp() override {
    // Create in-memory database with test schema.
    sqlite3* raw_db = nullptr;
    int rc = sqlite3_open(":memory:", &raw_db);
    ASSERT_EQ(rc, SQLITE_OK);
    test_db_ = std::unique_ptr<sqlite3, SqliteDeleter>(raw_db);

    // Create test tables and insert data.
    const char* schema_sql = R"(
      CREATE TABLE metadata (
        key TEXT NOT NULL,
        value TEXT NOT NULL
      );
      CREATE TABLE items (
        id INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        count INTEGER NOT NULL,
        data BLOB
      );
      INSERT INTO metadata (key, value) VALUES ('version', '1.0.0');
      INSERT INTO metadata (key, value) VALUES ('author', 'test_user');
      INSERT INTO items (id, name, count, data) VALUES (1, 'item_one', 42, X'48656C6C6F');
      INSERT INTO items (id, name, count, data) VALUES (2, 'item_two', 99, X'576F726C64');
    )";

    char* err_msg = nullptr;
    rc = sqlite3_exec(test_db_.get(), schema_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      ADD_FAILURE() << "Failed to create test schema: " << err_msg;
      sqlite3_free(err_msg);
    }
  }

  std::unique_ptr<sqlite3, SqliteDeleter> test_db_;
};

TEST_F(SqliteHelpersTest, SqliteStatementSelectText) {
  SqliteStatement stmt(test_db_.get(), "SELECT value FROM metadata WHERE key = 'version'");
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("1.0.0", stmt.GetColumnText(0));
  // Only one row matches, so next step should return false.
  EXPECT_FALSE(stmt.Step());
}

TEST_F(SqliteHelpersTest, SqliteStatementSelectMultipleRows) {
  SqliteStatement stmt(test_db_.get(), "SELECT key, value FROM metadata ORDER BY key");

  // First row
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("author", stmt.GetColumnText(0));
  EXPECT_EQ("test_user", stmt.GetColumnText(1));

  // Second row
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("version", stmt.GetColumnText(0));
  EXPECT_EQ("1.0.0", stmt.GetColumnText(1));

  // No more rows
  EXPECT_FALSE(stmt.Step());
}

TEST_F(SqliteHelpersTest, SqliteStatementSelectInt) {
  SqliteStatement stmt(test_db_.get(), "SELECT name, count FROM items WHERE id = 1");
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("item_one", stmt.GetColumnText(0));
  EXPECT_EQ(42, stmt.GetColumnInt(1));
  EXPECT_FALSE(stmt.Step());
}

TEST_F(SqliteHelpersTest, SqliteStatementSelectBlob) {
  SqliteStatement stmt(test_db_.get(), "SELECT name, data FROM items WHERE id = 1");
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("item_one", stmt.GetColumnText(0));

  const void* blob = stmt.GetColumnBlob(1);
  int bytes = stmt.GetColumnBytes(1);
  EXPECT_NE(blob, nullptr);
  EXPECT_EQ(bytes, 5);  // "Hello" is 5 bytes
  EXPECT_EQ(memcmp(blob, "Hello", 5), 0);
  EXPECT_FALSE(stmt.Step());
}

TEST_F(SqliteHelpersTest, SqliteStatementSelectCount) {
  SqliteStatement stmt(test_db_.get(), "SELECT COUNT(*) FROM items");
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ(2, stmt.GetColumnInt(0));
  EXPECT_FALSE(stmt.Step());
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput
