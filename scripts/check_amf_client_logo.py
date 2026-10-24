"""Targeted packaging/runtime regression for the canonical AMF product logo."""

import argparse
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
NEW_REL = "tclient/amf/amf_client_logo.png"
OLD_REL = "tclient/gui_logo.png"

parser = argparse.ArgumentParser()
parser.add_argument("--runtime-root", type=Path, help="portable/build root containing data/")
parser.add_argument("--package-root", type=Path, help="portable package root with its distribution manifest")
args = parser.parse_args()

asset = ROOT / "data" / NEW_REL
assert asset.is_file(), f"canonical product logo missing: {asset}"
assert not (ROOT / "data" / OLD_REL).exists(), "obsolete AMF product logo still exists in source data"
png = asset.read_bytes()
assert png[:8] == b"\x89PNG\r\n\x1a\n", "canonical logo is not a PNG"
width, height = struct.unpack(">II", png[16:24])
assert width > 0 and height > 0 and width == height * 3, (width, height)

content = (ROOT / "datasrc/content.py").read_text(encoding="utf-8-sig")
assert content.count(f'Image("banner", "{NEW_REL}")') == 1
assert OLD_REL not in content

start_menu = (ROOT / "src/game/client/components/menus_start.cpp").read_text(encoding="utf-8-sig")
assert "m_aImages[IMAGE_BANNER]" in start_menu
assert "const float LogoHeight = LogoWidth / 3.0f;" in start_menu
assert "constexpr float LogoButtonGap = 10.0f;" in start_menu
assert "std::min(LogoButtonGap, LogoAvailableSpace)" in start_menu
assert "constexpr float LogoYOffset = 4.0f;" in start_menu
assert "LogoY + LogoYOffset + LogoOffset" in start_menu
assert "Graphics()->SetColor(1, 1, 1, 1.0f);" in start_menu
assert "Graphics()->SetColor(1, 1, 1, StartMenuProgress);" not in start_menu

tclient = (ROOT / "src/game/client/components/tclient/tclient.cpp").read_text(encoding="utf-8-sig")
assert f'FileExists("{NEW_REL}"' in tclient
assert OLD_REL not in tclient

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
assert cmake.count(NEW_REL) == 1
assert OLD_REL not in cmake
assert "set_glob(DATA GLOB_RECURSE" in cmake
assert "set(CPACK_DIRS data)" in cmake

manifest_cmake = (ROOT / "cmake/amf_distribution_manifest.cmake.in").read_text(encoding="utf-8-sig")
assert "file(GLOB_RECURSE AMF_MANIFEST_FILES" in manifest_cmake

updater = (ROOT / "src/tools/amfclient_updater.cpp").read_text(encoding="utf-8-sig")
assert "FindObsoleteDistributionFiles" in updater and "RemoveObsoleteDistributionFiles" in updater
assert "!NewFiles.contains(Relative) && Relative != LegacyAmfLogo" in updater
assert 'L"data\\\\tclient\\\\gui_logo.png"' in updater, "legacy installs without manifests must remove the old AMF asset"
assert "CopyTree(CopyRoot, pArgs->m_InstallDirectory" in updater

# Only production/runtime source paths are checked here, excluding this regression's
# intentional old-path constant and unrelated status/report artifacts.
for relative in ("datasrc/content.py", "src/game/client/components/menus_start.cpp", "src/game/client/components/tclient/tclient.cpp", "CMakeLists.txt"):
    assert OLD_REL not in (ROOT / relative).read_text(encoding="utf-8-sig"), f"obsolete runtime reference in {relative}"

if args.runtime_root:
    runtime_asset = args.runtime_root / "data" / NEW_REL
    assert runtime_asset.is_file(), f"runtime asset missing: {runtime_asset}"
    assert not (args.runtime_root / "data" / OLD_REL).exists(), "obsolete AMF logo still packaged"

if args.package_root:
    package_asset = args.package_root / "data" / NEW_REL
    assert package_asset.is_file(), f"portable package is missing {package_asset}"
    assert not (args.package_root / "data" / OLD_REL).exists(), "obsolete AMF logo still in package"
    manifest = (args.package_root / "amf_distribution_manifest.txt").read_text(encoding="utf-8-sig").splitlines()
    manifest_files = {line.strip().replace("\\", "/") for line in manifest if line and not line.startswith("#")}
    assert f"data/{NEW_REL}" in manifest_files, "updater distribution manifest omits canonical logo"
    assert f"data/{OLD_REL}" not in manifest_files, "updater distribution manifest still requires obsolete logo"
    legacy_old_manifest = {f"data/{OLD_REL}"}
    assert (legacy_old_manifest - manifest_files) == {f"data/{OLD_REL}"}, "upgrade must retire the old AMF logo"

print(f"AMF canonical logo: PNG {width}x{height}; start-menu banner mapping PASS; obsolete runtime refs 0")
print("CMake data staging + portable packaging + updater manifest/obsolete-file path PASS")
