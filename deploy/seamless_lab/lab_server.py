"""Configure the pinned BBSC game API for the isolated 32313 shadNet lab."""

import ipaddress
import os
from pathlib import Path
import sys


def configure(template, advertise):
    ipaddress.IPv4Address(advertise)
    source = template.read_text(encoding="utf-8")
    placeholder = "http://{YOUR SHADNET SERVER}:31315"
    configured = f"http://{advertise}:32315"
    if source.count(placeholder) == 0 and source.count(configured) == 4:
        return
    if source.count(placeholder) != 4:
        raise RuntimeError("BBSC ss.info template changed; inspect before starting the lab")
    template.write_text(source.replace(placeholder, configured),
                        encoding="utf-8")


def patch_region_response(server):
    """Add a narrow region adapter to the pinned upstream ss.info handler."""
    source = server.read_text(encoding="utf-8")
    import_line = "from lab_region_info import adapt_region_ss_info\n"
    original_send = '            self._send(info, "text/plain; charset=utf-8")'
    patched_send = '            self._send(adapt_region_ss_info(path, info), "text/plain; charset=utf-8")'
    if import_line in source and patched_send in source:
        return
    if source.count("import bbhost_service\n") != 1 or source.count(original_send) != 1:
        raise RuntimeError("BBSC ss.info handler changed; inspect before starting the lab")
    source = source.replace("import bbhost_service\n", "import bbhost_service\n" + import_line)
    server.write_text(source.replace(original_send, patched_send), encoding="utf-8")


def main():
    advertise = os.environ.get("SEAMLESS_ADVERTISE_IP", "127.0.0.1")
    script_dir = Path("/opt/bbsc/server/gameserver")
    configure(script_dir / "ss_info_template.xml", advertise)
    patch_region_response(script_dir / "bbsc_gameserver.py")
    Path("/var/lib/bbsc").mkdir(parents=True, exist_ok=True)
    argv = [sys.executable, str(script_dir / "bbsc_gameserver.py"),
            "--shadnet", advertise, "--host", "0.0.0.0", "--advertise", advertise,
            "--port", "32443", "--relay-port", "32444", "--no-bbhost",
            "--bbhost-port", "0", "--bbhost-stun-port", "0", "--no-playlog",
            "--data", "/var/lib/bbsc/online-data.json",
            "--log-dir", "/var/lib/bbsc/logs"]
    os.execv(sys.executable, argv)


if __name__ == "__main__":
    main()
