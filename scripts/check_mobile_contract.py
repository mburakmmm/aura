#!/usr/bin/env python3
"""Checks Aura's platform, layer, and FFI source invariants.

The checker reads declarations after removing Nox comments and string contents,
so a diagnostic message mentioning a forbidden token does not create a false
positive. It intentionally checks imports rather than arbitrary text.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path


IMPORT_RE = re.compile(r"^\s*(?:from|import)\s+(aura(?:\.[A-Za-z_][A-Za-z0-9_]*)+)", re.MULTILINE)
EXTERN_RE = re.compile(r"^\s*extern\s+def\b", re.MULTILINE)
PLATFORM_TOKEN_RE = re.compile(
    r"\b(?:AppKit|UIKit|JNIEXPORT|Activity|UIView|NSView|UIWindow|MacSurface|MacGraphicsDevice)\b"
)
PLATFORM_FACADES = {
    "aura.platform.android",
    "aura.platform.ios",
    "aura.platform.macos",
}

MOBILE_WINDOW_EXPORT_RE = re.compile(
    r"^\s*(?:extern\s+\"C\"\s+)?(?:JNIEXPORT\s+)?(?:void|int|int64_t|double|float|const\s+char\s*\*)\s+"
    r"aura_window_[A-Za-z0-9_]+\s*\(",
    re.MULTILINE,
)

IOS_TEXT_INPUT_INTERFACE_RE = re.compile(
    r"@interface\s+(?:AuraCanvas|AuraTextInputView)\s*:\s*UIView\s*<\s*UITextInput\s*>",
    re.MULTILINE,
)
IOS_UIKEYINPUT_ONLY_RE = re.compile(
    r"@interface\s+(?:AuraCanvas|AuraTextInputView)\s*:\s*UIView\s*<\s*UIKeyInput\s*>",
    re.MULTILINE,
)

UPPER_LAYERS = {
    "animation",
    "app",
    "controls",
    "devtools",
    "files",
    "image",
    "input",
    "layout",
    "runtime",
    "semantics",
    "testing",
    "text",
    "widgets",
}


def strip_comments_and_strings(source: str) -> str:
    """Returns source shape with comments/string contents replaced by spaces."""

    out: list[str] = []
    i = 0
    quote = ""
    triple = False
    escaped = False
    while i < len(source):
        ch = source[i]
        if quote:
            if ch == "\n":
                out.append("\n")
                if not triple:
                    quote = ""
                escaped = False
                i += 1
                continue
            if escaped:
                out.append(" ")
                escaped = False
                i += 1
                continue
            if ch == "\\":
                out.append(" ")
                escaped = True
                i += 1
                continue
            if triple and source.startswith(quote * 3, i):
                out.extend("   ")
                i += 3
                quote = ""
                triple = False
                continue
            if not triple and ch == quote:
                out.append(" ")
                quote = ""
                i += 1
                continue
            out.append(" ")
            i += 1
            continue

        if ch == "#":
            while i < len(source) and source[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch in ("'", '"'):
            triple = source.startswith(ch * 3, i)
            quote = ch
            count = 3 if triple else 1
            out.extend(" " * count)
            i += count
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def module_layer(module: str) -> str:
    parts = module.split(".")
    return parts[1] if len(parts) > 1 else ""


def relative_module(root: Path, path: Path) -> str:
    relative = path.relative_to(root).with_suffix("")
    return ".".join(relative.parts)


def forbidden_import(owner: str, imported: str) -> str | None:
    owner_layer = module_layer(owner)
    imported_layer = module_layer(imported)
    if owner_layer != "platform" and imported in PLATFORM_FACADES:
        return "platform-specific facade outside aura/platform"
    if owner_layer == "core" and imported_layer not in {"", "core"}:
        return "core may only import core"
    if owner_layer == "foundation" and imported_layer not in {"", "core", "foundation"}:
        return "foundation may only import core/foundation"
    if owner_layer == "graphics" and imported_layer in UPPER_LAYERS | {"platform", "rendering"}:
        return "graphics may not import platform/rendering/UI layers"
    if owner_layer == "rendering" and imported_layer in UPPER_LAYERS | {"platform"}:
        allowed = {"semantics"}
        if imported_layer not in allowed:
            return "rendering may not import platform or widget/UI layers"
    if owner_layer == "platform" and imported_layer in UPPER_LAYERS | {"rendering"}:
        return "platform may not import rendering or widget/UI layers"
    if owner_layer == "widgets" and imported_layer == "platform":
        return "widgets may not import platform directly"
    return None


def check_file(root: Path, path: Path) -> list[str]:
    source = path.read_text(encoding="utf-8")
    clean = strip_comments_and_strings(source)
    relative = path.relative_to(root)
    module = relative_module(root, path)
    errors: list[str] = []

    is_platform = relative.parts[:2] == ("aura", "platform")
    is_example = relative.parts and relative.parts[0] == "examples"
    if (is_example or not is_platform) and PLATFORM_TOKEN_RE.search(clean):
        errors.append("platform-native identifier outside aura/platform")
    if EXTERN_RE.search(clean) and not is_platform:
        errors.append("extern def outside aura/platform")

    for imported in IMPORT_RE.findall(clean):
        if is_example and imported.startswith("aura.platform"):
            errors.append(f"example imports platform directly: {imported}")
            continue
        reason = forbidden_import(module, imported)
        if reason is not None:
            errors.append(f"{reason}: {imported}")
    return errors


def nox_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for folder in (root / "aura", root / "examples"):
        if folder.exists():
            files.extend(sorted(folder.rglob("*.nox")))
    return files


def check_tree(root: Path) -> list[str]:
    errors: list[str] = []
    for path in nox_files(root):
        for message in check_file(root, path):
            errors.append(f"{path.relative_to(root)}: {message}")
    errors.extend(check_mobile_host_contract(root))
    return errors


def check_mobile_host_contract(root: Path) -> list[str]:
    native = root / "native"
    if not (native / "ios").exists() or not (native / "android").exists():
        return []
    errors: list[str] = []
    for folder in (native / "ios", native / "android"):
        for path in sorted(folder.rglob("*")):
            if path.suffix not in {".c", ".cc", ".cpp", ".m", ".mm"}:
                continue
            source = path.read_text(encoding="utf-8")
            if MOBILE_WINDOW_EXPORT_RE.search(source):
                errors.append(
                    f"{path.relative_to(root)}: mobile adapter defines aura_window_* outside native/mobile"
                )

    required_objects = {
        root / "scripts" / "build_ios_app.sh": (
            "native/mobile/shell.c",
            "native/mobile/events.c",
            "native/mobile/event_queue.c",
            "native/mobile/app_runtime.c",
        ),
        root / "scripts" / "build_android_app.sh": (
            "native/mobile/shell.c",
            "native/mobile/events.c",
            "native/mobile/event_queue.c",
            "native/mobile/app_runtime.c",
        ),
    }
    for script, required in required_objects.items():
        if not script.exists():
            errors.append(f"{script.relative_to(root)}: missing mobile build script")
            continue
        source = script.read_text(encoding="utf-8")
        for item in required:
            if item not in source:
                errors.append(
                    f"{script.relative_to(root)}: common mobile ABI object is missing: {item}"
                )

    adapter_requirements = {
        native / "ios" / "scene.m": (
            "aura_shell_post_event",
            "aura_shell_metrics_v1",
            "aura_shell_lifecycle",
            "aura_shell_surface",
            "aura_mobile_runtime_start",
            "aura_mobile_runtime_request_stop",
            "aura_mobile_pointer_v1",
            "aura_mobile_key_v1",
            "pointerIdForTouch",
            "aura_shell_platform_route",
            "aura_shell_text_edit",
            "aura_host_text_state",
            "aura_host_text_geometry",
            "aura_host_text_configuration",
            "AURA_TEXT_EDIT_PERFORM_ACTION",
            "openURLContexts",
            "continueUserActivity",
        ),
        native / "android" / "nox_host.c": (
            "aura_shell_post_event",
            "aura_shell_metrics_v1",
            "aura_shell_lifecycle",
            "aura_shell_surface",
            "aura_mobile_runtime_start",
            "aura_mobile_runtime_request_stop",
            "aura_mobile_pointer_v1",
            "aura_mobile_key_v1",
            "aura_shell_platform_route",
            "aura_shell_text_edit",
            "aura_host_text_state",
            "aura_host_text_geometry",
            "aura_host_text_configuration",
        ),
        native / "android" / "AuraHost.java": (
            "nativeLifecycle",
            "nativeSurface",
            "onAttachedToWindow",
            "onDetachedFromWindow",
            "onStart",
            "onResume",
            "onPause",
            "onStop",
            "onDestroy",
            "ACTION_POINTER_DOWN",
            "ACTION_POINTER_UP",
            "getHistorySize",
            "nativeSystemBack",
            "OnBackAnimationCallback",
            "resolveSystemBack",
            "nativeKey",
            "onKeyDown",
            "onKeyUp",
            "nativePlatformRoute",
            "nativeTextEdit",
            "setTextState",
            "setTextGeometry",
            "setTextConfiguration",
            "performEditorAction",
            "beginBatchEdit",
            "endBatchEdit",
            "getExtractedText",
            "requestCursorUpdates",
            "updateCursorAnchorInfo",
            "onNewIntent",
        ),
    }
    for path, required in adapter_requirements.items():
        if not path.exists():
            errors.append(f"{path.relative_to(root)}: missing mobile adapter")
            continue
        source = path.read_text(encoding="utf-8")
        for symbol in required:
            if symbol not in source:
                errors.append(
                    f"{path.relative_to(root)}: adapter bypasses common mobile ABI: {symbol}"
                )
        if path.suffix in {".c", ".m"}:
            for forbidden in ("nox_app_create", "nox_app_start", "nox_exception_take"):
                if forbidden in source:
                    errors.append(
                        f"{path.relative_to(root)}: adapter owns Nox runtime outside native/mobile/app_runtime: {forbidden}"
                    )

    ios_adapter = native / "ios" / "scene.m"
    if ios_adapter.exists():
        ios_source = ios_adapter.read_text(encoding="utf-8")
        if not IOS_TEXT_INPUT_INTERFACE_RE.search(ios_source):
            errors.append(
                "native/ios/scene.m: iOS text input adapter must explicitly implement UITextInput"
            )
        if IOS_UIKEYINPUT_ONLY_RE.search(ios_source):
            errors.append(
                "native/ios/scene.m: UIKeyInput-only adapter is forbidden"
            )
        ios_text_input_methods = (
            "setMarkedText:(NSString *)markedText selectedRange:",
            "unmarkText",
            "selectedTextRange",
            "setSelectedTextRange:",
            "markedTextRange",
            "textInRange:",
            "replaceRange:(UITextRange *)range withText:",
            "beginningOfDocument",
            "endOfDocument",
            "textRangeFromPosition:",
            "positionFromPosition:",
            "comparePosition:",
            "offsetFromPosition:",
            "firstRectForRange:",
            "caretRectForPosition:",
            "closestPositionToPoint:",
            "selectionRectsForRange:",
            "inputDelegate",
            "tokenizer",
        )
        for method in ios_text_input_methods:
            if method not in ios_source:
                errors.append(
                    f"native/ios/scene.m: incomplete UITextInput adapter: {method}"
                )
        ordered_tokens = (
            "notifyTextWillChange",
            "notifySelectionWillChange",
            "self.editingText = buffer",
            "notifySelectionDidChange",
            "notifyTextDidChange",
        )
        marked_ok = False
        search_from = 0
        while True:
            marked_start = ios_source.find("- (void)setMarkedText:", search_from)
            if marked_start < 0:
                break
            marked_end = ios_source.find("\n- (void)unmarkText", marked_start)
            if marked_end < 0:
                break
            marked_body = ios_source[marked_start:marked_end]
            cursor = -1
            candidate_ok = True
            for token in ordered_tokens:
                position = marked_body.find(token, cursor + 1)
                if position < 0:
                    candidate_ok = False
                    break
                cursor = position
            if candidate_ok:
                marked_ok = True
                break
            search_from = marked_start + 1
        if not marked_ok:
            errors.append(
                "native/ios/scene.m: marked-text inputDelegate notifications "
                "must bracket mutation in order"
            )

    for clipboard_adapter in (
        native / "ios" / "scene.m",
        native / "android" / "nox_host.c",
    ):
        if not clipboard_adapter.exists():
            continue
        clipboard_source = clipboard_adapter.read_text(encoding="utf-8")
        if re.search(r"static\s+char\s+g_clip\s*\[", clipboard_source):
            errors.append(
                f"{clipboard_adapter.relative_to(root)}: fixed-size clipboard buffer is forbidden"
            )

    lifecycle_accessors = (
        "aura_window_event_lifecycle_state",
        "aura_window_event_lifecycle_reason",
        "aura_window_event_environment_value",
        "aura_window_event_memory_level",
        "aura_window_event_cutout_value",
    )
    lifecycle_backends = (
        native / "mobile" / "shell.c",
        native / "macos" / "aura_macos.m",
        native / "linux" / "aura_linux.cc",
        native / "windows" / "aura_windows.cc",
        native / "windows" / "aura_host.def",
    )
    for path in lifecycle_backends:
        if not path.exists():
            errors.append(f"{path.relative_to(root)}: missing lifecycle ABI backend")
            continue
        source = path.read_text(encoding="utf-8")
        for symbol in lifecycle_accessors:
            if symbol not in source:
                errors.append(f"{path.relative_to(root)}: lifecycle ABI symbol is missing: {symbol}")

    surface_accessors = (
        "aura_window_event_surface_value",
        "aura_window_surface_available",
        "aura_window_system_back_result",
        "aura_window_capabilities",
    )
    for path in lifecycle_backends:
        if not path.exists():
            continue
        source = path.read_text(encoding="utf-8")
        for symbol in surface_accessors:
            if symbol not in source:
                errors.append(f"{path.relative_to(root)}: surface ABI symbol is missing: {symbol}")

    text_edit_accessors = (
        "aura_window_event_text_edit_value",
        "aura_text_editing_state",
        "aura_text_geometry",
        "aura_text_configuration",
    )
    for path in lifecycle_backends:
        if not path.exists():
            continue
        source = path.read_text(encoding="utf-8")
        for symbol in text_edit_accessors:
            if symbol not in source:
                errors.append(f"{path.relative_to(root)}: text editing ABI symbol is missing: {symbol}")

    pointer_accessors = (
        "aura_window_event_device_id",
        "aura_window_event_pointer_id",
        "aura_window_event_pointer_kind",
        "aura_window_event_timestamp_ns",
    )
    for path in lifecycle_backends:
        if not path.exists():
            continue
        source = path.read_text(encoding="utf-8")
        for symbol in pointer_accessors:
            if symbol not in source:
                errors.append(f"{path.relative_to(root)}: pointer ABI symbol is missing: {symbol}")

    for path in lifecycle_backends:
        if not path.exists():
            continue
        source = path.read_text(encoding="utf-8")
        if "aura_window_event_modifiers" not in source:
            errors.append(f"{path.relative_to(root)}: keyboard modifier ABI symbol is missing")
    return errors


def write_fixture(root: Path, relative: str, source: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")


def run_self_test() -> int:
    cases = [
        ("aura/platform/bridge.nox", "extern def native() -> int\n", False),
        ("aura/widgets/good.nox", '# UIKit Activity extern def\nvalue: str = "UIView JNIEXPORT"\n', False),
        ("aura/widgets/bad.nox", "extern def native() -> int\n", True),
        ("aura/widgets/platform_import.nox", "from aura.platform.bridge import poll_events\n", True),
        ("aura/platform/widget_import.nox", "from aura.widgets.framework import Widget\n", True),
        ("aura/graphics/widget_import.nox", "from aura.widgets.framework import Widget\n", True),
        ("aura/runtime/platform_name.nox", "from aura.platform.macos import poll_events\n", True),
        ("examples/demo/main.nox", "from aura.platform.bridge import poll_events\n", True),
        ("examples/demo/token.nox", "UIKit = 1\n", True),
    ]
    for relative, source, should_fail in cases:
        with tempfile.TemporaryDirectory(prefix="aura-contract-") as temporary:
            root = Path(temporary)
            write_fixture(root, relative, source)
            failed = bool(check_tree(root))
            if failed != should_fail:
                print(f"mobile contract self-test failed: {relative}", file=sys.stderr)
                return 1
    print("mobile contract self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    errors = check_tree(args.root.resolve())
    if errors:
        for error in errors:
            print(f"mobile contract: {error}", file=sys.stderr)
        return 1
    print("mobile contract: layer, FFI, and platform boundaries are clean")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
