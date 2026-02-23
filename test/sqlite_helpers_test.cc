#include "maliput_geopackage/geopackage/sqlite_helpers.h"

#include <string>

#include <gtest/gtest.h>
#include <sqlite3.h>

namespace maliput {
namespace geopackage {
namespace test {

class SqliteHelpersTest : public ::testing::Test {
 protected:
  std::string database_path_{std::string(TEST_RESOURCES_DIR) + "/two_lane_road.gpkg"};
};

TEST_F(SqliteHelpersTest, SqliteDatabaseConstructor) { EXPECT_NO_THROW(SqliteDatabase{database_path_}); }

TEST_F(SqliteHelpersTest, SqliteStatementSelect) {
  SqliteDatabase db(database_path_);

  SqliteStatement stmt(db.get(), "SELECT value FROM maliput_metadata WHERE key = 'schema_version'");
  EXPECT_TRUE(stmt.Step());
  EXPECT_EQ("1.0.0", stmt.GetColumnText(0));
  // Only one line, so this should return false
  EXPECT_FALSE(stmt.Step());
}

TEST_F(SqliteHelpersTest, SqliteStatementSelectCount) {
  SqliteDatabase db(database_path_);
  // gpkg_contents should have entries as per schema.sql inserts.
  SqliteStatement stmt(db.get(), "SELECT count(*) FROM gpkg_contents");
  EXPECT_TRUE(stmt.Step());
  EXPECT_GT(stmt.GetColumnInt(0), 0);
  EXPECT_FALSE(stmt.Step());
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput