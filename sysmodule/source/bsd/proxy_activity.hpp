/**
 * @file proxy_activity.hpp
 * @brief One wake-up for every proxy socket, so poll and select can wait on many at once
 *
 * Each proxy socket has its own events, but poll and select watch several
 * sockets in one call. Every change that can make a proxy socket readable,
 * writable or closed calls Notify(); a waiter reads Generation(), checks its
 * sockets, and sleeps in WaitChange() only if the generation is still the
 * one it checked against, so no change between the check and the sleep is
 * missed.
 */

#pragma once

#include <stratosphere.hpp>

namespace ams::mitm::bsd::activity {

/// Changes so far; read before checking sockets
u64 Generation();

/// A proxy socket changed state: wakes every waiter
void Notify();

/// Sleeps until Notify() runs after `seen` was read, at most `ms`; true when it did
bool WaitChange(u64 seen, s32 ms);

}  // namespace ams::mitm::bsd::activity
