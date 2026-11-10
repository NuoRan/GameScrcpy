#pragma once

#include <array>
#include <string_view>

namespace gs::app {

inline constexpr std::array<std::string_view, 99> kThemeTokenNames{
    "bgBase",       "bgRaised",      "bgOverlay",     "bgElevated",    "bgSunken",     "bgFrosted",
    "bgCode",       "bgEditor",
    "strokeSubtle", "strokeDefault", "strokeStrong",  "stateHover",    "statePress",   "stateSelected",
    "highlight",    "knob",          "textPrimary",   "textSecondary", "textTertiary", "textDisabled",
    "textOnAccent", "accentSubtle",  "accentLine",    "accentText",    "accentGlow",   "scrim",
    "canvasDim",    "success",      "warning",       "danger",        "info",          "successText",  "warningText",
    "dangerText",   "infoText",      "successSubtle", "warningSubtle", "dangerSubtle", "infoSubtle",
    "dangerLine",   "warningLine",   "accentRing",    "accentEdge",    "accentHalo",   "accentClear",  "keyHintFill",
    "keyHintStroke", "keyHintText",  "keyHintRing",   "keyHintRingFill", "grip",
    "bpCanvas",     "bpGrid",        "bpNode",        "bpNodeStroke",  "bpChip",
    "bpEventTint",  "bpEventInk",    "bpActionTint",  "bpActionInk",   "bpWaitTint",   "bpWaitInk",
    "bpVisionTint", "bpVisionInk",   "bpFlowTint",    "bpFlowInk",     "bpCustomTint", "bpCustomInk",
    "bpCodeTint",   "bpCodeInk",
    "bpExec",       "bpBool",        "bpNumber",      "bpText",        "bpPoint",      "bpRegion",
    "bpImage",      "bpColor",       "bpComponent",   "bpKey",         "bpDuration",   "bpList",
    "bpAny",        "bpTransition",
    "bpComment",    "bpCommentStroke", "bpCommentBlue", "bpCommentBlueStroke", "bpCommentGreen",
    "bpCommentGreenStroke", "bpCommentAmber", "bpCommentAmberStroke", "bpCommentRed", "bpCommentRedStroke",
    "bpCommentPurple", "bpCommentPurpleStroke",
    "bpBubble",     "bpBubbleStroke", "bpLabel"};

}
