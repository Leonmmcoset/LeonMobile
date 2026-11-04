#include <iostream>
#include <leon/system.hpp>
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: lmsys SERVICE 'JSON-request'\n";
        return 2;
    }
    try {
        std::cout << leon::system_request(argv[1], leon::Json::parse(argv[2])).dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
