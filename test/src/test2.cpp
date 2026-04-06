#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>
#include <algorithm>
#include <functional>
#include <iomanip>

#include "smu_fmb.h"
#include "smu_tab.h"

class SmuIntegrationTest : public ::testing::Test {
    protected:
	const size_t POOL_SIZE = 4096;
	const uint16_t MIN_BLOCK = 128;
	const uint8_t ALIGN = 8;

	std::vector<std::byte> memory_pool;
	std::unique_ptr<SmuFmb> fmb;
	std::unique_ptr<SmuTab> tab;
	SmuTree::Node nilTab;

	void SetUp() override
	{
		memory_pool.assign(POOL_SIZE, std::byte{ 0 });

		fmb = std::make_unique<SmuFmb>(
			ALIGN, MIN_BLOCK, std::span<std::byte>(memory_pool));

		nilTab.color = SmuTree::Color::Black;
		nilTab.left = nilTab.right = nilTab.parent = &nilTab;
		tab = std::make_unique<SmuTab>(nilTab);
	}
};

TEST_F(SmuIntegrationTest, FullCycleAllocation)
{
	size_t blocksToReq = 3;
	size_t reqSize = MIN_BLOCK * blocksToReq;

	auto nodes = fmb->extract(reqSize);
	ASSERT_EQ(nodes.size(), blocksToReq);
	size_t initialFmbCount = fmb->stats.nodeCount.load();

	ASSERT_TRUE(tab->push(nodes));
	EXPECT_EQ(tab->stats.nodeCount.load(), blocksToReq);

	auto poppedNodes = tab->pop(nodes[0].keyData);
	ASSERT_EQ(poppedNodes.size(), blocksToReq);
	EXPECT_EQ(poppedNodes.data(),
		  nodes.data()); // Проверка, что это те же физические ноды
	EXPECT_EQ(tab->stats.nodeCount.load(), 0);

	ASSERT_TRUE(fmb->release(poppedNodes));
	EXPECT_EQ(fmb->stats.nodeCount.load(), initialFmbCount + blocksToReq);
}

TEST_F(SmuIntegrationTest, SecurityAndMiddlePointer)
{
	auto nodes = fmb->extract(MIN_BLOCK * 2);
	tab->push(nodes);

	auto middleBlock = nodes[1].keyData;
	auto failNodes = tab->pop(middleBlock);
	EXPECT_TRUE(failNodes.empty());

	std::byte *data = nodes[0].keyData.data();
	data[0] ^= static_cast<std::byte>(0xFF);

	auto corruptedPop = tab->pop(nodes[0].keyData);
	EXPECT_TRUE(corruptedPop.empty());
}

TEST_F(SmuIntegrationTest, InterleavedMigrationNoVector)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GT(totalBlocks, 0);

	size_t movedToTab = 0;

	for (size_t i = 0; i < totalBlocks; ++i) {
		auto nodeSpan = fmb->extract(MIN_BLOCK);
		ASSERT_EQ(nodeSpan.size(), 1)
			<< "Fmb должен выдать блок на итерации " << i;

		if (i % 2 == 0) {
			ASSERT_TRUE(tab->push(nodeSpan));
			movedToTab++;
		} else {
			ASSERT_TRUE(fmb->release(nodeSpan));
		}
	}

	EXPECT_EQ(tab->stats.nodeCount.load(), movedToTab);
	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks - movedToTab);
}

TEST_F(SmuIntegrationTest, InterleavedMigrationOdd)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GT(totalBlocks, 1)
		<< "Нужно хотя бы 2 блока для теста нечетности";

	size_t movedToTab = 0;

	for (size_t i = 0; i < totalBlocks; ++i) {
		auto nodeSpan = fmb->extract(MIN_BLOCK);
		ASSERT_EQ(nodeSpan.size(), 1);

		if (i % 2 != 0) {
			ASSERT_TRUE(tab->push(nodeSpan));
			movedToTab++;
		} else {
			ASSERT_TRUE(fmb->release(nodeSpan));
		}
	}

	EXPECT_EQ(tab->stats.nodeCount.load(), movedToTab);
	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks - movedToTab);
}

