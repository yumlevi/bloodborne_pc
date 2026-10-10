#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Starts the game on Linux and Windows; run.sh and run.bat only find a Python and call this.

    bash run.sh [--software] [--game-dir DIR] [bb-probe options...]
    run.bat [--game-dir DIR] [bb-probe options...]

Steps: the mods (scripts/mods.py), the game image (prepare.py, link_libc.py, link_modules.py,
content_profile.py), the render size for bbport.ini's output, the patches (patches.py), then
out/bb-probe is (re)built by build.sh unless BB_PREBUILT is set (on Windows in MSYS2's CLANG64
environment) and started.

--game-dir DIR  the game's folder (eboot.bin, sce_module, ...); else BB_GAME_DIR, else on
                Windows the last folder used (out/game_dir.txt), else ../CUSA03173 next to the
                port's folder. Restarts from the in-game menu keep it.
--software      Linux: Lavapipe, Mesa's CPU Vulkan driver.
BB_DRY_RUN=1    every step but the last: prints the bb-probe command and its environment
                instead of starting the game.
The other variables are described where they are read below.
"""
import glob
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
from mods import remove_overlay

WINDOWS = os.name == 'nt'
ROOT = Path(__file__).resolve().parent.parent
# The Python for the scripts: run.sh's $PYTHON (it may be a wrapper); on Windows the MSYS2 Python
# that run.bat runs this with (PYTHON there often names another installation).
PYTHON = sys.executable if WINDOWS else os.environ.get('PYTHON') or sys.executable
LAVAPIPE = ('/run/opengl-driver/share/vulkan/icd.d/lvp_icd*.json', '/usr/share/vulkan/icd.d/lvp_icd*.json')
INITIAL_ENVIRONMENT = dict(os.environ)


def env(name, default=''):
    """A variable's value; `default` when it is unset or empty (as ${name:-default})."""
    return os.environ.get(name) or default


def exit_status(code):
    """A child's status as a shell reports it (128 + the signal that ended it)."""
    return 128 - code if code < 0 else code


def run(arguments, capture=False, check=True, environment=None):
    """Runs a step in the port's folder; a failed step ends the launch with its status."""
    result = subprocess.run([str(a) for a in arguments], env=environment,
                            stdout=subprocess.PIPE if capture else None, text=True)
    if check and result.returncode:
        sys.exit(exit_status(result.returncode))
    return result.stdout.strip() if capture else result.returncode


