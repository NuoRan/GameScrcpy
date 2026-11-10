#include "ExtensionIcons.h"

#include "unit/ext/TestFiles.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <string>

using namespace gs;
using gs::ext::test::Scratch;
using gs::ext::test::writeFile;

namespace {

QString svgOf(const char* body)
{
    return app::iconPathOfSvg(QByteArray(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)") + body +
                              QByteArray("</svg>"));
}

}

TEST_CASE("ADR-0112: an extension's SVG icon becomes one path of the icon grid, made from paths, circles, rects, "
          "lines and polylines")
{
    CHECK(svgOf(R"(<path d="M4 4 L20 20"/>)") == "M4 4 L20 20");
    CHECK(svgOf(R"(<circle cx="12" cy="12" r="4"/>)") == "M 8 12 A 4 4 0 1 0 16 12 A 4 4 0 1 0 8 12 Z");
    CHECK(svgOf(R"(<rect x="2" y="3" width="20" height="18"/>)") == "M 2 3 H 22 V 21 H 2 Z");
    CHECK(svgOf(R"(<rect x="2" y="2" width="20" height="20" rx="2"/>)") ==
          "M 4 2 H 20 A 2 2 0 0 1 22 4 V 20 A 2 2 0 0 1 20 22 H 4 A 2 2 0 0 1 2 20 V 4 A 2 2 0 0 1 4 2 Z");
    CHECK(svgOf(R"(<line x1="1" y1="2" x2="3" y2="4"/>)") == "M 1 2 L 3 4");
    CHECK(svgOf(R"(<polyline points="1,2 3,4 5,6"/>)") == "M 1 2 L 3 4 L 5 6");
    CHECK(svgOf(R"(<polygon points="1 2 3 4 5 6"/>)") == "M 1 2 L 3 4 L 5 6 Z");
    CHECK(svgOf(R"(<g><title>Board</title><path d="M1 1h2"/><circle cx="2" cy="2" r="1"/></g>)") ==
          "M1 1h2 M 1 2 A 1 1 0 1 0 3 2 A 1 1 0 1 0 1 2 Z");
}

TEST_CASE("ADR-0112: an SVG icon with anything but plain shapes on the 24 grid is refused")
{
    CHECK(svgOf(R"(<script>alert(1)</script>)").isEmpty());
    CHECK(svgOf(R"(<image href="x.png"/>)").isEmpty());
    CHECK(svgOf(R"(<use href="#a"/>)").isEmpty());
    CHECK(svgOf(R"svg(<path d="M1 1" transform="scale(2)"/>)svg").isEmpty());
    CHECK(svgOf(R"svg(<path d="M1 1 url(#x)"/>)svg").isEmpty());
    CHECK(svgOf(R"(<circle cx="1" cy="1"/>)").isEmpty());
    CHECK(svgOf("").isEmpty());
    CHECK(app::iconPathOfSvg(R"(<svg viewBox="0 0 48 48"><path d="M1 1"/></svg>)").isEmpty());
    CHECK(app::iconPathOfSvg(R"(<!DOCTYPE svg [<!ENTITY a "b">]><svg><path d="M1 1"/></svg>)").isEmpty());
    CHECK(app::iconPathOfSvg(R"(<html><path d="M1 1"/></html>)").isEmpty());
    CHECK(app::iconPathOfSvg(QByteArray(static_cast<qsizetype>(app::kMostIconBytes) + 1, ' ')).isEmpty());
}

TEST_CASE("ADR-0112: an extension's icon file is read only from inside its folder and only as .svg")
{
    Scratch scratch;
    const std::filesystem::path folder = scratch / "alice.boards/1.0.0";
    writeFile(folder / "icons/board.svg", R"(<svg viewBox="0 0 24 24"><path d="M2 2 L22 22"/></svg>)");
    writeFile(folder / "icons/board.png", "not an svg");
    writeFile(scratch / "outside.svg", R"(<svg viewBox="0 0 24 24"><path d="M2 2 L22 22"/></svg>)");
    CHECK(app::extensionIconPath(folder, "icons/board.svg") == "M2 2 L22 22");
    CHECK(app::extensionIconPath(folder, "icons/board.png").isEmpty());
    CHECK(app::extensionIconPath(folder, "../../outside.svg").isEmpty());
    CHECK(app::extensionIconPath(folder, (scratch / "outside.svg").string()).isEmpty());
    CHECK(app::extensionIconPath(folder, "icons/missing.svg").isEmpty());
    CHECK(app::builtinIconName("loader-circle"));
    CHECK_FALSE(app::builtinIconName("svg:icons/board.svg"));
    CHECK_FALSE(app::builtinIconName(""));
}
