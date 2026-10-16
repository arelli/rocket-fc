# PlatformIO pre-build script: stamps the firmware with the git commit it was
# built from. The hash is printed at boot and sent in the BOOT telemetry
# event, so every flight log records exactly which code flew.
Import("env")  # noqa: F821  (provided by PlatformIO)

import subprocess


def git(*args):
    try:
        out = subprocess.check_output(
            ["git", *args], cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL  # noqa: F821
        )
        return out.decode().strip()
    except Exception:
        return ""


rev = git("rev-parse", "--short=8", "HEAD")
dirty = bool(git("status", "--porcelain"))
rev_hex = rev if len(rev) == 8 else "00000000"
label = (rev or "nogit") + ("-dirty" if dirty else "")

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("FC_GIT_REV", env.StringifyMacro(label)),  # noqa: F821
        ("FC_GIT_REV_U32", "0x%sUL" % rev_hex),
        ("FC_GIT_DIRTY", "1" if dirty else "0"),
    ]
)
print("rocket-fc build: git %s" % label)
