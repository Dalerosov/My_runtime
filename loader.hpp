#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>

class Loader {
private:
    std::string file_location;

public:
    Loader(std::string main_loc) : file_location(main_loc) {}
    Loader(Loader& l) : file_location(l.file_location) {}
    Loader(Loader&& l) noexcept : file_location(std::move(l.file_location)) {}

    std::shared_ptr<std::vector<std::string>> load() {
        std::ifstream file(file_location);
        std::cout << file_location + "\n";
        if (!file.is_open())
            throw std::runtime_error("failed to open: " + file_location);

        auto lines = std::make_shared<std::vector<std::string>>();
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == ';')
                continue;
            lines->push_back(line);
        }
        return lines;
    }
};