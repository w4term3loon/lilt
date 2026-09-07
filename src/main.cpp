// SPDX-License-Identifier: GPL-3.0-or-later
#include "app.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "lilt " << LILT_VERSION << '\n';
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--transcribe") {
        if (argc < 4 || argc > 5) {
            std::cerr << "Usage: lilt --transcribe MODEL.bin AUDIO.wav [language]\n"
                         "Audio: 16 kHz mono PCM WAV. Runs locally; writes text to stdout.\n";
            return 2;
        }
        try {
            std::cout << lilt::Engine::transcribe_file(argv[2], argv[3], argc == 5 ? argv[4] : "auto", 4) << '\n';
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "lilt: " << e.what() << '\n';
            return 1;
        }
    }
    lilt::App app;
    return app.run(argc, argv);
}
