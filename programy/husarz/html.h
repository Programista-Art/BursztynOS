#pragma once
#include "document.h"
#include "work.h"
namespace Husarz {
inline Work store_string_work(Document &d,const char *source,size_t size,size_t limit,uint32_t &offset){
    offset=0;if(!source)co_return;
    if(size>limit){size=limit;d.limit(limit==MaxStylesheetBytes?LimitCssBytes:LimitText);}
    if(size+1>ArenaSize-d.arena_used){d.limit(LimitArena);co_return;}
    offset=d.arena_used;d.arena_used+=uint32_t(size+1);
    for(size_t i=0;i<size;++i){if(!(i&63))co_await WorkPoint{1};d.arena[offset+i]=source[i];}
    d.arena[offset+size]=0;
}
inline bool entity(const char *s, size_t n, uint32_t &cp, size_t &used) {
    if (n < 3 || *s != '&')
        return false;
    size_t end = 1;
    while (end < n && end < 32 && s[end] != ';' && !space(s[end]) && s[end] != '<')
        ++end;
    if (end >= n || s[end] != ';')
        return false;
    const char *p = s + 1;
    size_t len = end - 1;
    struct Named {
        const char *name;
        uint32_t cp;
    };
    static constexpr Named names[] = {{"amp", 38},     {"lt", 60},      {"gt", 62},       {"quot", 34},
                                      {"apos", 39},    {"nbsp", 160},   {"copy", 169},    {"reg", 174},
                                      {"mdash", 8212}, {"ndash", 8211}, {"hellip", 8230}, {"bull", 8226}};
    if (*p == '#' || (*p >= '0' && *p <= '9') || *p == 'x' || *p == 'X') {
        if (*p == '#') {
            ++p;
            --len;
        }
        unsigned base = 10;
        if (len && (*p == 'x' || *p == 'X')) {
            base = 16;
            ++p;
            --len;
        }
        if (!len)
            return false;
        uint32_t v = 0;
        for (size_t i = 0; i < len; ++i) {
            char c = lower(p[i]);
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0 || unsigned(d) >= base)
                return false;
            if (v > (0x10ffff - unsigned(d)) / base) {
                v = 0x110000;
                break;
            }
            v = v * base + unsigned(d);
        }
        cp = !v || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff) ? 0xfffd : v;
        used = end + 1;
        return true;
    }
    for (const auto &v : names)
        if (equal_ci(p, len, v.name)) {
            cp = v.cp;
            used = end + 1;
            return true;
        }
    return false;
}
inline Work decoded_work(Document &d, const char *s, size_t n, size_t limit, bool entities,
                        size_t *consumed, uint32_t &result) {
    uint32_t start = d.arena_used;
    size_t bytes = 0, processed = 0;
    for (size_t i = 0; i < n;) { co_await WorkPoint{1};
        uint32_t cp;
        size_t used = 1;
        if (entities && entity(s + i, n - i, cp, used)) {
        } else
            cp = utf8(s + i, n - i, used);
        char b[4];
        unsigned k = encode(cp, b);
        if (bytes + k > limit || d.arena_used + k + 1 > ArenaSize) {
            d.limit(d.arena_used+k+1>ArenaSize?LimitArena:LimitText);
            break;
        }
        for (unsigned j = 0; j < k; ++j)
            { co_await WorkPoint{1}; d.arena[d.arena_used++] = b[j]; }
        bytes += k;
        i += used;
        processed = i;
    }
    if (consumed)
        *consumed = processed;
    if (d.arena_used >= ArenaSize) {
        d.truncated = true;
        {result=0;co_return;}
    }
    d.arena[d.arena_used++] = 0;
    {result=start;co_return;}
}
inline uint32_t decoded(Document &d,const char *s,size_t n,size_t limit=MaxText,bool entities=true,size_t *consumed=nullptr) { uint32_t result{};finish_work(decoded_work(d,s,n,limit,entities,consumed,result));return result; }

