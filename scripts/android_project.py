#!/usr/bin/env python3
"""Generate an Aura Android Gradle project from the mobile manifest.

The project lands under build/. Signing passwords and Play credentials are
read from the environment at build time and are not written into the project.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")
PAGE_ALIGN = 16384
AGP_VERSION = "9.5.0"
GRADLE_VERSION = "9.6.0"
TEMPLATE_VERSION = "1"
ABI = "arm64-v8a"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def application_id(app: dict, name: str) -> str:
    if NAME_RE.fullmatch(name) is None:
        raise ValueError(f"app name must be an identifier: {name}")
    return f"{app['bundle_id']}.{name.lower()}"


def manifest_xml(app: dict, name: str) -> str:
    label = name.replace("&", "&amp;").replace("<", "&lt;")
    cleartext = "true" if app["network"]["cleartext"] else "false"
    backup = "true" if app["android"]["allow_backup"] else "false"
    permissions = "\n".join(f'    <uses-permission android:name="{item}"/>' for item in app.get("permissions") or [])
    permission_block = f"\n{permissions}\n" if permissions else ""
    return f"""<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">{permission_block}
    <application
        android:label="{label}"
        android:icon="@drawable/ic_launcher"
        android:hasCode="true"
        android:extractNativeLibs="false"
        android:usesCleartextTraffic="{cleartext}"
        android:enableOnBackInvokedCallback="true"
        android:allowBackup="{backup}">
        <activity
            android:name="com.noxlang.aura.AuraHost"
            android:exported="true"
            android:configChanges="orientation|screenSize|screenLayout|keyboard|keyboardHidden|uiMode|density|fontScale|locale|layoutDirection|smallestScreenSize"
            android:windowSoftInputMode="adjustNothing">
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
"""


def proguard_text() -> str:
    return """-keep class com.noxlang.aura.AuraHost { *; }
-keep class com.noxlang.aura.AuraHost$* { *; }
-keepclasseswithmembernames class * { native <methods>; }
"""


def root_build() -> str:
    return f"""plugins {{
    id("com.android.application") version "{AGP_VERSION}" apply false
}}
"""


def settings_build(name: str) -> str:
    return f"""pluginManagement {{
    repositories {{
        google()
        mavenCentral()
        gradlePluginPortal()
    }}
}}
dependencyResolutionManagement {{
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {{
        google()
        mavenCentral()
    }}
}}
rootProject.name = "{name}"
include(":app")
"""


def app_build(app: dict, name: str) -> str:
    android = app["android"]
    identifier = application_id(app, name)
    return f"""plugins {{
    id("com.android.application")
}}

val auraRepo = providers.gradleProperty("aura.repo").get()
val auraJni = providers.gradleProperty("aura.jni").get()
val auraAssets = providers.gradleProperty("aura.assets").get()

