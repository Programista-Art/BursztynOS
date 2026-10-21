#pragma once
namespace Husarz {
// A real, low-priority stylesheet. These rules participate in the bounded CSS
// cascade with a distinct UA origin, below every applicable author declaration.
// Semantic display/fallback control behavior lives in defaults(); no UI palette
// or desktop preference is consulted by either layer.
inline constexpr char UserAgentStyleSheet[] =
    "html{display:block;font-size:16px}"
    "body{display:block;margin:8px}"
    "p{display:block;margin:1em 0}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}"
    "h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}"
    "h4{font-size:1em;margin:1.33em 0;font-weight:bold}"
    "h5{font-size:.83em;margin:1.67em 0;font-weight:bold}"
    "h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "ul,ol{display:block;padding-left:40px;margin:1em 0}"
    "table{margin:0;border-spacing:2px;border-collapse:separate}"
    "td,th{padding:1px}th{text-align:center;font-weight:bold}"
    "input,textarea,select{color:#000;background-color:#fff;border:1px solid #767676}"
    "button{color:#000;background-color:#efefef;border:1px solid #767676}";
}
