#!/usr/bin/env python3
"""Run, build, test, clean, and list devices for an Aura checkout or app."""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import app_icon  # noqa: E402
import mobile_package  # noqa: E402


PLATFORMS = {"macos", "ios", "android", "linux", "windows"}
PRODUCTS = {"apk", "aab", "ipa", "macos", "linux", "windows"}
RUN_MODES = {
    "macos": {"debug", "release"},
    "ios": {"simulator", "device"},
    "android": {"debug"},
    "linux": {"debug", "release"},
    "windows": {"debug", "release"},
}
BUILD_MODES = {
    "apk": {"debug", "release"},
    "aab": {"release"},
    "ipa": {"simulator", "device", "store"},
    "macos": {"debug", "release"},
    "linux": {"debug", "release"},
    "windows": {"debug", "release"},
}


def redact(text: str) -> str:
    return mobile_package.redact(text)


def repair_path() -> None:
    parts = [item for item in os.environ.get("PATH", "").split(":") if item]
    repo = Path(__file__).resolve().parents[1]
    candidates = [
        Path("/opt/homebrew/bin"),
        Path("/usr/local/bin"),
        Path.home() / ".nox-lang" / "bin",
        Path.home() / ".nox" / "bin",
        repo.parent / "nox-lang" / "zig-out" / "bin",
    ]
    nox_root = os.environ.get("NOX_ROOT", "")
    if nox_root:
        candidates.append(Path(nox_root) / "zig-out" / "bin")
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME") or ""
    if not sdk:
        default_sdk = Path.home() / "Library" / "Android" / "sdk"
        if default_sdk.is_dir():
            sdk = str(default_sdk)
    if sdk:
        candidates.append(Path(sdk) / "platform-tools")
    for candidate in candidates:
        text = str(candidate)
        if candidate.is_dir() and text not in parts:
            parts.insert(0, text)
    for system in ("/usr/bin", "/bin", "/usr/sbin", "/sbin"):
        if system not in parts:
            parts.append(system)
    os.environ["PATH"] = ":".join(parts)


def run_stream(args: list[str], cwd: Path) -> int:
    shown = " ".join(redact(item) for item in args)
    print("==> " + shown, flush=True)
    completed = subprocess.run(args, cwd=cwd, check=False)
    return completed.returncode


def find_checkout(start: Path) -> Path | None:
    current = start.resolve()
    while True:
        if (current / "bin" / "aura.nox").is_file() and (current / "scripts" / "aura_cmd.py").is_file():
            return current
        if current.parent == current:
            return None
        current = current.parent


def find_project(start: Path) -> Path:
    current = start.resolve()
    while True:
        if (current / "nox.json").is_file():
            return current
        if current.parent == current:
            return start.resolve()
        current = current.parent


def nox_home() -> Path:
    override = os.environ.get("NOX_HOME", "")
    if override:
        return Path(override)
    return Path.home() / ".nox"


def cache_for(sha: str) -> Path | None:
    if not sha:
        return None
    candidate = nox_home() / "pkg" / "mod" / "github.com" / "mburakmmm" / "aura" / sha
    if (candidate / "scripts" / "build_android_gradle.sh").is_file():
        return candidate
    return None


def installed_cache() -> Path | None:
    registry = nox_home() / "pkg" / "installed.json"
    if not registry.is_file():
        return None
    data = json.loads(registry.read_text(encoding="utf-8"))
    for item in data.get("packages") or []:
        if item.get("command_name") == "aura":
            return cache_for(str(item.get("resolved_sha", "")))
    return None


def find_aura_root(project: Path) -> Path | None:
    checkout = find_checkout(project)
    if checkout is not None:
        return checkout
    lock = project / "nox.lock"
    if lock.is_file():
        data = json.loads(lock.read_text(encoding="utf-8"))
        for item in data.get("packages") or []:
            if item.get("alias") == "aura":
                found = cache_for(str(item.get("resolved", "")))
                if found is not None:
                    return found
    return installed_cache()


def display_name(token: str) -> str:
    if token == "":
        return "App"
    return token[0].upper() + token[1:]


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def nearest_main(start: Path, project: Path) -> Path | None:
    current = start.resolve()
    boundary = project.resolve()
    while current != boundary:
        candidate = current / "main.nox"
        if candidate.is_file():
            return candidate
        parent = current.parent
        if parent == current:
            return None
        current = parent
    return None


