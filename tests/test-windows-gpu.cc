/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2005-2026 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Unit tests for parse_gpu_file() (src/data/os/windows/gpu.cc) — the pure
 * gpu.dat parsing logic, split out specifically to be testable without
 * triggering the real lhm-temp helper / scheduled task side effects that
 * read_gpu_info() has. See conky-windows issue #2.
 *
 * These write canned files to a scratch temp directory rather than
 * touching the real %ALLUSERSPROFILE%\Conky\gpu.dat path. */

#include "catch2/catch.hpp"

#include <data/os/windows/gpu.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path make_temp_gpu_file(const std::string &content) {
  static int counter = 0;
  auto path = std::filesystem::temp_directory_path() /
              ("conky_test_gpu_" + std::to_string(counter++) + ".dat");
  std::ofstream f(path, std::ios::binary);
  f << content;
  f.close();
  return path;
}

}  // namespace

TEST_CASE("parse_gpu_file parses a single GPU line", "[gpu]") {
  auto path = make_temp_gpu_file("0|63|0|34|2048|0|AMD Radeon(TM) Graphics\n");
  struct gpu_info gpus[MAX_GPUS] = {};

  int count = parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(count == 1);
  REQUIRE(gpus[0].present == 1);
  REQUIRE(gpus[0].temp_celsius == 63);
  REQUIRE(gpus[0].util_percent == 0);
  REQUIRE(gpus[0].mem_used == 34);
  REQUIRE(gpus[0].mem_total == 2048);
  REQUIRE(gpus[0].fan_rpm == 0);
  REQUIRE(std::string(gpus[0].name) == "AMD Radeon(TM) Graphics");

  std::filesystem::remove(path);
}

TEST_CASE("parse_gpu_file parses multiple GPUs indexed by id, not file order",
          "[gpu]") {
  // Real-world format observed from lhm-temp.exe on a machine with an
  // integrated AMD GPU (id 0) and a discrete NVIDIA GPU (id 1) — written
  // here out of order to confirm placement is by id, not by line position.
  auto path = make_temp_gpu_file(
      "1|82|96|7129|8192|2621|NVIDIA GeForce RTX 3070 Ti\r\n"
      "0|63|0|34|2048|0|AMD Radeon(TM) Graphics\r\n");
  struct gpu_info gpus[MAX_GPUS] = {};

  int count = parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(count == 2);
  REQUIRE(gpus[0].present == 1);
  REQUIRE(std::string(gpus[0].name) == "AMD Radeon(TM) Graphics");
  REQUIRE(gpus[1].present == 1);
  REQUIRE(gpus[1].temp_celsius == 82);
  REQUIRE(gpus[1].util_percent == 96);
  REQUIRE(std::string(gpus[1].name) == "NVIDIA GeForce RTX 3070 Ti");

  std::filesystem::remove(path);
}

TEST_CASE("parse_gpu_file strips trailing CRLF from the GPU name", "[gpu]") {
  auto path = make_temp_gpu_file("0|50|10|100|8192|1000|Some GPU Name\r\n");
  struct gpu_info gpus[MAX_GPUS] = {};

  parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(std::string(gpus[0].name) == "Some GPU Name");

  std::filesystem::remove(path);
}

TEST_CASE("parse_gpu_file returns -1 for a nonexistent file", "[gpu]") {
  struct gpu_info gpus[MAX_GPUS] = {};
  int count = parse_gpu_file(
      "C:\\this\\path\\definitely\\does\\not\\exist\\gpu.dat", gpus, MAX_GPUS);
  REQUIRE(count == -1);
}

TEST_CASE("parse_gpu_file returns 0 (not -1) for an empty file", "[gpu]") {
  // Matches lhm-temp.cs writing an empty gpu.dat when it finds zero GPU
  // hardware — a legitimate outcome, distinct from "file missing", and
  // must NOT trigger the helper-restart path in read_gpu_info().
  auto path = make_temp_gpu_file("");
  struct gpu_info gpus[MAX_GPUS] = {};

  int count = parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(count == 0);

  std::filesystem::remove(path);
}

TEST_CASE("parse_gpu_file skips malformed lines without crashing", "[gpu]") {
  auto path = make_temp_gpu_file(
      "not a valid line at all\n"
      "0|55|20|500|4096|800|Valid GPU\n"
      "\n"
      "garbage|garbage\n");
  struct gpu_info gpus[MAX_GPUS] = {};

  int count = parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(count == 1);
  REQUIRE(gpus[0].present == 1);
  REQUIRE(std::string(gpus[0].name) == "Valid GPU");

  std::filesystem::remove(path);
}

TEST_CASE("parse_gpu_file ignores an id beyond max_gpus", "[gpu]") {
  auto path = make_temp_gpu_file(
      "0|10|10|10|10|10|GPU Zero\n"
      "99|20|20|20|20|20|Out Of Range GPU\n");
  struct gpu_info gpus[MAX_GPUS] = {};

  int count = parse_gpu_file(path.string().c_str(), gpus, MAX_GPUS);

  REQUIRE(count == 1);
  REQUIRE(gpus[0].present == 1);
  REQUIRE(std::string(gpus[0].name) == "GPU Zero");

  std::filesystem::remove(path);
}
