# =========================================
# CONFIG:PARSE
# =========================================

echo [CONFIG] Loading parse breakpoints...\n
# Namespace: potluck::config
dprintf potluck::config::load_config, "[CONFIG:PARSE] Parsing config content\n"
# Namespace: potluck::config::detail
dprintf potluck::config::detail::parse_config_content, "[CONFIG:PARSE] Parsing config content\n"

echo [CONFIG] parse: 2 dprintf points\n
