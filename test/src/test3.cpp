#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>
#include <algorithm>
#include <functional>
#include <iomanip>

#include "smu.h"

class SmuInterfaceTest : public ::testing::Test {
    protected:
	const size_t POOL_SIZE = 8192;
	const uint16_t MIN_BLOCK = 128;
	const uint8_t ALIGN = 16;

	std::vector<std::byte> memory_pool;
	std::unique_ptr<Smu> smu;

	void SetUp() override
	{
		memory_pool.assign(POOL_SIZE, std::byte{ 0 });
		smu = std::make_unique<Smu>(ALIGN, MIN_BLOCK,
					    std::span<std::byte>(memory_pool));
	}
};

TEST_F(SmuInterfaceTest, StatisticsConsistency)
{
	size_t initialFreeBytes = smu->freeBytes();
	size_t initialTotalBlocks = smu->totalBlocks();

	EXPECT_GT(initialFreeBytes, 0);
	EXPECT_EQ(smu->busyBytes(), 0);
	EXPECT_EQ(smu->busyNodes(), 0);
	EXPECT_TRUE(smu->checkIntegrity());

	void *ptr = smu->allocate(MIN_BLOCK * 2);
	ASSERT_NE(ptr, nullptr);

	EXPECT_LT(smu->freeBytes(), initialFreeBytes);
	EXPECT_GT(smu->busyBytes(), 0);
	EXPECT_GT(smu->busyNodes(), 0);

	EXPECT_EQ(smu->freeNodes() + smu->busyNodes(), initialTotalBlocks);

	smu->deallocate(ptr);
	EXPECT_EQ(smu->busyBytes(), 0);
	EXPECT_EQ(smu->freeBytes(), initialFreeBytes);
	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuInterfaceTest, AdminSpaceOverhead)
{
	size_t admin = smu->getAdminSpace();
	size_t freeMem = smu->getFreeNodesMemory();
	size_t busyMem = smu->getBusyNodesMemory();

	EXPECT_GT(admin, freeMem + busyMem);

	void *ptr = smu->allocate(MIN_BLOCK);
	EXPECT_EQ(admin, smu->getAdminSpace());
	EXPECT_GT(smu->getBusyNodesMemory(), busyMem);
}

TEST_F(SmuInterfaceTest, ExhaustionAndDataCorruption)
{
	size_t totalInitialBlocks = smu->totalBlocks();
	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;

	for (size_t blocksToAlloc = totalInitialBlocks; blocksToAlloc > 0;
	     --blocksToAlloc) {
		size_t availableSpace = blocksToAlloc * MIN_BLOCK;
		size_t bytesToReq = (availableSpace > overhead) ?
					    (availableSpace - overhead) :
					    1;

		void *ptr = smu->allocate(bytesToReq);
		ASSERT_NE(ptr, nullptr)
			<< "Failed to alloc " << blocksToAlloc
			<< " blocks with " << bytesToReq << " bytes request";

		if (blocksToAlloc == totalInitialBlocks) {
			EXPECT_EQ(smu->freeNodes(), 0);
		}

		std::byte *dataPtr = static_cast<std::byte *>(ptr);
		std::memset(dataPtr, 0xEE, bytesToReq);

		ASSERT_TRUE(smu->deallocate(ptr)) << "Deallocate failed at "
						  << blocksToAlloc << " blocks";

		EXPECT_TRUE(smu->checkIntegrity())
			<< "Integrity lost after cycle " << blocksToAlloc;
		EXPECT_EQ(smu->freeNodes(), totalInitialBlocks);
		EXPECT_EQ(smu->busyNodes(), 0);

		void *probe = smu->allocate(1);
		ASSERT_NE(probe, nullptr);
		smu->deallocate(probe);
	}
}

