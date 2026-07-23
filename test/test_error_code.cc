#include "catch_amalgamated.hpp"
/*
 * test_error_code.cc — ErrorCode unit tests
 *
 * Tests ErrorEnum to string conversion, error category name,
 * error_condition integration, and segment boundaries.
 */
#include "util/ErrorCode.h"

using cosmo::util::ErrorEnum;

TEST_CASE("ErrorCode: Success converts to non-empty message", "[error-code]") {
    auto cond = make_error_condition(ErrorEnum::Success);

    SECTION("Category name is CWAI Error") {
        REQUIRE(std::string(cond.category().name()) == "CWAI Error");
    }

    SECTION("Success has message") {
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("Success value is 0") {
        REQUIRE(cond.value() == 0);
    }
}

TEST_CASE("ErrorCode: Generic error codes", "[error-code]") {
    SECTION("Failed has message") {
        auto cond = make_error_condition(ErrorEnum::Failed);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("InvalidParam has message") {
        auto cond = make_error_condition(ErrorEnum::InvalidParam);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("FileNotExist has message") {
        auto cond = make_error_condition(ErrorEnum::FileNotExist);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("NoMem has message") {
        auto cond = make_error_condition(ErrorEnum::NoMem);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("MandatoryParamMiss has message") {
        auto cond = make_error_condition(ErrorEnum::MandatoryParamMiss);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: Auth segment (0x2715+)", "[error-code]") {
    SECTION("AuthFailed has message") {
        auto cond = make_error_condition(ErrorEnum::AuthFailed);
        REQUIRE(cond.value() == 0x2715);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("NotLogin has message") {
        auto cond = make_error_condition(ErrorEnum::NotLogin);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("ModelFileName has message") {
        auto cond = make_error_condition(ErrorEnum::ModelFileName);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: Demux/Decoder segment", "[error-code]") {
    SECTION("DemuxOpenInvalidUrl has message") {
        auto cond = make_error_condition(ErrorEnum::DemuxOpenInvalidUrl);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("DecoderFrameFailed has message") {
        auto cond = make_error_condition(ErrorEnum::DecoderFrameFailed);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: Live preview transport segment", "[error-code]") {
    auto publish = make_error_condition(ErrorEnum::LiveStreamPublishFailed);
    auto timeout = make_error_condition(ErrorEnum::LiveStreamReadyTimeout);
    auto stopped = make_error_condition(ErrorEnum::LiveStreamStopped);

    REQUIRE(publish.value() == 0x3200);
    REQUIRE_FALSE(publish.message().empty());
    REQUIRE_FALSE(timeout.message().empty());
    REQUIRE_FALSE(stopped.message().empty());
    REQUIRE(cosmo::util::ErrorEnumName(ErrorEnum::LiveStreamPublishFailed) == "LiveStreamPublishFailed");
}

TEST_CASE("ErrorCode: Face segment (0x4000+)", "[error-code]") {
    SECTION("FaceIsNotInTheMiddle has message") {
        auto cond = make_error_condition(ErrorEnum::FaceIsNotInTheMiddle);
        REQUIRE(cond.value() == 0x4000);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("NoFaceDetected has message") {
        auto cond = make_error_condition(ErrorEnum::NoFaceDetected);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: AI segment (0x1000000+)", "[error-code]") {
    SECTION("AI_FAILED has message") {
        auto cond = make_error_condition(ErrorEnum::AI_FAILED);
        REQUIRE(cond.value() == 0x1000000);
        REQUIRE_FALSE(cond.message().empty());
    }

    SECTION("AI_DETECT_FAILED has message") {
        auto cond = make_error_condition(ErrorEnum::AI_DETECT_FAILED);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: Action segment (0x2000000+)", "[error-code]") {
    SECTION("ActionAlgNotExist has message") {
        auto cond = make_error_condition(ErrorEnum::ActionAlgNotExist);
        REQUIRE(cond.value() == 0x2000000);
        REQUIRE_FALSE(cond.message().empty());
    }
}

TEST_CASE("ErrorCode: Unknown code returns empty message", "[error-code]") {
    // Cast an arbitrary value not in the enum
    auto cond = make_error_condition(static_cast<ErrorEnum>(0xFFFFFFF));
    REQUIRE(cond.message().empty());
}

TEST_CASE("ErrorCode: is_error_condition_enum trait", "[error-code]") {
    REQUIRE(std::is_error_condition_enum<ErrorEnum>::value);
}

TEST_CASE("ErrorCode: error_condition comparison", "[error-code]") {
    SECTION("Same code compares equal") {
        auto c1 = make_error_condition(ErrorEnum::Success);
        auto c2 = make_error_condition(ErrorEnum::Success);
        REQUIRE(c1 == c2);
    }

    SECTION("Different codes compare not-equal") {
        auto c1 = make_error_condition(ErrorEnum::Success);
        auto c2 = make_error_condition(ErrorEnum::Failed);
        REQUIRE(c1 != c2);
    }
}