def resolve_entry(project: Path, root: Path | None, token: str, start: Path | None = None) -> tuple[Path, str]:
    if token == "":
        origin = project if start is None else start
        local = nearest_main(origin, project)
        if local is not None:
            return local, display_name(local.parent.name)
        manifest = read_json(project / "nox.json") if (project / "nox.json").is_file() else {}
        entry_name = str(manifest.get("entry", "main.nox"))
        entry = project / entry_name
        return entry, display_name(str(manifest.get("name", entry.parent.name)))
    if "/" not in token and "\\" not in token and not token.endswith(".nox") and root is not None:
        example = root / "examples" / token / "main.nox"
        if example.is_file():
            return example, display_name(token)
    candidate = Path(token)
    if not candidate.is_absolute():
        candidate = project / token
    parent = candidate.parent.name
    if parent not in {"", ".", "examples"}:
        name = display_name(parent if candidate.name == "main.nox" else candidate.stem)
    else:
        name = display_name(candidate.stem)
    return candidate, name


def bundle_prefix(root: Path) -> str:
    manifest = root / "examples" / "mobile" / "app.json"
    if not manifest.is_file():
        return "com.noxlang.aura"
    return str(read_json(manifest).get("bundle_id", "com.noxlang.aura"))


def last_line(text: str) -> str:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def capture(args: list[str], cwd: Path) -> tuple[int, str]:
    print("==> " + " ".join(redact(item) for item in args), flush=True)
    completed = subprocess.run(args, cwd=cwd, check=False, capture_output=True, text=True)
    if completed.stdout:
        print(redact(completed.stdout), end="" if completed.stdout.endswith("\n") else "\n")
    if completed.stderr:
        print(redact(completed.stderr), end="" if completed.stderr.endswith("\n") else "\n", file=sys.stderr)
    return completed.returncode, completed.stdout or ""


def adb_args(device: str) -> list[str]:
    executable = mobile_package.adb_executable() or "adb"
    if device:
        return [executable, "-s", device]
    return [executable]


def parse_simulators(text: str) -> list[str]:
    rows: list[str] = []
    for line in text.splitlines():
        start = line.find("(")
        end = line.find(")")
        if start < 0 or end < start:
            continue
        identifier = line[start + 1 : end]
        if len(identifier) < 30 or identifier.count("-") < 4:
            continue
        name = line[:start].strip()
        state = "Booted" if "(Booted)" in line else "Shutdown"
        rows.append(f"ios {identifier} {name} {state}")
    return rows


def parse_adb(text: str) -> list[str]:
    rows: list[str] = []
    for line in text.splitlines():
        if "\tdevice" not in line:
            continue
        serial = line.split("\t", 1)[0].strip()
        if serial and serial != "List of devices attached":
            rows.append(f"android {serial} device")
    return rows


class Request:
    def __init__(self) -> None:
        self.command = ""
        self.entry = ""
        self.product = ""
        self.platform = ""
        self.mode = ""
        self.device = ""
        self.verbose = False
        self.error = ""


def take_value(argv: list[str], index: int, flag: str) -> tuple[str, int, str]:
    if index + 1 >= len(argv):
        return "", index, f"missing value for {flag}"
    return argv[index + 1], index + 2, ""


def parse_args(argv: list[str]) -> Request:
    parsed = Request()
    if not argv:
        parsed.command = "help"
        return parsed
    parsed.command = argv[0]
    index = 1
    positionals: list[str] = []
    while index < len(argv):
        token = argv[index]
        if token in {"--platform", "--mode", "--device"}:
            value, index, error = take_value(argv, index, token)
            if error:
                parsed.error = error
                return parsed
            if token == "--platform":
                parsed.platform = value
            elif token == "--mode":
                parsed.mode = value
            else:
                parsed.device = value
        elif token in {"--verbose", "-v"}:
            parsed.verbose = True
            index += 1
        elif token.startswith("-"):
            parsed.error = "unknown option: " + token
            return parsed
        else:
            positionals.append(token)
            index += 1
    if parsed.command == "build":
        if not positionals:
            parsed.error = "build needs a product: apk, aab, ipa, macos, linux, or windows"
            return parsed
        parsed.product = positionals[0]
        if len(positionals) > 1:
            parsed.entry = positionals[1]
        if len(positionals) > 2:
            parsed.error = "build accepts one entry"
            return parsed
    elif parsed.command == "run":
        if positionals:
            parsed.entry = positionals[0]
        if len(positionals) > 1:
            parsed.error = "run accepts one entry"
            return parsed
    elif positionals and parsed.command not in {"new"}:
        parsed.error = parsed.command + " does not take a positional argument"
        return parsed
    return parsed


