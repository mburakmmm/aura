#!/usr/bin/env python3
"""Generate an Aura iOS Xcode project from the mobile manifest.

The project, entitlements and export plist land under build/. Signing
identifiers come from the local provisioning profile or the environment.
They are not written into the source tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
HOME_MARKERS = ("/" + "Users" + "/", "/" + "home" + "/")
APP_SOURCES = (
    "native/host/listing.c",
    "native/host/plugin.c",
    "native/ios/scene.m",
    "native/mobile/app_runtime.c",
    "native/mobile/ax_platform.c",
    "native/mobile/event_queue.c",
    "native/mobile/events.c",
    "native/mobile/shell.c",
    "native/mobile/storage.c",
    "native/mobile/text_units.c",
    "native/skia/paint.cc",
)
SKIA_LIBS = (
    "libskia.a",
    "libpng.a",
    "libjpeg.a",
    "libwebp.a",
    "libzlib.a",
    "libfreetype2.a",
    "libskcms.a",
)
FRAMEWORKS = (
    "UIKit",
    "Foundation",
    "CoreGraphics",
    "QuartzCore",
    "CoreText",
    "Security",
    "CFNetwork",
)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def bundle_id(app: dict, name: str) -> str:
    if NAME_RE.fullmatch(name) is None:
        raise ValueError(f"app name must be an identifier: {name}")
    base = app["bundle_id"]
    return f"{base}.{name}"


def orientations(names: list[str]) -> list[str]:
    values: list[str] = []
    if "portrait" in names:
        values.append("UIInterfaceOrientationPortrait")
    if "landscape" in names:
        values.extend(
            [
                "UIInterfaceOrientationLandscapeLeft",
                "UIInterfaceOrientationLandscapeRight",
            ]
        )
    return values


def purpose_map(values: list[str]) -> dict[str, str]:
    found: dict[str, str] = {}
    for item in values:
        if not isinstance(item, str) or "=" not in item:
            raise ValueError("purpose string must be Key=text")
        key, text = item.split("=", 1)
        if not key.endswith("UsageDescription") or text == "":
            raise ValueError(f"purpose string is not a usage description: {item}")
        found[key] = text
    return found


def extra_entitlements(values: list[str]) -> dict[str, object]:
    found: dict[str, object] = {}
    for item in values:
        if not isinstance(item, str) or item == "" or item.startswith("/"):
            raise ValueError(f"entitlement must be a key: {item}")
        if "=" in item:
            key, text = item.split("=", 1)
            found[key] = text
        else:
            found[item] = True
    return found


def info_plist(app: dict, name: str) -> dict:
    ios = app["ios"]
    identifier = bundle_id(app, name)
    plist: dict = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": name,
        "CFBundleExecutable": name,
        "CFBundleIconName": "AppIcon",
        "CFBundleIdentifier": identifier,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": name,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": app["version"],
        "CFBundleVersion": str(app["build_number"]),
        "ITSAppUsesNonExemptEncryption": False,
        "LSRequiresIPhoneOS": True,
        "UIRequiresFullScreen": True,
        "MinimumOSVersion": ios["minimum"],
        "NSUserActivityTypes": ["com.noxlang.aura.scene"],
        "UIApplicationSceneManifest": {
            "UIApplicationSupportsMultipleScenes": False,
            "UISceneConfigurations": {
                "UIWindowSceneSessionRoleApplication": [
                    {
                        "UISceneConfigurationName": "Default",
                        "UISceneDelegateClassName": "AuraSceneDelegate",
                    }
                ]
            },
        },
        "UIDeviceFamily": [1, 2],
        "UILaunchStoryboardName": "LaunchScreen",
        "UISupportedInterfaceOrientations": orientations(ios["orientations"]),
        "UISupportedInterfaceOrientations~ipad": orientations(ios["orientations"]),
    }
    plist.update(purpose_map(app.get("purpose_strings") or []))
    return plist


def entitlements_plist(team: str, identifier: str, app: dict, development: bool) -> dict:
    values = {
        "application-identifier": f"{team}.{identifier}",
        "com.apple.developer.team-identifier": team,
    }
    if development:
        values["get-task-allow"] = True
    values.update(extra_entitlements(app.get("entitlements") or []))
    return values


def xcode_entitlements(app: dict, development: bool) -> dict:
    values = {
        "application-identifier": "$(AppIdentifierPrefix)$(PRODUCT_BUNDLE_IDENTIFIER)",
        "com.apple.developer.team-identifier": "$(DEVELOPMENT_TEAM)",
    }
    if development:
        values["get-task-allow"] = True
    values.update(extra_entitlements(app.get("entitlements") or []))
    return values


def privacy_plist() -> dict:
    return {
        "NSPrivacyTracking": False,
        "NSPrivacyTrackingDomains": [],
        "NSPrivacyCollectedDataTypes": [],
        "NSPrivacyAccessedAPITypes": [],
    }


def launch_storyboard() -> str:
    return """<?xml version="1.0" encoding="UTF-8"?>
