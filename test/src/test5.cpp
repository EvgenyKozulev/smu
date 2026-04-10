#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>
#include <algorithm>
#include <iomanip>

#include "smu.h"

// =============================================================================
// EDGE CASES & NEGATIVE TESTS
// =============================================================================

class SmuEdgeCasesTest : public ::testing::Test {
    protected:
	const size_t POOL_SIZE = 4096;
	std::vector<std::byte> memory_pool;
	std::unique_ptr<Smu> smu;

	void SetUp() override
	{
		memory_pool.assign(POOL_SIZE, std::byte{ 0 });
	}
};

// === ALLOCATION BOUNDARY CONDITIONS ===

TEST_F(SmuEdgeCasesTest, AllocateZeroSizeHandling)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(0);
	// System allocates minimum block or returns nullptr - check it's consistent
	if (ptr != nullptr) {
		EXPECT_GT(smu->busyNodes(), 0) << "If allocated, should have busy nodes";
		EXPECT_TRUE(smu->deallocate(ptr));
	}
	EXPECT_EQ(smu->busyNodes(), 0);
}

TEST_F(SmuEdgeCasesTest, AllocateSingleByte)
{
	smu = std::make_unique<Smu>(8, 256, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(1);
	ASSERT_NE(ptr, nullptr) << "Should be able to allocate 1 byte";

	// Verify alignment
	uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
	EXPECT_EQ(addr % 8, 0) << "Pointer should be aligned to 8 bytes";

	EXPECT_TRUE(smu->deallocate(ptr));
	EXPECT_EQ(smu->busyNodes(), 0);
}

TEST_F(SmuEdgeCasesTest, AllocateNearMaxPoolSize)
{
	smu = std::make_unique<Smu>(8, 64, std::span<std::byte>(memory_pool));

	size_t admin_space = smu->getAdminSpace();
	size_t available_data = POOL_SIZE - admin_space;

	// Try to allocate most of available space, but leave margin for metadata
	size_t request_size = (available_data > 256) ? available_data - 256 : available_data / 2;
	void *ptr = smu->allocate(request_size);
	if (ptr != nullptr) {
		EXPECT_GT(smu->busyNodes(), 0);
		EXPECT_TRUE(smu->deallocate(ptr));
	}
	EXPECT_EQ(smu->busyNodes(), 0);
}

TEST_F(SmuEdgeCasesTest, AllocateMoreThanPoolSize)
{
	smu = std::make_unique<Smu>(8, 64, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(POOL_SIZE * 10);
	EXPECT_EQ(ptr, nullptr)
		<< "Should not allocate more than available pool";
	EXPECT_TRUE(smu->checkIntegrity());
}

// === ALIGNMENT VERIFICATION ===

TEST_F(SmuEdgeCasesTest, AlignmentPower2)
{
	for (uint8_t align : { 1, 2, 4, 8, 16, 32, 64 }) {
		memory_pool.assign(POOL_SIZE, std::byte{ 0 });
		smu = std::make_unique<Smu>(align, 128,
					    std::span<std::byte>(memory_pool));

		void *ptr = smu->allocate(64);
		ASSERT_NE(ptr, nullptr) << "Failed for alignment " << (int)align;

		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		EXPECT_EQ(addr % align, 0)
			<< "Address should be aligned to " << (int)align;

		smu->deallocate(ptr);
	}
}

TEST_F(SmuEdgeCasesTest, MultipleAllocationsAlignmentConsistency)
{
	smu = std::make_unique<Smu>(16, 128, std::span<std::byte>(memory_pool));

	std::vector<void *> ptrs;
	for (int i = 0; i < 5; ++i) {
		void *ptr = smu->allocate(64 + i * 12);
		ASSERT_NE(ptr, nullptr);

		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		EXPECT_EQ(addr % 16, 0)
			<< "Iteration " << i
			<< ": Address should maintain alignment";

		ptrs.push_back(ptr);
	}

	for (auto *ptr : ptrs) {
		EXPECT_TRUE(smu->deallocate(ptr));
	}

	EXPECT_EQ(smu->busyNodes(), 0);
}

// === NEGATIVE SCENARIOS ===

TEST_F(SmuEdgeCasesTest, DoubleFreeDetection)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(64);
	ASSERT_NE(ptr, nullptr);

	EXPECT_TRUE(smu->deallocate(ptr))
		<< "First deallocate should succeed";

	EXPECT_FALSE(smu->deallocate(ptr))
		<< "Second deallocate of same pointer should fail";

	EXPECT_TRUE(smu->checkIntegrity())
		<< "System should remain intact after failed double-free";
}

TEST_F(SmuEdgeCasesTest, DeallocateInvalidPointer)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	// Stack pointer (clearly invalid)
	int local_var = 42;
	void *invalid_ptr = &local_var;

	EXPECT_FALSE(smu->deallocate(invalid_ptr))
		<< "Deallocating stack pointer should fail";

	EXPECT_TRUE(smu->checkIntegrity())
		<< "System should remain valid after invalid deallocate";

	// Pointer outside memory pool
	void *external_ptr = reinterpret_cast<void *>(0xDEADBEEF);
	EXPECT_FALSE(smu->deallocate(external_ptr))
		<< "Deallocating external pointer should fail";

	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuEdgeCasesTest, UnalignedPointerDeallocateRobustness)
{
	smu = std::make_unique<Smu>(16, 128, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(64);
	ASSERT_NE(ptr, nullptr);

	uintptr_t original_addr = reinterpret_cast<uintptr_t>(ptr);

	// Modify pointer to be slightly unaligned (+1 byte)
	void *unaligned = reinterpret_cast<void *>(original_addr + 1);

	// System may or may not detect this - just verify it doesn't crash
	bool unaligned_result = smu->deallocate(unaligned);

	// After unaligned dealloc (whether it succeeds or fails), system should be intact
	EXPECT_TRUE(smu->checkIntegrity());

	// If unaligned dealloc succeeded, original is already freed
	// If it failed, original should still work
	if (!unaligned_result) {
		EXPECT_TRUE(smu->deallocate(ptr))
			<< "If unaligned dealloc failed, original should work";
	}

	EXPECT_EQ(smu->busyNodes(), 0);
}

TEST_F(SmuEdgeCasesTest, DeallocateNullPointer)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	// Most allocators just ignore nullptr deallocate
	EXPECT_FALSE(smu->deallocate(nullptr))
		<< "Deallocating nullptr should return false";

	EXPECT_TRUE(smu->checkIntegrity());
}

