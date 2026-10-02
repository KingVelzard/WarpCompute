// linter_test.cpp
// this file is INTENTIONALLY bad - it exists to test your linter/clangd/clang-tidy setup
// every comment explains what check should fire
// compile with: g++ -std=c++23 -Wall -Wextra linter_test.cpp (don't, just open it)

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// -------------------------------------------------------
// modernize-use-using
// should suggest: using MyInt = int;
using std::sort;

using MyInt   = int;
using FuncPtr = void (*)(int, int);

// -------------------------------------------------------
// modernize-avoid-c-arrays
// should suggest: std::array<int, 5>
constexpr int size        = 5;
std::array<int, size> arr = {1, 2, 4};

// -------------------------------------------------------
// readability-magic-numbers
// should flag: 42, 3.14, 1920, 1080
int magic(int x) {
    return x * 42 + 7;
}

float area(float r) {
    return 3.14f * r * r;
}

void resolution() {
    int width  = 1'920;
    int height = 1'080;
    (void)width;
    (void)height;
}

// -------------------------------------------------------
// modernize-use-nullptr
// should suggest: nullptr instead of NULL / 0
void null_test() {
    int* p = nullptr;
    int* q = nullptr;
    (void)p;
    (void)q;
}

// -------------------------------------------------------
// performance-avoid-endl
// should suggest: '\n' instead of std::endl
void endl_test() {
    std::cout << "hello" << '\n';
    std::cout << "world" << '\n';
}

// -------------------------------------------------------
// modernize-loop-convert
// should suggest: range-for
void loop_test(std::vector<int>& v) {
    for (int i : v) {
        std::cout << i << '\n';
    }
}

// -------------------------------------------------------
// modernize-use-auto
// should suggest: auto it = ...
void auto_test(std::vector<int>& v) {
    auto it = v.begin();
    (void)it;
}

// -------------------------------------------------------
// performance-unnecessary-copy-initialization
// should suggest taking by const ref or using auto&
void copy_test(const std::vector<int>& v) {
    const std::vector<int>& copy = v; // unnecessary copy if only reading
    (void)copy;
}

// -------------------------------------------------------
// modernize-pass-by-value + use-emplace
// should suggest: pass std::string by value and move, use emplace_back
void push_test(std::vector<std::string>& v, const std::string& s) {
    v.push_back(s);          // should suggest emplace_back
    v.emplace_back("hello"); // should suggest emplace_back("hello")
}

// -------------------------------------------------------
// modernize-use-default-member-init
// should suggest moving defaults into the struct
struct BadDefaults {
    int x{};
    float y{};
    bool flag{};

    BadDefaults() = default;
};

// -------------------------------------------------------
// modernize-use-equals-default / equals-delete
// should suggest = default and = delete
struct BadSpecials {
    BadSpecials()                              = default; // = default
    BadSpecials(BadSpecials&&)                 = delete;
    BadSpecials& operator=(const BadSpecials&) = default;
    BadSpecials& operator=(BadSpecials&&)      = delete;
    BadSpecials(const BadSpecials& other) { // = default
        (void)other;
    }
};

// -------------------------------------------------------
// cppcoreguidelines-special-member-functions (rule of 5)
// has destructor but missing move ctor/assign
struct RuleOfFive {
    int* data;
    RuleOfFive() : data(new int(0)) {}
    RuleOfFive(RuleOfFive&&)                 = delete;
    RuleOfFive& operator=(const RuleOfFive&) = default;
    RuleOfFive& operator=(RuleOfFive&&)      = delete;
    ~RuleOfFive() { delete data; }                             // destructor defined
    RuleOfFive(const RuleOfFive& o) : data(new int(*o.data)) { // copy ctor defined
    }
    // missing: move ctor, copy assign, move assign
};

// -------------------------------------------------------
// modernize-use-nodiscard
// return value is important but not marked [[nodiscard]]
[[nodiscard]] bool important_check(int x) {
    return x > 0;
}

void nodiscard_test() {
    important_check(42); // result ignored, should warn
}

// -------------------------------------------------------
// readability-container-size-empty
// should suggest: .empty() instead of .size() == 0
void empty_test(const std::vector<int>& v) {
    if (v.empty()) {
        std::cout << "empty\n";
    }
}

// -------------------------------------------------------
// modernize-use-transparent-functors
// should suggest: std::less<> over std::less<int>
void sort_test(std::vector<int>& v) {
    std::ranges::sort(v, std::less<>());
}

// -------------------------------------------------------
// readability-simplify-boolean-expr
// should simplify the return
bool is_positive(int x) {
    return x > 0;
}

// -------------------------------------------------------
// modernize-shrink-to-fit
// should suggest: v.shrink_to_fit()
void shrink_test(std::vector<int>& v) {
    v.shrink_to_fit(); // old swap trick
}

void use_after_move_test() {
    std::vector<int> v = {1, 2, 3};
    std::vector<int> w = std::move(v);
    v.push_back(4); // fires — using moved-from container
    int x = v[0];   // fires — indexing moved-from container
    (void)w;
    (void)x;
}

void use_after_move_ptr() {
    auto p = std::make_unique<int>(42);
    auto q = std::move(p);
    *p     = 5; // fires — dereferencing moved-from unique_ptr, this IS UB
    (void)q;
}

// -------------------------------------------------------
// bugprone-unchecked-optional-access
void optional_test() {
    std::optional<int> opt;
    int x = *opt; // unchecked dereference
    (void)x;
}

// -------------------------------------------------------
// readability-qualified-auto
// should suggest: auto* p
void qualified_auto_test(std::vector<int*>& v) {
    for (auto p : v) { // should be auto*
        (void)p;
    }
}

// -------------------------------------------------------
// modernize-avoid-bind
// should suggest a lambda instead
void bind_test() {

    auto f = []() { return 1 + 2; };
    (void)f;
}

// -------------------------------------------------------
// cppcoreguidelines-prefer-member-initializer
// x should be initialized in the initializer list, not the body
struct BadInit {
    int x;
    explicit BadInit(int val) {
        x = val; // should be : x(val) in initializer list
    }
};

struct Checker {
    bool important_check(int x) { return x > 0; } // will fire here
};

// -------------------------------------------------------
// readability-make-member-function-const
// get() doesn't modify anything, should be const
struct BadConst {
    int value = 42;
    int get() { return value; } // should be const
};

// -------------------------------------------------------
// modernize-use-ranges (C++20/23)
// should suggest std::ranges::sort
void ranges_test(std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    auto it = std::find(v.begin(), v.end(), 42);
    (void)it;
}

// -------------------------------------------------------
// misc-const-correctness
// these could all be const
void const_test() {
    int x         = 5;       // never modified, should be const
    std::string s = "hello"; // never modified, should be const
    (void)x;
    (void)s;
}

int main() {
    std::cout << "open this file in your editor and watch the squiggles\n";
    return 0;
}
