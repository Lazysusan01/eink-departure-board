"""
Feeds .env into the build as -D defines, before compilation.

An ESP32 has no filesystem to read secrets from at boot, so configuration is
necessarily compile-time. The obvious place is config.h - but config.h is
tracked, and the WiFi password and RTT token should not be. This script is the
seam: .env is gitignored, every value in it becomes a preprocessor define, and
config.h wraps each of its own defaults in #ifndef so an .env value wins.

Anything absent from .env simply keeps the default in config.h, which is why a
partial .env (say, trains but no WiFi yet) is a valid state rather than a
build error.

Wired in via `extra_scripts = pre:scripts/load_env.py` in platformio.ini.
"""

import os

Import("env")  # noqa: F821  - injected by SCons

ENV_PATH = os.path.join(env.subst("$PROJECT_DIR"), ".env")  # noqa: F821

# Accepts KEY=VALUE, KEY = VALUE, and bare `KEY   "VALUE"` - the last because
# it is what you get by copying a #define line out of config.h and deleting the
# word #define, which is a natural thing to do and shouldn't silently not work.
def parse_line(line):
    line = line.strip()
    if not line or line.startswith("#") or line.startswith(";"):
        return None

    if "=" in line:
        key, _, value = line.partition("=")
    else:
        parts = line.split(None, 1)
        if len(parts) != 2:
            return None
        key, value = parts

    key = key.strip()
    value = value.strip()
    if not key.replace("_", "").isalnum():
        return None

    # Strip one matched pair of surrounding quotes, and remember that it was
    # quoted: that is what distinguishes a string from a number below.
    quoted = len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'"
    if quoted:
        value = value[1:-1]

    return key, value, quoted


def is_number(text):
    try:
        float(text)
        return True
    except ValueError:
        return False


if not os.path.isfile(ENV_PATH):
    print("load_env: no .env found, using config.h defaults only")
else:
    defines = []
    for raw in open(ENV_PATH, "r", encoding="utf-8-sig"):
        parsed = parse_line(raw)
        if not parsed:
            continue
        key, value, quoted = parsed

        # Unquoted true/false/numbers pass through as C literals; everything
        # else becomes a string, which is what the config.h consumers expect.
        if not quoted and (is_number(value) or value in ("true", "false")):
            defines.append((key, value))
        else:
            defines.append((key, env.StringifyMacro(value)))  # noqa: F821

    if defines:
        env.Append(CPPDEFINES=defines)  # noqa: F821
        # Names only. Printing values here would put the token in every build
        # log, which is the thing .env exists to avoid.
        print("load_env: overriding from .env -> " +
              ", ".join(sorted(k for k, _ in defines)))
