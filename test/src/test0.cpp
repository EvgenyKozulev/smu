#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>

#include "smu_tree.h"

struct SmuTreeStorage {
	std::array<std::byte, 2048> memory;
	size_t nodesOffset = 0;
	size_t dataOffset = 2048;

	SmuTree::Node *allocateNode()
	{
		if (nodesOffset + sizeof(SmuTree::Node) > dataOffset)
			return nullptr;
		auto *n = new (&memory[nodesOffset]) SmuTree::Node();
		nodesOffset += sizeof(SmuTree::Node);
		return n;
	}
};

class SmuTreeTest : public ::testing::Test,
		    protected SmuTreeStorage,
		    public SmuTree {
    public:
	SmuTreeTest()
		: SmuTree(*allocateNode())
	{
	}

	int compare(std::span<std::byte> a, std::span<std::byte> b) override
	{
		if (a.data() < b.data())
			return -1;
		if (a.data() > b.data())
			return 1;
		return 0;
	}

	void collision(const Node *node, std::span<std::byte> key) override
	{
	}

	std::span<std::byte> allocateData(size_t size)
	{
		if (nodesOffset + size > dataOffset)
			return {};
		dataOffset -= size;
		return std::span<std::byte>(&memory[dataOffset], size);
	}

	bool insertInt(int value)
	{
		auto data = allocateData(sizeof(int));
		if (data.empty())
			return false;

		std::memcpy(data.data(), &value, sizeof(int));

		Node *n = allocateNode();
		if (!n)
			return false;

		return insert(n, data);
	}

	using SmuTreeStorage::allocateNode;
	using SmuTreeStorage::nodesOffset;
	using SmuTreeStorage::dataOffset;
	using SmuTreeStorage::memory;
};

TEST_F(SmuTreeTest, DualStackMemoryTest)
{
	EXPECT_TRUE(insertInt(100));
	EXPECT_TRUE(insertInt(200));

	EXPECT_EQ(stats.nodeCount.load(), 2);
	EXPECT_EQ(nodesOffset, sizeof(Node) * 3);
	EXPECT_EQ(dataOffset, 2048 - (sizeof(int) * 2));
}

TEST_F(SmuTreeTest, PointerOrdering)
{
	auto data1 = allocateData(10);
	auto data2 = allocateData(10);

	EXPECT_LT(data2.data(), data1.data());

	EXPECT_TRUE(insert(allocateNode(), data1));
	EXPECT_TRUE(insert(allocateNode(), data2));

	Node *found1 = find(data1);
	Node *found2 = find(data2);

	ASSERT_NE(found1, nullptr);
	ASSERT_NE(found2, nullptr);
	EXPECT_EQ(found1->keyData.data(), data1.data());
	EXPECT_EQ(found2->keyData.data(), data2.data());
}

TEST_F(SmuTreeTest, RemoveTest)
{
	int val = 500;
	auto data = allocateData(sizeof(int));
	std::memcpy(data.data(), &val, sizeof(int));

	Node *n = allocateNode();
	insert(n, data);

	EXPECT_EQ(stats.nodeCount.load(), 1);
	EXPECT_TRUE(remove(n));
	EXPECT_EQ(stats.nodeCount.load(), 0);
	EXPECT_EQ(find(data), nullptr);
}

TEST_F(SmuTreeTest, FillMemoryUntilFull)
{
	int count = 0;
	std::span<std::byte> lastInsertedData;

	while (true) {
		auto data = allocateData(sizeof(int));
		if (data.empty())
			break;

		std::memcpy(data.data(), &count, sizeof(int));

		Node *n = allocateNode();
		if (!n)
			break;

		if (insert(n, data)) {
			lastInsertedData = data;
			count++;
		} else {
			break;
		}
	}

	EXPECT_GT(count, 0);
	EXPECT_NE(find(lastInsertedData), nullptr);
}
