// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

#include "toolchain/validation/test.hpp"

#include "perimortem/core/access/bytes.h"
#include "perimortem/core/diagnostics/log.hpp"
#include "perimortem/core/reader/binary.hpp"
#include "perimortem/core/storage/bytes.hpp"
#include "perimortem/core/view/null_terminated.hpp"
#include "perimortem/core/writer/binary.hpp"
#include "perimortem/system/process/worker.hpp"

using namespace Perimortem::Core;
using namespace Perimortem::System;
using namespace Toolchain::Validation;

static Harness SystemProcessWorker = {
  .name = "System::Process::Worker",
};

static constexpr Count large_worker_payload_size = 4096;
static constexpr View::Bytes long_worker_name =
    "worker.thread.name.with.more.than.twenty.four.bytes"_view;
static constexpr Count long_worker_name_size = long_worker_name.get_size();
static Count spawning_log_events = 0;
static Count worker_log_events = 0;

static auto spawning_sink(
    Diagnostics::Log::Level,
    View::Bytes,
    const Diagnostics::Source&) -> void {
  spawning_log_events++;
}

static auto worker_sink(
    Diagnostics::Log::Level,
    View::Bytes,
    const Diagnostics::Source&) -> void {
  worker_log_events++;
}

struct WorkerPayloadResult {
  Count byte_count = 0;
  U64 checksum = 0;
  Bool read_successfully = False;
};

struct WorkerNameResult {
  Count name_size = 0;
  U64 checksum = 0;
  Bool matches_expected = False;
  Bool read_successfully = False;
  Bool on_main_thread = True;
};

struct WorkerCountResult {
  Count active_worker_count = 0;
  Count thread_id = 0;
  Bool started = False;
  Bool release = False;
  Bool read_successfully = False;
};

static auto yield_thread() -> void {

#ifdef _WIN32

  SwitchToThread();

#else

  sched_yield();

#endif
}

static auto fill_worker_payload(
    Storage::Bytes<large_worker_payload_size>& payload_bytes) -> void {
  for (Count i = 0; i < payload_bytes.get_size(); i++) {
    payload_bytes[i] = U8((i * 37 + 11) & 0xFF);
  }
}

static auto checksum_worker_payload(View::Bytes payload_bytes) -> U64 {
  U64 checksum = 1469598103934665603ull;
  for (Count i = 0; i < payload_bytes.get_size(); i++) {
    checksum ^= payload_bytes[i];
    checksum *= 1099511628211ull;
  }

  return checksum;
}

static auto read_large_payload_job(View::Bytes job_data) -> void {
  Reader::Binary<Data::ByteOrder::Native> reader(job_data);
  const auto result_address = reader.read_u64();
  const auto payload_bytes = reader.read_bytes(large_worker_payload_size);
  result_address.visit(
      [&](U64 address) {
        if (address == 0) {
          return;
        }

        payload_bytes.visit(
            [&](View::Bytes payload) {
              auto* worker_result =
                  Data::restore<WorkerPayloadResult*>(CppSize(address));
              worker_result->byte_count = payload.get_size();
              worker_result->checksum = checksum_worker_payload(payload);
              worker_result->read_successfully =
                  reader.get_location() == reader.get_size();
            },

            []() {});
      },

      []() {});
}

static auto read_thread_name_job(View::Bytes job_data) -> void {
  Reader::Binary<Data::ByteOrder::Native> reader(job_data);
  const auto result_address = reader.read_u64();
  const auto expected_name_size = reader.read_u64();
  result_address.visit(
      [&](U64 address) {
        if (address == 0) {
          return;
        }

        expected_name_size.visit(
            [&](U64 name_size) {
              reader.read_bytes(name_size).visit(
                  [&](View::Bytes expected_name) {
                    auto* worker_result =
                        Data::restore<WorkerNameResult*>(CppSize(address));
                    const View::Bytes actual_name =
                        Process::Worker::get_thread_name();
                    worker_result->on_main_thread =
                        Process::Worker::on_main_thread();
                    worker_result->name_size = actual_name.get_size();
                    worker_result->checksum =
                        checksum_worker_payload(actual_name);
                    worker_result->matches_expected =
                        actual_name == expected_name;
                    worker_result->read_successfully =
                        reader.get_location() == reader.get_size();
                  },

                  []() {});
            },

            []() {});
      },

      []() {});
}

static auto hold_worker_job(View::Bytes job_data) -> void {
  Reader::Binary<Data::ByteOrder::Native> reader(job_data);
  const auto result_address = reader.read_u64();
  result_address.visit(
      [&](U64 address) {
        if (address == 0) {
          return;
        }

        auto* worker_result =
            Data::restore<WorkerCountResult*>(CppSize(address));
        worker_result->active_worker_count =
            Process::Worker::get_worker_count();
        worker_result->thread_id = Process::Worker::get_thread_id();
        worker_result->read_successfully =
            reader.get_location() == reader.get_size();
        __atomic_store_n(
            &worker_result->started.value, True.value, __ATOMIC_RELEASE);
        while (Bool(__atomic_load_n(
                   &worker_result->release.value, __ATOMIC_ACQUIRE)) != True) {
          yield_thread();
        }
      },

      []() {});
}

