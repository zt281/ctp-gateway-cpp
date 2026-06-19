#include <gtest/gtest.h>
#include <sstream>
#include <regex>
#include "gateway_log.h"

// Helper to capture stdout/stderr
class StreamCapture {
public:
    StreamCapture(std::ostream& stream) : stream_(stream), old_buf_(stream.rdbuf()) {
        stream_.rdbuf(buffer_.rdbuf());
    }
    ~StreamCapture() { stream_.rdbuf(old_buf_); }
    std::string str() const { return buffer_.str(); }
private:
    std::ostream& stream_;
    std::streambuf* old_buf_;
    std::ostringstream buffer_;
};

// Test that LOG_INFO outputs to stdout with structured prefix
TEST(GatewayLog, LogInfoPrefixFormat) {
    StreamCapture cap(std::cout);
    LOG_INFO("Test message");
    std::string output = cap.str();

    // Should contain [timestamp][INFO][CtpGateway]
    std::regex prefix_re(R"(\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\]\[INFO\]\[CtpGateway\] )");
    EXPECT_TRUE(std::regex_search(output, prefix_re))
        << "Output: " << output;
    EXPECT_NE(output.find("Test message"), std::string::npos);
}

// Test that LOG_WARN outputs to stderr with structured prefix
TEST(GatewayLog, LogWarnPrefixFormat) {
    StreamCapture cap(std::cerr);
    LOG_WARN("Warning");
    std::string output = cap.str();

    std::regex prefix_re(R"(\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\]\[WARN\]\[CtpGateway\] )");
    EXPECT_TRUE(std::regex_search(output, prefix_re))
        << "Output: " << output;
    EXPECT_NE(output.find("Warning"), std::string::npos);
}

// Test that LOG_ERROR outputs to stderr with structured prefix
TEST(GatewayLog, LogErrorPrefixFormat) {
    StreamCapture cap(std::cerr);
    LOG_ERROR("Error code");
    std::string output = cap.str();

    std::regex prefix_re(R"(\[\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\]\[ERROR\]\[CtpGateway\] )");
    EXPECT_TRUE(std::regex_search(output, prefix_re))
        << "Output: " << output;
    EXPECT_NE(output.find("Error code"), std::string::npos);
}

// Test that multiple log calls each get their own timestamp
TEST(GatewayLog, MultipleLogsHaveIndependentTimestamps) {
    StreamCapture cap(std::cout);
    LOG_INFO("First");
    LOG_INFO("Second");
    std::string output = cap.str();

    // Count occurrences of [INFO][CtpGateway]
    size_t count = 0;
    size_t pos = 0;
    while ((pos = output.find("[INFO][CtpGateway]", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    EXPECT_EQ(count, 2);
}

// Test that the log macro ends with a newline
TEST(GatewayLog, LogEndsWithNewline) {
    StreamCapture cap(std::cout);
    LOG_INFO("Line test");
    std::string output = cap.str();
    EXPECT_EQ(output.back(), '\n');
}
