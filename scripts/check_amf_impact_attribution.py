#!/usr/bin/env python3
"""Check scoped AMF Impact attribution, licensing, and package coverage."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMPACT_DIR = ROOT / "src/game/client/components/tclient/impact"
LICENSE_PATH = ROOT / "licenses/AMF-Impact-LICENSE.txt"
LICENSE = LICENSE_PATH.read_text(encoding="utf-8")
UI = (ROOT / "src/game/client/components/tclient/menus_tclient.cpp").read_text(encoding="utf-8")
LOCALIZATION_TEST = (ROOT / "src/test/localization_runtime_test.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
ROOT_LICENSE = (ROOT / "license.txt").read_text(encoding="utf-8")

assert LICENSE.startswith("AMF IMPACT LICENSE")
assert "Copyright (c) 2026 alya?" in LICENSE
assert LICENSE.count("Copyright (c) 2026 alya?") == 1
assert "CMenus::RenderAmfRunStatisticsQuickOverlay" in LICENSE
assert "CMenus::RenderAmfRunStatistics" in LICENSE
assert "src/test/impact_test.cpp" in LICENSE
assert "AMF IMPACT / RUN STATISTICS LICENSE EXCEPTION" in ROOT_LICENSE
assert "does not apply" in ROOT_LICENSE and "AMF-Impact-LICENSE.txt" in ROOT_LICENSE

scope_section = LICENSE.split("following files:", 1)[1].split("\n\nIn src/", 1)[0]
covered_paths = re.findall(r"^\s+(src/game/client/components/tclient/impact/\S+)$", scope_section, re.MULTILINE)
assert len(covered_paths) == 28, f"unexpected Impact scope file count: {len(covered_paths)}"
assert len(covered_paths) == len(set(covered_paths)), "duplicate path in Impact license scope"
impact_sources = [ROOT / path for path in covered_paths]
for path in impact_sources:
	text = path.read_text(encoding="utf-8")
	assert "Copyright (c) 2026 alya?" in text, f"missing copyright notice: {path.relative_to(ROOT)}"
	assert "licenses/AMF-Impact-LICENSE.txt" in text, f"missing final license reference: {path.relative_to(ROOT)}"

impact_test = (ROOT / "src/test/impact_test.cpp").read_text(encoding="utf-8")
assert "Copyright (c) 2026 alya?" in impact_test
assert "licenses/AMF-Impact-LICENSE.txt" in impact_test
assert UI.count("Impact System by AMF Client") == 1, "visible attribution must occur exactly once"
assert "AttributionRow" in UI and "Panel.HSplitBottom(14.0f" in UI
assert UI.count("This notice covers only this function's original AMF-authored expression.") == 2
assert UI.count("Copyright (c) 2026 alya?") == 2
assert "Impact System by AMF Client" in LOCALIZATION_TEST

expected = {
	"russian": "Система Impact от AMF Client",
	"ukrainian": "Система Impact від AMF Client",
}

def catalog(path: Path) -> dict[tuple[str, str], str]:
	lines = path.read_text(encoding="utf-8-sig").splitlines()
	result = {}
	context = ""
	i = 0
	while i < len(lines):
		line = lines[i]
		if not line:
			i += 1
			continue
		if line.startswith("[") and line.endswith("]"):
			context = line[1:-1]
			i += 1
			continue
		if i + 1 < len(lines) and lines[i + 1].startswith("== "):
			result[(line, context)] = lines[i + 1][3:] or line
			i += 2
			context = ""
		else:
			i += 1
			context = ""
	return result


key = "Impact System by AMF Client"
for language, translation in expected.items():
	for config in ("Debug", "Release"):
		path = ROOT / f"build/{config}/data/languages/{language}.txt"
		assert path.is_file(), f"missing runtime catalog: {path}"
		assert catalog(path).get((key, "AMF Client")) == translation, f"runtime attribution lookup mismatch: {config}/{language}"
assert key not in catalog(ROOT / "data/languages/russian.txt") or catalog(ROOT / "data/languages/russian.txt")[(key, "AMF Client")] == expected["russian"]
assert key not in catalog(ROOT / "data/languages/ukrainian.txt") or catalog(ROOT / "data/languages/ukrainian.txt")[(key, "AMF Client")] == expected["ukrainian"]

# The final license is included in source and portable distributions, while
# the superseded draft is not referenced or packaged.
assert "licenses/AMF-Impact-LICENSE.txt" in CMAKE
assert CMAKE.count("licenses/AMF-Impact-LICENSE.txt") >= 2
assert not (ROOT / "licenses/AMF-Impact-LICENSE-DRAFT.txt").exists()
assert "AMF-Impact-LICENSE-DRAFT.txt" not in CMAKE
print("AMF Impact scoped notices, localized UI attribution, final license, and source/portable package configuration: PASS")
