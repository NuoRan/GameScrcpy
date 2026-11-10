#include "TestSupport.h"

#include <gs/esp32/Diagnosis.h>

#include <doctest/doctest.h>

#include <string_view>

using esp32test::Bytes;
using esp32test::fromHex;
using esp32test::textBytes;
using gs::esp32::BoardProblem;
using gs::esp32::BoardTrouble;
using gs::esp32::diagnoseHeard;

namespace {

BoardTrouble diagnose(std::string_view text)
{
    return diagnoseHeard(textBytes(text));
}

constexpr std::string_view kBootBanner = "ESP-ROM:esp32s3-20210327\r\n"
                                         "Build:Mar 27 2021\r\n"
                                         "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n";

}

TEST_CASE("a port that sent nothing is silent")
{
    const BoardTrouble trouble = diagnoseHeard({});
    CHECK(trouble.problem == BoardProblem::Silent);
    CHECK(trouble.bytes == 0U);
    CHECK(trouble.resetReason.empty());
}

TEST_CASE("the old QineTouch greeting means the old firmware")
{
    CHECK(diagnose("QineTouch v3.2\r\n").problem == BoardProblem::OldFirmware);
    CHECK(diagnose("boot\r\nqinetouch ready\r\n").problem == BoardProblem::OldFirmware);
    CHECK(diagnose("GAMESCRCPY QINETOUCH").problem == BoardProblem::OldFirmware);
}

TEST_CASE("a stream of old 12-byte touch packets means the old firmware too")
{
    const Bytes packets = fromHex("f4010064000000c800000002"
                                  "f40001ff7f00000000000002");
    CHECK(diagnoseHeard(packets).problem == BoardProblem::OldFirmware);
    CHECK(diagnoseHeard(fromHex("f4010064000000c800000002ff")).problem == BoardProblem::WrongPort);
}

TEST_CASE("boot ROM text with no HELLO means the board keeps restarting, and the reset reason is kept")
{
    const BoardTrouble trouble = diagnose(kBootBanner);
    CHECK(trouble.problem == BoardProblem::Restarting);
    CHECK(trouble.resetReason == "0x1 (POWERON)");
    CHECK(trouble.bytes == kBootBanner.size());
    const BoardTrouble twice = diagnose(std::string(kBootBanner) + std::string(kBootBanner));
    CHECK(twice.problem == BoardProblem::Restarting);
    CHECK(twice.restarts == 2);
}

TEST_CASE("a lone boot line still counts as restarting")
{
    CHECK(diagnose("boot:0x13\r\n").problem == BoardProblem::Restarting);
    CHECK(diagnose("rst:0xc (RTC_SW_CPU_RST)\r\n").problem == BoardProblem::Restarting);
}

TEST_CASE("waiting for download is download mode even after boot text")
{
    CHECK(diagnose("waiting for download\r\n").problem == BoardProblem::DownloadMode);
    CHECK(diagnose(std::string(kBootBanner) + "waiting for download\r\n").problem == BoardProblem::DownloadMode);
    CHECK(diagnose(kBootBanner).problem == BoardProblem::Restarting);
}

TEST_CASE("bytes that are not readable ESP32 talk are a wrong port or speed")
{
    CHECK(diagnoseHeard(fromHex("0080ff0013fe7f01a2c4")).problem == BoardProblem::WrongPort);
    CHECK(diagnoseHeard(fromHex("f4")).problem == BoardProblem::WrongPort);
    CHECK(diagnose("hello from some other device").problem == BoardProblem::WrongPort);
}

TEST_CASE("mixed captures keep the most useful answer")
{
    CHECK(diagnose(std::string(kBootBanner) + "qinetouch\r\n").problem == BoardProblem::OldFirmware);
    CHECK(diagnose(std::string(kBootBanner) + std::string(kBootBanner) + "waiting for download\r\n").problem ==
          BoardProblem::DownloadMode);
    CHECK(diagnose(std::string(kBootBanner) + "\x01\x02\xff").problem == BoardProblem::Restarting);
}

TEST_CASE("every problem has a name for the log")
{
    CHECK(std::string_view(gs::esp32::boardProblemName(BoardProblem::Silent)) == "the port sent nothing");
    CHECK(std::string_view(gs::esp32::boardProblemName(BoardProblem::None)) == "no problem");
}
