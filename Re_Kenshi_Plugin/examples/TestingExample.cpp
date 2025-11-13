/**
 * Testing Utilities Example for Re_Kenshi Plugin
 *
 * This file demonstrates how to use the testing framework.
 */

#include "../include/TestingUtilities.h"
#include "../include/Configuration.h"
#include "../include/MemoryScanner.h"
#include "../include/KenshiStructures.h"
#include <iostream>
#include <thread>

using namespace ReKenshi::Testing;
using namespace ReKenshi;

//=============================================================================
// Example 1: Basic Unit Tests
//=============================================================================

void TestAssertions() {
    // These assertions will pass
    TEST_ASSERT(true);
    TEST_ASSERT(1 == 1);
    TEST_ASSERT_EQUAL(42, 42);
    TEST_ASSERT_NOT_EQUAL(1, 2);

    int* ptr = nullptr;
    TEST_ASSERT_NULL(ptr);

    int value = 10;
    int* validPtr = &value;
    TEST_ASSERT_NOT_NULL(validPtr);

    std::cout << "All assertions passed!" << std::endl;
}

void TestConfiguration() {
    auto& config = Config::Configuration::GetInstance();

    // Test default values
    TEST_ASSERT_EQUAL(config.IPC().pipeName, std::string("ReKenshi_IPC"));
    TEST_ASSERT_EQUAL(config.Multiplayer().syncRate, 10.0f);

    // Test modification
    config.Multiplayer().syncRate = 20.0f;
    TEST_ASSERT_EQUAL(config.Multiplayer().syncRate, 20.0f);

    // Test validation
    config.Multiplayer().syncRate = -5.0f;
    TEST_ASSERT(!config.Validate());

    // Reset
    config.LoadDefaults();
    TEST_ASSERT(config.Validate());
}

void Example_BasicUnitTests() {
    TestSuite suite("Basic Unit Tests");

    suite.AddTest("Test Assertions", []() {
        TestAssertions();
    });

    suite.AddTest("Test Configuration", []() {
        TestConfiguration();
    });

    suite.AddTest("Test Simple Math", []() {
        int result = 2 + 2;
        TEST_ASSERT_EQUAL(4, result);
    });

    suite.AddTest("Test String Operations", []() {
        std::string str = "Hello";
        str += " World";
        TEST_ASSERT_EQUAL(std::string("Hello World"), str);
    });

    suite.RunAll();
    suite.PrintReport();
}

//=============================================================================
// Example 2: Memory Testing
//=============================================================================

void Example_MemoryTesting() {
    std::cout << "========== Memory Testing ==========\n\n";

    // Allocate test memory
    uintptr_t testBlock = MemoryTestUtilities::AllocateTestMemory(1024);
    TEST_ASSERT_NOT_EQUAL(testBlock, (uintptr_t)0);

    // Write test pattern
    std::vector<uint8_t> pattern = { 0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD };
    MemoryTestUtilities::WriteTestPattern(testBlock, pattern);

    // Verify pattern
    bool verified = MemoryTestUtilities::VerifyTestPattern(testBlock, pattern);
    TEST_ASSERT(verified);

    // Test reading structures
    Kenshi::Vector3 testVector;
    testVector.x = 1.0f;
    testVector.y = 2.0f;
    testVector.z = 3.0f;

    uintptr_t vectorAddr = reinterpret_cast<uintptr_t>(&testVector);

    Kenshi::Vector3 readVector;
    bool success = Memory::MemoryScanner::ReadMemory(vectorAddr, readVector);
    TEST_ASSERT(success);
    TEST_ASSERT_EQUAL(testVector.x, readVector.x);
    TEST_ASSERT_EQUAL(testVector.y, readVector.y);
    TEST_ASSERT_EQUAL(testVector.z, readVector.z);

    // Cleanup
    MemoryTestUtilities::FreeAllTestMemory();

    std::cout << "Memory testing completed successfully!\n\n";
}

//=============================================================================
// Example 3: Mock IPC Client Testing
//=============================================================================

void Example_MockIPCTesting() {
    std::cout << "========== Mock IPC Testing ==========\n\n";

    MockIPCClient mockClient;

    // Test connection
    TEST_ASSERT(!mockClient.IsConnected());
    mockClient.Connect();
    TEST_ASSERT(mockClient.IsConnected());

    // Test sending messages
    mockClient.SendMessage("{\"type\":\"test\"}");
    mockClient.SendMessage("{\"type\":\"test2\"}");

    const auto& sentMessages = mockClient.GetSentMessages();
    TEST_ASSERT_EQUAL(2, (int)sentMessages.size());
    TEST_ASSERT_EQUAL(std::string("{\"type\":\"test\"}"), sentMessages[0]);

    // Test receiving messages with callback
    bool callbackFired = false;
    std::string receivedData;

    mockClient.SetMessageCallback([&](const std::string& msg) {
        callbackFired = true;
        receivedData = msg;
    });

    mockClient.SimulateReceive("{\"type\":\"response\"}");
    TEST_ASSERT(callbackFired);
    TEST_ASSERT_EQUAL(std::string("{\"type\":\"response\"}"), receivedData);

    // Cleanup
    mockClient.Disconnect();
    TEST_ASSERT(!mockClient.IsConnected());

    std::cout << "Mock IPC testing completed successfully!\n\n";
}

