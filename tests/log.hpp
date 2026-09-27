// # Tetrodotoxin
// Copyright (c) 2023-present Matt Kaes and contributors

#pragma once

#include "perimortem/core/view/bytes.hpp"

namespace Perimortem::Tests::Log {

// Tests install this sink only while they inspect Perimortem diagnostics.
// The common runner neither redirects application logging nor depends on it.
auto begin() -> void;
auto end() -> void;
auto captured_message() -> Core::View::Bytes;
auto error_contains(Core::View::Bytes message) -> bool;

}  // namespace Perimortem::Tests::Log