<document type="com.apple.InterfaceBuilder3.CocoaTouch.Storyboard.XIB" version="3.0" toolsVersion="21701" targetRuntime="iOS.CocoaTouch" propertyAccessControl="none" useAutolayout="YES" launchScreen="YES" useTraitCollections="YES" colorMatched="YES">
    <scenes>
        <scene sceneID="AURASCENE01">
            <objects>
                <viewController storyboardIdentifier="Launch" id="AURAVC01" sceneMemberID="viewController">
                    <view key="view" contentMode="scaleToFill" id="AURAVIEW01">
                        <rect key="frame" x="0.0" y="0.0" width="390" height="844"/>
                        <color key="backgroundColor" red="0.9725490196078431" green="0.9647058823529412" blue="0.9490196078431372" alpha="1" colorSpace="custom" customColorSpace="sRGB"/>
                    </view>
                </viewController>
                <placeholder placeholderIdentifier="IBFirstResponder" id="AURAFR01" sceneMemberID="firstResponder"/>
            </objects>
        </scene>
    </scenes>
</document>
"""


def icon_rgb(x: int, y: int) -> tuple[int, int, int]:
    if 96 <= x < 928 and 96 <= y < 928:
        if y >= 800:
            return (196, 122, 74)
        return (248, 246, 242)
    return (28, 27, 25)


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def icon_png() -> bytes:
    width = 1024
    height = 1024
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(icon_rgb(x, y))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + _png_chunk(b"IEND", b"")


def write_icon(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(icon_png())


def asset_catalog(directory: Path) -> None:
    images = directory / "AppIcon.appiconset"
    images.mkdir(parents=True, exist_ok=True)
    write_icon(images / "AppIcon.png")
    (directory / "Contents.json").write_text(
        json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n",
        encoding="utf-8",
    )
    (images / "Contents.json").write_text(
        json.dumps(
            {
                "images": [
                    {
                        "filename": "AppIcon.png",
                        "idiom": "universal",
                        "platform": "ios",
                        "size": "1024x1024",
                    }
                ],
                "info": {"author": "xcode", "version": 1},
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def write_plist(path: Path, value: dict) -> None:
    with path.open("wb") as handle:
        plistlib.dump(value, handle, fmt=plistlib.FMT_XML, sort_keys=False)


def stable_id(key: str) -> str:
    return hashlib.sha1(key.encode("utf-8")).hexdigest()[:24].upper()


def source_kind(path: str) -> str:
    if path.endswith(".m"):
        return "sourcecode.c.objc"
    if path.endswith(".cc"):
        return "sourcecode.cpp.cpp"
    return "sourcecode.c.c"


def pbx_quote(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def pbx_list(items: tuple[str, ...] | list[str]) -> str:
    return "(" + ", ".join(pbx_quote(item) for item in items) + ")"


def select_profile(
    profiles: list[dict],
    identifier: str,
    development: bool,
    team: str = "",
    identity: str = "",
) -> dict | None:
    best: dict | None = None
    best_score = -1
    for profile in profiles:
        if bool(profile.get("get_task")) != development:
            continue
        if team and profile.get("team") != team:
            continue
        app_id = str(profile.get("app_id", ""))
        profile_team, dot, suffix = app_id.partition(".")
        if dot != "." or profile_team == "" or profile_team == "*":
            continue
        if suffix != identifier and suffix != "*":
            continue
        certs = {item.upper() for item in profile.get("cert_shas", [])}
        if identity and identity.upper() not in certs:
            continue
        score = 10 if suffix == identifier else 1
        if score > best_score:
            best = profile
            best_score = score
    return best


def choose_identity(profile: dict, identities: list[dict], requested: str = "") -> dict | None:
    allowed = [item for item in identities if item["sha"].upper() in {cert.upper() for cert in profile.get("cert_shas", [])}]
    if requested:
        needle = requested.upper()
        for item in allowed:
            if item["sha"].upper() == needle or requested == item["name"]:
                return item
        return None
    for item in allowed:
        if item["name"].startswith("Apple Development"):
            return item
    return allowed[0] if allowed else None


def load_identities() -> list[dict]:
    text = subprocess.check_output(["security", "find-identity", "-v", "-p", "codesigning"], text=True)
    found = []
    for line in text.splitlines():
        match = re.search(r"([0-9A-F]{40}) \"(.*)\"", line)
        if match:
            found.append({"sha": match.group(1), "name": match.group(2)})
    return found


def load_profiles(directory: Path) -> list[dict]:
    profiles = []
    if not directory.is_dir():
        return profiles
    for path in sorted(directory.glob("*.mobileprovision")):
        raw = subprocess.check_output(["security", "cms", "-D", "-i", str(path)])
        data = plistlib.loads(raw)
        platforms = data.get("Platform") or []
        if "iOS" not in platforms:
            continue
        entitlements = data.get("Entitlements") or {}
        teams = data.get("TeamIdentifier") or []
        if len(teams) != 1:
            continue
        certs = [hashlib.sha1(bytes(item)).hexdigest().upper() for item in data.get("DeveloperCertificates") or []]
        profiles.append(
            {
                "path": str(path),
                "uuid": data.get("UUID", ""),
                "name": data.get("Name", ""),
                "team": teams[0],
                "app_id": entitlements.get("application-identifier", ""),
                "get_task": bool(entitlements.get("get-task-allow")),
                "cert_shas": certs,
            }
        )
    return profiles


def resolve_signing(root: Path, name: str, kind: str) -> dict:
    app = load_json(root / "examples" / "mobile" / "app.json")
    identifier = bundle_id(app, name)
    development = kind == "development"
    requested_team = os.environ.get("AURA_IOS_TEAM_ID", "")
    requested_identity = os.environ.get("AURA_IOS_SIGN_IDENTITY", "")
    requested_profile = os.environ.get("AURA_IOS_PROFILE_UUID", "")
    directory = Path(os.environ.get("AURA_IOS_PROFILE_DIR", str(Path.home() / "Library/Developer/Xcode/UserData/Provisioning Profiles")))
    profiles = load_profiles(directory)
    if requested_profile:
        profiles = [item for item in profiles if item["uuid"] == requested_profile]
    profile = select_profile(profiles, identifier, development, requested_team, requested_identity)
    if profile is None:
        kind_name = "development" if development else "App Store"
        raise SystemExit(f"no {kind_name} profile matches {identifier}")
    identity = choose_identity(profile, load_identities(), requested_identity)
    if identity is None:
        raise SystemExit(f"no signing identity in profile {profile['uuid']} matches {identifier}")
    return {
        "team": profile["team"],
        "identity": identity["sha"],
        "identity_name": identity["name"],
        "profile_uuid": profile["uuid"],
        "profile_name": profile["name"],
        "profile_path": profile["path"],
        "bundle_id": identifier,
        "kind": kind,
    }


def export_options(signing: dict) -> dict:
    method = "debugging" if signing["kind"] == "development" else "app-store-connect"
    return {
        "method": method,
        "signingStyle": "automatic",
        "teamID": signing["team"],
        "stripSwiftSymbols": True,
        "uploadSymbols": signing["kind"] != "development",
        "manageAppVersionAndBuildNumber": False,
    }


def _settings(name: str, app: dict, signing: dict | None, configuration: str) -> list[tuple[str, str]]:
    ios = app["ios"]
    development = configuration != "Store"
    slice_name = "ios-simulator-arm64" if configuration == "Debug" else "ios-arm64"
    sdk = "iphonesimulator" if configuration == "Debug" else "iphoneos"
    values = [
        ("AURA_BUILD_NUMBER", str(app["build_number"])),
        ("AURA_MARKETING_VERSION", app["version"]),
        ("AURA_NOX_SLICE", slice_name),
        ("ALWAYS_SEARCH_USER_PATHS", "NO"),
        ("CLANG_CXX_LANGUAGE_STANDARD", "c++17"),
        ("CLANG_ENABLE_MODULES", "NO"),
        ("CLANG_ENABLE_OBJC_ARC", "YES"),
        ("CODE_SIGN_ENTITLEMENTS", f"{name}.entitlements"),
        ("CURRENT_PROJECT_VERSION", "$(AURA_BUILD_NUMBER)"),
        ("DEBUG_INFORMATION_FORMAT", "dwarf" if configuration == "Debug" else "dwarf-with-dsym"),
        ("ENABLE_NS_ASSERTIONS", "YES" if configuration == "Debug" else "NO"),
        ("ENABLE_USER_SCRIPT_SANDBOXING", "NO"),
        ("GENERATE_INFOPLIST_FILE", "NO"),
        ("INFOPLIST_FILE", "Info.plist"),
        ("IPHONEOS_DEPLOYMENT_TARGET", ios["minimum"]),
        ("LD_RUNPATH_SEARCH_PATHS", "$(inherited) @executable_path/Frameworks"),
        ("MARKETING_VERSION", "$(AURA_MARKETING_VERSION)"),
        ("ONLY_ACTIVE_ARCH", "NO"),
        ("PRODUCT_BUNDLE_IDENTIFIER", bundle_id(app, name)),
        ("PRODUCT_NAME", name),
        ("SDKROOT", sdk),
        ("SUPPORTED_PLATFORMS", sdk),
        ("TARGETED_DEVICE_FAMILY", "1,2"),
    ]
    if configuration == "Debug" or signing is None:
        values.extend(
            [
                ("CODE_SIGNING_ALLOWED", "NO"),
                ("CODE_SIGN_IDENTITY", "-"),
            ]
        )
    else:
        certificate = "Apple Development" if configuration != "Store" else "Apple Distribution"
        values.extend(
            [
                ("CODE_SIGN_IDENTITY", certificate),
                ("CODE_SIGN_STYLE", "Automatic"),
                ("DEVELOPMENT_TEAM", signing["team"]),
            ]
        )
    if not development:
        values.append(("AURA_SIGNING_KIND", "store"))
    headers = (
        "$(AURA_REPO)/native/mobile",
        "$(AURA_REPO)/native/host",
        "$(AURA_REPO)/native/skia",
        "$(NOX_ROOT)/include",
        "$(SKIA_SRC)",
    )
    link = ["$(BUILT_PRODUCTS_DIR)/app.o", "$(NOX_ROOT)/zig-out/lib/noxrt-$(AURA_NOX_SLICE).o"]
    link.extend(f"$(SKIA_OUT)/{item}" for item in SKIA_LIBS)
    for framework in FRAMEWORKS:
        link.extend(["-framework", framework])
    link.append("-lc++")
    values.append(("HEADER_SEARCH_PATHS", pbx_list(headers)))
    values.append(("OTHER_LDFLAGS", pbx_list(link)))
    values.append(("ASSETCATALOG_COMPILER_APPICON_NAME", "AppIcon"))
    return values


def _probe_settings(app: dict, name: str, configuration: str, signing: dict | None) -> list[tuple[str, str]]:
    sdk = "iphonesimulator" if configuration == "Debug" else "iphoneos"
    values = [
        ("CLANG_ENABLE_OBJC_ARC", "NO"),
        ("CODE_SIGNING_ALLOWED", "NO"),
        ("CODE_SIGN_IDENTITY", "-"),
        ("CURRENT_PROJECT_VERSION", str(app["build_number"])),
        ("DEFINES_MODULE", "NO"),
        ("DYLIB_COMPATIBILITY_VERSION", "1"),
        ("DYLIB_CURRENT_VERSION", "1"),
        ("EXECUTABLE_EXTENSION", "dylib"),
        ("EXECUTABLE_PREFIX", ""),
        ("GENERATE_INFOPLIST_FILE", "NO"),
        ("INFOPLIST_FILE", "Probe-Info.plist"),
        ("IPHONEOS_DEPLOYMENT_TARGET", app["ios"]["minimum"]),
        ("LD_DYLIB_INSTALL_NAME", "@executable_path/native/plugins/libaura_probe.dylib"),
        ("MACH_O_TYPE", "mh_dylib"),
        ("MARKETING_VERSION", app["version"]),
        ("PRODUCT_BUNDLE_IDENTIFIER", f"{bundle_id(app, name)}.probe"),
        ("PRODUCT_NAME", "libaura_probe"),
        ("SDKROOT", sdk),
        ("SKIP_INSTALL", "YES"),
        ("SUPPORTED_PLATFORMS", sdk),
        ("CREATE_INFOPLIST_SECTION_IN_BINARY", "YES"),
    ]
    return values


def project_text(root: Path, out: Path, name: str, app: dict, signing: dict | None, configuration: str) -> str:
    prefix = Path(os.path.relpath(root, out)).as_posix()
    if prefix.startswith("/"):
        raise ValueError("project paths must stay relative")
    ids: dict[str, str] = {}

    def ident(key: str) -> str:
        if key not in ids:
            ids[key] = stable_id(f"{name}:{key}")
        return ids[key]

    def file_ref(key: str, path: str, kind: str) -> str:
        return (
            f"\t\t{ident(key)} = {{isa = PBXFileReference; explicitFileType = {pbx_quote(kind)}; "
            f"includeInIndex = 0; path = {pbx_quote(path)}; sourceTree = SOURCE_ROOT; }};\n"
        )

    configs = ("Debug", "Release", "Store")
    lines = [
        "// !$*UTF8*$!\n",
        "{\n",
        "\tarchiveVersion = 1;\n",
        "\tclasses = {\n",
        "\t};\n",
        "\tobjectVersion = 56;\n",
        "\tobjects = {\n\n",
    ]
    build_files = []
    file_refs = []
    source_ids = []
    for path in APP_SOURCES:
        key = f"src:{path}"
        flags = '-fno-objc-arc' if path.endswith("scene.m") else ""
        settings = f" settings = {{COMPILER_FLAGS = \"{flags}\"; }};" if flags else ""
        build_files.append(f"\t\t{ident('build:' + path)} = {{isa = PBXBuildFile; fileRef = {ident(key)};{settings} }};\n")
        file_refs.append(file_ref(key, f"{prefix}/{path}", source_kind(path)))
        source_ids.append(ident("build:" + path))
    probe_key = "src:native/plugins/probe.c"
    build_files.append(f"\t\t{ident('build:probe')} = {{isa = PBXBuildFile; fileRef = {ident(probe_key)}; }};\n")
    file_refs.append(file_ref(probe_key, f"{prefix}/native/plugins/probe.c", source_kind("native/plugins/probe.c")))
    resources = (
        ("resource:launch", "LaunchScreen.storyboard", "file.storyboard"),
        ("resource:assets", "Assets.xcassets", "folder.assetcatalog"),
        ("resource:privacy", "PrivacyInfo.xcprivacy", "text.xml"),
    )
    resource_ids = []
    for key, path, kind in resources:
        build_files.append(f"\t\t{ident('build:' + key)} = {{isa = PBXBuildFile; fileRef = {ident(key)}; }};\n")
        file_refs.append(file_ref(key, path, kind))
        resource_ids.append(ident("build:" + key))
    file_refs.append(file_ref("info", "Info.plist", "text.plist.xml"))
    file_refs.append(file_ref("probe-info", "Probe-Info.plist", "text.plist.xml"))
    file_refs.append(file_ref("entitlements", f"{name}.entitlements", "text.plist.entitlements"))
    file_refs.append(
        f"\t\t{ident('product:app')} = {{isa = PBXFileReference; explicitFileType = wrapper.application; "
        f"includeInIndex = 0; path = {name}.app; sourceTree = BUILT_PRODUCTS_DIR; }};\n"
    )
    file_refs.append(
        "\t\t"
        + ident("product:probe")
        + " = {isa = PBXFileReference; explicitFileType = \"compiled.mach-o.dylib\"; includeInIndex = 0; path = libaura_probe.dylib; sourceTree = BUILT_PRODUCTS_DIR; };\n"
    )
    lines.append("/* Begin PBXBuildFile section */\n")
    lines.extend(build_files)
    lines.append("/* End PBXBuildFile section */\n\n")
    lines.append("/* Begin PBXFileReference section */\n")
    lines.extend(file_refs)
    lines.append("/* End PBXFileReference section */\n\n")
    lines.append(
        "/* Begin PBXFrameworksBuildPhase section */\n"
        f"\t\t{ident('phase:frameworks')} = {{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (\n\t\t); runOnlyForDeploymentPostprocessing = 0; }};\n"
        f"\t\t{ident('phase:probe-frameworks')} = {{isa = PBXFrameworksBuildPhase; buildActionMask = 2147483647; files = (\n\t\t); runOnlyForDeploymentPostprocessing = 0; }};\n"
        "/* End PBXFrameworksBuildPhase section */\n\n"
    )
    source_children = ", ".join(ident(f"src:{path}") for path in APP_SOURCES)
    lines.append(
        "/* Begin PBXGroup section */\n"
        f"\t\t{ident('group:root')} = {{isa = PBXGroup; children = ({ident('group:sources')}, {ident('group:packaging')}, {ident('group:products')}); sourceTree = \"<group>\"; }};\n"
        f"\t\t{ident('group:sources')} = {{isa = PBXGroup; children = ({source_children}, {ident(probe_key)}); name = Sources; sourceTree = \"<group>\"; }};\n"
        f"\t\t{ident('group:packaging')} = {{isa = PBXGroup; children = ({ident('info')}, {ident('probe-info')}, {ident('entitlements')}, {ident('resource:launch')}, {ident('resource:assets')}, {ident('resource:privacy')}); name = Packaging; sourceTree = \"<group>\"; }};\n"
        f"\t\t{ident('group:products')} = {{isa = PBXGroup; children = ({ident('product:app')}, {ident('product:probe')}); name = Products; sourceTree = \"<group>\"; }};\n"
        "/* End PBXGroup section */\n\n"
    )
    lines.append(
        "/* Begin PBXNativeTarget section */\n"
        f"\t\t{ident('target:app')} = {{isa = PBXNativeTarget; buildConfigurationList = {ident('configs:app')}; buildPhases = ({ident('phase:nox')}, {ident('phase:sources')}, {ident('phase:frameworks')}, {ident('phase:resources')}, {ident('phase:embed')}); buildRules = (); dependencies = ({ident('dep:probe')}); name = {name}; productName = {name}; productReference = {ident('product:app')}; productType = \"com.apple.product-type.application\"; }};\n"
        f"\t\t{ident('target:probe')} = {{isa = PBXNativeTarget; buildConfigurationList = {ident('configs:probe')}; buildPhases = ({ident('phase:probe-sources')}, {ident('phase:probe-frameworks')}); buildRules = (); dependencies = (); name = libaura_probe; productName = libaura_probe; productReference = {ident('product:probe')}; productType = \"com.apple.product-type.library.dynamic\"; }};\n"
        "/* End PBXNativeTarget section */\n\n"
    )
    lines.append(
        "/* Begin PBXProject section */\n"
        f"\t\t{ident('project')} = {{isa = PBXProject; attributes = {{LastUpgradeCheck = 1500; }}; buildConfigurationList = {ident('configs:project')}; compatibilityVersion = \"Xcode 14.0\"; developmentRegion = en; hasScannedForEncodings = 0; knownRegions = (en, Base); mainGroup = {ident('group:root')}; productRefGroup = {ident('group:products')}; projectDirPath = \"\"; projectRoot = \"\"; targets = ({ident('target:app')}, {ident('target:probe')}); }};\n"
        "/* End PBXProject section */\n\n"
    )
    resource_list = ", ".join(resource_ids)
    source_list = ", ".join(source_ids)
    lines.append(
        "/* Begin PBXResourcesBuildPhase section */\n"
        f"\t\t{ident('phase:resources')} = {{isa = PBXResourcesBuildPhase; buildActionMask = 2147483647; files = ({resource_list}); runOnlyForDeploymentPostprocessing = 0; }};\n"
        "/* End PBXResourcesBuildPhase section */\n\n"
    )
    lines.append(
        "/* Begin PBXShellScriptBuildPhase section */\n"
        f"\t\t{ident('phase:nox')} = {{isa = PBXShellScriptBuildPhase; alwaysOutOfDate = 1; buildActionMask = 2147483647; files = (); inputPaths = (); name = \"Compile Nox\"; outputPaths = (\"$(BUILT_PRODUCTS_DIR)/app.o\"); runOnlyForDeploymentPostprocessing = 0; shellPath = /bin/sh; shellScript = \"\\\"${{AURA_REPO}}/native/ios/compile_nox.sh\\\"\\n\"; }};\n"
        f"\t\t{ident('phase:embed')} = {{isa = PBXShellScriptBuildPhase; alwaysOutOfDate = 1; buildActionMask = 2147483647; files = (); inputPaths = (); name = \"Embed plugin\"; outputPaths = (); runOnlyForDeploymentPostprocessing = 0; shellPath = /bin/sh; shellScript = \"\\\"${{AURA_REPO}}/native/ios/embed_plugin.sh\\\"\\n\"; }};\n"
        "/* End PBXShellScriptBuildPhase section */\n\n"
    )
    lines.append(
        "/* Begin PBXSourcesBuildPhase section */\n"
        f"\t\t{ident('phase:sources')} = {{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({source_list}); runOnlyForDeploymentPostprocessing = 0; }};\n"
        f"\t\t{ident('phase:probe-sources')} = {{isa = PBXSourcesBuildPhase; buildActionMask = 2147483647; files = ({ident('build:probe')}); runOnlyForDeploymentPostprocessing = 0; }};\n"
        "/* End PBXSourcesBuildPhase section */\n\n"
    )
    lines.append(
        "/* Begin PBXContainerItemProxy section */\n"
        f"\t\t{ident('proxy:probe')} = {{isa = PBXContainerItemProxy; containerPortal = {ident('project')}; proxyType = 1; remoteGlobalIDString = {ident('target:probe')}; remoteInfo = libaura_probe; }};\n"
        "/* End PBXContainerItemProxy section */\n\n"
        "/* Begin PBXTargetDependency section */\n"
        f"\t\t{ident('dep:probe')} = {{isa = PBXTargetDependency; target = {ident('target:probe')}; targetProxy = {ident('proxy:probe')}; }};\n"
        "/* End PBXTargetDependency section */\n\n"
    )

    def config_block(key: str, pairs: list[tuple[str, str]]) -> str:
        body = "".join(
            f"\t\t\t\t{setting} = {value if value.startswith('(') else pbx_quote(value)};\n" for setting, value in pairs
        )
        return f"\t\t{ident(key)} = {{isa = XCBuildConfiguration; buildSettings = {{\n{body}\t\t\t}}; name = {key.split(':')[-1]}; }};\n"

    lines.append("/* Begin XCBuildConfiguration section */\n")
    for config in configs:
        lines.append(config_block(f"project:{config}", [("ALWAYS_SEARCH_USER_PATHS", "NO")]))
        lines.append(config_block(f"app:{config}", _settings(name, app, signing, config)))
        lines.append(config_block(f"probe:{config}", _probe_settings(app, name, config, signing)))
    lines.append("/* End XCBuildConfiguration section */\n\n")
    lines.append("/* Begin XCConfigurationList section */\n")
    for owner in ("project", "app", "probe"):
        members = ", ".join(ident(f"{owner}:{config}") for config in configs)
        lines.append(
            f"\t\t{ident('configs:' + owner)} = {{isa = XCConfigurationList; buildConfigurations = ({members}); defaultConfigurationIsVisible = 0; defaultConfigurationName = {configuration}; }};\n"
        )
    lines.append("/* End XCConfigurationList section */\n")
    lines.append("\t};\n")
    lines.append(f"\trootObject = {ident('project')};\n}}\n")
    text = "".join(lines)
    seen: dict[str, str] = {}
    for key, value in ids.items():
        if value in seen:
            raise RuntimeError(f"duplicate project id for {key} and {seen[value]}")
        seen[value] = key
    for marker in HOME_MARKERS:
        if marker in text:
            raise RuntimeError("generated project contains a home path")
    return text


def scheme_text(name: str, configuration: str) -> str:
    blueprint = stable_id(f"{name}:target:app")
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion="1500" version="1.7">
  <BuildAction parallelizeBuildables="YES" buildImplicitDependencies="YES">
    <BuildActionEntries>
      <BuildActionEntry buildForTesting="YES" buildForRunning="YES" buildForProfiling="YES" buildForArchiving="YES" buildForAnalyzing="YES">
        <BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="{blueprint}" BuildableName="{name}.app" BlueprintName="{name}" ReferencedContainer="container:{name}.xcodeproj"/>
      </BuildActionEntry>
    </BuildActionEntries>
  </BuildAction>
  <ArchiveAction buildConfiguration="{configuration}" revealArchiveInOrganizer="NO" customArchiveName="{name}"/>
</Scheme>
"""


