#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <iostream>
#include <sstream>

namespace ReKenshi {
namespace Testing {

/**
 * Simple test assertion macros
 */
#define TEST_ASSERT(condition) \
    if (!(condition)) { \
        std::ostringstream msg; \
        msg << "ASSERTION FAILED: " << #condition << " at " << __FILE__ << ":" << __LINE__; \
        throw TestFailureException(msg.str()); \
    }

#define TEST_ASSERT_EQUAL(expected, actual) \
    if ((expected) != (actual)) { \
        std::ostringstream msg; \
        msg << "ASSERTION FAILED: Expected " << (expected) << " but got " << (actual) \
            << " at " << __FILE__ << ":" << __LINE__; \
        throw TestFailureException(msg.str()); \
    }

#define TEST_ASSERT_NOT_EQUAL(expected, actual) \
    if ((expected) == (actual)) { \
        std::ostringstream msg; \
        msg << "ASSERTION FAILED: Expected not equal to " << (expected) \
            << " at " << __FILE__ << ":" << __LINE__; \
        throw TestFailureException(msg.str()); \
    }

#define TEST_ASSERT_NULL(ptr) \
    if ((ptr) != nullptr) { \
        std::ostringstream msg; \
        msg << "ASSERTION FAILED: Expected null pointer at " << __FILE__ << ":" << __LINE__; \
        throw TestFailureException(msg.str()); \
    }

#define TEST_ASSERT_NOT_NULL(ptr) \
    if ((ptr) == nullptr) { \
        std::ostringstream msg; \
        msg << "ASSERTION FAILED: Expected non-null pointer at " << __FILE__ << ":" << __LINE__; \
        throw TestFailureException(msg.str()); \
    }

#define TEST_ASSERT_THROWS(expression, exception_type) \
    { \
        bool threw = false; \
        try { \
            expression; \
        } catch (const exception_type&) { \
            threw = true; \
        } \
        if (!threw) { \
            std::ostringstream msg; \
            msg << "ASSERTION FAILED: Expected exception " << #exception_type \
                << " at " << __FILE__ << ":" << __LINE__; \
            throw TestFailureException(msg.str()); \
        } \
    }

/**
 * Test failure exception
 */
class TestFailureException : public std::exception {
public:
    explicit TestFailureException(const std::string& message)
        : m_message(message) {}

    const char* what() const noexcept override {
        return m_message.c_str();
    }

private:
    std::string m_message;
};

/**
 * Test case structure
 */
struct TestCase {
    std::string name;
    std::function<void()> testFunc;
    bool enabled = true;
};

/**
 * Test result
 */
struct TestResult {
    std::string testName;
    bool passed = false;
    std::string errorMessage;
    double executionTimeMs = 0.0;
};

/**
 * Test suite
 */
class TestSuite {
public:
    explicit TestSuite(const std::string& name)
        : m_name(name) {}

    void AddTest(const std::string& name, std::function<void()> testFunc) {
        TestCase testCase;
        testCase.name = name;
        testCase.testFunc = testFunc;
        m_tests.push_back(testCase);
    }

    void RunAll() {
        m_results.clear();

        std::cout << "========================================\n";
        std::cout << "Running Test Suite: " << m_name << "\n";
        std::cout << "========================================\n\n";

        int passed = 0;
        int failed = 0;

        for (const auto& test : m_tests) {
            if (!test.enabled) {
                std::cout << "[ SKIP ] " << test.name << "\n";
                continue;
            }

            std::cout << "[ RUN  ] " << test.name << "\n";

            TestResult result;
            result.testName = test.name;

            auto start = std::chrono::high_resolution_clock::now();

            try {
                test.testFunc();
                result.passed = true;
                passed++;
                std::cout << "[ PASS ] " << test.name << "\n";
            } catch (const TestFailureException& e) {
                result.passed = false;
                result.errorMessage = e.what();
                failed++;
                std::cout << "[ FAIL ] " << test.name << "\n";
                std::cout << "         " << e.what() << "\n";
            } catch (const std::exception& e) {
                result.passed = false;
                result.errorMessage = std::string("Unexpected exception: ") + e.what();
                failed++;
                std::cout << "[ FAIL ] " << test.name << "\n";
                std::cout << "         Unexpected exception: " << e.what() << "\n";
            } catch (...) {
                result.passed = false;
                result.errorMessage = "Unknown exception";
                failed++;
                std::cout << "[ FAIL ] " << test.name << "\n";
                std::cout << "         Unknown exception\n";
            }

            auto end = std::chrono::high_resolution_clock::now();
            result.executionTimeMs = std::chrono::duration<double, std::milli>(end - start).count();

            m_results.push_back(result);
            std::cout << "\n";
        }

        std::cout << "========================================\n";
        std::cout << "Test Suite Summary: " << m_name << "\n";
        std::cout << "========================================\n";
        std::cout << "Total: " << (passed + failed) << "\n";
        std::cout << "Passed: " << passed << "\n";
        std::cout << "Failed: " << failed << "\n";
        std::cout << "========================================\n\n";
    }

