#!/usr/bin/env python3
"""Mobile package, toolchain, and source-provenance checks.

The personal path prefixes are assembled at runtime so this file can be
scanned by the same rule it enforces.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path


HOME_PREFIX = "/" + "Users" + "/"
UNIX_HOME_PREFIX = "/" + "home" + "/"
WINDOWS_HOME_PREFIX = "C:" + "\\Users" + "\\"
PRIVATE_KEY = "BEGIN " + "PRIVATE KEY"
OR_TRUE = "||" + " true"
PASSWORD_VALUE = re.compile(
    r"(?:--ks-pass|--key-pass|-storepass|-keypass)\s+(\S+)"
)
PUBLIC_DEBUG_PASSWORDS = {"android", "pass:android"}
BUNDLE_ID = re.compile(r"^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
NOX_VERSION_ASSIGN = re.compile(r"NOX_VERSION\s*=\s*\"?(v[0-9][^\"\s]*)")
ORIENTATIONS = {"landscape", "portrait"}
FORBIDDEN_SUFFIXES = {
    ".apk",
    ".aab",
    ".ipa",
    ".keystore",
    ".jks",
    ".p12",
    ".mobileprovision",
    ".pem",
    ".p8",
}
BINARY_OK = {".png", ".jpg", ".jpeg", ".ttf", ".otf", ".gif", ".webp", ".icns", ".ico"}
SCAN_SUFFIXES = {
    ".sh",
    ".py",
    ".nox",
    ".c",
    ".h",
    ".m",
    ".cc",
    ".java",
    ".json",
    ".yml",
    ".yaml",
    ".xml",
    ".def",
}
SKIP_DIRS = {".git", ".nox", "build", "third_party", "native/build"}


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            block = handle.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def walk_files(root: Path) -> list[Path]:
    found: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(root):
        current = Path(dirpath)
        relative = current.relative_to(root)
        dirnames[:] = [
            name
            for name in dirnames
            if name not in SKIP_DIRS and str(relative / name) not in SKIP_DIRS
        ]
        for name in filenames:
            found.append(current / name)
    return found


def repo_files(root: Path) -> list[Path]:
    listed = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        check=False,
        capture_output=True,
    )
    if listed.returncode != 0:
        return walk_files(root)
    return [root / name.decode("utf-8", errors="surrogateescape") for name in listed.stdout.split(b"\0") if name]


def scan_sources(root: Path) -> list[str]:
    errors: list[str] = []
    for path in repo_files(root):
        relative = path.relative_to(root).as_posix()
        suffix = path.suffix.lower()
        if suffix in FORBIDDEN_SUFFIXES:
            errors.append(f"{relative}: generated or signing file is in the source tree")
            continue
        if path.is_symlink():
            continue
        data = path.read_bytes()
        if b"\0" in data[:8192] and suffix not in BINARY_OK:
            errors.append(f"{relative}: binary content is not a listed asset")
            continue
        if suffix not in SCAN_SUFFIXES:
            continue
        text = data.decode("utf-8", errors="replace")
        if HOME_PREFIX in text or UNIX_HOME_PREFIX in text or WINDOWS_HOME_PREFIX in text:
            errors.append(f"{relative}: personal absolute path is a source contract")
        if PRIVATE_KEY in text:
            errors.append(f"{relative}: private key material is in source")
        for match in PASSWORD_VALUE.finditer(text):
            value = (match.group(1) or "").strip("\"'")
            if value not in PUBLIC_DEBUG_PASSWORDS:
                errors.append(f"{relative}: signing password is not the public debug credential")
        if suffix == ".sh" or suffix == ".yml" or suffix == ".yaml":
            for line in text.splitlines():
                if OR_TRUE in line and not re.search(r"\b(cat|ls)\b", line):
                    errors.append(f"{relative}: a critical step is forced green")
    return errors


def validate_app(document: dict, root: Path, toolchain: dict) -> list[str]:
    errors: list[str] = []
    if document.get("schema") != 1:
        errors.append("app manifest schema must be 1")
    bundle = document.get("bundle_id", "")
    if not isinstance(bundle, str) or BUNDLE_ID.fullmatch(bundle) is None:
        errors.append("bundle id must be reverse-dns")
    if document.get("display_name", "") == "":
        errors.append("display name is empty")
    if document.get("version") != toolchain["aura"] or VERSION.fullmatch(str(document.get("version", ""))) is None:
        errors.append("app version must match the Aura pin")
    build = document.get("build_number")
    if not isinstance(build, int) or isinstance(build, bool) or build < 1:
        errors.append("build number must be an integer >= 1")
    ios = document.get("ios")
    android = document.get("android")
    if not isinstance(ios, dict) or ios.get("minimum") != toolchain["ios"]["minimum"]:
        errors.append("iOS minimum must match the toolchain pin")
    if not isinstance(android, dict):
        errors.append("android manifest section is missing")
        android = {}
    if android.get("min_sdk") != toolchain["android"]["min_sdk"]:
        errors.append("Android min sdk must match the toolchain pin")
    if android.get("target_sdk") != toolchain["android"]["target_sdk"]:
        errors.append("Android target sdk must match the toolchain pin")
    if android.get("allow_backup") is not False:
        errors.append("Android backup must be explicitly disabled")
    for label, section in (("ios", ios), ("android", android)):
        orientations = section.get("orientations")
        if orientations != sorted(set(orientations or [])):
            errors.append(f"{label} orientations must be unique and sorted")
        elif not set(orientations or []).issubset(ORIENTATIONS):
            errors.append(f"{label} orientation is unsupported")
    for field in ("permissions", "purpose_strings", "entitlements", "icons", "deep_links"):
        values = document.get(field)
        if not isinstance(values, list) or values != sorted(values):
            errors.append(f"{field} must be a sorted unique list")
        elif len(values) != len(set(json.dumps(item, sort_keys=True) for item in values)):
            errors.append(f"{field} contains a duplicate")
    network = document.get("network")
    if not isinstance(network, dict) or network.get("cleartext") is not False:
        errors.append("cleartext network traffic must be disabled")
    assets = document.get("assets")
    if not isinstance(assets, list):
        return errors + ["assets must be a list"]
    paths = [item.get("path") for item in assets if isinstance(item, dict)]
    if paths != sorted(paths):
        errors.append("assets must be sorted by path")
    if len(paths) != len(set(paths)):
        errors.append("asset paths collide")
    for item in assets:
        if not isinstance(item, dict):
            errors.append("asset entry must be an object")
            continue
        name = item.get("path", "")
        digest = item.get("sha256", "")
        if not isinstance(name, str) or name == "" or name.startswith("/") or ".." in name.split("/"):
            errors.append("asset path must be a relative repo path")
            continue
        target = root / name
        if not target.is_file():
            errors.append(f"missing asset: {name}")
            continue
        actual = sha256_file(target)
        if actual != digest:
            errors.append(f"asset hash mismatch: {name}")
    return errors


def toolchain_errors(root: Path) -> list[str]:
    errors: list[str] = []
    manifest_path = root / "toolchain" / "manifest.json"
    app_path = root / "examples" / "mobile" / "app.json"
    try:
        toolchain = load_json(manifest_path)
        app = load_json(app_path)
        package = load_json(root / "nox.json")
        components = load_json(root / "toolchain" / "components.json")
    except (OSError, json.JSONDecodeError) as exc:
        return [f"package manifest cannot be read: {exc}"]
    if package.get("version") != toolchain.get("aura"):
        errors.append("nox.json version and the Aura pin differ")
    nox = toolchain.get("nox", {})
    if nox.get("minimum") != "2.0.0" or nox.get("maximum") != "2.0.0" or nox.get("ci") != "v2.0.0":
        errors.append("Nox pin must stay on the tested 2.0.0 range")
    if nox.get("embed_abi") != 1:
        errors.append("Nox embed ABI pin must be 1")
    identity = (root / "native" / "mobile" / "build_identity.h").read_text(encoding="utf-8")
    if f'#define AURA_VERSION "{toolchain["aura"]}"' not in identity:
        errors.append("build identity drifted from the Aura pin")
    if f'#define AURA_EMBED_ABI {toolchain["nox"]["embed_abi"]}' not in identity:
        errors.append("build identity drifted from the embed ABI pin")
    android_xml = (root / "native" / "android" / "AndroidManifest.xml").read_text(encoding="utf-8")
    if f'minSdkVersion="{toolchain["android"]["min_sdk"]}"' not in android_xml:
        errors.append("AndroidManifest min sdk drifted from the toolchain pin")
    if f'targetSdkVersion="{toolchain["android"]["target_sdk"]}"' not in android_xml:
        errors.append("AndroidManifest target sdk drifted from the toolchain pin")
    if 'android:allowBackup="false"' not in android_xml:
        errors.append("AndroidManifest must disable backup")
    ios_script = (root / "scripts" / "build_ios_app.sh").read_text(encoding="utf-8")
    android_script = (root / "scripts" / "build_android_app.sh").read_text(encoding="utf-8")
    if toolchain["ios"]["minimum"] not in ios_script:
        errors.append("iOS build script drifted from the minimum pin")
    if toolchain["android"]["ndk"] not in android_script and '["ndk"]' not in android_script:
        errors.append("Android build script does not read the NDK pin")
    if "manifest.json" not in android_script or "manifest.json" not in ios_script:
        errors.append("mobile build scripts must read toolchain/manifest.json")
    workflow = (root / ".github" / "workflows" / "certify.yml").read_text(encoding="utf-8")
    pins = NOX_VERSION_ASSIGN.findall(workflow)
    if not pins or any(pin != toolchain["nox"]["ci"] for pin in pins):
        errors.append("CI Nox version drifted from the toolchain pin")
    names = [item.get("name") for item in components.get("components", [])]
    if names != sorted(set(names)):
        errors.append("component names must be unique and sorted")
    ignore = (root / ".gitignore").read_text(encoding="utf-8")
    for token in ("*.apk", "*.aab", "*.ipa", "*.keystore", "*.jks", "*.p12", "*.mobileprovision"):
        if token not in ignore:
            errors.append(f"gitignore is missing {token}")
    errors.extend(validate_app(app, root, toolchain))
    errors.extend(scan_sources(root))
    proof = canonical(proof_document(root))
    if HOME_PREFIX in proof or UNIX_HOME_PREFIX in proof or "storepass" in proof or PRIVATE_KEY in proof:
        errors.append("build proof contains a path or signing secret")
    if (root / ".git").exists() and '"aura_commit":"unavailable"' in proof:
        errors.append("Aura commit could not be recorded")
    return errors


def proof_document(root: Path) -> dict:
    toolchain = load_json(root / "toolchain" / "manifest.json")
    aura = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    nox_commit = "unavailable"
    nox_root = os.environ.get("NOX_ROOT", "")
    sibling = root.parent / "nox-lang"
    candidate = Path(nox_root) if nox_root else sibling
    if (candidate / "include" / "nox_embed.h").is_file():
        nox = subprocess.run(
            ["git", "-C", str(candidate), "rev-parse", "HEAD"],
            check=False,
            capture_output=True,
            text=True,
        )
        if nox.returncode == 0:
            nox_commit = nox.stdout.strip()
    return {
        "kind": "build-proof",
        "run": False,
        "backend": "skia-raster",
        "host": platform.system(),
        "aura_commit": aura.stdout.strip() if aura.returncode == 0 else "unavailable",
        "nox_commit": nox_commit,
        "nox_ci": toolchain["nox"]["ci"],
        "embed_abi": toolchain["nox"]["embed_abi"],
        "toolchain_sha256": sha256_file(root / "toolchain" / "manifest.json"),
        "component_sha256": sha256_file(root / "toolchain" / "components.json"),
    }


def sbom_document(root: Path) -> dict:
    components = load_json(root / "toolchain" / "components.json")["components"]
    ordered = sorted(components, key=lambda item: item["name"])
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "aura-mobile",
        "documentNamespace": "https://aura.noxlang.local/sbom/mobile",
        "packages": [
            {
                "name": item["name"],
                "SPDXID": "SPDXRef-" + item["name"].replace(" ", "-"),
                "versionInfo": item["version"],
                "licenseDeclared": item["license"],
            }
            for item in ordered
        ],
    }


def command_text(args: list[str]) -> tuple[int, str]:
    try:
        completed = subprocess.run(args, check=False, capture_output=True, text=True, timeout=8)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, str(exc)
    return completed.returncode, (completed.stdout or "") + (completed.stderr or "")


def collect_facts(root: Path) -> dict[str, str]:
    facts: dict[str, str] = {"host": platform.system() + " " + platform.machine()}
    code, text = command_text(["java", "-version"])
    match = re.search(r'version "(\d+)', text)
    facts["java"] = match.group(1) if code == 0 and match else "missing"
    version_args = {
        "xcodebuild": ["xcodebuild", "-version"],
        "zig": ["zig", "version"],
        "clang": ["clang", "--version"],
        "gradle": ["gradle", "-version"],
        "adb": ["adb", "version"],
    }
    for name, args in version_args.items():
        code, text = command_text(args)
        if name == "xcodebuild":
            first = text.splitlines()[0] if code == 0 and text.splitlines() else "missing"
            facts[name] = first
        elif code == 0 and text.strip():
            facts[name] = text.splitlines()[0].strip()
        else:
            facts[name] = "missing"
    nox_root = os.environ.get("NOX_ROOT", "")
    candidate = Path(nox_root) if nox_root else root.parent / "nox-lang"
    facts["nox_source"] = "present" if (candidate / "include" / "nox_embed.h").is_file() else "missing"
    toolchain = load_json(root / "toolchain" / "manifest.json")
    pin = toolchain["android"]["ndk"]
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    if not sdk:
        default_sdk = Path.home() / "Library" / "Android" / "sdk"
        sdk = str(default_sdk) if default_sdk.is_dir() else ""
    ndk = os.environ.get("ANDROID_NDK_HOME") or (str(Path(sdk) / "ndk" / pin) if sdk else "")
    facts["ndk"] = "present" if ndk and Path(ndk).is_dir() else "missing"
    skia = os.environ.get("SKIA_SRC") or f"/tmp/skia-{toolchain['skia']}-src"
    facts["skia"] = "present" if Path(skia).is_dir() else "missing"
    code, text = command_text(["xcrun", "simctl", "list", "devices", "available"])
    if code == 0:
        facts["simulators"] = str(len(re.findall(r"\([0-9A-Fa-f-]{36}\)", text)))
    else:
        facts["simulators"] = "unavailable"
    if facts["adb"] != "missing":
        code, text = command_text(["adb", "devices"])
        devices = [line for line in text.splitlines() if line.endswith("\tdevice")]
        facts["android_devices"] = str(len(devices)) if code == 0 else "unavailable"
    else:
        facts["android_devices"] = "unavailable"
    code, text = command_text(["security", "find-identity", "-p", "codesigning", "-v"])
    found = re.search(r"(\d+) valid identities found", text)
    facts["ios_identities"] = found.group(1) if code == 0 and found else "unavailable"
    facts["debug_keystore"] = "present" if (root / "build" / "android" / "debug.keystore").is_file() else "absent"
    return facts


def doctor_report(root: Path, facts: dict[str, str]) -> tuple[str, int]:
    toolchain = load_json(root / "toolchain" / "manifest.json")
    lines = [
        f"host: {facts['host']}",
        f"aura: {toolchain['aura']}",
        f"nox: {toolchain['nox']['minimum']}..{toolchain['nox']['maximum']} ci {toolchain['nox']['ci']}",
        f"embed_abi: {toolchain['nox']['embed_abi']}",
        f"skia: {toolchain['skia']} source {facts['skia']}",
        f"targets: {', '.join(toolchain['targets'])}",
        f"nox_source: {facts['nox_source']}",
        f"android_ndk: {toolchain['android']['ndk']} {facts['ndk']}",
        f"ios_minimum: {toolchain['ios']['minimum']}",
        f"java: {facts['java']}",
        f"xcode: {facts['xcodebuild']}",
        f"clang: {facts['clang']}",
        f"zig: {facts['zig']}",
        f"gradle: {facts['gradle']}",
        f"simulators: {facts['simulators']}",
        f"android_devices: {facts['android_devices']}",
        f"signing.ios_identities: {facts['ios_identities']}",
        f"signing.android_debug_keystore: {facts['debug_keystore']}",
    ]
    missing: list[str] = []
    if facts["nox_source"] != "present":
        missing.append("Nox source is missing. Set NOX_ROOT to the tree that contains include/nox_embed.h.")
    if facts["ndk"] != "present":
        missing.append(
            f"Android NDK {toolchain['android']['ndk']} is missing. Install that NDK or set ANDROID_NDK_HOME."
        )
    java = facts["java"]
    if java == "missing" or int(java) < toolchain["android"]["jdk_minimum"]:
        missing.append(f"JDK {toolchain['android']['jdk_minimum']} or newer is required. Install a JDK and put java on PATH.")
    for tool in toolchain["unpinned"]:
        key = "xcodebuild" if tool == "xcode" else tool
        if facts.get(key) == "missing":
            lines.append(f"optional-missing: {tool}")
    text = "\n".join(lines + missing)
    if HOME_PREFIX in text or "storepass" in text or PRIVATE_KEY in text:
        return "doctor report leaked a path or secret", 1
    return text, 1 if missing else 0


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="aura-package-") as temporary:
        root = Path(temporary)
        (root / "secret.keystore").write_text("not-a-real-key", encoding="utf-8")
        if not any("signing file" in error for error in scan_sources(root)):
            print("package self-test: keystore was accepted", file=sys.stderr)
            return 1
    with tempfile.TemporaryDirectory(prefix="aura-package-") as temporary:
        root = Path(temporary)
        script = root / "build.sh"
        script.write_text("echo " + HOME_PREFIX + "someone\nbuild " + OR_TRUE + "\n", encoding="utf-8")
        errors = scan_sources(root)
        if not any("personal absolute path" in error for error in errors):
            print("package self-test: personal path was accepted", file=sys.stderr)
            return 1
        if not any("forced green" in error for error in errors):
            print("package self-test: forced-green step was accepted", file=sys.stderr)
            return 1
        script.write_text("keytool -store" + "pass secret\n", encoding="utf-8")
        if not any("signing password" in error for error in scan_sources(root)):
            print("package self-test: private password was accepted", file=sys.stderr)
            return 1
        script.write_text("cat log " + OR_TRUE + "\n", encoding="utf-8")
        if any("forced green" in error for error in scan_sources(root)):
            print("package self-test: diagnostic cat was rejected", file=sys.stderr)
            return 1
    aura = Path(__file__).resolve().parent.parent
    proof = proof_document(aura)
    encoded = canonical(proof)
    for key in ("aura_commit", "nox_commit", "nox_ci", "embed_abi", "toolchain_sha256", "backend", "run"):
        if key not in proof:
            print(f"package self-test: proof missing {key}", file=sys.stderr)
            return 1
    if proof["run"] is not False or proof["backend"] != "skia-raster":
        print("package self-test: proof kind is wrong", file=sys.stderr)
        return 1
    if HOME_PREFIX in encoded or "storepass" in encoded:
        print("package self-test: proof leaked", file=sys.stderr)
        return 1
    first = canonical(sbom_document(aura))
    second = canonical(sbom_document(aura))
    if first != second or "Skia" not in first:
        print("package self-test: sbom is not stable", file=sys.stderr)
        return 1
    sample = aura / "tests" / "fixtures" / "pixel.png"
    if sha256_file(sample) != "b1ff9c8ea3a780bad09b346c423d2d0e46815926879b18e841d928376a946640":
        print("package self-test: fixture hash changed", file=sys.stderr)
        return 1
    app = load_json(aura / "examples" / "mobile" / "app.json")
    toolchain = load_json(aura / "toolchain" / "manifest.json")
    if validate_app(app, aura, toolchain):
        print("package self-test: checked-in app manifest is invalid", file=sys.stderr)
        return 1
    broken = json.loads(json.dumps(app))
    broken["assets"] = list(reversed(broken["assets"]))
    if not validate_app(broken, aura, toolchain):
        print("package self-test: unsorted assets were accepted", file=sys.stderr)
        return 1
    print("mobile package self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="?", choices=["doctor", "proof", "sbom", "hash"])
    parser.add_argument("path", nargs="?")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    if args.self_test:
        return run_self_test()
    if args.check:
        errors = toolchain_errors(root)
        if errors:
            for error in errors:
                print(f"mobile package: {error}", file=sys.stderr)
            return 1
        print("mobile package: toolchain, manifest, and source scan are clean")
        return 0
    if args.command == "doctor":
        text, code = doctor_report(root, collect_facts(root))
        print(text)
        return code
    if args.command == "proof":
        print(canonical(proof_document(root)))
        return 0
    if args.command == "sbom":
        print(canonical(sbom_document(root)))
        return 0
    if args.command == "hash":
        if args.path is None:
            print("usage: mobile_package.py hash FILE", file=sys.stderr)
            return 2
        print(sha256_file(Path(args.path)))
        return 0
    parser.print_help(sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