// === FRAGMENTATION WITH VARIOUS SIZES ===

TEST_F(SmuEdgeCasesTest, RandomSizeFragmentation)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	std::vector<void *> ptrs;
	size_t total_allocated = 0;

	// Allocate with random sizes
	for (int i = 0; i < 10; ++i) {
		size_t size = (64 * (i + 1)) % 200 + 16;
		void *ptr = smu->allocate(size);

		if (ptr == nullptr)
			break;

		ptrs.push_back(ptr);
		total_allocated += size;
	}

	EXPECT_GT(ptrs.size(), 0) << "Should have allocated something";
	EXPECT_TRUE(smu->checkIntegrity());

	// Check stats sum up
	EXPECT_GE(smu->busyBytes(), total_allocated - 256);

	// Free odd indices
	size_t freed_count = 0;
	for (size_t i = 1; i < ptrs.size(); i += 2) {
		if (smu->deallocate(ptrs[i])) {
			freed_count++;
		}
	}

	EXPECT_EQ(smu->busyNodes() + smu->freeNodes(),
		  smu->totalBlocks());

	// Free rest
	for (size_t i = 0; i < ptrs.size(); i += 2) {
		smu->deallocate(ptrs[i]);
	}

	EXPECT_EQ(smu->busyNodes(), 0);
	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuEdgeCasesTest, AllocateLargeSmallPatternCoalescing)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	void *large1 = smu->allocate(512);
	void *small1 = smu->allocate(64);
	void *large2 = smu->allocate(512);
	void *small2 = smu->allocate(64);

	if (large1 && small1 && large2 && small2) {
		EXPECT_TRUE(smu->checkIntegrity());

		// Free large blocks
		smu->deallocate(large1);
		smu->deallocate(large2);

		EXPECT_TRUE(smu->checkIntegrity());

		// Allocating small should work (fragments are available)
		void *small3 = smu->allocate(64);
		EXPECT_NE(small3, nullptr)
			<< "Should be able to allocate from freed large blocks";

		if (small3) {
			smu->deallocate(small3);
		}

		smu->deallocate(small1);
		smu->deallocate(small2);
	}

	EXPECT_EQ(smu->busyNodes(), 0);
}

