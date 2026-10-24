"""Regression check for the Auto Margin readout's animated clipping bounds."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8-sig")


def constant(name: str) -> float:
    match = re.search(rf"const float {name} = ([0-9]+(?:\.[0-9]+)?)f?;", SOURCE)
    assert match, f"missing UI layout constant: {name}"
    return float(match.group(1))


line_size = constant("LineSize")
small_gap = constant("MarginSmall")
extra_small_gap = constant("MarginExtraSmall")
assert line_size == 20.0
assert small_gap == 5.0
assert extra_small_gap == 2.5

assert "const float InputModeDetailRows = InputsBestMode ? 6.0f : 2.0f;" in SOURCE
assert "constexpr float InputCommonDetailRows = 4.0f;" in SOURCE
assert re.search(
    r"FullInputDetailsHeight\s*=\s*\n\s*InputCommonDetailRows\s*\*\s*\(MarginExtraSmall\s*\+\s*LineSize\)\s*\+\s*\n\s*InputModeDetailRows\s*\*\s*\(MarginSmall\s*\+\s*LineSize\);",
    SOURCE,
)

fast_start = SOURCE.index("if(DisplayedInputsMode == AMF_INPUTS_FAST)")
best_start = SOURCE.index("else if(DisplayedInputsMode == AMF_INPUTS_BEST)", fast_start)
saiko_start = SOURCE.index("else if(DisplayedInputsMode == AMF_INPUTS_SAIKO)", best_start)
fast_branch = SOURCE[fast_start:best_start]
best_branch = SOURCE[best_start:saiko_start]
assert fast_branch.count("CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);") == 2
assert best_branch.count("CardContent.HSplitTop(MarginSmall, nullptr, &CardContent);") == 6

readout = SOURCE.index('Ui()->DoLabel(&Label, aAutoMargin, 11.0f, TEXTALIGN_ML, AutoMarginProps);')
readout_layout = SOURCE[readout - 500:readout]
assert "CardContent.HSplitTop(LineSize, &Label, &CardContent);" in readout_layout
assert "InputDetailsClip.h = std::round(FullInputDetailsHeight * InputDetailsProgress / UiPixelSize) * UiPixelSize;" in SOURCE
assert "InputDetailsContent.h = FullInputDetailsHeight;" in SOURCE
assert "CardHeight = BaseCardHeight + FullInputDetailsHeight * InputDetailsProgress;" in SOURCE

common_rows = 4
for mode_rows, old_rows in ((2, 6), (6, 10)):
    content_height = common_rows * (extra_small_gap + line_size) + mode_rows * (small_gap + line_size)
    old_reserved_height = old_rows * (extra_small_gap + line_size)
    assert content_height > old_reserved_height
    assert content_height - old_reserved_height == (mode_rows * (small_gap - extra_small_gap))
    assert line_size > 11.0  # the unchanged 11px label has its complete row for glyph metrics

print("Auto Margin descender layout: 20px label row, full mode-specific clip/card height, Fast and Best Input PASS")
