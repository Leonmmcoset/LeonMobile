#include "calculator/model.hpp"
#include "editor/document.hpp"
#include "files/directory.hpp"
#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

int main() {
    using namespace leon::apps;
    Calculator calculator;
    for (const auto &key : {"7", "+", "3", "="})
        calculator.press(key);
    assert(calculator.display == "10");
    for (const auto &key : {"C", "9", "÷", "0", "="})
        calculator.press(key);
    assert(calculator.display == "Error");
    calculator.press("C");
    calculator.press(".");
    calculator.press(".");
    assert(calculator.display == "0.");

    char pattern[] = "/tmp/leon-models-XXXXXX";
    auto *temporary = mkdtemp(pattern);
    assert(temporary);
    Directory directory{temporary};
    directory.create("notes", true);
    directory.create("empty.txt", false);
    Document document{(directory.path / "notes/test.txt").string(), "你好，Leon\n"};
    document.save();
    document.text.clear();
    document.load();
    assert(document.text == "你好，Leon\n");
    assert(directory.entries().size() == 2);
    for (const auto &name : {"../escape", "", ".."}) {
        bool rejected = false;
        try {
            directory.create(name, false);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        assert(rejected);
    }
    std::filesystem::resize_file(document.path, 1024 * 1024 + 1);
    bool rejected = false;
    try {
        document.load();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);
    std::filesystem::remove_all(temporary);
}