    const std::vector<TestResult>& GetResults() const {
        return m_results;
    }

    void PrintReport() const {
        std::cout << "========================================\n";
        std::cout << "Detailed Test Report: " << m_name << "\n";
        std::cout << "========================================\n\n";

        for (const auto& result : m_results) {
            std::cout << (result.passed ? "[✓]" : "[✗]") << " " << result.testName << "\n";
            std::cout << "    Time: " << result.executionTimeMs << " ms\n";

            if (!result.passed) {
                std::cout << "    Error: " << result.errorMessage << "\n";
            }

            std::cout << "\n";
        }
    }

private:
    std::string m_name;
    std::vector<TestCase> m_tests;
    std::vector<TestResult> m_results;
};

/**
 * Mock IPC Client for testing
 */
class MockIPCClient {
public:
    void Connect() { m_connected = true; }
    void Disconnect() { m_connected = false; }
    bool IsConnected() const { return m_connected; }

    void SendMessage(const std::string& message) {
        m_sentMessages.push_back(message);
    }

    void SimulateReceive(const std::string& message) {
        m_receivedMessages.push_back(message);
        if (m_messageCallback) {
            m_messageCallback(message);
        }
    }

    void SetMessageCallback(std::function<void(const std::string&)> callback) {
        m_messageCallback = callback;
    }

    const std::vector<std::string>& GetSentMessages() const {
        return m_sentMessages;
    }

    void ClearMessages() {
        m_sentMessages.clear();
        m_receivedMessages.clear();
    }

private:
    bool m_connected = false;
    std::vector<std::string> m_sentMessages;
    std::vector<std::string> m_receivedMessages;
    std::function<void(const std::string&)> m_messageCallback;
};

/**
 * Memory testing utilities
 */
class MemoryTestUtilities {
public:
    // Allocate test memory block
    static uintptr_t AllocateTestMemory(size_t size) {
        void* mem = malloc(size);
        if (mem) {
            std::memset(mem, 0, size);
            s_allocatedBlocks.push_back(mem);
        }
        return reinterpret_cast<uintptr_t>(mem);
    }

    // Write test pattern to memory
    static void WriteTestPattern(uintptr_t address, const std::vector<uint8_t>& pattern) {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(address);
        for (size_t i = 0; i < pattern.size(); i++) {
            ptr[i] = pattern[i];
        }
    }

    // Verify test pattern
    static bool VerifyTestPattern(uintptr_t address, const std::vector<uint8_t>& pattern) {
        uint8_t* ptr = reinterpret_cast<uint8_t*>(address);
        for (size_t i = 0; i < pattern.size(); i++) {
            if (ptr[i] != pattern[i]) {
                return false;
            }
        }
        return true;
    }

    // Free all allocated test memory
    static void FreeAllTestMemory() {
        for (void* block : s_allocatedBlocks) {
            free(block);
        }
        s_allocatedBlocks.clear();
    }

private:
    static std::vector<void*> s_allocatedBlocks;
};

/**
 * Performance benchmark utilities
 */
class BenchmarkUtilities {
public:
    template<typename Func>
    static double MeasureExecutionTime(Func func, int iterations = 1) {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            func();
        }

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

        return totalMs / iterations;
    }

    template<typename Func>
    static void BenchmarkFunction(const std::string& name, Func func, int iterations = 1000) {
        std::cout << "Benchmarking: " << name << "\n";
        std::cout << "Iterations: " << iterations << "\n";

        double avgTime = MeasureExecutionTime(func, iterations);

        std::cout << "Average time: " << avgTime << " ms\n";
        std::cout << "Total time: " << (avgTime * iterations) << " ms\n";
        std::cout << "Ops/sec: " << (1000.0 / avgTime) << "\n\n";
    }
};

/**
 * Test fixture base class
 */
class TestFixture {
public:
    virtual ~TestFixture() = default;

    virtual void SetUp() {}
    virtual void TearDown() {}

protected:
    // Helper to run a test with setup/teardown
    void RunTest(std::function<void()> testFunc) {
        SetUp();
        try {
            testFunc();
        } catch (...) {
            TearDown();
            throw;
        }
        TearDown();
    }
};

/**
 * Integration test helpers
 */
class IntegrationTestHelpers {
public:
    // Wait for condition with timeout
    static bool WaitForCondition(std::function<bool()> condition, int timeoutMs = 5000) {
        auto start = std::chrono::steady_clock::now();

        while (true) {
            if (condition()) {
                return true;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

            if (elapsed >= timeoutMs) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // Retry operation with exponential backoff
    template<typename Func>
    static bool RetryWithBackoff(Func func, int maxAttempts = 3, int initialDelayMs = 100) {
        int delay = initialDelayMs;

        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            if (func()) {
                return true;
            }

            if (attempt < maxAttempts - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                delay *= 2;  // Exponential backoff
            }
        }

        return false;
    }
};

} // namespace Testing
} // namespace ReKenshi
