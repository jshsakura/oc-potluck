# =========================================
# DEBUG:LOGGER
# =========================================

echo [DEBUG] Loading logger breakpoints...\n
# Namespace: potluck::debug
dprintf potluck::debug::Logger::init, "[DEBUG:LOGGER] Logger initialized\n"
dprintf potluck::debug::Logger::open_file, "[DEBUG:LOGGER] Log file opened\n"
dprintf potluck::debug::Logger::close_file, "[DEBUG:LOGGER] Log file closed\n"

echo [DEBUG] logger: 3 dprintf points\n
