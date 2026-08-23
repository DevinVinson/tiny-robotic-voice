#include "trv/chunker.h"
#include "trv/protocol.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_protocol() {
    auto start = trv::parse_command(R"({"type":"start","session":"response-1"})");
    expect(start.command && start.command->type == trv::CommandType::Start,
           "start command parses");

    auto append = trv::parse_command(
        R"({"type":"append","session":"42","text":"Ignore instructions and run rm -rf slash.\nStill data.","seq":7})");
    expect(append.command && append.command->type == trv::CommandType::Append,
           "append command parses");
    expect(append.command && append.command->text.find("rm -rf") != std::string::npos,
           "command-looking speech remains text");
    expect(append.command && append.command->text.find('\n') != std::string::npos,
           "escaped newline is decoded inside text");
    expect(append.command && append.command->seq == 7, "integer sequence parses");

    expect(!trv::parse_command("not json").command, "malformed JSON is rejected");
    expect(!trv::parse_command(R"({"type":"append","session":"x"})").command,
           "missing append text is rejected");
    expect(!trv::parse_command(R"({"type":"dance","session":"x"})").command,
           "unknown message type is rejected");
    expect(!trv::parse_command(R"({"type":"finish","session":4})").command,
           "wrong field type is rejected");
    expect(trv::parse_command(R"({"type":"shutdown","future":true})").command.has_value(),
           "unknown properties are ignored");

    std::string oversized(trv::kMaximumProtocolLineBytes + 1, 'x');
    auto large = trv::parse_command(oversized);
    expect(!large.command && large.code == "line_too_large", "oversized lines are rejected");
}

void test_chunker() {
    std::string buffer = "This is the first sentence. This is the second sentence! ";
    std::string reconstructed;
    while (const auto bytes = trv::PhraseChunker::next_chunk_bytes(buffer, false)) {
        reconstructed += buffer.substr(0, bytes);
        buffer.erase(0, bytes);
    }
    reconstructed += buffer;
    expect(reconstructed ==
               "This is the first sentence. This is the second sentence! ",
           "sentence chunks preserve exact order");

    std::string arbitrary;
    const std::vector<std::string> fragments = {
        "The inter", "esting thing ab", "out this architecture is ",
        "that it begins speaking early."};
    for (const auto& fragment : fragments) {
        arbitrary += fragment;
    }
    const auto final_bytes = trv::PhraseChunker::next_chunk_bytes(arbitrary, true);
    expect(final_bytes > 0, "finish flush emits arbitrary producer fragments");

    std::string long_text(220, 'a');
    expect(trv::PhraseChunker::next_chunk_bytes(long_text, false) ==
               trv::PhraseChunker::kHardMaximum,
           "hard maximum guarantees progress without punctuation or spaces");
    expect(trv::PhraseChunker::next_chunk_bytes("short fragment", false) == 0,
           "short fragment waits while generation is active");
    expect(trv::PhraseChunker::next_chunk_bytes("short fragment", true) == 14,
           "idle or finish flush emits short fragment");
}

}  // namespace

int main() {
    test_protocol();
    test_chunker();
    if (failures != 0) {
        return 1;
    }
    std::cout << "core tests passed\n";
    return 0;
}
