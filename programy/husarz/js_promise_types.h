#pragma once
// Included inside Husarz::Js after Value and VM frames.
constexpr unsigned PromiseLimit=128, ReactionLimit=256, MicrotaskLimit=64, MicrotasksPerTurn=16, ContinuationLimit=8;
enum class PromiseState:uint8_t {Pending,Fulfilled,Rejected};
struct PromiseRecord {PromiseState state{};Value result{};uint16_t first{},last{},adopting{};bool resolving{};};
struct Reaction {Value fulfilled{},rejected{};uint16_t child{},next{};bool queued{};uint8_t mode{};};
struct Microtask {uint16_t reaction{},source{},continuation{};bool resume{};};
struct Continuation {
    Value stack[StackLimit]{};Frame frames[FrameLimit]{};Handler handlers[HandlerLimit]{};
    uint16_t sp{},frame_count{},handler_count{},scope{},ip{},promise{};Value receiver{},settled{};bool active{},rejected{};
};
