#include "trv/protocol.h"

#include "yyjson.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace trv {
namespace {

using DocPtr = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
using MutDocPtr = std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)>;

ParseResult fail(std::string code, std::string message) {
    return {std::nullopt, std::move(code), std::move(message)};
}

bool get_string(yyjson_val* object, const char* key, std::string& output) {
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!yyjson_is_str(value)) {
        return false;
    }
    output.assign(yyjson_get_str(value), yyjson_get_len(value));
    return output.find('\0') == std::string::npos;
}

}  // namespace

ParseResult parse_command(const std::string& line) {
    if (line.size() > kMaximumProtocolLineBytes) {
        return fail("line_too_large", "Protocol line exceeds the 1 MiB limit.");
    }

    yyjson_read_err error{};
    DocPtr doc(yyjson_read_opts(const_cast<char*>(line.data()), line.size(),
                                YYJSON_READ_NOFLAG, nullptr, &error),
               &yyjson_doc_free);
    if (!doc) {
        return fail("invalid_json", "Input is not valid JSON.");
    }
    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (!yyjson_is_obj(root)) {
        return fail("invalid_message", "Protocol message must be a JSON object.");
    }

    std::string type;
    if (!get_string(root, "type", type)) {
        return fail("missing_field", "Field 'type' must be a string.");
    }

    Command command{};
    if (type == "shutdown") {
        command.type = CommandType::Shutdown;
    } else {
        if (!get_string(root, "session", command.session) || command.session.empty()) {
            return fail("missing_field", "Field 'session' must be a non-empty string.");
        }
        if (type == "start") {
            command.type = CommandType::Start;
        } else if (type == "append") {
            command.type = CommandType::Append;
            if (!get_string(root, "text", command.text)) {
                return fail("missing_field", "Field 'text' must be a string.");
            }
        } else if (type == "finish") {
            command.type = CommandType::Finish;
        } else if (type == "interrupt") {
            command.type = CommandType::Interrupt;
        } else {
            return fail("unknown_type", "Unknown protocol message type.");
        }
    }

    if (yyjson_val* seq = yyjson_obj_get(root, "seq")) {
        if (yyjson_is_sint(seq)) {
            command.seq = yyjson_get_sint(seq);
        } else if (yyjson_is_uint(seq) &&
                   yyjson_get_uint(seq) <=
                       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            command.seq = static_cast<std::int64_t>(yyjson_get_uint(seq));
        } else {
            return fail("invalid_field", "Optional field 'seq' must be an integer.");
        }
    }
    return {std::move(command), {}, {}};
}

std::string json_event(const std::string& type, const std::string& session,
                       const std::string& code, const std::string& message,
                       bool recoverable, std::optional<std::int64_t> seq,
                       std::optional<std::size_t> bytes) {
    MutDocPtr doc(yyjson_mut_doc_new(nullptr), &yyjson_mut_doc_free);
    yyjson_mut_val* root = yyjson_mut_obj(doc.get());
    yyjson_mut_doc_set_root(doc.get(), root);
    yyjson_mut_obj_add_strcpy(doc.get(), root, "type", type.c_str());
    if (!session.empty()) {
        yyjson_mut_obj_add_strcpy(doc.get(), root, "session", session.c_str());
    }
    if (!code.empty()) {
        yyjson_mut_obj_add_strcpy(doc.get(), root, "code", code.c_str());
        yyjson_mut_obj_add_bool(doc.get(), root, "recoverable", recoverable);
    }
    if (!message.empty()) {
        yyjson_mut_obj_add_strcpy(doc.get(), root, "message", message.c_str());
    }
    if (seq) {
        yyjson_mut_obj_add_sint(doc.get(), root, "seq", *seq);
    }
    if (bytes) {
        yyjson_mut_obj_add_uint(doc.get(), root, "bytes", *bytes);
    }
    char* encoded = yyjson_mut_write(doc.get(), YYJSON_WRITE_NOFLAG, nullptr);
    if (!encoded) {
        return R"({"type":"error","code":"internal_error","recoverable":false})";
    }
    std::string result(encoded);
    std::free(encoded);
    return result;
}

}  // namespace trv