def apply_defaults(parsed: Request) -> None:
    if parsed.command == "run" and parsed.platform == "":
        parsed.platform = "macos"
    if parsed.command == "run" and parsed.mode == "":
        parsed.mode = "simulator" if parsed.platform == "ios" else "debug"
    if parsed.command == "build" and parsed.mode == "":
        if parsed.product == "aab":
            parsed.mode = "release"
        elif parsed.product == "ipa":
            parsed.mode = "device"
        else:
            parsed.mode = "debug"


def validate(parsed: Request) -> str:
    if parsed.error:
        return parsed.error
    if parsed.command == "run":
        if parsed.platform not in PLATFORMS:
            return "platform must be macos, ios, android, linux, or windows"
        if parsed.mode not in RUN_MODES[parsed.platform]:
            return f"{parsed.platform} run mode must be one of: " + ", ".join(sorted(RUN_MODES[parsed.platform]))
        if parsed.platform == "ios" and parsed.mode == "device" and parsed.device == "":
            return "ios device run needs --device"
        if parsed.platform == "android" and parsed.mode != "debug":
            return "android release is an app bundle. Use: aura build aab"
    if parsed.command == "build":
        if parsed.product not in PRODUCTS:
            return "product must be apk, aab, ipa, macos, linux, or windows"
        if parsed.mode not in BUILD_MODES[parsed.product]:
            return f"{parsed.product} mode must be one of: " + ", ".join(sorted(BUILD_MODES[parsed.product]))
    return ""


def require_root(project: Path) -> Path:
    root = find_aura_root(project)
    if root is None:
        print("Aura source was not found. Run this inside the Aura checkout, or fetch the aura dependency.", file=sys.stderr)
        raise SystemExit(2)
    return root


def require_file(path: Path) -> None:
    if not path.is_file():
        print(f"entry not found: {path}", file=sys.stderr)
        raise SystemExit(2)


def command_devices() -> int:
    rows: list[str] = []
    code, text = mobile_package.command_text(["xcrun", "simctl", "list", "devices", "available"])
    if code == 0:
        rows.extend(parse_simulators(text))
    executable = mobile_package.adb_executable()
    if executable:
        code, text = mobile_package.command_text([executable, "devices"])
        if code == 0:
            rows.extend(parse_adb(text))
    if not rows:
        print("no devices")
        return 0
    print("\n".join(rows))
    return 0


def command_doctor(project: Path, verbose: bool) -> int:
    root = find_aura_root(project)
    if root is None:
        print("aura doctor needs an Aura checkout or a fetched aura package.", file=sys.stderr)
        return 2
    text, code = mobile_package.doctor_report(root, mobile_package.collect_facts(root), verbose)
    print(text)
    return code


def command_clean(project: Path) -> int:
    target = project / "build"
    if target.is_dir():
        import shutil

        shutil.rmtree(target)
    print("cleaned build")
    return 0


def command_test(project: Path) -> int:
    if (project / "bin" / "aura.nox").is_file() and (project / "scripts" / "test.sh").is_file():
        return run_stream([str(project / "scripts" / "test.sh")], project)
    return run_stream(["noxc", "test"], project)


def ensure_host(root: Path) -> int:
    host = root / "native" / "libaura_host.dylib"
    script = root / "scripts" / "build_host.sh"
    if host.is_file() or not script.is_file():
        return 0
    return run_stream([str(script)], root)


