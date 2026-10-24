#pragma once
#include "css.h"
namespace Husarz {
struct NodeHandle {
    uint32_t generation{};
    uint16_t id{},identity{};
};
struct ScriptLimits {
    uint32_t instructions{100000}, source_bytes{MaxScriptSource}, recursion{32}, timers{16};
};
class ScriptBudget {
    ScriptLimits limits_;
    uint32_t instructions_{}, depth_{};

  public:
    explicit ScriptBudget(ScriptLimits limits = {}) : limits_(limits) {}
    bool consume(uint32_t count = 1) {
        if (count > limits_.instructions - instructions_)
            return false;
        instructions_ += count;
        return true;
    }
    uint32_t used()const{return instructions_;}
    uint32_t remaining()const{return limits_.instructions-instructions_;}
    bool enter() {
        if (depth_ >= limits_.recursion)
            return false;
        ++depth_;
        return true;
    }
    void leave() {
        if (depth_)
            --depth_;
    }
};
// The runtime sees handles and bounded strings. It receives no BWS functions,
// native pointer values, GUI objects or hardware access capability.
class ScriptDom {
    Document &document_;

  public:
    explicit ScriptDom(Document &document) : document_(document) {}
    Document &document(){return document_;}
    bool valid(NodeHandle h) const {
        return h.generation == document_.generation && h.id > 0 && h.id < document_.node_count && (!h.identity || document_.nodes[h.id].identity==h.identity);
    }
    NodeHandle handle(uint16_t id)const {
        return id>0&&id<document_.node_count&&document_.nodes[id].identity?NodeHandle{document_.generation,id,document_.nodes[id].identity}:NodeHandle{};
    }
    NodeHandle by_identity(uint16_t identity)const {
        if(identity&&identity<MaxNodes){uint16_t id=document_.identity_index[identity];if(id&&id<document_.node_count&&document_.nodes[id].identity==identity)return handle(id);}
        return {};
    }
    NodeHandle get_element_by_id(const char *id) const {
        size_t n = length(id, MaxAttrLength + 1);
        if (n > MaxAttrLength)
            return {};
        for (uint16_t i = 1; i < document_.node_count; ++i)
            if (!document_.nodes[i].detached && equal(document_.attr(i, "id"), id))
                return {document_.generation, i, document_.nodes[i].identity};
        return {};
    }
    NodeHandle query_selector(const char *query) const {
        size_t n = length(query, 257);
        if (n > 256)
            return {};
        Selector s = selector({query, n});
        uint32_t budget = 65536;
        for (uint16_t i = 1; i < document_.node_count && budget; ++i)
            if (!document_.nodes[i].detached && matches(document_, i, s, budget))
                return {document_.generation, i, document_.nodes[i].identity};
        return {};
    }
    bool text_content(NodeHandle h, char *out, size_t cap) const {
        if (!valid(h) || !out || !cap)
            return false;
        size_t used = 0;
        out[0] = 0;
        unsigned end = h.id + 1;
        while (end < document_.node_count && document_.nodes[end].depth > document_.nodes[h.id].depth)
            ++end;
        for (unsigned i = h.id; i < end; ++i) {
            const auto &n = document_.nodes[i];
            if (!n.is_text && !n.raw)
                continue;
            const char *text = document_.str(n.text);
            for (size_t p = 0; p < n.text_length; ++p) {
                if (used + 1 == cap) {
                    out[used] = 0;
                    return false;
                }
                out[used++] = text[p];
            }
        }
        out[used] = 0;
        return true;
    }
    // The native JS bridge extends these read capabilities with validated
    // document-owned identities, transactional mutation and revision invalidation.
};
enum class ScriptResult { Unavailable, SourceLimit, BudgetExceeded, Complete, Error, Suspended };
class ScriptExecutor {
  public:
    virtual ~ScriptExecutor() = default;
    virtual ScriptResult execute(ScriptDom &, const char *, size_t, ScriptBudget &) = 0;
};
inline ScriptResult execute_script(Document &d, const Script &script, ScriptExecutor *executor,
                                   ScriptLimits limits = {}) {
    if (script.node >= d.node_count || script.source >= d.arena_used)
        return ScriptResult::Error;
    if (script.truncated || d.nodes[script.node].text_length > limits.source_bytes)
        return ScriptResult::SourceLimit;
    if (!executor || *d.str(script.url))
        return ScriptResult::Unavailable;
    ScriptDom dom(d);
    ScriptBudget budget(limits);
    return executor->execute(dom, d.str(script.source), d.nodes[script.node].text_length, budget);
}
} // namespace Husarz
