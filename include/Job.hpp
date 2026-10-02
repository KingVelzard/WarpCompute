#include <coroutine>
#include <cstdint>
#include <span>
#include <variant>

using PayloadVariant = std::variant<std::span<const float>, std::span<const double>, std::span<const int>>;
using ResultVariant  = std::variant<std::span<float>, std::span<double>, std::span<int>>;

// trivially copyable
struct Job {
    // all non-owning to heap frame memory
    PayloadVariant payload;
    PayloadVariant result;
    std::coroutine_handle<> waiter{};
    uint8_t reactor_id;
};

enum class OperationType : uint8_t {
    Saxpy  = 0,
    VecAdd = 1,
    Dot    = 2,
};

enum class DataType : uint8_t {
    F32 = 0,
    F64 = 1,
    I32 = 2,
};

struct ParsedJob {
    OperationType op;                   // operation
    DataType type;                      // data type
    uint32_t count;                     // element count
    std::span<const std::byte> payload; // payload
};