static auto select_worker_logger(View::Bytes) -> void {
  Diagnostics::Log::set_sink(worker_sink);
  Diagnostics::Log::set_level(Diagnostics::Log::Level::Debug);
}

VALIDATION_TEST(SystemProcessWorker, copies_large_job) {
  WorkerPayloadResult worker_result;
  Storage::Bytes<large_worker_payload_size> expected_payload;
  fill_worker_payload(expected_payload);

  const U64 expected_checksum = checksum_worker_payload(expected_payload);

  Storage::Bytes<sizeof(U64) + large_worker_payload_size + sizeof(U64) * 2>
      job_storage;
  Count source_offset = 1;
  if ((Count(job_storage.get_data() + source_offset) & (sizeof(U64) - 1)) ==
      0) {
    source_offset++;
  }

  Access::Bytes job_data(
      job_storage.get_data() + source_offset,
      job_storage.get_size() - source_offset);

  Writer::Binary<Data::ByteOrder::Native> writer(job_data);
  writer << U64(Data::address(&worker_result)) << expected_payload.get_view();
  ASSERT(writer.is_valid());

  Process::Worker worker = Process::Worker::start(
      "worker.payload"_view, read_large_payload_job, writer);
  for (Count i = 0; i < job_storage.get_size(); i++) {
    job_storage[i] = 0xA5;
  }

  worker.join();
  EXPECT(worker_result.read_successfully);
  EXPECT_EQ(worker_result.byte_count, large_worker_payload_size);
  EXPECT_EQ(worker_result.checksum, expected_checksum);
}

VALIDATION_TEST(SystemProcessWorker, copies_thread_name) {
  WorkerNameResult worker_result;
  Storage::Bytes<long_worker_name_size> requested_name(long_worker_name);
  const U64 expected_checksum = checksum_worker_payload(long_worker_name);

  Storage::Bytes<sizeof(U64) * 2 + long_worker_name_size + sizeof(U64) * 2>
      job_storage;

  Writer::Binary<Data::ByteOrder::Native> writer(job_storage.get_access());
  writer << U64(Data::address(&worker_result)) << U64(long_worker_name_size)
         << requested_name.get_view();
  ASSERT(writer.is_valid());

  Process::Worker worker = Process::Worker::start(
      requested_name.get_view(), read_thread_name_job, writer);
  for (Count i = 0; i < requested_name.get_size(); i++) {
    requested_name[i] = 0;
  }

  worker.join();
  EXPECT(worker_result.read_successfully);
  EXPECT(worker_result.matches_expected);
  EXPECT_EQ(worker_result.name_size, long_worker_name_size);
  EXPECT_EQ(worker_result.checksum, expected_checksum);
}

VALIDATION_TEST(SystemProcessWorker, unnamed_worker) {
  // A diagnostic name is optional. Omitting it must not change the worker's
  // identity or substitute the foreign thread's default name.
  WorkerNameResult worker_result;
  Storage::Bytes<sizeof(U64) * 2> storage;
  Writer::Binary<Data::ByteOrder::Native> writer(storage.get_access());
  writer << U64(Data::address(&worker_result)) << U64(0);

  auto worker = Process::Worker::start(""_view, read_thread_name_job, writer);
  worker.join();

  EXPECT(worker_result.read_successfully);
  EXPECT(!worker_result.on_main_thread);
  EXPECT(worker_result.matches_expected);
  EXPECT_EQ(worker_result.name_size, Count(0));
}

VALIDATION_TEST(SystemProcessWorker, worker_count) {
  WorkerCountResult worker_result;
  const Count baseline_worker_count = Process::Worker::get_worker_count();

  Storage::Bytes<sizeof(U64)> job_storage;
  Writer::Binary<Data::ByteOrder::Native> writer(job_storage.get_access());
  writer << U64(Data::address(&worker_result));
  ASSERT(writer.is_valid());

  Process::Worker worker =
      Process::Worker::start("worker.count"_view, hold_worker_job, writer);
  while (Bool(__atomic_load_n(
             &worker_result.started.value, __ATOMIC_ACQUIRE)) != True) {
    yield_thread();
  }

  EXPECT(worker_result.read_successfully);
  EXPECT(worker_result.thread_id < Process::Worker::max_workers());
  EXPECT_EQ(worker_result.active_worker_count, baseline_worker_count + 1);
  EXPECT_EQ(Process::Worker::get_worker_count(), baseline_worker_count + 1);

  __atomic_store_n(&worker_result.release.value, True.value, __ATOMIC_RELEASE);
  worker.join();

  EXPECT_EQ(Process::Worker::get_worker_count(), baseline_worker_count);
}

VALIDATION_TEST(SystemProcessWorker, completion_uses_worker_logger) {
  const auto original_sink = Diagnostics::Log::get_sink();
  const auto original_level = Diagnostics::Log::get_level();
  spawning_log_events = 0;
  worker_log_events = 0;

  Diagnostics::Log::set_sink(spawning_sink);

  auto worker =
      Process::Worker::start("worker.logger"_view, select_worker_logger);
  worker.join();

  Diagnostics::Log::set_sink(original_sink);
  Diagnostics::Log::set_level(original_level);

  EXPECT_EQ(spawning_log_events, Count(0));
  EXPECT_EQ(worker_log_events, Count(1));
}