//=============================================================================
// Example 4: Performance Benchmarking
//=============================================================================

void Example_Benchmarking() {
    std::cout << "========== Performance Benchmarking ==========\n\n";

    // Benchmark simple operations
    BenchmarkUtilities::BenchmarkFunction("Vector Addition", []() {
        Kenshi::Vector3 a = { 1.0f, 2.0f, 3.0f };
        Kenshi::Vector3 b = { 4.0f, 5.0f, 6.0f };
        Kenshi::Vector3 result;
        result.x = a.x + b.x;
        result.y = a.y + b.y;
        result.z = a.z + b.z;
    }, 10000);

    // Benchmark memory operations
    BenchmarkUtilities::BenchmarkFunction("Memory Allocation", []() {
        void* mem = malloc(1024);
        free(mem);
    }, 1000);

    // Benchmark configuration access
    BenchmarkUtilities::BenchmarkFunction("Configuration Access", []() {
        auto& config = Config::Configuration::GetInstance();
        float rate = config.Multiplayer().syncRate;
        (void)rate;  // Suppress unused warning
    }, 10000);

    std::cout << "Benchmarking completed!\n\n";
}

//=============================================================================
// Example 5: Test Fixtures
//=============================================================================

class ConfigurationTestFixture : public TestFixture {
public:
    void SetUp() override {
        // Save original config
        m_originalSyncRate = Config::Configuration::GetInstance().Multiplayer().syncRate;
        std::cout << "    [Setup] Saved original configuration\n";
    }

    void TearDown() override {
        // Restore original config
        Config::Configuration::GetInstance().Multiplayer().syncRate = m_originalSyncRate;
        std::cout << "    [Teardown] Restored original configuration\n";
    }

private:
    float m_originalSyncRate = 0.0f;
};

void Example_TestFixtures() {
    std::cout << "========== Test Fixtures ==========\n\n";

    ConfigurationTestFixture fixture;

    std::cout << "Test 1: Modify Configuration\n";
    fixture.RunTest([]() {
        auto& config = Config::Configuration::GetInstance();
        config.Multiplayer().syncRate = 99.0f;
        TEST_ASSERT_EQUAL(99.0f, config.Multiplayer().syncRate);
    });

    std::cout << "Test 2: Verify Restoration\n";
    fixture.RunTest([]() {
        auto& config = Config::Configuration::GetInstance();
        // Should be back to original value (10.0f default)
        TEST_ASSERT_EQUAL(10.0f, config.Multiplayer().syncRate);
    });

    std::cout << "Test fixtures completed!\n\n";
}

//=============================================================================
// Example 6: Integration Testing Helpers
//=============================================================================

void Example_IntegrationHelpers() {
    std::cout << "========== Integration Test Helpers ==========\n\n";

    // Test wait for condition
    std::cout << "Testing WaitForCondition...\n";

    bool flag = false;
    std::thread delayedSetter([&flag]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flag = true;
    });

    bool success = IntegrationTestHelpers::WaitForCondition([&flag]() {
        return flag;
    }, 1000);

    TEST_ASSERT(success);
    delayedSetter.join();
    std::cout << "WaitForCondition test passed!\n\n";

    // Test retry with backoff
    std::cout << "Testing RetryWithBackoff...\n";

    int attemptCount = 0;
    bool retrySuccess = IntegrationTestHelpers::RetryWithBackoff([&attemptCount]() {
        attemptCount++;
        return attemptCount >= 2;  // Succeed on second attempt
    }, 5, 10);

    TEST_ASSERT(retrySuccess);
    TEST_ASSERT_EQUAL(2, attemptCount);
    std::cout << "RetryWithBackoff test passed!\n\n";

    std::cout << "Integration test helpers completed!\n\n";
}

//=============================================================================
// Example 7: Comprehensive Test Suite
//=============================================================================

