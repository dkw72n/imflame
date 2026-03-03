#include "data_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static FlameNode parseNode(const json& j) {
    FlameNode node;
    node.name = j.at("name").get<std::string>();

    for (const auto& s : j.at("samples")) {
        Sample sample;
        sample.time = s[0].get<double>();
        sample.value = s[1].get<double>();
        node.samples.push_back(sample);
    }

    for (const auto& child : j.at("children")) {
        node.children.push_back(parseNode(child));
    }

    return node;
}

FlameNode loadFlameData(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    json j = json::parse(ifs);
    return parseNode(j);
}