def install_android(root: Path, name: str, apk: Path, device: str) -> int:
    if run_stream(adb_args(device) + ["install", "-r", str(apk)], root) != 0:
        return 1
    application = bundle_prefix(root) + "." + name.lower()
    component = application + "/com.noxlang.aura.AuraHost"
    return run_stream(adb_args(device) + ["shell", "am", "start", "-n", component], root)


def install_simulator(app: Path, device: str, application: str, cwd: Path) -> int:
    target = device or "booted"
    if run_stream(["xcrun", "simctl", "install", target, str(app)], cwd) != 0:
        return 1
    return run_stream(["xcrun", "simctl", "launch", target, application], cwd)


def install_device(ipa: Path, device: str, application: str, cwd: Path) -> int:
    if run_stream(["xcrun", "devicectl", "device", "install", "app", "--device", device, str(ipa)], cwd) != 0:
        return 1
    return run_stream(["xcrun", "devicectl", "device", "process", "launch", "--device", device, application], cwd)


def host_matches(product: str) -> bool:
    system = platform.system()
    return (product == "macos" and system == "Darwin") or (product == "linux" and system == "Linux") or (product == "windows" and system == "Windows")


def desktop_target(product: str) -> str:
    machine = platform.machine().lower()
    if product == "windows":
        return "windows-x64"
    if product == "linux":
        if platform.system() == "Linux" and machine in {"aarch64", "arm64"}:
            return "linux-arm64"
        return "linux-x64"
    if machine in {"aarch64", "arm64"}:
        return "macos-arm64"
    return "macos-x64"