def use_lavapipe():
    """--software: Mesa's CPU Vulkan driver, unless VK_DRIVER_FILES names a driver already."""
    if not env('VK_DRIVER_FILES'):
        for candidate in (path for pattern in LAVAPIPE for path in sorted(glob.glob(pattern))):
            if os.path.isfile(candidate):
                os.environ['VK_DRIVER_FILES'] = candidate
                break
    if not env('VK_DRIVER_FILES'):
        sys.exit('Lavapipe not found; set VK_DRIVER_FILES.')
    os.environ['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'


def msys_root():
    return Path(env('BB_MSYS2', r'C:\msys64'))


def build():
    """build.sh; on Windows in a CLANG64 login shell (its clang, cmake, ninja and pkg-config)."""
    if not WINDOWS:
        run(['bash', 'build.sh'])
        return
    bash = msys_root() / 'usr/bin/bash.exe'
    if not bash.is_file():
        sys.exit(f'MSYS2 not found at {msys_root()} (set BB_MSYS2, or BB_PREBUILT=1 with a built out/)')
    run([bash, '-lc', 'bash build.sh'], environment=dict(os.environ, MSYSTEM='CLANG64', CHERE_INVOKING='1'))


def optimize_fsr4(prebuilt):
    """FSR 4: faster post passes next to the downloaded ones (incremental; tools/fsr4_optimize.sh).
    Linux only: MSYS2's spirv-cross cannot decompile them ("Cannot resolve expression type")."""
    if not WINDOWS and not prebuilt and os.path.isdir('fsr4_shaders') and shutil.which('spirv-cross'):
        if run(['bash', 'tools/fsr4_optimize.sh'], check=False):
            print('FSR 4: optimized post passes not built', file=sys.stderr)


def configured_live_resolution(config):
    """bbport.ini's live_resolution (0, 1 or auto; the last such line), else ''."""
    value = ''
    try:
        lines = Path(config).read_text(errors='replace').splitlines()
    except OSError:
        return value
    for line in lines:
        match = re.fullmatch(r'live_resolution=([01]|auto)', line)
        if match:
            value = match.group(1)
    return value


def direct_memory_mb_for_output(output):
    """Guest direct-memory budget for a startup-patched output resolution.

    Bloodborne's native 5056 MiB budget is sufficient through 1080p. Higher resolutions create
    much larger guest render targets and keep the established extra 4096 MiB headroom.
    Unknown custom values stay on the conservative high budget.
    """
    match = re.fullmatch(r'(\d+)x(\d+)', output or '')
    if not match:
        return '9152'
    width, height = map(int, match.groups())
    return '5056' if width <= 1920 and height <= 1080 else '9152'


def clear_automatic_direct_memory():
    """Discard a budget chosen for the previous launch, preserving an explicit override."""
    if os.environ.get('BB_AUTO_DMEM') == '1':
        os.environ.pop('BB_DMEM_MB', None)
        os.environ.pop('BB_AUTO_DMEM', None)


def configure_direct_memory(output):
    """Set and mark an automatic budget unless BB_DMEM_MB was supplied explicitly."""
    configured = env('BB_DMEM_MB')
    if configured:
        return configured
    configured = direct_memory_mb_for_output(output)
    os.environ['BB_DMEM_MB'] = configured
    os.environ['BB_AUTO_DMEM'] = '1'
    return configured


def gpu_check(caps):
    """live_resolution=auto: '1' when the GPU check (tools/gpu_capabilities.c) recommends live
    resolution changes, else '0' (also when it cannot run)."""
    try:
        result = subprocess.run([caps, '--live-resolution'], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, errors='replace')
    except OSError as error:
        print(f'{caps}: {error.strerror}', file=sys.stderr)
        return '0'
    # MangoHud (MANGOHUD=1) announces itself in every Vulkan program: only the check's own lines.
    for line in result.stderr.splitlines():
        if 'MANGOHUD' not in line:
            print(line, file=sys.stderr)
    return result.stdout.strip() if result.returncode == 0 else '0'


def programs(out, prebuilt):
    """(bb-probe, the GPU check): BB_PROBE, else the build's (out/), else the package's (bin/)."""
    if WINDOWS:
        return (os.path.join(ROOT, env('BB_PROBE', os.path.join(out, 'bb-probe.exe'))),
                os.path.join(out, 'bb-gpu-capabilities.exe'))
    probe = env('BB_PROBE', 'bin/bb-probe') if prebuilt else 'out/bb-probe'
    if env('BB_PROBE'):
        caps = os.path.join(os.path.dirname(env('BB_PROBE')) or '.', 'bb-gpu-capabilities')
    else:
        caps = 'bin/bb-gpu-capabilities' if prebuilt else 'out/bb-gpu-capabilities'
    return probe, caps


def quoted(command):
    return ' '.join(shlex.quote(str(c)) for c in command)


def describe_dry_run(command, probe):
    print('Dry run (BB_DRY_RUN=1): the game is not started.')
    print('Command:', quoted(command))
    if not (os.path.isfile(probe) or shutil.which(probe)):
        print(f'  ({probe} does not exist)')
    print('Working directory:', os.getcwd())
    # What bb-probe would get: BB_*, VK_*, MANGOHUD and whatever this launcher set (marked *).
    changed = {k for k in {*os.environ, *INITIAL_ENVIRONMENT} if os.environ.get(k) != INITIAL_ENVIRONMENT.get(k)}
    shown = {k for k in os.environ if k.startswith(('BB_', 'VK_')) or k == 'MANGOHUD'} | changed
    print('Environment (* set by this launcher):')
    for key in sorted(shown):
        value, before = os.environ.get(key), INITIAL_ENVIRONMENT.get(key)
        mark = '*' if key in changed else ' '
        if value is None:
            print(f' {mark} {key} (removed)')
        elif before and value.endswith(os.pathsep + before):
            print(f' {mark} {key}={value[:-len(before)]}' + (f'%{key}%' if WINDOWS else f'${key}'))
        else:
            print(f' {mark} {key}={value}')


class Interrupted(BaseException):
    """SIGTERM or SIGINT while the mod view exists: it is removed, then the launcher ends by
    the same signal (as run.sh did with its EXIT trap)."""


def interrupted(number, _frame):
    raise Interrupted(number)


def ignore_interrupts():
    # As for a background job of a shell script (run.sh): the launcher forwards SIGTERM instead.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGQUIT, signal.SIG_IGN)


def start(command, probe, overlay):
    """Runs bb-probe and returns its status; on Linux without a mod view it replaces this
    process (the GTK launcher's process, and probe.c's restart execs run.sh in place)."""
    if WINDOWS:
        print('Starting:', quoted(command), flush=True)
        try:
            return subprocess.call([str(c) for c in command])
        except KeyboardInterrupt:
            return 130
    try:
        if overlay:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, preexec_fn=ignore_interrupts)
        else:
            sys.stdout.flush()
            sys.stderr.flush()
            # Python ignores these, and an exec keeps ignored signals.
            for name in ('SIGPIPE', 'SIGXFSZ'):
                if hasattr(signal, name):
                    signal.signal(getattr(signal, name), signal.SIG_DFL)
            os.execvp(probe, command)
    except OSError as error:
        print(f'{probe}: {error.strerror}', file=sys.stderr)
        return 127 if isinstance(error, FileNotFoundError) else 126

    def forward(_number, _frame):
        child.send_signal(signal.SIGTERM)

    signal.signal(signal.SIGTERM, forward)
    signal.signal(signal.SIGINT, forward)
    # The view is removed only after the game (and any restart it exec'd) has ended.
    return exit_status(child.wait())


