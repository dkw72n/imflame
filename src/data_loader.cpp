#include "data_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_set>
#include <atomic>
#include <thread>
#include <chrono>

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

// 全局字符串池，生命周期覆盖整个程序运行期间
// FlameNode::name 指针指向此池中的字符串，必须保证池不被提前析构
static StringPool& getGlobalStringPool() {
    static StringPool pool;
    return pool;
}

// 带进度轮询的 SAX 解析器包装
// 在单独的线程中定期检查文件读取位置来更新进度
class ProgressPoller {
public:
    ProgressPoller(std::istream* is, ProgressCallback callback, std::streamsize totalSize)
        : is_(is), callback_(std::move(callback)), totalSize_(totalSize), running_(true) {
        thread_ = std::thread([this]() {
            while (running_) {
                if (is_ && callback_ && totalSize_ > 0) {
                    std::streampos pos = is_->tellg();
                    if (pos >= 0) {
                        double p = std::min(0.95, static_cast<double>(pos) / static_cast<double>(totalSize_));
                        callback_(p);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    ~ProgressPoller() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    std::istream* is_;
    ProgressCallback callback_;
    std::streamsize totalSize_;
    std::atomic<bool> running_;
    std::thread thread_;
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
    StringPool& stringPool = getGlobalStringPool();
    
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

FlameNode loadFlameData(const std::string& filepath, ProgressCallback progressCallback) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    // 获取文件大小
    ifs.seekg(0, std::ios::end);
    std::streamsize totalSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    // 启动进度轮询线程
    std::unique_ptr<ProgressPoller> poller;
    if (progressCallback) {
        poller = std::make_unique<ProgressPoller>(&ifs, progressCallback, totalSize);
    }

    FlameNodeSax sax;
    bool success = json::sax_parse(ifs, &sax);
    
    if (!success) {
        throw std::runtime_error("SAX parsing failed for file: " + filepath);
    }
    
    // 报告最终进度
    if (progressCallback) {
        progressCallback(1.0);
    }
    
    return convertTempNode(std::move(sax.result));
}
