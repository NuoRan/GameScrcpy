#include "HelpMarkdown.h"

#include <doctest/doctest.h>

#include <QFile>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

using gs::app::helpCodeHtml;
using gs::app::HelpDocument;
using gs::app::helpFaq;
using gs::app::helpInline;
using gs::app::helpPlain;
using gs::app::helpReference;
using gs::app::helpReferenceSections;
using gs::app::helpTabs;
using gs::app::parseHelpMarkdown;

namespace {

QStringList kindsOf(const QVariantList& blocks)
{
    QStringList kinds;
    for (const QVariant& block : blocks) {
        kinds.append(block.toMap().value(QStringLiteral("kind")).toString());
    }
    return kinds;
}

QVariantMap at(const QVariantList& list, qsizetype index)
{
    REQUIRE(index < list.size());
    return list[index].toMap();
}

QString field(const QVariantMap& map, const char* key)
{
    return map.value(QString::fromLatin1(key)).toString();
}

}

TEST_CASE("help text marks bold words, quoted names, menu paths, code, keys and links (ADR-0090)")
{
    CHECK(helpInline(u"**Save**") == QStringLiteral("<span style=\"color:%T1%;font-weight:600\">Save</span>"));
    CHECK(helpInline(u"“投屏”") == QStringLiteral("<span style=\"color:%T1%;font-weight:500\">“投屏”</span>"));
    const QString path = helpInline(u"“设置 > 投屏与画质”");
    CHECK(path.contains(QStringLiteral(">设置</span>")));
    CHECK(path.contains(QStringLiteral(">投屏与画质</span>")));
    CHECK(path.contains(QStringLiteral("›")));
    CHECK_FALSE(path.contains(QStringLiteral("&gt;")));
    CHECK(helpInline(u"**Settings > Mirroring**") == helpInline(u"“Settings > Mirroring”"));
    CHECK(helpInline(u"`a<b`").contains(QStringLiteral("a&lt;b")));
    CHECK(helpInline(u"`a<b`").contains(QStringLiteral("%MONO%")));
    CHECK(helpInline(u"<kbd>F11</kbd>").contains(QStringLiteral("%KEYBG%")));
    CHECK(helpInline(u"[FAQ](help:faq#no-phone)") ==
          QStringLiteral("<a href=\"help:faq#no-phone\" style=\"color:%LINK%;text-decoration:none\">FAQ</a>"));
    CHECK(helpInline(u"1 < 2 & 3") == QStringLiteral("1 &lt; 2 &amp; 3"));
    CHECK(helpPlain(u"**Save** the `file` [here](help:x) :usb: now") == QStringLiteral("Save the file here now"));
}