void Example_ComprehensiveTestSuite() {
    TestSuite suite("Re_Kenshi Plugin Tests");

    // Configuration tests
    suite.AddTest("Config: Default Values", []() {
        auto& config = Config::Configuration::GetInstance();
        config.LoadDefaults();
        TEST_ASSERT_EQUAL(std::string("ReKenshi_IPC"), config.IPC().pipeName);
        TEST_ASSERT_EQUAL(10.0f, config.Multiplayer().syncRate);
    });

    suite.AddTest("Config: Validation", []() {
        auto& config = Config::Configuration::GetInstance();
        config.LoadDefaults();
        TEST_ASSERT(config.Validate());

        config.Multiplayer().syncRate = -1.0f;
        TEST_ASSERT(!config.Validate());

        config.LoadDefaults();
    });

    // Memory tests
    suite.AddTest("Memory: Allocation", []() {
        uintptr_t addr = MemoryTestUtilities::AllocateTestMemory(256);
        TEST_ASSERT_NOT_EQUAL((uintptr_t)0, addr);
    });

    suite.AddTest("Memory: Pattern Write/Read", []() {
        uintptr_t addr = MemoryTestUtilities::AllocateTestMemory(256);
        std::vector<uint8_t> pattern = { 0x01, 0x02, 0x03, 0x04 };

        MemoryTestUtilities::WriteTestPattern(addr, pattern);
        bool verified = MemoryTestUtilities::VerifyTestPattern(addr, pattern);
        TEST_ASSERT(verified);
    });

    // IPC tests
    suite.AddTest("IPC: Connection", []() {
        MockIPCClient client;
        TEST_ASSERT(!client.IsConnected());
        client.Connect();
        TEST_ASSERT(client.IsConnected());
    });

    suite.AddTest("IPC: Message Sending", []() {
        MockIPCClient client;
        client.Connect();
        client.SendMessage("test1");
        client.SendMessage("test2");

        const auto& messages = client.GetSentMessages();
        TEST_ASSERT_EQUAL(2, (int)messages.size());
    });

    // Structure tests
    suite.AddTest("Structures: Vector3", []() {
        Kenshi::Vector3 v;
        v.x = 1.0f;
        v.y = 2.0f;
        v.z = 3.0f;

        TEST_ASSERT_EQUAL(1.0f, v.x);
        TEST_ASSERT_EQUAL(2.0f, v.y);
        TEST_ASSERT_EQUAL(3.0f, v.z);
    });

    suite.AddTest("Structures: CharacterData Size", []() {
        // Verify structure size is reasonable
        size_t size = sizeof(Kenshi::CharacterData);
        TEST_ASSERT(size > 0);
        TEST_ASSERT(size < 10000);  // Sanity check
    });

    // Run all tests
    suite.RunAll();
    suite.PrintReport();

    // Cleanup
    MemoryTestUtilities::FreeAllTestMemory();
}

//=============================================================================
// Example 8: Error Handling Tests
//=============================================================================

void Example_ErrorHandling() {
    TestSuite suite("Error Handling Tests");

    suite.AddTest("Exception Throwing", []() {
        // This test expects an exception
        TEST_ASSERT_THROWS(
            throw std::runtime_error("Expected error"),
            std::runtime_error
        );
    });

    suite.AddTest("Null Pointer Handling", []() {
        int* nullPtr = nullptr;
        TEST_ASSERT_NULL(nullPtr);

        int value = 42;
        int* validPtr = &value;
        TEST_ASSERT_NOT_NULL(validPtr);
    });

    suite.AddTest("Boundary Conditions", []() {
        // Test edge cases
        float epsilon = 0.0001f;
        float a = 1.0f;
        float b = 1.0f + epsilon;

        // These are close but not exactly equal
        TEST_ASSERT_NOT_EQUAL(a, b);
    });

    suite.RunAll();
}

//=============================================================================
// Main Test Runner
//=============================================================================

void RunAllTests() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║        Re_Kenshi Plugin - Test Suite Runner           ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    try {
        Example_BasicUnitTests();
        Example_MemoryTesting();
        Example_MockIPCTesting();
        Example_Benchmarking();
        Example_TestFixtures();
        Example_IntegrationHelpers();
        Example_ComprehensiveTestSuite();
        Example_ErrorHandling();

        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════╗\n";
        std::cout << "║             ALL TESTS COMPLETED SUCCESSFULLY           ║\n";
        std::cout << "╚════════════════════════════════════════════════════════╝\n";
        std::cout << "\n";

    } catch (const std::exception& e) {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════╗\n";
        std::cout << "║                  TESTS FAILED                          ║\n";
        std::cout << "╚════════════════════════════════════════════════════════╝\n";
        std::cout << "Error: " << e.what() << "\n";
        std::cout << "\n";
    }
}

// Entry point for running tests
int main() {
    RunAllTests();
    return 0;
}