def probe_info(app: dict, name: str) -> dict:
    return {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "libaura_probe",
        "CFBundleIdentifier": f"{bundle_id(app, name)}.probe",
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "libaura_probe",
        "CFBundlePackageType": "FMWK",
        "CFBundleShortVersionString": app["version"],
        "CFBundleVersion": str(app["build_number"]),
        "MinimumOSVersion": app["ios"]["minimum"],
    }


def generate(root: Path, out: Path, name: str, signing: dict | None, configuration: str) -> None:
    app = load_json(root / "examples" / "mobile" / "app.json")
    out.mkdir(parents=True, exist_ok=True)
    project = out / f"{name}.xcodeproj"
    project.mkdir(exist_ok=True)
    (project / "project.pbxproj").write_text(project_text(root, out, name, app, signing, configuration), encoding="utf-8")
    scheme_dir = project / "xcshareddata" / "xcschemes"
    scheme_dir.mkdir(parents=True, exist_ok=True)
    (scheme_dir / "Aura.xcscheme").write_text(scheme_text(name, configuration), encoding="utf-8")
    write_plist(out / "Info.plist", info_plist(app, name))
    write_plist(out / "Probe-Info.plist", probe_info(app, name))
    development = configuration != "Store"
    if signing is None:
        write_plist(out / f"{name}.entitlements", xcode_entitlements(app, development))
    else:
        write_plist(out / f"{name}.entitlements", xcode_entitlements(app, signing["kind"] == "development"))
        write_plist(out / "ExportOptions.plist", export_options(signing))
    write_plist(out / "PrivacyInfo.xcprivacy", privacy_plist())
    (out / "LaunchScreen.storyboard").write_text(launch_storyboard(), encoding="utf-8")
    asset_catalog(out / "Assets.xcassets")


