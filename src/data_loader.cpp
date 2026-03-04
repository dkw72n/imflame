#include "data_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_set>

using json = nlohmann::json;

// 字符串池，用于减少重复字符串的内存占用
class StringPool {
public:
    const std::string* get(const std::string& str) {
        auto it = pool_.find(str);
        if (it != pool_.end()) {
            return &(*it);
        }
        auto res = pool_.insert(str);
        return &(*res.first);
    }
private:
    std::unordered_set<std::string> pool_;
};

class FlameNodeSax : public nlohmann::json_sax<json> {
public:
    struct TempNode {
        const std::string* name = nullptr;
        std::vector<Sample> samples;
        std::vector<TempNode> children;
    };

    TempNode result;
    std::vector<TempNode> nodeStack;
    StringPool stringPool;
    
    enum class State {
        None,
        InNode,
        InSamplesArray,
        InSampleTuple,
        InChildrenArray
    };
    std::vector<State> stateStack;
    std::string currentKey;
    
    double currentSampleTime = 0.0;
    bool hasTime = false;

    bool null() override { return true; }
    bool boolean(bool /*val*/) override { return true; }
    
    bool number_integer(number_integer_t val) override { return handle_number(static_cast<double>(val)); }
    bool number_unsigned(number_unsigned_t val) override { return handle_number(static_cast<double>(val)); }
    bool number_float(number_float_t val, const string_t& /*s*/) override { return handle_number(static_cast<double>(val)); }
    
    bool handle_number(double val) {
        if (!stateStack.empty() && stateStack.back() == State::InSampleTuple) {
            if (!hasTime) {
                currentSampleTime = val;
                hasTime = true;
            } else {
                nodeStack.back().samples.push_back({currentSampleTime, val});
            }
        }
        return true;
    }

    bool string(string_t& val) override {
        if (!stateStack.empty() && stateStack.back() == State::InNode && currentKey == "name") {
            nodeStack.back().name = stringPool.get(val);
        }
        return true;
    }

    bool start_object(std::size_t /*elements*/) override {
        nodeStack.push_back(TempNode{});
        stateStack.push_back(State::InNode);
        currentKey = "";
        return true;
    }

    bool key(string_t& val) override {
        currentKey = val;
        return true;
    }

    bool end_object() override {
        TempNode completedNode = std::move(nodeStack.back());
        nodeStack.pop_back();
        stateStack.pop_back(); // pop InNode
        
        // §6.1 — 子节点按名称字母序排列
        std::sort(completedNode.children.begin(), completedNode.children.end(), [](const TempNode& a, const TempNode& b) {
            if (a.name && b.name) return *a.name < *b.name;
            return a.name != nullptr;
        });
        completedNode.samples.shrink_to_fit();
        completedNode.children.shrink_to_fit();

        if (nodeStack.empty()) {
            result = std::move(completedNode);
        } else {
            nodeStack.back().children.push_back(std::move(completedNode));
        }
        return true;
    }

    bool start_array(std::size_t elements) override {
        if (!stateStack.empty()) {
            State currentState = stateStack.back();
            if (currentState == State::InNode) {
                if (currentKey == "samples") {
                    stateStack.push_back(State::InSamplesArray);
                    if (elements != std::size_t(-1)) {
                        nodeStack.back().samples.reserve(elements);
                    }
                } else if (currentKey == "children") {
                    stateStack.push_back(State::InChildrenArray);
                    if (elements != std::size_t(-1)) {
                        nodeStack.back().children.reserve(elements);
                    }
                } else {
                    stateStack.push_back(State::None);
                }
            } else if (currentState == State::InSamplesArray) {
                stateStack.push_back(State::InSampleTuple);
                hasTime = false;
            } else {
                stateStack.push_back(State::None);
            }
        } else {
            stateStack.push_back(State::None);
        }
        return true;
    }

    bool end_array() override {
        stateStack.pop_back();
        return true;
    }

    bool binary(nlohmann::json::binary_t& /*val*/) override {
        return true;
    }

    bool parse_error(std::size_t position, const std::string& /*last_token*/, const nlohmann::json::exception& ex) override {
        throw std::runtime_error(std::string("JSON parse error at ") + std::to_string(position) + ": " + ex.what());
        return false;
    }
};

static FlameNode convertTempNode(FlameNodeSax::TempNode&& temp) {
    FlameNode node;
    node.name = temp.name;
    
    node.sample_count = static_cast<uint32_t>(temp.samples.size());
    if (node.sample_count > 0) {
        node.samples = std::make_unique<Sample[]>(node.sample_count);
        std::copy(temp.samples.begin(), temp.samples.end(), node.samples.get());
    }

    node.child_count = static_cast<uint32_t>(temp.children.size());
    if (node.child_count > 0) {
        node.children = std::make_unique<FlameNode[]>(node.child_count);
        for (uint32_t i = 0; i < node.child_count; ++i) {
            node.children[i] = convertTempNode(std::move(temp.children[i]));
        }
    }
    
    return node;
}

FlameNode loadFlameData(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    
    FlameNodeSax sax;
    bool success = json::sax_parse(ifs, &sax);
    if (!success) {
        throw std::runtime_error("SAX parsing failed for file: " + filepath);
    }
    
    return convertTempNode(std::move(sax.result));
}