// === CONSTRUCTOR PARAMETER RANGES ===

TEST_F(SmuEdgeCasesTest, MinimalAlignment)
{
	std::vector<std::byte> large_pool(16384, std::byte{ 0 });
	smu = std::make_unique<Smu>(1, 64, std::span<std::byte>(large_pool));

	void *ptr = smu->allocate(100);
	ASSERT_NE(ptr, nullptr);

	uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
	EXPECT_EQ(addr % 1, 0); // Always true, but documents intent

	EXPECT_TRUE(smu->deallocate(ptr));
	EXPECT_EQ(smu->busyNodes(), 0);
}

TEST_F(SmuEdgeCasesTest, LargeAlignment)
{
	std::vector<std::byte> large_pool(8192, std::byte{ 0 });
	smu = std::make_unique<Smu>(64, 256, std::span<std::byte>(large_pool));

	void *ptr = smu->allocate(256);
	if (ptr != nullptr) {
		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		EXPECT_EQ(addr % 64, 0);

		EXPECT_TRUE(smu->deallocate(ptr));
	}
}

TEST_F(SmuEdgeCasesTest, AlignmentGreaterThanMinBlock)
{
	std::vector<std::byte> large_pool(4096, std::byte{ 0 });
	// align=32 > minBlock=16
	smu = std::make_unique<Smu>(32, 16, std::span<std::byte>(large_pool));

	void *ptr = smu->allocate(32);
	if (ptr != nullptr) {
		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		EXPECT_EQ(addr % 32, 0) << "Should respect alignment > minBlock";
		EXPECT_TRUE(smu->deallocate(ptr));
	}
}

// === STATS CONSISTENCY AFTER FAILURES ===

TEST_F(SmuEdgeCasesTest, StatsAfterAllocationFailure)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	size_t initial_busy_bytes = smu->busyBytes();
	size_t initial_busy_nodes = smu->busyNodes();

	void *failed_alloc = smu->allocate(POOL_SIZE * 100);
	EXPECT_EQ(failed_alloc, nullptr);

	EXPECT_EQ(smu->busyBytes(), initial_busy_bytes)
		<< "Failed allocation should not change stats";
	EXPECT_EQ(smu->busyNodes(), initial_busy_nodes);
}

TEST_F(SmuEdgeCasesTest, StatsAfterDeallocateFailure)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	void *ptr = smu->allocate(64);
	ASSERT_NE(ptr, nullptr);

	size_t busy_before = smu->busyNodes();

	EXPECT_FALSE(smu->deallocate(reinterpret_cast<void *>(0xBEEF)));

	EXPECT_EQ(smu->busyNodes(), busy_before)
		<< "Failed deallocate should not change stats";
}

// === CYCLIC ALLOCATION-DEALLOCATION PATTERNS ===