def _merge_plist(base: Path, extra: Path) -> None:
    if not extra.is_file():
        return
    with base.open("rb") as handle:
        current = plistlib.load(handle)
    with extra.open("rb") as handle:
        incoming = plistlib.load(handle)
    current.update(incoming)
    write_plist(base, current)


def stamp(root: Path, app_path: Path, name: str, slice_name: str, signing: dict | None) -> None:
    app = load_json(root / "examples" / "mobile" / "app.json")
    app_path.mkdir(parents=True, exist_ok=True)
    info = app_path / "Info.plist"
    write_plist(info, info_plist(app, name))
    write_plist(app_path / "PrivacyInfo.xcprivacy", privacy_plist())
    catalog = app_path.parent / "AppIcon.xcassets"
    if catalog.exists():
        for child in sorted(catalog.rglob("*"), reverse=True):
            if child.is_file():
                child.unlink()
            elif child.is_dir():
                child.rmdir()
    asset_catalog(catalog)
    partial = app_path.parent / "AppIcon-partial.plist"
    platform = "iphoneos" if slice_name == "device" else "iphonesimulator"
    subprocess.run(
        [
            "xcrun",
            "actool",
            str(catalog),
            "--compile",
            str(app_path),
            "--platform",
            platform,
            "--minimum-deployment-target",
            app["ios"]["minimum"],
            "--app-icon",
            "AppIcon",
            "--output-partial-info-plist",
            str(partial),
        ],
        check=True,
    )
    _merge_plist(info, partial)
    if slice_name != "device":
        return
    if signing is None:
        raise SystemExit("device stamp requires signing")
    plugin = app_path / "native" / "plugins" / "libaura_probe.dylib"
    if not plugin.is_file():
        raise SystemExit(f"probe dylib yok: {plugin}")
    profile = Path(signing["profile_path"])
    if not profile.is_file():
        raise SystemExit(f"profile yok: {profile}")
    target = app_path / "embedded.mobileprovision"
    target.write_bytes(profile.read_bytes())
    entitlements = app_path.parent / f"{name}.entitlements.plist"
    write_plist(entitlements, entitlements_plist(signing["team"], signing["bundle_id"], app, signing["kind"] == "development"))
    subprocess.run(["codesign", "--force", "--sign", signing["identity"], str(plugin)], check=True)
    subprocess.run(
        [
            "codesign",
            "--force",
            "--sign",
            signing["identity"],
            "--entitlements",
            str(entitlements),
            "--generate-entitlement-der",
            str(app_path),
        ],
        check=True,
    )