android {{
    namespace = "com.noxlang.aura"
    compileSdk = {android["target_sdk"]}
    defaultConfig {{
        applicationId = "{identifier}"
        minSdk = {android["min_sdk"]}
        targetSdk = {android["target_sdk"]}
        versionCode = {app["build_number"]}
        versionName = "{app["version"]}"
        ndk {{
            abiFilters.clear()
            abiFilters.add("{ABI}")
        }}
    }}
    packaging {{
        jniLibs {{
            useLegacyPackaging = false
        }}
    }}
    signingConfigs {{
        create("release") {{
            val store = System.getenv("AURA_ANDROID_STORE_FILE") ?: ""
            if (store.isNotEmpty()) {{
                storeFile = file(store)
                storePassword = System.getenv("AURA_ANDROID_STORE_PASSWORD")
                keyAlias = System.getenv("AURA_ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("AURA_ANDROID_KEY_PASSWORD")
            }}
        }}
    }}
    buildTypes {{
        release {{
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
        }}
        debug {{
            signingConfig = signingConfigs.getByName("debug")
        }}
    }}
    compileOptions {{
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }}
    sourceSets.getByName("main") {{
        java.setSrcDirs(listOf("$auraRepo/native/android"))
        jniLibs.setSrcDirs(listOf(auraJni))
        assets.setSrcDirs(listOf(auraAssets))
    }}
}}
"""


def gradle_properties() -> str:
    return f"""org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
android.useAndroidX=false
android.nonTransitiveRClass=true
aura.template={TEMPLATE_VERSION}
aura.agp={AGP_VERSION}
aura.gradle={GRADLE_VERSION}
aura.abi={ABI}
"""


def launcher_icon(root: Path) -> bytes:
    override = os.environ.get("AURA_APP_ICON", "")
    if override:
        path = Path(override)
        if path.is_file():
            return path.read_bytes()
    brand = root / "assets" / "brand" / "icon-1024.png"
    if brand.is_file():
        return brand.read_bytes()
    return (root / "native" / "ios" / "AppIcon.png").read_bytes()


def generate(root: Path, out: Path, name: str) -> None:
    app = load_json(root / "examples" / "mobile" / "app.json")
    out.mkdir(parents=True, exist_ok=True)
    (out / "settings.gradle.kts").write_text(settings_build(name), encoding="utf-8")
    (out / "build.gradle.kts").write_text(root_build(), encoding="utf-8")
    (out / "gradle.properties").write_text(gradle_properties(), encoding="utf-8")
    app_dir = out / "app"
    app_dir.mkdir(exist_ok=True)
    (app_dir / "build.gradle.kts").write_text(app_build(app, name), encoding="utf-8")
    (app_dir / "proguard-rules.pro").write_text(proguard_text(), encoding="utf-8")
    main = app_dir / "src" / "main"
    (main / "res" / "drawable").mkdir(parents=True, exist_ok=True)
    (main / "AndroidManifest.xml").write_text(manifest_xml(app, name), encoding="utf-8")
    icon = launcher_icon(root)
    target = main / "res" / "drawable" / "ic_launcher.png"
    target.write_bytes(icon)


def parse_load_aligns(text: str) -> list[int]:
    aligns = []
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0] != "LOAD":
            continue
        aligns.append(int(parts[-1], 16))
    return aligns


def alignment_errors(text: str, label: str) -> list[str]:
    aligns = parse_load_aligns(text)
    if not aligns:
        return [f"{label}: no LOAD segments"]
    errors = []
    for align in aligns:
        if align < PAGE_ALIGN:
            errors.append(f"{label}: LOAD align {align} is below {PAGE_ALIGN}")
    return errors


def check_library(path: Path, readelf: Path) -> list[str]:
    result = subprocess.run([str(readelf), "-l", str(path)], check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return [f"{path.name}: readelf failed"]
    return alignment_errors(result.stdout, path.name)


def run_self_test() -> int:
    sample = """
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x1000 0x1000 R   0x4000
  LOAD           0x001000 0x0000000000001000 0x0000000000001000 0x1000 0x1000 R E 0x4000
"""
    if alignment_errors(sample, "ok.so"):
        print("android project self-test: 16 KiB library was rejected", file=sys.stderr)
        return 1
    small = sample.replace("0x4000", "0x1000")
    if not alignment_errors(small, "old.so"):
        print("android project self-test: 4 KiB library was accepted", file=sys.stderr)
        return 1
    app = {
        "bundle_id": "com.noxlang.aura",
        "version": "1.0.0",
        "build_number": 1,
        "android": {"min_sdk": 24, "target_sdk": 36, "allow_backup": False},
        "network": {"cleartext": False},
        "permissions": [],
    }
    if application_id(app, "Notes") != "com.noxlang.aura.notes":
        print("android project self-test: application id was not lowercase", file=sys.stderr)
        return 1
    text = app_build(app, "Notes")
    manifest = manifest_xml(app, "Notes")
    if "com.noxlang.aura.notes" not in text or "minSdk = 24" not in text or "useLegacyPackaging = false" not in text:
        print("android project self-test: gradle contract is missing", file=sys.stderr)
        return 1
    if "signingConfigs.getByName(\"debug\")" not in text or "isMinifyEnabled = true" not in text:
        print("android project self-test: debug and release signing were not separated", file=sys.stderr)
        return 1
    if "AURA_ANDROID_STORE_PASSWORD" not in text or "storePassword =" not in text:
        print("android project self-test: release key is not read from the environment", file=sys.stderr)
        return 1
    if 'android:extractNativeLibs="false"' not in manifest or 'android:allowBackup="false"' not in manifest:
        print("android project self-test: manifest policy is missing", file=sys.stderr)
        return 1
    if "/" + "Users" + "/" in text or "/" + "Users" + "/" in manifest:
        print("android project self-test: project contains a home path", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory() as raw:
        root = Path(raw) / "repo"
        icon_dir = root / "native" / "ios"
        icon_dir.mkdir(parents=True)
        (icon_dir / "AppIcon.png").write_bytes(b"\x89PNG\r\n\x1a\n")
        (root / "examples" / "mobile").mkdir(parents=True)
        (root / "examples" / "mobile" / "app.json").write_text(json.dumps(app), encoding="utf-8")
        out = Path(raw) / "project"
        generate(root, out, "Notes")
        stored = (out / "app" / "build.gradle.kts").read_text(encoding="utf-8")
        props = (out / "gradle.properties").read_text(encoding="utf-8")
        if stored != text or f"aura.template={TEMPLATE_VERSION}" not in props or f"aura.abi={ABI}" not in props:
            print("android project self-test: generated project drifted", file=sys.stderr)
            return 1
    print("android project self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="?", choices=["generate", "align"])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--name", default="")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--readelf", type=Path)
    parser.add_argument("--lib", type=Path, action="append", default=[])
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    root = args.root.resolve()
    if args.command == "generate":
        if args.out is None or args.name == "":
            print("usage: android_project.py generate --name NAME --out DIR", file=sys.stderr)
            return 2
        generate(root, args.out, args.name)
        print(args.out)
        return 0
    if args.command == "align":
        if args.readelf is None or not args.lib:
            print("usage: android_project.py align --readelf READELF --lib FILE", file=sys.stderr)
            return 2
        errors: list[str] = []
        for lib in args.lib:
            errors.extend(check_library(lib, args.readelf))
        if errors:
            for error in errors:
                print(f"android align: {error}", file=sys.stderr)
            return 1
        print(f"android align: {len(args.lib)} libraries are {PAGE_ALIGN}-byte aligned")
        return 0
    parser.print_help(sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
