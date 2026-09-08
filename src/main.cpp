// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "ren " << REN_VERSION << '\n';
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--transcribe") {
        if (argc != 4) {
            std::cerr << "Usage: ren --transcribe MODEL.bin AUDIO.wav\n"
                         "Audio: 16 kHz mono WAV. English, local; writes text to stdout.\n";
            return 2;
        }
        try {
            ren::Engine engine;
            std::cout << engine.transcribe_file(argv[2], argv[3], 4) << '\n';
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "ren: " << e.what() << '\n';
            return 1;
        }
    }
    ren::App app;
    return app.run(argc, argv);
}
