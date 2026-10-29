#pragma once

namespace inst {

/** Another module already mitms ldn:u or bsd:u while PotLuck is not the one running. */
bool otherMitmActive();

/** Ask the running module to read config.ini again, so a saved change applies without a restart. */
bool reloadModuleConfig();

} // namespace inst
