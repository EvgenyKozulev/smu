#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>
#include <algorithm>
#include <functional>
#include <iomanip>

#include "smu_fmb.h"

class SmuFmbTest : public ::testing::Test {
    protected:
	const size_t POOL_SIZE = 4096;
	const uint16_t MIN_BLOCK = 128;
	const uint8_t ALIGN = 8;

	std::vector<std::byte> memory_pool;
	std::unique_ptr<SmuFmb> fmb;

	void SetUp() override
	{
		memory_pool.assign(POOL_SIZE, std::byte{ 0 });
		fmb = std::make_unique<SmuFmb>(
			ALIGN, MIN_BLOCK, std::span<std::byte>(memory_pool));
	}

	void TearDown() override
	{
		fmb.reset();
	}
};

TEST_F(SmuFmbTest, InitializationCorrectness)
{
	size_t nodesInTree = fmb->stats.nodeCount.load();
	size_t treeMetaSize = fmb->stats.treeSize.load();
	size_t freeDataSize = fmb->stats.keyDataSize.load();
	size_t blockCount = fmb->getBlockCount();

	EXPECT_EQ(nodesInTree, blockCount)
		<< "Все созданные ноды должны быть в дереве свободных блоков";

	EXPECT_EQ(freeDataSize, blockCount * MIN_BLOCK)
		<< "Суммарный объем свободной памяти должен соответствовать количеству блоков";

	EXPECT_GE(treeMetaSize, nodesInTree * sizeof(SmuTree::Node))
		<< "Размер дерева должен покрывать все аллоцированные ноды";

	std::cout << "[ STATS ] Nodes: " << nodesInTree
		  << " | Free Data: " << freeDataSize << " bytes" << std::endl;
}

TEST_F(SmuFmbTest, ExtractAllBlocks)
{
	size_t totalBlocks = fmb->getBlockCount();
	size_t totalDataSize = totalBlocks * MIN_BLOCK;

	auto nodes = fmb->extract(totalDataSize);

	ASSERT_EQ(nodes.size(), totalBlocks)
		<< "Должны были получить все существующие ноды";
	EXPECT_FALSE(nodes.empty());

	EXPECT_EQ(fmb->stats.nodeCount.load(), 0)
		<< "В дереве не должно остаться узлов";
	EXPECT_EQ(fmb->stats.keyDataSize.load(), 0)
		<< "В дереве не должно остаться свободной памяти";

	std::byte *start = nodes.front().keyData.data();
	size_t fullSpanSize = nodes.size() * MIN_BLOCK;

	auto emptyRequest = fmb->extract(MIN_BLOCK);
	EXPECT_TRUE(emptyRequest.empty())
		<< "После полной очистки памяти не должно быть доступных блоков";

	std::cout << "[ STATS ] Successfully extracted " << totalBlocks
		  << " blocks, tree is now empty." << std::endl;
}

TEST_F(SmuFmbTest, ExtractAndReleaseAllBlocks)
{
	size_t totalBlocks = fmb->getBlockCount();
	size_t totalDataSize = totalBlocks * MIN_BLOCK;

	auto nodes = fmb->extract(totalDataSize);
	ASSERT_EQ(nodes.size(), totalBlocks);

	EXPECT_EQ(fmb->stats.nodeCount.load(), 0);
	EXPECT_EQ(fmb->stats.keyDataSize.load(), 0);

	bool releaseResult = fmb->release(nodes);

	EXPECT_TRUE(releaseResult)
		<< "Все блоки должны успешно вернуться в дерево";

	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks)
		<< "Количество нод после возврата должно восстановиться";

	EXPECT_EQ(fmb->stats.keyDataSize.load(), totalDataSize)
		<< "Объем свободной памяти должен вернуться к исходному";

	auto secondTry = fmb->extract(MIN_BLOCK);
	EXPECT_EQ(secondTry.size(), 1)
		<< "После возврата память должна быть снова доступна для выделения";
}