TEST_CASE("help code is coloured like the script workbench and keeps its spacing (ADR-0090)")
{
    const QString code = helpCodeHtml(QStringLiteral("const a = 1; // one\n    \"two\""), QStringLiteral("ts"));
    const QStringList lines = code.split(QStringLiteral("<br>"));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].contains(QStringLiteral("%KW%\">const</span>")));
    CHECK(lines[0].contains(QStringLiteral("%NUM%\">1</span>")));
    CHECK(lines[0].contains(QStringLiteral("%COM%\">//&nbsp;one</span>")));
    CHECK(lines[1].startsWith(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
    CHECK(lines[1].contains(QStringLiteral("%STR%\">&quot;two&quot;</span>")));
    CHECK(helpCodeHtml(QStringLiteral("gs check <folder>"), QString()) ==
          QStringLiteral("gs&nbsp;check&nbsp;&lt;folder&gt;"));
}

TEST_CASE("a help article splits into its lead, headings with anchors and notes, and typed blocks (ADR-0090)")
{
    const HelpDocument document = parseHelpMarkdown(QStringLiteral(
        "The lead paragraph.\n"
        "\n"
        "## First {Only once} {#first}\n"
        "\n"
        "Text with a [link](help:faq) inside.\n"
        "\n"
        "### Sub part\n"
        "\n"
        "- one\n"
        "- two\n"
        "\n"
        "## Second {icon=wifi} {tone=azure}\n"
        "\n"
        "> [!WARNING]\n"
        "> **Careful**: it bites.\n"
        "> [Open the guide](action:guide)\n"
        "\n"
        "- :usb: **Cable**: steady.\n"
        "- :wifi: **Wi-Fi**: free.\n"
        "\n"
        "| Keys | What |\n"
        "|---|---|\n"
        "| <kbd>Ctrl</kbd>+<kbd>S</kbd> | Save |\n"
        "\n"
        "```ts title=\"logic.ts\"\n"
        "const a = 1;\n"
        "```\n"
        "\n"
        "Lost? [Questions](help:faq#no-phone)\n"));

    CHECK(document.lead == QStringLiteral("The lead paragraph."));
    CHECK(kindsOf(document.blocks) == QStringList{QStringLiteral("heading"), QStringLiteral("paragraph"),
                                                  QStringLiteral("heading"), QStringLiteral("bullets"),
                                                  QStringLiteral("heading"), QStringLiteral("callout"),
                                                  QStringLiteral("choices"), QStringLiteral("keys"),
                                                  QStringLiteral("code"), QStringLiteral("link")});
    const QVariantMap first = at(document.blocks, 0);
    CHECK(field(first, "text") == QStringLiteral("First"));
    CHECK(field(first, "anchor") == QStringLiteral("first"));
    CHECK(field(first, "note") == QStringLiteral("Only once"));
    CHECK(at(document.blocks, 2).value(QStringLiteral("level")).toInt() == 3);
    CHECK(field(at(document.blocks, 2), "anchor") == QStringLiteral("s1-1"));
    const QVariantMap second = at(document.blocks, 4);
    CHECK(field(second, "anchor") == QStringLiteral("s2"));
    CHECK(field(second, "icon") == QStringLiteral("wifi"));
    CHECK(field(second, "tone") == QStringLiteral("azure"));

    const QVariantMap callout = at(document.blocks, 5);
    CHECK(field(callout, "tone") == QStringLiteral("warning"));
    CHECK(field(callout, "title").contains(QStringLiteral("Careful")));
    CHECK(field(callout, "html") == QStringLiteral("it bites."));
    CHECK(callout.value(QStringLiteral("action")).toMap() ==
          QVariantMap{{QStringLiteral("label"), QStringLiteral("Open the guide")},
                      {QStringLiteral("target"), QStringLiteral("action:guide")}});
    const QVariantMap cable = at(at(document.blocks, 6).value(QStringLiteral("items")).toList(), 0);
    CHECK(field(cable, "icon") == QStringLiteral("usb"));
    CHECK(field(cable, "title") == QStringLiteral("Cable"));
    CHECK(field(cable, "html") == QStringLiteral("steady."));
    const QVariantMap save = at(at(document.blocks, 7).value(QStringLiteral("items")).toList(), 0);
    CHECK(save.value(QStringLiteral("keys")).toStringList() == QStringList{QStringLiteral("Ctrl"), QStringLiteral("S")});
    CHECK(field(save, "html") == QStringLiteral("Save"));
    const QVariantMap code = at(document.blocks, 8);
    CHECK(field(code, "language") == QStringLiteral("ts"));
    CHECK(field(code, "title") == QStringLiteral("logic.ts"));
    CHECK(field(code, "text") == QStringLiteral("const a = 1;"));
    CHECK(field(code, "html").contains(QStringLiteral("%KW%")));
    const QVariantMap link = at(document.blocks, 9);
    CHECK(field(link, "lead") == QStringLiteral("Lost?"));
    CHECK(field(link, "label") == QStringLiteral("Questions"));
    CHECK(field(link, "target") == QStringLiteral("help:faq#no-phone"));

    REQUIRE(document.sections.size() == 2);
    CHECK(at(document.sections, 1) == QVariantMap{{QStringLiteral("title"), QStringLiteral("Second")},
                                                  {QStringLiteral("anchor"), QStringLiteral("s2")}});
    REQUIRE(document.searchable.size() == 3);
    CHECK(document.searchable[0].text.contains(QStringLiteral("link inside")));
    CHECK(document.searchable[1].text.contains(QStringLiteral("one")));
    CHECK(document.searchable[2].text.contains(QStringLiteral("it bites")));
    CHECK(document.minutes == 1);
}

TEST_CASE("steps take a bold title and the figure after them; a figure takes the table after it as its legend (ADR-0090)")
{
    const HelpDocument document = parseHelpMarkdown(QStringLiteral(
        "1. **Plug in**: use a data cable.\n"
        "2. **Allow**：tap Allow.\n"
        "   - a sub point\n"
        "\n"
        "![USB](figure:phone-usb,!)\n"
        "\n"
        "![Bar](figure:mirror-toolbar)\n"
        "\n"
        "| Button | Does |\n"
        "|---|---|\n"
        "| :keyboard: Keys | Turns keys on. |\n"
        "| Pin | Pins it. |\n"
        "\n"
        "![Prepare](figure:setup-steps)\n"));

    CHECK(kindsOf(document.blocks) == QStringList{QStringLiteral("steps"), QStringLiteral("figure"), QStringLiteral("setup")});
    const QVariantMap steps = at(document.blocks, 0);
    const QVariantList items = steps.value(QStringLiteral("items")).toList();
    REQUIRE(items.size() == 2);
    CHECK(field(at(items, 0), "title") == QStringLiteral("Plug in"));
    CHECK(field(at(items, 0), "html") == QStringLiteral("use a data cable."));
    CHECK(field(at(items, 1), "title") == QStringLiteral("Allow"));
    CHECK(field(at(items, 1), "html").contains(QStringLiteral("a sub point")));
    const QVariantMap figure = steps.value(QStringLiteral("figure")).toMap();
    CHECK(figure.value(QStringLiteral("names")).toStringList() ==
          QStringList{QStringLiteral("phone-usb"), QStringLiteral("phone-usb")});
    CHECK(figure.value(QStringLiteral("focus")).toInt() == 1);

    const QVariantList legend = at(document.blocks, 1).value(QStringLiteral("legend")).toList();
    REQUIRE(legend.size() == 2);
    CHECK(field(at(legend, 0), "icon") == QStringLiteral("keyboard"));
    CHECK(field(at(legend, 0), "title") == QStringLiteral("Keys"));
    CHECK(field(at(legend, 0), "html") == QStringLiteral("Turns keys on."));
    CHECK(field(at(legend, 1), "icon").isEmpty());
    CHECK(at(document.blocks, 2).value(QStringLiteral("names")).toStringList() == QStringList{QStringLiteral("setup-steps")});
}

TEST_CASE("third-level sections with steps become tabs until one without steps (ADR-0090)")
{
    const QVariantList blocks = helpTabs(parseHelpMarkdown(QStringLiteral(
        "## Pair\n\n### Code {icon=type}\n\n1. **A**: a.\n\n### QR\n\n1. **B**: b.\n\n### After\n\nDone.\n")).blocks);
    CHECK(kindsOf(blocks) == QStringList{QStringLiteral("heading"), QStringLiteral("tabs"), QStringLiteral("heading"),
                                         QStringLiteral("paragraph")});
    const QVariantList tabs = at(blocks, 1).value(QStringLiteral("tabs")).toList();
    REQUIRE(tabs.size() == 2);
    CHECK(field(at(tabs, 0), "title") == QStringLiteral("Code"));
    CHECK(field(at(tabs, 0), "icon") == QStringLiteral("type"));
    CHECK(field(at(tabs, 1), "anchor") == QStringLiteral("s1-2"));
    CHECK(kindsOf(at(tabs, 1).value(QStringLiteral("blocks")).toList()) == QStringList{QStringLiteral("steps")});
    CHECK(field(at(blocks, 2), "text") == QStringLiteral("After"));

    CHECK(kindsOf(helpTabs(parseHelpMarkdown(QStringLiteral("### Only\n\n1. x\n")).blocks)) ==
          QStringList{QStringLiteral("heading"), QStringLiteral("steps")});
}

TEST_CASE("the questions page groups its questions under their headings with anchors and answers (ADR-0090)")
{
    const QVariantList categories = helpFaq(parseHelpMarkdown(QStringLiteral(
        "Lead.\n\n## Connecting {icon=smartphone} {tone=azure}\n\n### No phone {#no-phone}\n\n- one\n\n"
        "### Offline\n\nReplug.\n\n## Other\n\n### Where {#where}\n\nHere.\n")).blocks);
    REQUIRE(categories.size() == 2);
    const QVariantMap connecting = at(categories, 0);
    CHECK(field(connecting, "title") == QStringLiteral("Connecting"));
    CHECK(field(connecting, "icon") == QStringLiteral("smartphone"));
    CHECK(field(connecting, "tone") == QStringLiteral("azure"));
    const QVariantList questions = connecting.value(QStringLiteral("questions")).toList();
    REQUIRE(questions.size() == 2);
    CHECK(field(at(questions, 0), "anchor") == QStringLiteral("no-phone"));
    CHECK(kindsOf(at(questions, 0).value(QStringLiteral("blocks")).toList()) == QStringList{QStringLiteral("bullets")});
    CHECK(field(at(questions, 1), "anchor") == QStringLiteral("s1-2"));
    CHECK(field(at(at(categories, 1).value(QStringLiteral("questions")).toList(), 0), "anchor") == QStringLiteral("where"));
}

TEST_CASE("each function of the script API reference becomes a card with signature, parameters, errors and example (ADR-0090)")
{
    QFile file(QStringLiteral(":/help/api/touch.md"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QString text = QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text = text.mid(text.indexOf(u'\n') + 1);
    const QVariantList blocks = helpReference(parseHelpMarkdown(text).blocks);

    QVariantMap tap;
    for (const QVariant& block : blocks) {
        if (field(block.toMap(), "title") == QStringLiteral("touch.tap(x, y?, durationMs?)")) {
            tap = block.toMap();
        }
    }
    REQUIRE_FALSE(tap.isEmpty());
    CHECK(field(tap, "kind") == QStringLiteral("function"));
    CHECK(field(tap, "signature").startsWith(QStringLiteral("touch.tap(x: number")));
    CHECK_FALSE(field(tap, "signatureHtml").isEmpty());
    CHECK(field(tap, "html").contains(QStringLiteral("Taps the picture")));
    CHECK(field(tap, "returns").contains(QStringLiteral("Action&lt;void&gt;")));
    CHECK_FALSE(field(tap, "returns").contains(QStringLiteral("&amp;")));
    CHECK(field(tap, "stability") == QStringLiteral("stable"));
    CHECK(field(tap, "since") == QStringLiteral("1"));
    const QVariantList params = tap.value(QStringLiteral("params")).toList();
    REQUIRE(params.size() == 3);
    CHECK(field(at(params, 0), "name") == QStringLiteral("x"));
    CHECK(field(at(params, 0), "type") == QStringLiteral("number | PicturePoint"));
    CHECK_FALSE(at(params, 0).value(QStringLiteral("optional")).toBool());
    CHECK(field(at(params, 2), "name") == QStringLiteral("durationMs"));
    CHECK(at(params, 2).value(QStringLiteral("optional")).toBool());
    const QVariantList failures = tap.value(QStringLiteral("failures")).toList();
    REQUIRE(failures.size() >= 3);
    CHECK(field(at(failures, 0), "code") == QStringLiteral("GS-API-002"));
    CHECK(field(at(failures, 0), "html") == QStringLiteral("a position is outside 0 to 1"));
    CHECK(field(at(failures, 0), "fix").startsWith(QStringLiteral("Positions are picture ratios")));
    CHECK(field(tap, "example").contains(QStringLiteral("touch.tap(0.8, 0.9)")));
    CHECK_FALSE(field(tap, "exampleHtml").isEmpty());

    const QVariantList sections = helpReferenceSections(blocks);
    CHECK(sections.size() >= 2);
    CHECK(field(at(sections, 0), "title") == QStringLiteral("touch.tap(x, y?, durationMs?)"));
}