TEST_F(SmuInterfaceTest, ChessboardFragmentation)
{
	size_t totalBlocks = smu->totalBlocks();
	void *ptrs[256];
	ASSERT_LE(totalBlocks, 256);

	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;
	size_t oneBlockSize = MIN_BLOCK - overhead;

	for (size_t i = 0; i < totalBlocks; ++i) {
		ptrs[i] = smu->allocate(oneBlockSize);
		ASSERT_NE(ptrs[i], nullptr) << "Failed to fill at block " << i;
	}
	EXPECT_EQ(smu->freeNodes(), 0);
	EXPECT_EQ(smu->busyNodes(), totalBlocks);

	size_t freedCount = 0;
	for (size_t i = 0; i < totalBlocks; i += 2) {
		ASSERT_TRUE(smu->deallocate(ptrs[i]));
		ptrs[i] = nullptr;
		freedCount++;
	}

	EXPECT_EQ(smu->freeNodes(), freedCount);
	EXPECT_EQ(smu->busyNodes(), totalBlocks - freedCount);
	EXPECT_TRUE(smu->checkIntegrity());

	void *bigPtr = smu->allocate(MIN_BLOCK * 2 - overhead);
	EXPECT_EQ(bigPtr, nullptr)
		<< "Should not be able to alloc contiguous blocks in chessboard";

	for (size_t i = 0; i < totalBlocks; ++i) {
		if (ptrs[i] != nullptr) {
			ASSERT_TRUE(smu->deallocate(ptrs[i]));
		}
	}

	EXPECT_EQ(smu->freeNodes(), totalBlocks);
	void *fullPtr = smu->allocate(totalBlocks * MIN_BLOCK - overhead);
	EXPECT_NE(fullPtr, nullptr)
		<< "Coalescing failed after clearing chessboard";

	smu->deallocate(fullPtr);
	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuInterfaceTest, ChessboardCorruptionIsolation)
{
	size_t totalBlocks = smu->totalBlocks();
	ASSERT_GT(totalBlocks, 4);

	std::vector<void *> ptrs(totalBlocks, nullptr);
	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;
	size_t oneBlockSize = MIN_BLOCK - overhead;

	for (size_t i = 0; i < totalBlocks; ++i) {
		ptrs[i] = smu->allocate(oneBlockSize);
		ASSERT_NE(ptrs[i], nullptr);
	}

	for (size_t i = 0; i < totalBlocks; i += 2) {
		ASSERT_TRUE(smu->deallocate(ptrs[i]));
		ptrs[i] = nullptr;
	}

	void *targetPtr = ptrs[1];

	uintptr_t addr = reinterpret_cast<uintptr_t>(targetPtr);
	uintptr_t mask = static_cast<uintptr_t>(ALIGN) - 1;
	uintptr_t headLimit = addr - sizeof(SmuTab::MetaHead);
	std::byte *masterAddr =
		reinterpret_cast<std::byte *>(headLimit & ~mask);

	auto *head = reinterpret_cast<SmuTab::MetaHead *>(masterAddr);

	head->xorMeta ^= 0xFFFFFFFF;

	EXPECT_FALSE(smu->deallocate(targetPtr))
		<< "System should reject corrupted MetaHead";

	EXPECT_TRUE(smu->checkIntegrity())
		<< "Integrity should remain valid as no nodes were incorrectly moved";

	void *probe = smu->allocate(oneBlockSize);
	EXPECT_NE(probe, nullptr)
		<< "Allocator should still work for healthy free blocks";
	smu->deallocate(probe);

	EXPECT_TRUE(smu->deallocate(ptrs[3]))
		<< "Healthy block should be deallocated normally";
	ptrs[3] = nullptr;

	for (size_t i = 0; i < totalBlocks; ++i) {
		if (ptrs[i] != nullptr && i != 1) {
			smu->deallocate(ptrs[i]);
		}
	}
}