TEST_F(SmuFmbTest, OddBlocksRelease)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GE(totalBlocks, 4)
		<< "Нужно хотя бы 4 блока для вменяемого теста";

	auto allNodes = fmb->extract(totalBlocks * MIN_BLOCK);
	ASSERT_EQ(allNodes.size(), totalBlocks);

	size_t returnedCount = 0;
	for (size_t i = 0; i < allNodes.size(); i += 2) {
		auto singleNodeSpan = allNodes.subspan(i, 1);
		bool res = fmb->release(singleNodeSpan);
		EXPECT_TRUE(res)
			<< "Ошибка при возврате блока под индексом " << i;
		if (res)
			returnedCount++;
	}

	EXPECT_EQ(fmb->stats.nodeCount.load(), returnedCount);
	EXPECT_EQ(fmb->stats.keyDataSize.load(), returnedCount * MIN_BLOCK);

	auto failNodes = fmb->extract(MIN_BLOCK * 2);
	EXPECT_TRUE(failNodes.empty())
		<< "Аллокатор не должен отдавать 2 блока в условиях шахматной фрагментации";

	auto successNode = fmb->extract(MIN_BLOCK);
	EXPECT_EQ(successNode.size(), 1);
}

TEST_F(SmuFmbTest, EvenBlocksRelease)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GE(totalBlocks, 4) << "Нужно хотя бы 4 блока для теста";

	auto allNodes = fmb->extract(totalBlocks * MIN_BLOCK);
	ASSERT_EQ(allNodes.size(), totalBlocks);

	size_t returnedCount = 0;
	for (size_t i = 1; i < allNodes.size(); i += 2) {
		auto singleNodeSpan = allNodes.subspan(i, 1);
		bool res = fmb->release(singleNodeSpan);
		EXPECT_TRUE(res)
			<< "Ошибка при возврате блока под индексом " << i;
		if (res)
			returnedCount++;
	}

	size_t expectedCount = totalBlocks / 2;
	EXPECT_EQ(fmb->stats.nodeCount.load(), expectedCount);
	EXPECT_EQ(fmb->stats.keyDataSize.load(), expectedCount * MIN_BLOCK);

	auto failNodes = fmb->extract(MIN_BLOCK * 2);
	EXPECT_TRUE(failNodes.empty())
		<< "Не должно быть 2 непрерывных блоков при четной фрагментации";

	auto successNode = fmb->extract(MIN_BLOCK);
	EXPECT_EQ(successNode.size(), 1);
}

TEST_F(SmuFmbTest, FindBestFitGapHandling)
{
	size_t totalBlocks = fmb->getBlockCount();

	ASSERT_GE(totalBlocks, 7)
		<< "Недостаточно блоков для создания сложной топологии";

	auto allNodes = fmb->extract(totalBlocks * MIN_BLOCK);
	ASSERT_EQ(allNodes.size(), totalBlocks);
	ASSERT_EQ(fmb->stats.nodeCount.load(), 0);

	EXPECT_TRUE(fmb->release(allNodes.subspan(0, 1)));

	EXPECT_TRUE(fmb->release(allNodes.subspan(4, totalBlocks - 4)));

	EXPECT_EQ(fmb->stats.nodeCount.load(), 1 + (totalBlocks - 4));

	size_t requestedSize = MIN_BLOCK * 3;
	auto targetNodes = fmb->extract(requestedSize);

	ASSERT_EQ(targetNodes.size(), 3)
		<< "Должны были найти кусок из 3-х блоков";

	std::byte *expectedAddr = allNodes[4].keyData.data();
	EXPECT_EQ(targetNodes.front().keyData.data(), expectedAddr)
		<< "Аллокатор должен был пропустить первый блок и найти цепочку после пропуска";

	EXPECT_EQ(fmb->stats.nodeCount.load(), 1 + (totalBlocks - 4) - 3);

	auto firstBlockAgain = fmb->extract(MIN_BLOCK);
	EXPECT_EQ(firstBlockAgain.size(), 1);
	EXPECT_EQ(firstBlockAgain.front().keyData.data(),
		  allNodes[0].keyData.data());
}