struct Span {
    const char *data{};
    size_t size{};
};
struct HtmlToken {
    enum Kind { End, Text, Open, Close, Ignore } kind{End};
    Span name{}, text{};
    struct Attr {
        Span name, value;
    } attrs[MaxAttrsPerNode];
    unsigned count{};
    bool self_close{}, overflow{};
};
class HtmlTokenizer {
  public:
    const char *source;
    size_t size, pos{};
    HtmlTokenizer(const char *s, size_t n) : source(s), size(n) {}
    bool starts(size_t p, const char *s) const {
        size_t n = length(s, 64);
        return p + n <= size && equal_ci(source + p, n, s);
    }
    Work next_work(HtmlToken &result) {
        HtmlToken t;
        if (pos >= size)
            {result=t;co_return;}
        if (source[pos] != '<') {
            size_t start = pos;
            while (pos < size && source[pos] != '<' && pos - start < MaxText / 3) { co_await WorkPoint{1};
                size_t used;
                utf8(source + pos, size - pos, used);
                pos += used;
            }
            t.kind = HtmlToken::Text;
            t.text = {source + start, pos - start};
            {result=t;co_return;}
        }
        if (starts(pos, "<!--")) {
            pos += 4;
            while (pos < size && !starts(pos, "-->"))
                { co_await WorkPoint{1}; ++pos; }
            if (pos < size)
                pos += 3;
            t.kind = HtmlToken::Ignore;
            {result=t;co_return;}
        }
        if (pos + 1 < size && (source[pos + 1] == '!' || source[pos + 1] == '?')) {
            pos += 2;
            char quote = 0;
            while (pos < size) { co_await WorkPoint{1};
                char c = source[pos++];
                if (quote) {
                    if (c == quote)
                        quote = 0;
                } else if (c == '\'' || c == '"')
                    quote = c;
                else if (c == '>')
                    break;
            }
            t.kind = HtmlToken::Ignore;
            {result=t;co_return;}
        }
        size_t begin = pos++;
        bool close = pos < size && source[pos] == '/';
        if (close)
            ++pos;
        while (pos < size && space(source[pos]))
            { co_await WorkPoint{1}; ++pos; }
        size_t ns = pos;
        while (pos < size && ident(source[pos]))
            { co_await WorkPoint{1}; ++pos; }
        if (pos == ns) {
            t.kind = HtmlToken::Text;
            t.text = {source + begin, 1};
            pos = begin + 1;
            {result=t;co_return;}
        }
        t.name = {source + ns, pos - ns};
        t.kind = close ? HtmlToken::Close : HtmlToken::Open;
        while (pos < size) { co_await WorkPoint{1};
            while (pos < size && space(source[pos]))
                { co_await WorkPoint{1}; ++pos; }
            if (pos == size)
                break;
            if (source[pos] == '>') {
                ++pos;
                break;
            }
            if (source[pos] == '/' && pos + 1 < size && source[pos + 1] == '>') {
                t.self_close = true;
                pos += 2;
                break;
            }
            size_t as = pos;
            while (pos < size && !space(source[pos]) && source[pos] != '=' && source[pos] != '>' &&
                   source[pos] != '/' && source[pos] != '<' && source[pos] != '\'' && source[pos] != '"')
                { co_await WorkPoint{1}; ++pos; }
            if (pos == as) {
                if (source[pos] == '<')
                    break;
                ++pos;
                continue;
            }
            Span name{source + as, pos - as};
            while (pos < size && space(source[pos]))
                { co_await WorkPoint{1}; ++pos; }
            Span value{};
            if (pos < size && source[pos] == '=') {
                ++pos;
                while (pos < size && space(source[pos]))
                    { co_await WorkPoint{1}; ++pos; }
                char q = 0;
                if (pos < size && (source[pos] == '\'' || source[pos] == '"'))
                    q = source[pos++];
                size_t vs = pos;
                if (q) {
                    while (pos < size && source[pos] != q)
                        { co_await WorkPoint{1}; ++pos; }
                    value = {source + vs, pos - vs};
                    if (pos < size)
                        ++pos;
                } else {
                    while (pos < size && !space(source[pos]) && source[pos] != '>' && source[pos] != '<')
                        { co_await WorkPoint{1}; ++pos; }
                    value = {source + vs, pos - vs};
                }
            }
            if (t.count < MaxAttrsPerNode)
                t.attrs[t.count++] = {name, value};
            else
                t.overflow = true;
        }
        {result=t;co_return;}
    }
    inline Work raw_work(const char *tag, Span &result) {
        size_t start = pos;
        while (pos < size) { co_await WorkPoint{1};
            if (source[pos] == '<' && pos + 2 < size && source[pos + 1] == '/') {
                size_t p = pos + 2, n = length(tag, 64);
                if (p + n <= size && equal_ci(source + p, n, tag) &&
                    (p + n == size || space(source[p + n]) || source[p + n] == '>')) {
                    Span out{source + start, pos - start};
                    while (pos < size && source[pos] != '>')
                        { co_await WorkPoint{1}; ++pos; }
                    if (pos < size)
                        ++pos;
                    {result=out;co_return;}
                }
            }
            ++pos;
        }
        {result={source + start, pos - start};co_return;}
    }
inline Span raw(const char *tag) { Span result{};finish_work(raw_work(tag,result));return result; }

};
inline bool void_tag(const char *s) {
    return equal(s, "br") || equal(s, "hr") || equal(s, "img") || equal(s, "meta") || equal(s, "input") ||
           equal(s, "link") || equal(s, "base") || equal(s, "wbr") || equal(s, "area") ||
           equal(s, "source") || equal(s, "embed") || equal(s, "col");
}
inline bool block_tag(const char *s) {
    return equal(s, "div") || equal(s, "p") || equal(s, "section") || equal(s, "article") ||
           equal(s, "header") || equal(s, "footer") || equal(s, "nav") || equal(s, "main") ||
           equal(s, "ul") || equal(s, "ol") || equal(s, "table") || equal(s, "blockquote") ||
           equal(s, "pre") || (s[0] == 'h' && s[1] >= '1' && s[1] <= '6' && !s[2]);
}
inline Work parse_html_work(Document &d, const char *source, size_t size, bool html = true) {
    HUSARZ_SCOPE(d, Html);
    d.reset();
    ++d.parse_count;
    if (!source)
        size = 0;
    if (size > MaxSource) {
        size = MaxSource;
        d.truncated = true;
    }
    if (!html) {
        uint16_t pre = d.add(0);
        d.nodes[pre].tag = d.store("pre", 3);
        for (size_t p = 0; p < size;) { co_await WorkPoint{1};
            size_t n = size - p;
            if (n > MaxText / 3) {
                n = MaxText / 3;
                while (n && (uint8_t(source[p + n]) & 0xc0) == 0x80)
                    { co_await WorkPoint{1}; --n; }
                if (!n)
                    n = 1;
            }
            auto id = d.add(pre, true);
            if (!id)
                break;
            co_await decoded_work(d,source+p,n,MaxText,false,nullptr,d.nodes[id].text);
            d.nodes[id].text_length = uint32_t(length(d.str(d.nodes[id].text), MaxText));
            p += n;
        }
        co_return;
    }
    HtmlTokenizer tok(source, size);
    uint16_t stack[MaxDepth + 1]{};
    uint16_t html_id = d.add(0), head_id = 0, body_id = 0;
    d.nodes[html_id].tag = d.store("html", 4);
    unsigned depth = 1, overflow = 0;
    stack[1] = html_id;
    auto ensure_body = [&]() {
        if (!body_id) {
            body_id = d.add(html_id);
            if (body_id)
                d.nodes[body_id].tag = d.store("body", 4);
        }
        if (body_id) {
            depth = 2;
            stack[2] = body_id;
        }
    };
    while (tok.pos < size) { HUSARZ_PROGRESS(Html,tok.pos,size);co_await WorkPoint{1};
        HtmlToken t;co_await tok.next_work(t);
        if (t.overflow)
            d.truncated = true;
        if (t.kind == HtmlToken::Ignore)
            continue;
        if (t.kind == HtmlToken::End)
            break;
        if (t.kind == HtmlToken::Text) {
            if (depth == 1 || (depth == 2 && stack[2] == head_id)) {
                bool blank = true;
                for (size_t j = 0; j < t.text.size; ++j)
                    { co_await WorkPoint{1}; if (!space(t.text.data[j]))
                        blank = false; }
                if (blank)
                    continue;
                ensure_body();
            }
            if (depth >= MaxDepth)
                d.truncated = true;
            auto id = d.add(stack[depth < MaxDepth ? depth : MaxDepth - 1], true);
            if (!id)
                break;
            co_await decoded_work(d,t.text.data,t.text.size,MaxText,true,nullptr,d.nodes[id].text);
            d.nodes[id].text_length = uint32_t(length(d.str(d.nodes[id].text), MaxText));
            continue;
        }
        if (t.name.size > 64) {
            d.truncated = true;
            continue;
        }
        char name[65];
        for (size_t i = 0; i < t.name.size; ++i)
            { co_await WorkPoint{1}; name[i] = lower(t.name.data[i]); }
        name[t.name.size] = 0;
        if (t.kind == HtmlToken::Close) {
            if (equal(name, "html")) {
                depth = 1;
                overflow = 0;
                continue;
            }
            if (overflow) {
                --overflow;
                continue;
            }
            for (unsigned k = depth; k > 0; --k)
                { co_await WorkPoint{1}; if (d.tag(stack[k], name)) {
                    depth = k - 1;
                    break;
                } }
            continue;
        }
        bool wrapper = false;
        uint16_t reuse = 0;
        bool metadata = equal(name, "style") || equal(name, "title") || equal(name, "meta") ||
                        equal(name, "link") || equal(name, "base") || equal(name, "script");
        if (equal(name, "html")) {
            depth = 1;
            reuse = html_id;
            wrapper = true;
        } else if (equal(name, "head") && !body_id) {
            if (!head_id) {
                head_id = d.add(html_id);
                if (head_id)
                    d.nodes[head_id].tag = d.store("head", 4);
            }
            depth = 2;
            stack[2] = head_id;
            reuse = head_id;
            wrapper = true;
        } else if (equal(name, "body")) {
            ensure_body();
            reuse = body_id;
            wrapper = true;
        } else if (!body_id && metadata && (depth == 1 || (depth == 2 && stack[2] == head_id))) {
            if (!head_id) {
                head_id = d.add(html_id);
                if (head_id)
                    d.nodes[head_id].tag = d.store("head", 4);
            }
            depth = 2;
            stack[2] = head_id;
        } else if (depth == 1 || (depth == 2 && stack[2] == head_id && !metadata))
            ensure_body();
        // A small set of common optional end tags. Recovery stays bounded.
        if (!overflow && depth) {
            const char *current = d.str(d.nodes[stack[depth]].tag);
            if ((equal(current, "p") && block_tag(name)) || (equal(current, "li") && equal(name, "li")) ||
                (equal(current, "option") && equal(name, "option")) ||
                ((equal(current, "td") || equal(current, "th")) &&
                 (equal(name, "td") || equal(name, "th") || equal(name, "tr"))))
                --depth;
            if (equal(name, "tr") && d.tag(stack[depth], "tr"))
                --depth;
        }
        if (depth == MaxDepth || overflow) {
            d.truncated = true;
            if (!void_tag(name) && !t.self_close)
                ++overflow;
            continue;
        }
        auto id = reuse ? reuse : d.add(stack[depth]);
        if (!id)
            break;
        Node &n = d.nodes[id];
        n.tag = d.store(name, t.name.size, 64);
        if (!n.attr_count)
            n.attr_start = uint16_t(d.attr_count);
        const unsigned count = n.attr_count && wrapper ? 0 : t.count;
        for (unsigned i = 0; i < count; ++i) { co_await WorkPoint{1};
            if (d.attr_count == MaxAttrs) {
                d.truncated = true;
                break;
            }
            const auto &a = t.attrs[i];
            bool duplicate = false;
            for (unsigned j = 0; j < n.attr_count; ++j)
                { co_await WorkPoint{1}; if (equal_ci(a.name.data, a.name.size, d.str(d.attrs[n.attr_start + j].name)))
                    duplicate = true; }
            if (duplicate)
                continue;
            Attribute at;
            at.name = d.store(a.name.data, a.name.size, 64, true);
            co_await decoded_work(d,a.value.data,a.value.size,MaxAttrLength,true,nullptr,at.value);
            d.attrs[d.attr_count++] = at;
            ++n.attr_count;
        }
        if (equal(name, "base") && !d.base_url)
            d.base_url = d.store(d.attr(id, "href"), length(d.attr(id, "href"), MaxAttrLength));
        if (equal(name, "script") || equal(name, "style") || equal(name, "title") ||
            equal(name, "textarea")) {
            Span raw;co_await tok.raw_work(name,raw);
            n.raw = true;
            size_t cap = equal(name, "script") ? MaxScriptSource : equal(name, "style") ? MaxStylesheetBytes : MaxText;
            size_t raw_consumed = 0;
            co_await decoded_work(d,raw.data,raw.size,cap,!equal(name,"script") && !equal(name,"style"),&raw_consumed,n.text);
            n.text_length = uint32_t(length(d.str(n.text), cap));
            if (equal(name, "title"))
                d.title = n.text;
            if (equal(name, "script")) {
                if (d.script_count < MaxScripts) {
                    auto &s = d.scripts[d.script_count++];
                    s = {id, n.text, d.store(d.attr(id, "src"), length(d.attr(id, "src"), MaxAttrLength)),
                         d.store(d.attr(id, "type"), length(d.attr(id, "type"), MaxAttrLength)),
                         raw.size > MaxScriptSource || raw_consumed < raw.size};
                } else
                    d.truncated = true;
            }
            continue;
        }
        if (!wrapper && !void_tag(name) && !t.self_close)
            stack[++depth] = id;
    }
}
inline void parse_html(Document &d,const char *source,size_t size,bool html=true) { finish_work(parse_html_work(d,source,size,html)); }

} // namespace Husarz
