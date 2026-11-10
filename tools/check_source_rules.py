#!/usr/bin/env python3
"""Static source rules that code review alone does not hold (ADR-0037 R5, R9).

R5          Product code never runs or parses human-readable tool output such as dumpsys or
            "wm size"; device state comes from server messages or getprop keys.
R9          Product code never synchronizes with fixed delays: no sleeps, no single-shot timers
            with a delay, no postDelayed. Where a delay is the behaviour itself (a debounce, a
            tooltip), the line carries "source-rule-allow R9: <reason>".
SCREENSHOT  Nothing captures the screen with CopyFromScreen: it grabs whatever window is on top,
            private content included.
I18N        Every user-facing string has its translation (PLAN 4.13): no entry of a .ts file under
            src/ is left unfinished, and the source strings handed to qsTr, tr, translate and the
            NOOP markers are English (UI-012): Chinese lives only in the .ts file.
TOKENS      The design system's components take colors, durations and sizes from the theme
            (UI-001): no color literal, derived color or bare number for them in src/ui/controls.
TEXTFORMAT  Every QML Text and Label under src/ declares its textFormat: the default AutoText
            renders strings that look like HTML as rich text, which loads images from any URL,
            and extension manifests put untrusted text on screen (ADR-0066).

Usage: check_source_rules.py [--root DIR] | --self-test
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ALLOW = re.compile(r"source-rule-allow\s+(?P<rule>[A-Z0-9]+)\s*:\s*(?P<reason>\S.*)?")
STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')

PRODUCT_GLOBS = (
    "src/**/*.h",
    "src/**/*.cpp",
    "src/**/*.qml",
    "android/*/src/main/**/*.java",
    "android/*/src/main/**/*.kt",
)
EVERYWHERE_SKIP = ("legacy/", "build/", "third_party/", "deps/", "third_party_ref/", "scrcpy-3.3.4/",
                   "QineTouch-main/", ".git/", "docs/", "android/.gradle/")
EVERYWHERE_SUFFIXES = {".h", ".cpp", ".java", ".kt", ".py", ".ps1", ".psm1", ".cs", ".qml", ".js"}


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: re.Pattern[str]
    message: str
    strings_only: bool = False


R5_RULES = (
    Rule("R5", re.compile(r"\bdumpsys\b"), "runs dumpsys; use server messages or getprop", strings_only=True),
    Rule("R5", re.compile(r"\bwm\s+(size|density|overscan)\b"), "runs wm; use server messages", strings_only=True),
)

R9_RULES = (
    Rule("R9", re.compile(r"\bsleep_(for|until)\s*\("), "sleeps instead of waiting for an event"),
    Rule("R9", re.compile(r"(?<![\w.])Sleep\s*\("), "sleeps instead of waiting for an event"),
    Rule("R9", re.compile(r"\bQThread::(u|m)?sleep\s*\("), "sleeps instead of waiting for an event"),
    Rule("R9", re.compile(r"\bsingleShot\s*\(\s*(?!0\s*[,)])(?!std::chrono::\w+\s*[({]\s*0\s*[)}])"),
         "single-shot timer with a delay"),
    Rule("R9", re.compile(r"\bsetSingleShot\s*\(\s*true\s*\)"), "single-shot timer with a delay"),
    Rule("R9", re.compile(r"\b(Thread|SystemClock)\.sleep\s*\("), "sleeps instead of waiting for an event"),
    Rule("R9", re.compile(r"\bTimeUnit\.\w+\.sleep\s*\("), "sleeps instead of waiting for an event"),
    Rule("R9", re.compile(r"\bpostDelayed\s*\("), "posts with a fixed delay"),
    Rule("R9", re.compile(r"(?<![\w.])delay\s*\(\s*\d"), "coroutine delay used as a wait"),
)

I18N_RULES = (
    Rule("I18N", re.compile(r'type="unfinished"'), "untranslated text: fill in the translation"),
)

SOURCE_TEXT_GLOBS = ("src/**/*.h", "src/**/*.cpp", "src/**/*.qml")
CJK = "\u3000-\u303f\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\uff00-\uffef"
SOURCE_TEXT_RULES = (
    Rule("I18N",
         re.compile(r"\b(qsTr|tr|translate|QT_TR_NOOP|QT_TRANSLATE_NOOP|GS_TRANSLATE_NOOP)\s*\([^;)]*\"[^\"]*[" + CJK + r"]"),
         "source strings are English: put the Chinese in the .ts file"),
)

SCREENSHOT_RULES = (
    Rule("SCREENSHOT", re.compile(r"CopyFromScreen"), "use PrintWindow(PW_RENDERFULLCONTENT) on the own window"),
)

TOKEN_GLOBS = ("src/ui/controls/**/*.qml",)
BARE_NUMBER = r"\s*:\s*[1-9]\d*(?=\s*(?://.*)?$)"
TOKEN_RULES = (
    Rule("TOKENS", re.compile(r"#[0-9A-Fa-f]{3,8}\b"), "color literal: use a Theme.colors token", strings_only=True),
    Rule("TOKENS", re.compile(r"\bQt\.(rgba|hsla|hsva|lighter|darker|tint|color)\s*\("),
         "derived color: add a token to ColorTokens.qml"),
    Rule("TOKENS", re.compile(r"\b(duration|interval|delay|timeout)\s*:\s*\d"), "bare duration: use Theme.motion"),
    Rule("TOKENS", re.compile(r"\b(blur|spread|offset\.[xy])" + BARE_NUMBER), "bare shadow: use Theme.shadow"),
    Rule("TOKENS", re.compile(r"\bpixelSize\s*:\s*\d"), "bare font size: use Theme.type"),
    Rule("TOKENS", re.compile(r"\bradius" + BARE_NUMBER), "bare radius: use Theme.radius"),
    Rule("TOKENS", re.compile(r"\b(implicitWidth|implicitHeight|width|height)" + BARE_NUMBER),
         "bare size: use Theme.size"),
    Rule("TOKENS", re.compile(r"\b(spacing|\w*[pP]adding|\w*[mM]argins?)" + BARE_NUMBER), "bare spacing: use Theme.space"),
    Rule("TOKENS", re.compile(r"\bopacity\s*:\s*0?\.\d"), "bare opacity: use a Theme value"),
)


TEXT_ELEMENT = re.compile(r"(?<![\w.])(?:T\.)?(?:Text|Label)\s*\{")
TEXT_FORMAT = re.compile(r"\btextFormat\s*:")


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    rule: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.message}"


def check_text(path: str, text: str, rules: tuple[Rule, ...]) -> list[Violation]:
    violations: list[Violation] = []
    for number, line in enumerate(text.splitlines(), start=1):
        allowed: set[str] = set()
        allow = ALLOW.search(line)
        if allow:
            if not allow.group("reason"):
                violations.append(Violation(path, number, allow.group("rule"), "allow marker without a reason"))
            allowed.add(allow.group("rule"))
            line = line[: allow.start()]
        for rule in rules:
            if rule.name in allowed:
                continue
            haystacks = STRING_LITERAL.findall(line) if rule.strings_only else [line]
            if any(rule.pattern.search(h) for h in haystacks):
                violations.append(Violation(path, number, rule.name, rule.message))
    return violations


def check_text_formats(path: str, text: str) -> list[Violation]:
    violations: list[Violation] = []
    lines = text.splitlines()
    for number, line in enumerate(lines):
        match = TEXT_ELEMENT.search(line)
        if not match:
            continue
        allow = ALLOW.search(line)
        if allow and allow.group("rule") == "TEXTFORMAT":
            if not allow.group("reason"):
                violations.append(Violation(path, number + 1, "TEXTFORMAT", "allow marker without a reason"))
            continue
        depth = 0
        block = []
        for index in range(number, len(lines)):
            segment = lines[index][match.end() - 1:] if index == number else lines[index]
            block.append(segment)
            depth += segment.count("{") - segment.count("}")
            if depth <= 0:
                break
        if not TEXT_FORMAT.search("\n".join(block)):
            violations.append(Violation(path, number + 1, "TEXTFORMAT",
                                        "Text without textFormat: set Text.PlainText unless markup is intended"))
    return violations


def product_files(root: Path) -> list[Path]:
    files: set[Path] = set()
    for pattern in PRODUCT_GLOBS:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def all_files(root: Path) -> list[Path]:
    files = []
    for directory, subdirectories, names in os.walk(root):
        relative = Path(directory).relative_to(root).as_posix()
        prefix = "" if relative == "." else relative + "/"
        subdirectories[:] = [d for d in subdirectories if not (prefix + d + "/").startswith(EVERYWHERE_SKIP)]
        files += [Path(directory) / n for n in names if Path(n).suffix in EVERYWHERE_SUFFIXES]
    return sorted(files)


def check_tree(root: Path) -> list[Violation]:
    this_file = Path(__file__).resolve()
    violations: list[Violation] = []
    for path in product_files(root):
        text = path.read_text(encoding="utf-8")
        violations += check_text(path.relative_to(root).as_posix(), text, R5_RULES + R9_RULES)
    for path in sorted(root.glob("src/**/i18n/*.ts")):
        violations += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"), I18N_RULES)
    for path in sorted({p for pattern in SOURCE_TEXT_GLOBS for p in root.glob(pattern)}):
        violations += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"),
                                 SOURCE_TEXT_RULES)
    for path in sorted({p for pattern in TOKEN_GLOBS for p in root.glob(pattern)}):
        violations += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"), TOKEN_RULES)
    for path in sorted(root.glob("src/**/*.qml")):
        violations += check_text_formats(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))
    for path in all_files(root):
        if path.resolve() == this_file:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        violations += check_text(path.relative_to(root).as_posix(), text, SCREENSHOT_RULES)
    return violations


def self_test() -> int:
    product = R5_RULES + R9_RULES
    flagged = [
        'run("dumpsys window displays");',
        'shell("wm size");',
        "std::this_thread::sleep_for(100ms);",
        "Sleep(50);",
        "QThread::msleep(10);",
        "QTimer::singleShot(200, this, &X::retry);",
        "QTimer::singleShot(std::chrono::milliseconds(300), [] {});",
        "timer.setSingleShot(true);",
        "Thread.sleep(100);",
        "SystemClock.sleep(5);",
        "TimeUnit.MILLISECONDS.sleep(10);",
        "handler.postDelayed(task, 500);",
        "delay(100)",
        "QTimer::singleShot(200, f); // source-rule-allow R9:",
    ]
    clean = [
        "// We never parse dumpsys output (R5).",
        'getprop("ro.serialno");',
        "QTimer::singleShot(0, this, &X::later);",
        "QTimer::singleShot(std::chrono::milliseconds(0), f);",
        "QTimer::singleShot(300, this, &X::hideTip); // source-rule-allow R9: tooltip delay is the behaviour",
        "m_wake.wait_for(lock, stop, backoff, pred);",
        "cv.wait_until(lock, deadline);",
        "int delayMs = config.delay(5);",
        "auto sleeping = isSleeping();",
    ]
    failures = 0
    for line in flagged:
        if not check_text("t", line, product):
            print(f"self-test: not flagged: {line}")
            failures += 1
    for line in clean:
        if found := check_text("t", line, product):
            print(f"self-test: wrongly flagged: {line} -> {found[0]}")
            failures += 1
    if not check_text("t", "bitmap = g.CopyFromScreen(x, y)", SCREENSHOT_RULES):
        print("self-test: CopyFromScreen not flagged")
        failures += 1
    if not check_text("t", '<translation type="unfinished"></translation>', I18N_RULES):
        print("self-test: an unfinished translation not flagged")
        failures += 1
    if check_text("t", "<translation>版本 %1</translation>", I18N_RULES):
        print("self-test: a finished translation wrongly flagged")
        failures += 1
    source_flagged = [
        'text: qsTr("设置")',
        'title: qsTr("Volume %1 · 音量")',
        'QCoreApplication::translate("Commands", "静音")',
        '.title = GS_TRANSLATE_NOOP("Settings", "语言"),',
    ]
    source_clean = [
        'text: qsTr("Settings")',
        'labels: ({ zh_CN: "简体中文", en: "English" })',
        'QCoreApplication::translate("Commands", command.title)',
        'text: qsTr("Version %1 · %2").arg(AppInfo.version).arg(AppInfo.revision)',
    ]
    for line in source_flagged:
        if not check_text("t", line, SOURCE_TEXT_RULES):
            print(f"self-test: not flagged: {line}")
            failures += 1
    for line in source_clean:
        if found := check_text("t", line, SOURCE_TEXT_RULES):
            print(f"self-test: wrongly flagged: {line} -> {found[0]}")
            failures += 1
    token_flagged = [
        'color: "#FF6B3D"',
        "color: Qt.rgba(1, 1, 1, 0.1)",
        "border.color: Qt.darker(Theme.colors.accent, 1.2)",
        "duration: 120",
        "interval: 500",
        "delay: 500",
        "blur: 64",
        "offset.y: 24",
        "font.pixelSize: 13",
        "radius: 6",
        "implicitHeight: 32",
        "border.width: 1 // hairline",
        "spacing: 8",
        "leftPadding: 12",
        "anchors.margins: 4",
        "opacity: 0.4",
    ]
    token_clean = [
        "color: Theme.colors.accent",
        'color: "transparent"',
        "// The accent was #FF6B3D before the redesign.",
        "duration: Theme.motion.fast",
        "delay: Theme.motion.tooltipDelay",
        "offset.y: Theme.shadow.keyOffset",
        "radius: height / 2",
        "border.width: control.checked ? 0 : Theme.size.stroke",
        "border.width: 0",
        "width: Theme.size.switchKnob",
        "x: 0",
        "anchors.margins: -2 * gap",
        "opacity: enabled ? 1 : Theme.disabledOpacity",
        "to: 1",
    ]
    for line in token_flagged:
        if not check_text("t", line, TOKEN_RULES):
            print(f"self-test: not flagged: {line}")
            failures += 1
    for line in token_clean:
        if found := check_text("t", line, TOKEN_RULES):
            print(f"self-test: wrongly flagged: {line} -> {found[0]}")
            failures += 1
    format_flagged = [
        "Text {\n    text: entry.name\n}",
        "contentItem: Text {\n    text: control.displayText\n}",
        "component Caption: Text {\n    color: Theme.colors.textTertiary\n}",
        "Label { text: name }",
        "Text {\n    Behavior on opacity { NumberAnimation {} }\n    text: name\n}",
    ]
    format_clean = [
        "Text {\n    textFormat: Text.PlainText\n    text: entry.name\n}",
        "Text { textFormat: Text.StyledText; text: marked(title) }",
        "TextField {\n    text: value\n}",
        "TextInput { text: value }",
        "wrapMode: Text.Wrap",
        "Text { text: name } // source-rule-allow TEXTFORMAT: fixture",
    ]
    for snippet in format_flagged:
        if not check_text_formats("t", snippet):
            print(f"self-test: not flagged: {snippet!r}")
            failures += 1
    for snippet in format_clean:
        if found := check_text_formats("t", snippet):
            print(f"self-test: wrongly flagged: {snippet!r} -> {found[0]}")
            failures += 1
    cases = (len(flagged) + len(clean) + 3 + len(source_flagged) + len(source_clean) + len(token_flagged)
             + len(token_clean) + len(format_flagged) + len(format_clean))
    print("self-test:", "FAILED" if failures else "ok", f"({cases} cases)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    violations = check_tree(args.root.resolve())
    for violation in violations:
        print(violation)
    print(f"source rules: {len(violations)} violation(s)")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