TEST_F(SmuFmbTest, MemoryIntegrityAndDataPersistence)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GT(totalBlocks, 0);

	auto allNodes = fmb->extract(totalBlocks * MIN_BLOCK);
	ASSERT_EQ(allNodes.size(), totalBlocks);

	for (size_t i = 0; i < totalBlocks; ++i) {
		std::span<std::byte> blockData = allNodes[i].keyData;
		for (size_t j = 0; j < blockData.size_bytes(); ++j) {
			blockData[j] = static_cast<std::byte>((i + j) % 256);
		}
	}

	bool releaseRes = fmb->release(allNodes);
	ASSERT_TRUE(releaseRes)
		<< "Metadata corruption detected during data write!";

	auto it = fmb->begin();
	auto sentinel = fmb->end();
	size_t verifiedBlocks = 0;

	std::byte *poolStart =
		reinterpret_cast<std::byte *>(it.get()->keyData.data());

	while (it != sentinel) {
		SmuTree::Node *currentNode = it.get();
		std::span<std::byte> blockData = currentNode->keyData;

		size_t blockIdx =
			(reinterpret_cast<std::byte *>(blockData.data()) -
			 poolStart) /
			MIN_BLOCK;

		for (size_t j = 0; j < blockData.size_bytes(); ++j) {
			std::byte expected =
				static_cast<std::byte>((blockIdx + j) % 256);
			ASSERT_EQ(blockData[j], expected)
				<< "Data corruption at block " << blockIdx
				<< " offset " << j;
		}

		verifiedBlocks++;
		++it;
	}

	EXPECT_EQ(verifiedBlocks, totalBlocks);
}

TEST_F(SmuFmbTest, DoubleFreeAndForeignNode)
{
	auto node = fmb->extract(MIN_BLOCK);
	ASSERT_EQ(node.size(), 1);

	EXPECT_TRUE(fmb->release(node));

	EXPECT_FALSE(fmb->release(node));

	std::vector<SmuTree::Node> fakeNodes(1);
	EXPECT_FALSE(fmb->release(std::span<SmuTree::Node>(fakeNodes)));
}

TEST_F(SmuFmbTest, StressTestRandomSize)
{
	std::vector<std::span<SmuTree::Node> > allocations;
	size_t totalBlocks = fmb->getBlockCount();

	for (size_t i = 0; i < 5; ++i) {
		size_t sizeToReq = ((i % 3) + 1) * MIN_BLOCK;
		auto nodes = fmb->extract(sizeToReq);
		if (!nodes.empty()) {
			allocations.push_back(nodes);
		}
	}

	for (auto it = allocations.rbegin(); it != allocations.rend(); ++it) {
		EXPECT_TRUE(fmb->release(*it));
	}

	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks);
	EXPECT_EQ(fmb->stats.keyDataSize.load(), totalBlocks * MIN_BLOCK);
}

TEST_F(SmuFmbTest, NegativeInterleavedAllocation)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GE(totalBlocks, 4)
		<< "Нужно хотя бы 4 блока для теста фрагментации";

	auto allNodes = fmb->extract(totalBlocks * MIN_BLOCK);
	ASSERT_EQ(allNodes.size(), totalBlocks);
	ASSERT_EQ(fmb->stats.nodeCount.load(), 0);

	size_t returnedCount = 0;
	for (size_t i = 0; i < allNodes.size(); i += 2) {
		auto singleNodeSpan = allNodes.subspan(i, 1);
		ASSERT_TRUE(fmb->release(singleNodeSpan));
		returnedCount++;
	}

	EXPECT_EQ(fmb->stats.nodeCount.load(), returnedCount);

	auto failedNodes = fmb->extract(MIN_BLOCK * 2);

	EXPECT_TRUE(failedNodes.empty())
		<< "Ошибка! Аллокатор выдал 2 блока, хотя они разделены занятыми участками.";
	EXPECT_EQ(failedNodes.size(), 0);

	auto singleNode = fmb->extract(MIN_BLOCK);
	EXPECT_EQ(singleNode.size(), 1);
	EXPECT_FALSE(singleNode.empty());
}
