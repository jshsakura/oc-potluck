#pragma once
#include <cstdint>
#include <vector>
#include "logic.hpp"

namespace inst {

/** Look at the console the way the module does before it connects: Nintendo DNS blocking, serial
 *  number, emuMMC or sysMMC, blank_prodinfo. Needs the sockets (net::init) for the DNS lookups. */
Facts gatherFacts(bool online);

/** The application ids installed on this console (the home menu's games), up to a few thousand. */
std::vector<uint64_t> installedApplications();

} // namespace inst