TEST_F(SmuInterfaceTest, SequentialAlignmentValidation)
{
	std::array<size_t, 5> sizes = { static_cast<size_t>(MIN_BLOCK / 2),
					static_cast<size_t>(MIN_BLOCK),
					static_cast<size_t>(1),
					static_cast<size_t>(MIN_BLOCK * 2),
					static_cast<size_t>(7) };

	std::vector<void *> ptrs;
	uintptr_t alignMask = static_cast<uintptr_t>(ALIGN) - 1;

	for (size_t i = 0; i < 20; ++i) {
		size_t requestSize = sizes[i % sizes.size()];
		void *ptr = smu->allocate(requestSize);

		if (ptr == nullptr)
			break;

		ptrs.push_back(ptr);

		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
		EXPECT_EQ(addr & alignMask, 0)
			<< "Pointer " << ptr << " is not aligned to "
			<< (int)ALIGN;

		std::byte *headStart = static_cast<std::byte *>(ptr) -
				       sizeof(SmuTab::MetaHead);
		EXPECT_GE(headStart, memory_pool.data());
	}

	for (void *ptr : ptrs) {
		ASSERT_TRUE(smu->deallocate(ptr));
	}

	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuInterfaceTest, MemoryPersistenceAndImmutability)
{
	size_t totalBlocks = smu->totalBlocks();
	std::vector<void *> ptrs;
	std::vector<std::vector<std::byte> > referenceData;

	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;

	for (size_t i = 0; i < totalBlocks; ++i) {
		size_t blocksToReq = (i % 3 == 0) ? 2 : 1;
		size_t currentAvailable = blocksToReq * MIN_BLOCK;
		if (currentAvailable <= overhead)
			continue;

		size_t size = currentAvailable - overhead;

		void *ptr = smu->allocate(size);
		if (!ptr)
			break;

		ptrs.push_back(ptr);

		std::vector<std::byte> pattern(size);
		for (size_t j = 0; j < size; ++j) {
			pattern[j] = static_cast<std::byte>((i + j) & 0xFF);
		}

		std::memcpy(ptr, pattern.data(), size);
		referenceData.push_back(std::move(pattern));
	}

	for (void *ptr : ptrs) {
		ASSERT_TRUE(smu->deallocate(ptr));
	}

	for (size_t i = 0; i < ptrs.size(); ++i) {
		size_t size = referenceData[i].size();
		void *newPtr = smu->allocate(size);

		ASSERT_NE(newPtr, nullptr);
		ASSERT_EQ(newPtr, ptrs[i]) << "Address mismatch at index " << i;

		int cmp = std::memcmp(newPtr, referenceData[i].data(), size);
		EXPECT_EQ(cmp, 0) << "Data corruption detected at index " << i;
	}

	for (void *ptr : ptrs) {
		smu->deallocate(ptr);
	}

	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuInterfaceTest, FullAccountingStatistics)
{
	size_t totalBlocks = smu->totalBlocks();
	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;
	size_t oneBlockSize = MIN_BLOCK - overhead;

	EXPECT_EQ(smu->freeNodes(), totalBlocks);
	size_t nilSize = sizeof(SmuTree::Node);

	std::vector<void *> ptrs;
	for (size_t i = 1; i <= totalBlocks; ++i) {
		void *ptr = smu->allocate(oneBlockSize);
		ASSERT_NE(ptr, nullptr);
		ptrs.push_back(ptr);

		EXPECT_EQ(smu->busyBytes(), i * MIN_BLOCK);
	}

	EXPECT_EQ(smu->freeNodes(), 0);
	EXPECT_EQ(smu->getFreeNodesMemory(), nilSize);

	for (void *ptr : ptrs) {
		smu->deallocate(ptr);
	}

	EXPECT_EQ(smu->busyNodes(), 0);
	EXPECT_EQ(smu->getBusyNodesMemory(), nilSize);
	EXPECT_TRUE(smu->checkIntegrity());
}

TEST_F(SmuInterfaceTest, SystemEfficiencyAnalysis)
{
	size_t totalBlocks = smu->totalBlocks();
	size_t overhead = sizeof(SmuTab::MetaHead) + ALIGN;
	size_t oneBlockSize = MIN_BLOCK - overhead;

	size_t totalPoolBytes = POOL_SIZE; // Весь кусок из std::vector
	size_t maxPossibleUserData = totalBlocks * oneBlockSize;

	std::vector<void *> ptrs;
	for (size_t i = 0; i < totalBlocks; ++i) {
		void *ptr = smu->allocate(oneBlockSize);
		ASSERT_NE(ptr, nullptr);
		ptrs.push_back(ptr);
	}

	size_t adminSpace = smu->getAdminSpace();
	size_t busyBytes = smu->busyBytes(); // Физические блоки в TAB
	size_t busyNodesMem = smu->getBusyNodesMemory();

	double efficiency =
		(static_cast<double>(maxPossibleUserData) / totalPoolBytes) *
		100.0;
	double overheadPercent =
		(static_cast<double>(adminSpace) / totalPoolBytes) * 100.0;

	std::cout << "\n[ SMU EFFICIENCY REPORT ]" << std::endl;
	std::cout << "Total Pool Size:   " << totalPoolBytes << " bytes"
		  << std::endl;
	std::cout << "Admin Overhead:    " << adminSpace << " bytes ("
		  << std::fixed << std::setprecision(2) << overheadPercent
		  << "%)" << std::endl;
	std::cout << "User Data Capacity: " << maxPossibleUserData << " bytes ("
		  << efficiency << "%)" << std::endl;
	std::cout << "Blocks Count:      " << totalBlocks << std::endl;
	std::cout << "Min Block Size:    " << MIN_BLOCK << " bytes"
		  << std::endl;

	EXPECT_GT(efficiency, 0.0);
	EXPECT_LE(efficiency, 100.0);
	EXPECT_TRUE(smu->checkIntegrity());

	for (void *ptr : ptrs) {
		smu->deallocate(ptr);
	}
}
