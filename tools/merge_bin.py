Import("env")

import shutil
from pathlib import Path


def merge_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    project_dir = Path(env.subst("$PROJECT_DIR"))
    pioenv = env.subst("$PIOENV")
    framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))

    firmware = build_dir / "firmware.bin"
    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    boot_app0 = framework_dir / "tools" / "partitions" / "boot_app0.bin"

    missing = [str(p) for p in (firmware, bootloader, partitions, boot_app0) if not p.exists()]
    if missing:
        print("[merge_bin] missing files: " + ", ".join(missing))
        return

    segments = [
        (0x1000, bootloader),
        (0x8000, partitions),
        (0xE000, boot_app0),
        (0x10000, firmware),
    ]

    end = max(offset + path.stat().st_size for offset, path in segments)
    image = bytearray(b"\xff" * end)
    for offset, path in segments:
        data = path.read_bytes()
        image[offset:offset + len(data)] = data

    out_dir = project_dir / "firmware"
    out_dir.mkdir(parents=True, exist_ok=True)

    out = out_dir / f"bitaxe-monitor-{pioenv}.bin"
    out.write_bytes(image)
    print(f"[merge_bin] {out} ({len(image)} bytes)")

    app_out = out_dir / f"bitaxe-monitor-{pioenv}-app.bin"
    shutil.copyfile(firmware, app_out)
    print(f"[merge_bin] {app_out} ({app_out.stat().st_size} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