def run_self_test() -> int:
    icon = icon_png()
    if icon[:8] != b"\x89PNG\r\n\x1a\n":
        print("ios project self-test: icon is not a png", file=sys.stderr)
        return 1
    width, height = struct.unpack(">II", icon[16:24])
    if (width, height) != (1024, 1024) or icon_png() != icon:
        print("ios project self-test: icon is not a stable 1024 image", file=sys.stderr)
        return 1
    profiles = [
        {"team": "TEAM1", "app_id": "TEAM1.*", "get_task": True, "uuid": "wild", "cert_shas": ["AAA"]},
        {"team": "TEAM1", "app_id": "TEAM1.com.noxlang.aura.Notes", "get_task": True, "uuid": "exact", "cert_shas": ["AAA"]},
        {"team": "TEAM1", "app_id": "TEAM1.*", "get_task": False, "uuid": "store", "cert_shas": ["BBB"]},
        {"team": "TEAM2", "app_id": "TEAM2.*", "get_task": True, "uuid": "other", "cert_shas": ["CCC"]},
    ]
    chosen = select_profile(profiles, "com.noxlang.aura.Notes", True)
    if chosen is None or chosen["uuid"] != "exact":
        print("ios project self-test: exact profile did not win", file=sys.stderr)
        return 1
    if select_profile(profiles, "com.noxlang.aura.Todo", True, team="TEAM2")["uuid"] != "other":
        print("ios project self-test: team filter failed", file=sys.stderr)
        return 1
    if select_profile(profiles, "com.noxlang.aura.Todo", False)["uuid"] != "store":
        print("ios project self-test: store profile was ignored", file=sys.stderr)
        return 1
    if select_profile(profiles, "com.other.App", True, team="TEAM9") is not None:
        print("ios project self-test: unrelated team matched", file=sys.stderr)
        return 1
    exact_only = [item for item in profiles if item["uuid"] == "exact"]
    if select_profile(exact_only, "com.other.App", True) is not None:
        print("ios project self-test: exact profile matched another bundle", file=sys.stderr)
        return 1
    identity = choose_identity({"cert_shas": ["AAA", "BBB"]}, [{"sha": "BBB", "name": "Apple Distribution"}, {"sha": "AAA", "name": "Apple Development"}])
    if identity is None or identity["sha"] != "AAA":
        print("ios project self-test: development identity was not preferred", file=sys.stderr)
        return 1
    app = {
        "bundle_id": "com.noxlang.aura",
        "version": "1.0.0",
        "build_number": 1,
        "ios": {"minimum": "17.0", "orientations": ["landscape", "portrait"]},
        "purpose_strings": ["NSPhotoLibraryUsageDescription=Aura reads the selected picture."],
        "entitlements": [],
    }
    plist = info_plist(app, "Notes")
    if plist["CFBundleIdentifier"] != "com.noxlang.aura.Notes" or plist["MinimumOSVersion"] != "17.0" or plist.get("UIRequiresFullScreen") is not True:
        print("ios project self-test: manifest fields were dropped", file=sys.stderr)
        return 1
    if plist["NSPhotoLibraryUsageDescription"] != "Aura reads the selected picture.":
        print("ios project self-test: purpose string was dropped", file=sys.stderr)
        return 1
    if "UIInterfaceOrientationLandscapeLeft" not in plist["UISupportedInterfaceOrientations"]:
        print("ios project self-test: orientation was dropped", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw) / "repo"
        out = Path(raw) / "out"
        (root / "examples" / "mobile").mkdir(parents=True)
        (root / "examples" / "mobile" / "app.json").write_text(json.dumps(app), encoding="utf-8")
        generate(root, out, "Notes", {"team": "TEAM1", "identity": "AAA", "profile_uuid": "exact", "kind": "development", "bundle_id": "com.noxlang.aura.Notes"}, "Release")
        generate(root, out, "Notes", {"team": "TEAM1", "identity": "AAA", "profile_uuid": "exact", "kind": "development", "bundle_id": "com.noxlang.aura.Notes"}, "Release")
        text = (out / "Notes.xcodeproj" / "project.pbxproj").read_text(encoding="utf-8")
        if ("/" + "Users" + "/") in text or ("/" + "tmp" + "/") in text or "-fno-objc-arc" not in text:
            print("ios project self-test: project text is not portable", file=sys.stderr)
            return 1
        if "dwarf-with-dsym" not in text or "libaura_probe" not in text:
            print("ios project self-test: dSYM or probe target is missing", file=sys.stderr)
            return 1
        with (out / "Info.plist").open("rb") as handle:
            stored = plistlib.load(handle)
        if stored["CFBundleShortVersionString"] != "1.0.0" or stored["CFBundleVersion"] != "1":
            print("ios project self-test: version did not come from the manifest", file=sys.stderr)
            return 1
        with (out / "Notes.entitlements").open("rb") as handle:
            ent = plistlib.load(handle)
        if ent.get("get-task-allow") is not True or "$(PRODUCT_BUNDLE_IDENTIFIER)" not in ent["application-identifier"]:
            print("ios project self-test: development entitlements are wrong", file=sys.stderr)
            return 1
        with (out / "PrivacyInfo.xcprivacy").open("rb") as handle:
            privacy = plistlib.load(handle)
        if privacy.get("NSPrivacyTracking") is not False:
            print("ios project self-test: privacy manifest tracks", file=sys.stderr)
            return 1
        icon_path = out / "Assets.xcassets" / "AppIcon.appiconset" / "AppIcon.png"
        if icon_path.read_bytes() != icon:
            print("ios project self-test: catalog icon drifted", file=sys.stderr)
            return 1
    print("ios project self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="?", choices=["signing", "generate", "stamp", "icon"])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--name", default="")
    parser.add_argument("--kind", default="development")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--slice", default="device")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--signing", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    root = args.root.resolve()
    if args.command == "icon":
        if args.out is None:
            print("usage: ios_project.py icon --out FILE", file=sys.stderr)
            return 2
        write_icon(args.out)
        return 0
    if args.command == "signing":
        if args.out is None or args.name == "":
            print("usage: ios_project.py signing --name NAME --kind development|store --out FILE", file=sys.stderr)
            return 2
        kind = "development" if args.kind == "development" else "store"
        resolved = resolve_signing(root, args.name, kind)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(resolved, indent=2) + "\n", encoding="utf-8")
        print(f"signing: {resolved['bundle_id']} team {resolved['team']} profile {resolved['profile_uuid']}")
        return 0
    if args.command == "generate":
        if args.out is None or args.name == "":
            print("usage: ios_project.py generate --name NAME --out DIR --signing FILE", file=sys.stderr)
            return 2
        signing = json.loads(args.signing.read_text(encoding="utf-8")) if args.signing else None
        generate(root, args.out, args.name, signing, args.configuration)
        print(args.out / f"{args.name}.xcodeproj")
        return 0
    if args.command == "stamp":
        if args.app is None or args.name == "":
            print("usage: ios_project.py stamp --name NAME --slice device|simulator --app APP [--signing FILE]", file=sys.stderr)
            return 2
        signing = json.loads(args.signing.read_text(encoding="utf-8")) if args.signing else None
        stamp(root, args.app, args.name, args.slice, signing)
        return 0
    parser.print_help(sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
