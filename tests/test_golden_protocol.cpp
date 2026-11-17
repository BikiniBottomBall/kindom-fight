// 跨语言协议契约测试：和 client/godot/tests/test_decoder.gd 共读同一份向量文件。
//
// 为什么需要它：帧格式在 C++（src/net/protocol.cpp）和 GDScript
// （client/godot/scripts/kf_protocol.gd）里各写了一遍。test_protocol.cpp 的 12 个
// 用例只能保护 C++ —— 只要两边没有共同的事实来源，"客户端把 seq 解成 65536" 这类
// bug 就只能靠手工联调发现，而那通常是凌晨两点。
//
// 向量文件：client/godot/tests/golden_frames.txt
// 查找顺序：环境变量 KF_GOLDEN_FILE → 从当前目录向上最多 5 层找这个相对路径
//           （所以从仓库根目录跑、从 build/ 跑、从 ctest 的 build 目录跑都能找到）。
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "net/decoder.h"
#include "net/protocol.h"
#include "test_framework.h"

using namespace kingdom_fight;

namespace {

constexpr const char* kGoldenRelative = "client/godot/tests/golden_frames.txt";

struct GoldenCase {
    std::string name;
    std::string bytes;
    Cmd cmd = Cmd::Ping;
    std::uint32_t seq = 0;
    std::string payload;
    bool expect_error = false;
};

void fail(const std::string& message) { ::lg_test::report_failure(__FILE__, __LINE__, message); }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 跳过空白等非十六进制字符；两个十六进制数字凑一个字节。
std::string parse_hex(const std::string& text) {
    std::string out;
    int high = -1;
    for (const char c : text) {
        const int value = hex_value(c);
        if (value < 0) continue;
        if (high < 0) {
            high = value;
            continue;
        }
        out.push_back(static_cast<char>((high << 4) | value));
        high = -1;
    }
    return out;
}

std::string trim(const std::string& text) {
    const auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) ++begin;
    while (end > begin && is_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::string find_golden_file() {
    if (const char* from_env = std::getenv("KF_GOLDEN_FILE"); from_env != nullptr && *from_env != '\0') {
        return from_env;
    }
    std::error_code error;
    std::filesystem::path directory = std::filesystem::current_path(error);
    if (error) return {};
    for (int depth = 0; depth < 5 && !directory.empty(); ++depth) {
        const std::filesystem::path candidate = directory / kGoldenRelative;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate.string();
        }
        if (!directory.has_parent_path()) break;
        directory = directory.parent_path();
    }
    return {};
}

const std::vector<GoldenCase>& golden_cases() {
    static const std::vector<GoldenCase> cases = [] {
        std::vector<GoldenCase> parsed;
        const std::string path = find_golden_file();
        if (path.empty()) {
            fail("找不到 " + std::string(kGoldenRelative) + "（可用 KF_GOLDEN_FILE 指定）");
            return parsed;
        }

        std::ifstream in(path);
        if (!in) {
            fail("打不开 golden 文件: " + path);
            return parsed;
        }

        std::string line;
        int line_number = 0;
        while (std::getline(in, line)) {
            ++line_number;
            const std::string text = trim(line);
            if (text.empty() || text[0] == '#') continue;

            const std::vector<std::string> parts = split(text, '|');
            if (parts.size() < 2) {
                fail("golden 第 " + std::to_string(line_number) + " 行格式不对");
                continue;
            }

            GoldenCase item;
            item.name = trim(parts[0]);
            item.expect_error = !item.name.empty() && item.name[0] == '!';
            if (item.expect_error) item.name.erase(0, 1);
            item.bytes = parse_hex(parts[1]);

            if (!item.expect_error) {
                if (parts.size() < 5) {
                    fail(item.name + ": 成功用例需要 name|hex|cmd|seq|payload");
                    continue;
                }
                item.cmd = static_cast<Cmd>(std::stoi(trim(parts[2])));
                item.seq = static_cast<std::uint32_t>(std::stoul(trim(parts[3])));
                const std::string payload_hex = trim(parts[4]);
                item.payload = (payload_hex == "-") ? std::string{} : parse_hex(payload_hex);
            }

            parsed.push_back(std::move(item));
        }
        return parsed;
    }();
    return cases;
}

}  // namespace

TEST(GoldenVectorsAreLoaded) {
    const auto& cases = golden_cases();
    int ok_cases = 0;
    int error_cases = 0;
    for (const auto& item : cases) {
        if (item.expect_error) {
            ++error_cases;
        } else {
            ++ok_cases;
        }
    }
    CHECK(ok_cases >= 3);
    CHECK(error_cases >= 2);
}

TEST(GoldenFramesDecodeToExpectedFields) {
    for (const auto& item : golden_cases()) {
        if (item.expect_error) continue;

        FrameDecoder decoder;
        decoder.feed(item.bytes);
        Frame frame{};
        if (decoder.next(frame) != DecodeResult::Decoded) {
            fail(item.name + ": 没能解出帧（客户端会遇到同样的问题）");
            continue;
        }

        CHECK_EQ(std::string(to_string(frame.cmd)), std::string(to_string(item.cmd)));
        CHECK_EQ(frame.seq, item.seq);
        CHECK_EQ(frame.payload, item.payload);
        CHECK(decoder.next(frame) == DecodeResult::NeedMore);
        CHECK_EQ(static_cast<int>(decoder.buffered()), 0);
    }
}

TEST(GoldenFramesReEncodeByteIdentically) {
    // 反向验证：把解出来的字段重新编码，必须逐字节等于向量。
    // 这条锁住"服务端下行"和"客户端上行"是同一个格式。
    for (const auto& item : golden_cases()) {
        if (item.expect_error) continue;
        CHECK_EQ(encode_frame(item.cmd, item.seq, item.payload), item.bytes);
    }
}

TEST(GoldenErrorVectorsAreRejected) {
    for (const auto& item : golden_cases()) {
        if (!item.expect_error) continue;

        FrameDecoder decoder;
        decoder.feed(item.bytes);
        Frame frame{};
        CHECK(decoder.next(frame) == DecodeResult::Error);
        CHECK(decoder.failed());
    }
}

TEST(GoldenFramesSurviveConcatenationAndOneByteFeeds) {
    std::string stream;
    int expected = 0;
    for (const auto& item : golden_cases()) {
        if (item.expect_error) continue;
        stream += item.bytes;
        ++expected;
    }
    if (expected == 0) {
        fail("没有可用的成功用例");
        return;
    }

    // 粘包：一次喂完
    FrameDecoder batched;
    batched.feed(stream);
    Frame frame{};
    int decoded = 0;
    while (batched.next(frame) == DecodeResult::Decoded) ++decoded;
    CHECK_EQ(decoded, expected);

    // 最极端的半包：一次一个字节
    FrameDecoder slow;
    int slow_decoded = 0;
    for (const char byte : stream) {
        slow.feed(&byte, 1);
        while (slow.next(frame) == DecodeResult::Decoded) ++slow_decoded;
    }
    CHECK_EQ(slow_decoded, expected);
}