def library_matches(path: Path, product: str) -> bool:
    if not path.is_file():
        return False
    magic = path.read_bytes()[:4]
    if product == "linux":
        return magic == b"\x7fELF"
    if product == "windows":
        return magic[:2] == b"MZ"
    return magic[:4] in {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe"}


def noxc_executable(target: str) -> str:
    repo = Path(__file__).resolve().parents[1]
    candidates = [
        repo.parent / "nox-lang" / "zig-out" / "bin" / "noxc",
        Path.home() / ".nox-lang" / "bin" / "noxc",
        Path.home() / ".nox" / "bin" / "noxc",
    ]
    nox_root = os.environ.get("NOX_ROOT", "")
    if nox_root:
        candidates.insert(0, Path(nox_root) / "zig-out" / "bin" / "noxc")
    found = [candidate for candidate in candidates if candidate.is_file()]
    located = shutil.which("noxc")
    if located:
        found.append(Path(located))
    if target:
        for candidate in found:
            runtime = candidate.resolve().parent.parent / "lib" / f"noxrt-{target}.o"
            if runtime.is_file():
                return str(candidate)
        if found:
            return str(found[0])
    if found:
        return str(found[0])
    return "noxc"


def desktop_output(project: Path, product: str, name: str) -> Path:
    if product == "macos":
        return project / "build" / "macos" / f"{name}.app" / "Contents" / "MacOS" / name
    if product == "windows":
        return project / "build" / "windows" / f"{name}.exe"
    return project / "build" / "linux" / name


def stamp_desktop(project: Path, product: str, name: str, icon: Path, bundle_id: str) -> None:
    if product == "macos":
        app_icon.stamp_macos(project / "build" / "macos" / f"{name}.app", name, icon, bundle_id)
        return
    if product == "windows":
        app_icon.stamp_windows(project / "build" / "windows", name, icon)
        return
    app_icon.stamp_linux(project / "build" / "linux", name, icon)


def compile_desktop(root: Path, entry: Path, product: str, output: Path) -> int:
    output.parent.mkdir(parents=True, exist_ok=True)
    host = root / "native" / "libaura_host.dylib"
    target = desktop_target(product)
    if host_matches(product):
        if ensure_host(root) != 0:
            return 1
        if not library_matches(host, product):
            print("the Aura host library does not match this operating system", file=sys.stderr)
            return 1
        return run_stream([noxc_executable(""), "build", "-o", str(output), str(entry)], root)
    if not library_matches(host, product):
        print(
            f"aura cannot link {target} with the host library on this machine. scripts/build_host.sh produces that library on {product}; Nox then compiles the program with --target {target}.",
            file=sys.stderr,
        )
        return 1
    compiler = noxc_executable(target)
    runtime = Path(compiler).resolve().parent.parent / "lib" / f"noxrt-{target}.o"
    if not runtime.is_file():
        print(f"noxc does not include the {target} runtime", file=sys.stderr)
        return 1
    return run_stream([compiler, "build", "--target", target, "-o", str(output), str(entry)], root)


def command_desktop(project: Path, root: Path, entry: Path, name: str, product: str, launch: bool) -> int:
    icon = app_icon.resolve_icon(project, root)
    if icon.is_file():
        os.environ["AURA_APP_ICON"] = str(icon)
    output = desktop_output(project, product, name)
    if compile_desktop(root, entry, product, output) != 0:
        return 1
    if icon.is_file():
        stamp_desktop(project, product, name, icon, bundle_prefix(root) + "." + name.lower())
    if launch and host_matches(product):
        return run_stream([str(output)], project)
    print(output if product != "macos" else project / "build" / "macos" / f"{name}.app")
    return 0


def activate_icon(project: Path, root: Path) -> None:
    icon = app_icon.resolve_icon(project, root)
    if icon.is_file():
        os.environ["AURA_APP_ICON"] = str(icon)


def command_run(project: Path, parsed: Request) -> int:
    root = require_root(project)
    activate_icon(project, root)
    entry, name = resolve_entry(project, root, parsed.entry, Path.cwd())
    require_file(entry)
    application = bundle_prefix(root) + "." + name.lower()
    if parsed.platform in {"macos", "linux", "windows"}:
        return command_desktop(project, root, entry, name, parsed.platform, True)
    if parsed.platform == "android":
        code, output = capture(
            [str(root / "scripts" / "build_android_gradle.sh"), str(entry), name, "debug"],
            root,
        )
        if code != 0:
            return code
        apk = Path(last_line(output))
        if not apk.is_file():
            print("android build did not report an apk", file=sys.stderr)
            return 1
        return install_android(root, name, apk, parsed.device)
    code, output = capture(
        [str(root / "scripts" / "build_ios_xcode.sh"), str(entry), name, parsed.mode],
        root,
    )
    if code != 0:
        return code
    artifact = Path(last_line(output))
    if not artifact.exists():
        print("ios build did not report an artifact", file=sys.stderr)
        return 1
    if parsed.mode == "simulator":
        return install_simulator(artifact, parsed.device, application, root)
    return install_device(artifact, parsed.device, application, root)


def command_build(project: Path, parsed: Request) -> int:
    root = require_root(project)
    activate_icon(project, root)
    entry, name = resolve_entry(project, root, parsed.entry, Path.cwd())
    require_file(entry)
    if parsed.product in {"macos", "linux", "windows"}:
        return command_desktop(project, root, entry, name, parsed.product, False)
    if parsed.product in {"apk", "aab"}:
        kind = "debug" if parsed.product == "apk" and parsed.mode == "debug" else "release"
        environment = os.environ.copy()
        environment["AURA_ANDROID_PRODUCT"] = "apk" if parsed.product == "apk" else "aab"
        print("==> android " + parsed.product + " " + kind, flush=True)
        completed = subprocess.run(
            [str(root / "scripts" / "build_android_gradle.sh"), str(entry), name, kind],
            cwd=root,
            check=False,
            env=environment,
        )
        return completed.returncode
    return run_stream([str(root / "scripts" / "build_ios_xcode.sh"), str(entry), name, parsed.mode], root)


def dispatch(argv: list[str], project: Path) -> int:
    parsed = parse_args(argv)
    if parsed.command in {"", "help"}:
        print(
            "\n".join(
                [
                    "usage: aura <command>",
                    "  version",
                    "  new <name>",
                    "  doctor [-v]",
                    "  devices",
                    "  run [entry] [--platform macos|ios|android|linux|windows] [--mode debug|release|simulator|device] [--device id]",
                    "  build apk|aab|ipa|macos|linux|windows [entry] [--mode debug|release|simulator|device|store]",
                    "  test",
                    "  clean",
                    "  help",
                ]
            )
        )
        return 0
    if parsed.command == "doctor":
        return command_doctor(project, parsed.verbose)
    if parsed.command == "devices":
        if parsed.error:
            print(parsed.error, file=sys.stderr)
            return 2
        return command_devices()
    if parsed.command == "clean":
        if parsed.error:
            print(parsed.error, file=sys.stderr)
            return 2
        return command_clean(project)
    if parsed.command == "test":
        if parsed.error:
            print(parsed.error, file=sys.stderr)
            return 2
        return command_test(project)
    apply_defaults(parsed)
    error = validate(parsed)
    if error:
        print(error, file=sys.stderr)
        return 2
    if parsed.command == "run":
        return command_run(project, parsed)
    if parsed.command == "build":
        return command_build(project, parsed)
    print("unknown command: " + parsed.command, file=sys.stderr)
    return 2


def self_test() -> int:
    simulators = parse_simulators(
        "    iPhone 17 (AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE) (Booted)\n"
        "    iPhone 17 Pro (BBBBBBBB-BBBB-CCCC-DDDD-EEEEEEEEEEEE) (Shutdown)\n"
    )
    if simulators != [
        "ios AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE iPhone 17 Booted",
        "ios BBBBBBBB-BBBB-CCCC-DDDD-EEEEEEEEEEEE iPhone 17 Pro Shutdown",
    ]:
        print("aura command self-test: simulators were parsed wrong", file=sys.stderr)
        return 1
    attached = parse_adb("List of devices attached\nb8a62e23\tdevice\n")
    if attached != ["android b8a62e23 device"]:
        print("aura command self-test: adb devices were parsed wrong", file=sys.stderr)
        return 1
    parsed = parse_args(["run", "notes", "--platform", "android", "--device", "b8a62e23"])
    apply_defaults(parsed)
    if validate(parsed) or parsed.entry != "notes" or parsed.platform != "android" or parsed.mode != "debug" or parsed.device != "b8a62e23":
        print("aura command self-test: run flags were not kept", file=sys.stderr)
        return 1
    release = parse_args(["run", "--platform", "android", "--mode", "release"])
    apply_defaults(release)
    if validate(release) == "":
        print("aura command self-test: android release run was accepted", file=sys.stderr)
        return 1
    built = parse_args(["build", "aab", "notes"])
    apply_defaults(built)
    if validate(built) or built.mode != "release" or built.product != "aab":
        print("aura command self-test: aab did not default to release", file=sys.stderr)
        return 1
    if display_name("notes") != "Notes":
        print("aura command self-test: display name was not capitalized", file=sys.stderr)
        return 1
    import tempfile

    with tempfile.TemporaryDirectory() as raw:
        tree = Path(raw)
        project = tree / "aura"
        dashboard = project / "examples" / "dashboard"
        dashboard.mkdir(parents=True)
        (project / "nox.json").write_text('{"name":"aura","entry":"examples/counter/main.nox"}\n', encoding="utf-8")
        (dashboard / "main.nox").write_text("print(1)\n", encoding="utf-8")
        found, name = resolve_entry(project, project, "", dashboard)
        if found.resolve() != (dashboard / "main.nox").resolve() or name != "Dashboard":
            print("aura command self-test: a directory with main.nox did not win over the package entry", file=sys.stderr)
            return 1
        package_entry, package_name = resolve_entry(project, project, "", project)
        if package_entry.resolve() != (project / "examples" / "counter" / "main.nox").resolve() or package_name != "Aura":
            print("aura command self-test: the package entry was not kept at the project root", file=sys.stderr)
            return 1
    linux = parse_args(["build", "linux", "notes"])
    apply_defaults(linux)
    if validate(linux) or linux.product != "linux" or linux.mode != "debug":
        print("aura command self-test: linux build was rejected", file=sys.stderr)
        return 1
    windows = parse_args(["run", "--platform", "windows"])
    apply_defaults(windows)
    if validate(windows) or windows.platform != "windows" or windows.mode != "debug":
        print("aura command self-test: windows run was rejected", file=sys.stderr)
        return 1
    print("aura command self-test: ok")
    return 0


def main() -> int:
    repair_path()
    if "--self-test" in sys.argv:
        return self_test()
    project = find_project(Path.cwd())
    return dispatch(sys.argv[1:], project)


if __name__ == "__main__":
    raise SystemExit(main())
