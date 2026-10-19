#!/usr/bin/env bash
# Starts the Bloodborne launcher (GTK4). With Python, PyGObject, GTK 4 and libadwaita installed
# it runs directly; otherwise through nix-shell (launcher/shell.nix).
here=$(cd -- "$(dirname -- "$0")" && pwd)
if python3 -c 'import gi; gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1")' 2>/dev/null; then
    exec python3 "$here/bbport_launcher.py" "$@"
fi
exec nix-shell "$here/shell.nix" --run "python3 '$here/bbport_launcher.py'"
