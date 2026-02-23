// BSD 3-Clause License
//
// Copyright (c) 2026, Woven by Toyota
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include <string>

#include <sqlite3.h>

#include "maliput/common/maliput_throw.h"

// RAII wrapper for sqlite3 database connection.
class SqliteDatabase {
 public:
  explicit SqliteDatabase(const std::string& db_path);
  ~SqliteDatabase();

  sqlite3* get() const;

 private:
  sqlite3* db_{nullptr};
};

// RAII wrapper for sqlite3 statement.
class SqliteStatement {
 public:
  SqliteStatement(sqlite3* db, const std::string& query);
  ~SqliteStatement();

  bool Step();

  std::string GetColumnText(int col);
  int GetColumnInt(int col);
  const void* GetColumnBlob(int col);
  int GetColumnBytes(int col);

 private:
  sqlite3_stmt* stmt_{nullptr};
};