def main():
    caller_dir = os.getcwd()
    os.chdir(ROOT)
    sys.stdout.reconfigure(line_buffering=True)
    if not WINDOWS:
        signal.signal(signal.SIGINT, signal.SIG_DFL)  # Ctrl+C ends the launch as it ended run.sh
    arguments = sys.argv[1:]
    game_option = None
    while True:
        if not WINDOWS and arguments[:1] == ['--software']:
            arguments = arguments[1:]
            use_lavapipe()
        elif arguments[:1] == ['--game-dir'] and len(arguments) > 1:
            game_option, arguments = arguments[1], arguments[2:]
        else:
            break
    # BB_PREBUILT=1 (packaged builds, the AppImage; on Windows a built out/): nothing is built
    # and no nix-shell is needed. Linux takes any value, Windows 1.
    prebuilt = env('BB_PREBUILT') == '1' if WINDOWS else bool(env('BB_PREBUILT'))
    # BB_DATA_DIR: writable folder for the generated files (out/), saves (user/), bbport.ini, mods
    # and patches; by default the port's folder.
    data = env('BB_DATA_DIR', str(ROOT) if WINDOWS else '.')
    out = os.path.join(data, 'out')
    os.makedirs(out, exist_ok=True)
    os.environ['BB_CONFIG'] = config = env('BB_CONFIG', os.path.join(data, 'bbport.ini'))
    # FSR 4.1.1 assets (tools/fsr4cap/build_assets.sh): in the port's folder or the data folder.
    if not env('BB_FSR411_DIR') and not os.path.isdir('fsr4_411') and os.path.isdir(os.path.join(data, 'fsr4_411')):
        os.environ['BB_FSR411_DIR'] = os.path.join(data, 'fsr4_411')

    # The game folder. A relative --game-dir or BB_GAME_DIR is taken from the folder this started
    # in: run.sh changes to the port's folder first, run.bat does not.
    remembered = os.path.join(out, 'game_dir.txt')
    game, base = game_option or env('BB_GAME_DIR'), caller_dir
    if not game and WINDOWS and os.path.isfile(remembered):
        game = Path(remembered).read_text(encoding='utf-8').strip()
    if not game:
        game, base = str(ROOT.parent / 'CUSA03173') if WINDOWS else '../CUSA03173', ROOT
    if not os.path.isfile(os.path.join(base, game, 'eboot.bin')):
        sys.exit(f'No eboot.bin in {game} (pass --game-dir or set BB_GAME_DIR).')
    original = Path(base, game).resolve()
    if WINDOWS:
        # The last folder that worked is remembered, so run.bat alone starts the game afterwards.
        Path(remembered).write_text(str(original), encoding='utf-8')
    if WINDOWS or game_option:
        os.environ['BB_GAME_DIR'] = str(original)  # for restarts
    if WINDOWS:
        # The in-game menu's "Apply and restart" runs this launcher again (probe.c runtime_restart;
        # from the port's folder, hence the resolved --game-dir). On Linux the game execs
        # `bash run.sh` instead, which BB_GAME_DIR above serves.
        restart = ['--game-dir', str(original), *arguments] if game_option else sys.argv[1:]
        os.environ['BB_RESTART_COMMAND'] = subprocess.list2cmdline(
            [PYTHON, str(Path(__file__).resolve()), *restart])

    merged = run([PYTHON, 'scripts/mods.py', original, '--out', out,
                  '--mods-dir', env('BB_MODS_DIR', os.path.join(data, 'mods')),
                  '--config', env('BB_MODS_CONFIG', os.path.join(data, 'mods.json')),
                  '--enabled', env('BB_MODS_ENABLED', '1')], capture=True)
    if not merged:
        sys.exit('Mods: no game folder from scripts/mods.py')
    # A private merged view lasts for this launch, including restarts. Only our own view is removed.
    overlay = merged if Path(merged).resolve() != original else None
    try:
        if overlay and not WINDOWS:
            signal.signal(signal.SIGTERM, interrupted)
            signal.signal(signal.SIGINT, interrupted)
        for script, extra in (('prepare.py', []), ('link_libc.py', []), ('link_modules.py', []),
                              ('content_profile.py', ['--sku', env('BB_CONTENT_SKU', 'full')])):
            run([PYTHON, f'scripts/{script}', merged, '--out', out, *extra])
        # Sizes chosen below for the previous launch are recomputed after an in-game restart.
        if os.environ.get('BB_AUTO_RENDER_RES') == '1':
            for key in ('BB_RENDER_RES', 'BB_OUTPUT_RES', 'BB_AUTO_RENDER_RES'):
                os.environ.pop(key, None)
        # An in-game restart may change the output resolution. Recompute only values selected by
        # this launcher; an explicit BB_DMEM_MB supplied by a developer remains authoritative.
        clear_automatic_direct_memory()
        # BB_RENDER_RES=WxH explicitly sets the game's render resolution (a patch at start).
        # Frame rate: BB_FPS=uncap (default; delta-time patch, vblank follows the display),
        # 60/90 (fixed-timestep patches) or 30 (unpatched). BB_PATCHES adds patch names ("a;b").
        fps = env('BB_FPS', 'uncap')
        # bbport.ini output_res other than 1080p (720p for the Steam Deck, 1440p, 2160p): the whole
        # game renders at the preset's size of the output (a patch), the upscaler fills the output,
        # the UI is drawn at the output size. Preset and output changes need a restart.
        # Live resolution changes keep the guest at 1920x1080 and scale host targets at run time
        # instead (output and presets change in the menu without a restart, but post-processing
        # stays at 1080p and scene targets are copied back: much slower on the Steam Deck and
        # older GPUs). Chosen by BB_LIVE_RES=0/1, else bbport.ini live_resolution=0/1/auto (auto:
        # the GPU check, strong discrete GPUs get them); off when unset. 1080p output and TAA
        # always use the live path.
        scaled_render = scaled_output = None
        if not env('BB_RENDER_RES'):
            sizes = run([PYTHON, 'scripts/patches.py', '--print-scaled', '--settings', config],
                        capture=True, check=False).split()
            if len(sizes) == 2:
                scaled_render, scaled_output = sizes
        probe, caps = programs(out, prebuilt)
        if WINDOWS and not prebuilt:
            build()  # before the GPU check, which it builds (Linux builds last, as run.sh did)
        live = '0'
        if scaled_output:
            live = env('BB_LIVE_RES') or configured_live_resolution(config)
            if live == 'auto':
                live = gpu_check(caps)
            live = '1' if live == '1' else '0'
        if live == '1':
            print(f'Output {scaled_output}: live resolution changes (live_resolution=0: startup patch)')
        elif scaled_output:
            os.environ.update(BB_RENDER_RES=scaled_render, BB_OUTPUT_RES=scaled_output, BB_AUTO_RENDER_RES='1')
            direct_memory = configure_direct_memory(scaled_output)
            print(f'Output {scaled_output}: scene {scaled_render}, direct memory {direct_memory} MiB '
                  '(live_resolution=1: live changes)')
        run([PYTHON, 'scripts/patches.py', '--out', out, '--fps', fps, '--extra', env('BB_PATCHES'),
             '--settings', config, '--game-dir', merged, '--render-res', env('BB_RENDER_RES'),
             '--output-res', env('BB_OUTPUT_RES'),
             '--patches-dir', env('BB_PATCHES_DIR', os.path.join(data, 'patches')),
             '--patches-config', env('BB_PATCHES_CONFIG', os.path.join(data, 'patches.json'))])
        if not env('BB_VBLANK_HZ'):
            os.environ['BB_VBLANK_HZ'] = {'uncap': '0', '90': '90'}.get(fps, '60')
        if not WINDOWS:
            optimize_fsr4(prebuilt)
            if not prebuilt:
                build()
        command = [probe, os.path.join(out, 'boot-linked.bin'),
                   '--content-profile', os.path.join(out, 'content.bin'),
                   '--patches', os.path.join(out, 'patches.bin'), '--app0', merged,
                   '--user', env('BB_USER_DIR', os.path.join(data, 'user')),
                   '--timeout', env('BB_TIMEOUT', '0'), *arguments]
        if WINDOWS:
            # MSYS2's DLLs (libc++, SDL3, FFmpeg, ...). System32 is searched before PATH, so the
            # Vulkan loader stays the one installed with the GPU driver.
            os.environ['PATH'] = os.pathsep.join([str(msys_root() / 'clang64/bin'), os.environ.get('PATH', '')])
        if os.environ.get('BB_DRY_RUN') == '1':
            describe_dry_run(command, probe)
            return 0
        return start(command, probe, overlay)
    finally:
        if overlay:
            remove_overlay(overlay)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Interrupted as received:
        number = received.args[0]
        signal.signal(number, signal.SIG_DFL)
        os.kill(os.getpid(), number)
        sys.exit(128 + number)
