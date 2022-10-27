// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _MSC_VER
#  define CONSTEVAL constexpr
#else
#  define CONSTEVAL consteval
#endif