TEST_F(SmuIntegrationTest, IntegrityThroughTables)
{
	size_t totalBlocks = fmb->getBlockCount();
	ASSERT_GT(totalBlocks, 1)
		<< "Нужно хотя бы 2 блока для теста нечетности";

	size_t movedToTab = 0;

	for (size_t i = 0; i < totalBlocks; ++i) {
		auto nodeSpan = fmb->extract(MIN_BLOCK);
		ASSERT_EQ(nodeSpan.size(), 1);

		size_t blockIdx =
			(nodeSpan[0].keyData.data() - fmb->getDataStart()) /
			MIN_BLOCK;

		if (blockIdx % 2 != 0) {
			ASSERT_TRUE(tab->push(nodeSpan));
			movedToTab++;

			std::byte *data = nodeSpan[0].keyData.data();
			for (size_t j = sizeof(SmuTab::MetaHead); j < MIN_BLOCK;
			     ++j) {
				data[j] = static_cast<std::byte>(
					(blockIdx + j) & 0xFF);
			}
		} else {
			ASSERT_TRUE(fmb->release(nodeSpan));
		}
	}

	EXPECT_EQ(tab->stats.nodeCount.load(), movedToTab);
	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks - movedToTab);

	auto it = tab->begin();
	auto sentinel = tab->end();
	size_t checkedInTab = 0;

	while (it != sentinel) {
		SmuTree::Node *node = it.get();
		std::byte *data = node->keyData.data();

		size_t blockIdx = (data - fmb->getDataStart()) / MIN_BLOCK;

		for (size_t j = sizeof(SmuTab::MetaHead); j < MIN_BLOCK; ++j) {
			std::byte expected =
				static_cast<std::byte>((blockIdx + j) & 0xFF);
			ASSERT_EQ(data[j], expected)
				<< "Данные повреждены в Tab! Физический блок: "
				<< blockIdx << ", Смещение байта: " << j;
		}

		checkedInTab++;
		++it;
	}

	EXPECT_EQ(checkedInTab, movedToTab);
}

TEST_F(SmuIntegrationTest, MetaHeadValidation)
{
	size_t totalBlocks = fmb->getBlockCount();

	size_t currentStep = 1;
	size_t allocatedBlocks = 0;

	while (allocatedBlocks + currentStep <= totalBlocks) {
		size_t reqSize = currentStep * MIN_BLOCK;
		auto nodes = fmb->extract(reqSize);
		ASSERT_FALSE(nodes.empty());

		uintptr_t expectedNodeAddr =
			reinterpret_cast<uintptr_t>(&nodes[0]);
		size_t expectedCount = nodes.size();

		ASSERT_TRUE(tab->push(nodes));

		std::byte *data = nodes[0].keyData.data();
		auto *head = reinterpret_cast<SmuTab::MetaHead *>(data);

		EXPECT_EQ(head->node, expectedNodeAddr)
			<< "Ошибка в MetaHead: неверный адрес Node для аллокации размером "
			<< currentStep;

		EXPECT_EQ(head->nodeCount, expectedCount)
			<< "Ошибка в MetaHead: неверное количество блоков";

		EXPECT_EQ(head->xorMeta, head->node ^ head->nodeCount)
			<< "Ошибка в MetaHead: XOR-сумма не совпадает";

		allocatedBlocks += currentStep;
		currentStep++;
	}

	while (tab->stats.nodeCount.load() > 0) {
		auto it = tab->begin();
		auto popped = tab->pop(it.get()->keyData);
		ASSERT_FALSE(popped.empty());
		fmb->release(popped);
	}

	EXPECT_EQ(tab->stats.nodeCount.load(), 0);
	EXPECT_EQ(fmb->stats.nodeCount.load(), totalBlocks);
}

TEST_F(SmuIntegrationTest, SlidingWindowStress)
{
	size_t total = fmb->getBlockCount();

	for (size_t i = 0; i < total - 3; ++i) {
		auto nodes = fmb->extract(MIN_BLOCK * 3);
		ASSERT_EQ(nodes.size(), 3);

		ASSERT_TRUE(tab->push(nodes));

		auto popped = tab->pop(nodes.front().keyData);
		ASSERT_EQ(popped.size(), 3);

		ASSERT_TRUE(fmb->release(popped));

		auto shift = fmb->extract(MIN_BLOCK);
	}
}

TEST_F(SmuIntegrationTest, AdvancedCorruption)
{
	auto nodes = fmb->extract(MIN_BLOCK * 2);
	ASSERT_FALSE(nodes.empty());

	ASSERT_TRUE(tab->push(nodes));

	std::byte *data = nodes[0].keyData.data();
	auto *head = reinterpret_cast<SmuTab::MetaHead *>(data);

	size_t fakeCount = 1000;
	head->nodeCount = fakeCount;
	head->xorMeta = head->node ^ fakeCount;

	auto popped = tab->pop(nodes[0].keyData);

	EXPECT_TRUE(popped.empty())
		<< "System should detect fake nodeCount via remove() failure in the middle of a chain";
}
