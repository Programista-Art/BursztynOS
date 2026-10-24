#pragma once
// Included after Winner/offer_declaration declarations in css.h.
namespace Husarz {
inline uint32_t custom_hash(Span name) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < name.size; ++i)
        hash = (hash ^ uint8_t(name.data[i])) * 16777619U;
    return hash;
}
inline bool custom_name(Span s) {
    if (s.size < 3 || s.size > MaxVariableName || s.data[0] != '-' || s.data[1] != '-')
        return false;
    for (size_t i = 2; i < s.size; ++i)
        if (!ident(s.data[i]))
            return false;
    return true;
}
inline void offer_custom(Document &d, uint16_t node, const Declaration &decl, int specificity,
                         uint64_t order) {
    Span name{d.str(decl.name), length(d.str(decl.name), MaxVariableName + 1)};
    if (!custom_name(name))
        return;
    uint32_t hash = custom_hash(name);
    unsigned count = 0;
    CustomProperty *slot = nullptr;
    for (uint16_t p = d.custom_heads[node]; p; p = d.custom[p - 1].next) {
        ++count;
        if (d.custom[p - 1].hash == hash && equal(d.str(d.custom[p - 1].name), d.str(decl.name))) {
            slot = &d.custom[p - 1];
            break;
        }
    }
    if (slot) {
        unsigned rank = decl.important ? 2 : 1, old = slot->important ? 2 : 1;
        if (rank < old || (rank == old && (specificity < slot->specificity ||
                                           (specificity == slot->specificity && order < slot->order))))
            return;
    } else {
        if (count >= MaxCustomPerElement || d.custom_count == MaxCustomProperties) {
            d.limit(LimitVariables);
            return;
        }
        slot = &d.custom[d.custom_count++];
        slot->next = d.custom_heads[node];
        d.custom_heads[node] = uint16_t(d.custom_count);
        slot->owner = node;
    }
    slot->hash = hash;
    slot->name = decl.name;
    slot->value = decl.value;
    slot->specificity = specificity;
    slot->order = order;
    slot->important = decl.important;
}
struct VariableResolver {
    const Document &d;
    unsigned budget{MaxSliceWork}, depth{};
    uint32_t path[MaxVariableDepth]{};
    bool cycle{};
    bool spend(size_t n) {
        if (n > budget) {
            budget = 0;
            return false;
        }
        budget -= unsigned(n);
        return true;
    }
    const CustomProperty *find(uint16_t node, Span name) {
        uint32_t hash = custom_hash(name);
        for (unsigned ancestors = 0; ancestors <= MaxDepth; ++ancestors) {
            for (uint16_t p = d.custom_heads[node]; p; p = d.custom[p - 1].next) {
                if (!spend(1))
                    return nullptr;
                auto &entry = d.custom[p - 1];
                if (entry.hash != hash)
                    continue;
                const char *key = d.str(entry.name);
                size_t n = length(key, MaxVariableName + 1);
                if (n == name.size) {
                    if (!spend(n))
                        return nullptr;
                    if (span_equal({key, n}, name)) {
                        Span value{d.str(entry.value), length(d.str(entry.value), MaxResolvedValue + 1)};
                        if (value_is(value, "inherit") || value_is(value, "unset"))
                            break;
                        return &entry;
                    }
                }
            }
            if (!node)
                break;
            node = d.nodes[node].parent;
        }
        return nullptr;
    }
    bool expand(uint16_t node, Span input, char *out, size_t cap, size_t &used) {
        if (depth >= MaxVariableDepth || input.size > MaxResolvedValue)
            return false;
        for (size_t p = 0; p < input.size;) {
            if (!spend(1))
                return false;
            // CSS strings are opaque, including literal "var(" text.
            if (input.data[p] == '\'' || input.data[p] == '"') {
                char q = input.data[p];
                size_t b = p++;
                while (p < input.size) {
                    if (!spend(1))
                        return false;
                    char c = input.data[p++];
                    if (c == '\\' && p < input.size)
                        ++p;
                    else if (c == q)
                        break;
                }
                if (p - b >= cap - used)
                    return false;
                for (size_t i = b; i < p; ++i)
                    out[used++] = input.data[i];
                continue;
            }
            if (p + 4 <= input.size && equal_ci(input.data + p, 4, "var(")) {
                size_t b = p + 4, end = b, comma = input.size;
                unsigned nesting = 1;
                char quote = 0;
                for (; end < input.size; ++end) {
                    if (!spend(1))
                        return false;
                    char c = input.data[end];
                    if (quote) {
                        if (c == '\\' && end + 1 < input.size)
                            ++end;
                        else if (c == quote)
                            quote = 0;
                    } else if (c == '\'' || c == '"')
                        quote = c;
                    else if (c == '(')
                        ++nesting;
                    else if (c == ')') {
                        if (!--nesting)
                            break;
                    } else if (c == ',' && nesting == 1 && comma == input.size)
                        comma = end;
                }
                if (end == input.size)
                    return false;
                Span name = trimmed(input.data + b, (comma < end ? comma : end) - b);
                if (!custom_name(name))
                    return false;
                auto *entry = find(node, name);
                size_t saved = used;
                bool ok = false;
                bool prior_cycle = cycle;
                if (entry) {
                    uint32_t key = uint32_t(entry - d.custom) + 1;
                    bool repeated = false;
                    for (unsigned i = 0; i < depth; ++i)
                        if (path[i] == key)
                            repeated = true;
                    if (repeated)
                        cycle = true;
                    else if (depth < MaxVariableDepth) {
                        path[depth++] = key;
                        Span value{d.str(entry->value), length(d.str(entry->value), MaxResolvedValue + 1)};
                        if (!value_is(value, "initial"))
                            ok = expand(entry->owner, value, out, cap, used);
                        --depth;
                    }
                }
                if (cycle != prior_cycle)
                    ok = false;
                if (!ok) {
                    used = saved;
                    if (comma >= end)
                        return false;
                    // A cycle invalidates its custom property graph. Only a
                    // consuming declaration's fallback may recover from it.
                    if (depth && cycle)
                        return false;
                    cycle = prior_cycle;
                    ++depth;
                    ok = expand(node, {input.data + comma + 1, end - comma - 1}, out, cap, used);
                    --depth;
                    if (!ok)
                        return false;
                }
                p = end + 1;
            } else {
                if (used + 1 >= cap)
                    return false;
                out[used++] = input.data[p++];
            }
        }
        out[used] = 0;
        return true;
    }
};
inline bool has_variable(Span v) {
    for (size_t i = 0; i + 4 <= v.size; ++i)
        if (equal_ci(v.data + i, 4, "var("))
            return true;
    return false;
}
// Use the remaining shared slice, charging exactly the bounded resolver work.
// A partial attempt that needs more room retries once in a fresh slice.
inline Work resolve_variable_work(Document &d, uint16_t node, Span input, uint32_t &offset, bool &valid) {
    valid = false;
    offset = 0;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        co_await WorkPoint{1};
        if (running_work->units == MaxSliceWork)
            co_await WorkPoint{1};
        unsigned available = MaxSliceWork - running_work->units;
        char expanded[MaxResolvedValue + 1]{};
        size_t used = 0;
        VariableResolver resolver{d};
        resolver.budget = available;
        bool ok = resolver.expand(node, input, expanded, sizeof(expanded), used);
        unsigned spent = available - resolver.budget;
        if (spent)
            co_await WorkPoint{spent};
        if (!ok && !resolver.budget && attempt == 0) {
            co_await WorkPoint{MaxSliceWork};
            co_await WorkPoint{1};
            continue;
        }
        if (ok) {
            offset = d.store_computed(expanded, used, MaxResolvedValue);
            valid = offset != 0;
        }
        co_return;
    }
}
} // namespace Husarz