TEST_F(SmuEdgeCasesTest, RepeatedAllocDeallocCycles)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	for (int cycle = 0; cycle < 20; ++cycle) {
		std::vector<void *> ptrs;

		// Allocate
		for (int i = 0; i < 5; ++i) {
			void *ptr = smu->allocate(128);
			if (ptr == nullptr)
				break;
			ptrs.push_back(ptr);
		}

		EXPECT_TRUE(smu->checkIntegrity())
			<< "Cycle " << cycle << " after allocate";

		// Deallocate
		for (auto *ptr : ptrs) {
			EXPECT_TRUE(smu->deallocate(ptr))
				<< "Cycle " << cycle << " deallocate failed";
		}

		EXPECT_EQ(smu->busyNodes(), 0)
			<< "Cycle " << cycle
			<< " should have zero busy nodes after full dealloc";
	}
}

TEST_F(SmuEdgeCasesTest, AlternatingReleasePattern)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	void *ptrs[16];
	size_t count = 0;

	// Fill with fixed size
	for (int i = 0; i < 16; ++i) {
		ptrs[i] = smu->allocate(128);
		if (ptrs[i] == nullptr)
			break;
		count++;
	}

	ASSERT_GT(count, 0);

	// Release in complex pattern: 0,1,2,4,8,3,5,6,7...
	std::vector<size_t> release_order = { 0, 1, 2, 4, 8, 3, 5, 6, 7, 9, 10,
					      11, 12, 13, 14, 15 };

	for (size_t idx : release_order) {
		if (idx >= count || ptrs[idx] == nullptr)
			continue;

		EXPECT_TRUE(smu->deallocate(ptrs[idx]));
		EXPECT_TRUE(smu->checkIntegrity());
	}

	EXPECT_EQ(smu->busyNodes(), 0);
}

// === COLOR TREE CONSISTENCY (RED-BLACK) ===

TEST_F(SmuEdgeCasesTest, TreeColorInvariantAfterMixedOps)
{
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(memory_pool));

	std::vector<void *> ptrs;

	// Allocate many blocks to trigger tree rebalancing
	for (int i = 0; i < 20; ++i) {
		void *ptr = smu->allocate(64);
		if (ptr == nullptr)
			break;
		ptrs.push_back(ptr);
	}

	// Check that color counts make sense
	size_t total_nodes = smu->busyNodes() + smu->freeNodes();
	EXPECT_GT(total_nodes, 0);

	// Free every other one
	for (size_t i = 0; i < ptrs.size(); i += 2) {
		EXPECT_TRUE(smu->deallocate(ptrs[i]));
	}

	// Tree should still be internally valid
	EXPECT_TRUE(smu->checkIntegrity());

	for (size_t i = 1; i < ptrs.size(); i += 2) {
		EXPECT_TRUE(smu->deallocate(ptrs[i]));
	}

	EXPECT_EQ(smu->busyNodes(), 0);
}

// === EDGE CASE SIZES ===

TEST_F(SmuEdgeCasesTest, AllocatePowerOf2Sizes)
{
	std::vector<std::byte> large_pool(65536, std::byte{ 0 });
	smu = std::make_unique<Smu>(8, 64, std::span<std::byte>(large_pool));

	for (size_t size : { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 }) {
		void *ptr = smu->allocate(size);
		ASSERT_NE(ptr, nullptr)
			<< "Failed to allocate power-of-2 size: " << size;

		EXPECT_TRUE(smu->deallocate(ptr));
		EXPECT_EQ(smu->busyNodes(), 0);
	}
}

TEST_F(SmuEdgeCasesTest, AllocateNonPowerOf2Sizes)
{
	std::vector<std::byte> large_pool(32768, std::byte{ 0 });
	smu = std::make_unique<Smu>(8, 128, std::span<std::byte>(large_pool));

	for (size_t size : { 3, 7, 13, 29, 63, 100, 255, 513, 1000 }) {
		void *ptr = smu->allocate(size);
		if (ptr == nullptr)
			break; // Pool exhausted

		EXPECT_TRUE(smu->deallocate(ptr));
		EXPECT_EQ(smu->busyNodes(), 0)
			<< "Failed cleanup after deallocating size: " << size;
	}
}
