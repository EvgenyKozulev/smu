#include <gtest/gtest.h>
#include <cstring>
#include <array>
#include <new>
#include <algorithm>
#include <functional>
#include <iomanip>

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

	using testCollisionFunc =
		std::function<void(const Node *node, std::span<std::byte> &key)>;

	testCollisionFunc collisionInvoke;

	int compare(std::span<std::byte> a, std::span<std::byte> b) override
	{
		if (a.data() < b.data())
			return -1;
		if (a.data() > b.data())
			return 1;
		return 0;
	}

	void collision(const Node *node, std::span<std::byte> &key) override
	{
		if (collisionInvoke)
			std::invoke(collisionInvoke, node, key);
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

TEST_F(SmuTreeTest, FillMemoryCheckBalance)
{
	std::vector<Node *> insertedNodes;
	int count = 0;

	while (true) {
		auto data = allocateData(sizeof(int));
		if (data.empty())
			break;

		std::memcpy(data.data(), &count, sizeof(int));

		Node *n = allocateNode();
		if (!n)
			break;

		if (insert(n, data)) {
			insertedNodes.push_back(n);
			count++;

			ASSERT_EQ(stats.redCount.load() +
					  stats.blackCount.load(),
				  stats.nodeCount.load())
				<< "Color stats mismatch after insert #"
				<< count;
		} else {
			break;
		}
	}

	EXPECT_GT(count, 0);

	while (!insertedNodes.empty()) {
		Node *n = insertedNodes.back();
		insertedNodes.pop_back();

		ASSERT_TRUE(remove(n));

		ASSERT_EQ(stats.redCount.load() + stats.blackCount.load(),
			  stats.nodeCount.load())
			<< "Color stats mismatch after remove. Remaining: "
			<< insertedNodes.size();
	}

	EXPECT_EQ(stats.nodeCount.load(), 0);
	EXPECT_EQ(stats.redCount.load(), 0);
	EXPECT_EQ(stats.blackCount.load(), 0);
}

TEST_F(SmuTreeTest, FillMemoryRemoveForward)
{
	std::vector<Node *> insertedNodes;
	int count = 0;

	while (true) {
		auto data = allocateData(sizeof(int));
		if (data.empty())
			break;

		std::memcpy(data.data(), &count, sizeof(int));

		Node *n = allocateNode();
		if (!n)
			break;

		if (insert(n, data)) {
			insertedNodes.push_back(n);
			count++;
		} else {
			break;
		}
	}

	ASSERT_GT(count, 0);

	for (size_t i = 0; i < insertedNodes.size(); ++i) {
		Node *n = insertedNodes[i];

		ASSERT_TRUE(remove(n))
			<< "Failed to remove node at index " << i;
		ASSERT_EQ(stats.redCount.load() + stats.blackCount.load(),
			  stats.nodeCount.load())
			<< "Color stats mismatch after forward remove at index: "
			<< i;
	}

	EXPECT_EQ(stats.nodeCount.load(), 0);
	EXPECT_EQ(stats.redCount.load(), 0);
	EXPECT_EQ(stats.blackCount.load(), 0);
}

TEST_F(SmuTreeTest, FillMemoryRandomAccess)
{
	std::vector<Node *> insertedNodes;
	int val = 0;

	while (true) {
		auto data = allocateData(sizeof(int));
		if (data.empty())
			break;

		std::memcpy(data.data(), &val, sizeof(int));
		Node *n = allocateNode();
		if (!n)
			break;

		if (insert(n, data)) {
			insertedNodes.push_back(n);
			val++;
		} else {
			break;
		}
	}

	size_t totalInserted = insertedNodes.size();
	ASSERT_GT(totalInserted, 5)
		<< "Too few nodes inserted, check memory limits";

	size_t midIndex = totalInserted / 2;

	for (int i = 0; i < 3; ++i) {
		if (insertedNodes.empty())
			break;

		Node *target = insertedNodes[midIndex];

		ASSERT_TRUE(remove(target))
			<< "Failed to remove node from middle at step " << i;

		insertedNodes.erase(insertedNodes.begin() + midIndex);

		if (midIndex > 0)
			midIndex--;

		ASSERT_EQ(stats.redCount.load() + stats.blackCount.load(),
			  stats.nodeCount.load())
			<< "Color stats mismatch after middle removal";
	}

	for (Node *n : insertedNodes) {
		ASSERT_TRUE(remove(n));
	}

	EXPECT_EQ(stats.nodeCount.load(), 0);
	EXPECT_EQ(stats.redCount.load(), 0);
	EXPECT_EQ(stats.blackCount.load(), 0);
}

TEST_F(SmuTreeTest, IteratorMemoryAddressOrder)
{
	std::vector<void *> allocatedAddresses;
	std::array<int, 5> values = { 10, 20, 30, 40, 50 };

	for (int v : values) {
		auto data = allocateData(sizeof(int));
		std::memcpy(data.data(), &v, sizeof(int));
		allocatedAddresses.push_back(data.data());
		ASSERT_TRUE(insert(allocateNode(), data));
	}

	std::sort(allocatedAddresses.begin(), allocatedAddresses.end());

	int count = 0;
	for (auto it = begin(); it != end(); ++it) {
		EXPECT_EQ(it->keyData.data(), allocatedAddresses[count])
			<< "Address mismatch at step " << count;
		count++;
	}
	EXPECT_EQ(count, allocatedAddresses.size());
}

TEST_F(SmuTreeTest, IteratorEmptyAndSingle)
{
	EXPECT_EQ(begin(), end());

	insertInt(999);
	auto it = begin();
	ASSERT_NE(it, end());

	int val;
	std::memcpy(&val, it->keyData.data(), sizeof(int));
	EXPECT_EQ(val, 999);

	++it;
	EXPECT_EQ(it, end());
}

TEST_F(SmuTreeTest, CleanupWithIteratorNoVector)
{
	for (int i = 0; i < 20; ++i) {
		insertInt(i);
	}
	ASSERT_GT(stats.nodeCount.load(), 0);

	while (begin() != end()) {
		Node *target = begin().get();
		ASSERT_TRUE(remove(target));
	}

	EXPECT_EQ(stats.nodeCount.load(), 0);
	EXPECT_EQ(stats.redCount.load(), 0);
	EXPECT_EQ(stats.blackCount.load(), 0);
}

TEST_F(SmuTreeTest, CollisionReturnFalse)
{
	auto data = allocateData(8);
	ASSERT_TRUE(insert(allocateNode(), data));

	collisionInvoke = nullptr;
	EXPECT_FALSE(insert(allocateNode(), data));
}

TEST_F(SmuTreeTest, CollisionShiftAndSucceed)
{
	auto data = allocateData(8); // Допустим адрес 0x1000
	ASSERT_TRUE(insert(allocateNode(), data));

	collisionInvoke = [](const Node *node, std::span<std::byte> &key) {
		key = std::span<std::byte>(key.data() + 1, key.size_bytes());
	};

	EXPECT_TRUE(insert(allocateNode(), data));
	EXPECT_EQ(stats.nodeCount.load(), 2);
}

TEST_F(SmuTreeTest, CollisionDoubleConflict)
{
	auto addr1 = allocateData(8);
	auto addr2 = std::span<std::byte>(addr1.data() + 1, 8);

	ASSERT_TRUE(insert(allocateNode(), addr1)); // Заняли 0x1000
	ASSERT_TRUE(insert(allocateNode(), addr2)); // Заняли 0x1001

	collisionInvoke = [](const Node *node, std::span<std::byte> &key) {
		key = std::span<std::byte>(key.data() + 1, key.size_bytes());
	};

	EXPECT_FALSE(insert(allocateNode(), addr1));
	EXPECT_EQ(stats.nodeCount.load(), 2);
}

TEST_F(SmuTreeTest, RemoveInvalidNodes)
{
	EXPECT_FALSE(remove(static_cast<Node *>(nullptr)));

	auto data = allocateData(8);
	EXPECT_FALSE(remove(data));

	Node fakeNode;
	fakeNode.keyData = data;
	EXPECT_FALSE(remove(&fakeNode));

	EXPECT_EQ(stats.nodeCount.load(), 0);
}

TEST_F(SmuTreeTest, SafetyInvalidRemove)
{
	EXPECT_FALSE(remove(static_cast<Node *>(nullptr)));

	auto data = allocateData(4);
	EXPECT_FALSE(remove(data));

	Node foreignNode;
	foreignNode.keyData = data;
	EXPECT_FALSE(remove(&foreignNode));

	insertInt(123);
	Node *n = begin().get();
	ASSERT_TRUE(remove(n));
	EXPECT_FALSE(remove(n));

	EXPECT_EQ(stats.nodeCount.load(), 0);
}

TEST_F(SmuTreeTest, MemoryEfficiencyLimit)
{
	size_t initialDataOffset = dataOffset;
	size_t initialNodesOffset = nodesOffset;
	int count = 0;

	while (true) {
		auto data = allocateData(1);
		if (data.empty())
			break;

		Node *n = allocateNode();
		if (!n) {
			dataOffset += 1;
			break;
		}

		if (insert(n, data)) {
			count++;
		}
	}

	size_t usedNodesSpace = nodesOffset - initialNodesOffset;
	size_t usedDataSpace = initialDataOffset - dataOffset;
	size_t totalUsed = usedNodesSpace + usedDataSpace;

	std::cout << "[ STATS    ] Total nodes inserted: " << count
		  << std::endl;
	std::cout << "[ STATS    ] Memory used for Nodes: " << usedNodesSpace
		  << " bytes" << std::endl;
	std::cout << "[ STATS    ] Memory used for Data:  " << usedDataSpace
		  << " bytes" << std::endl;
	std::cout << "[ STATS    ] Efficiency (Data/Total): " << (std::fixed)
		  << (std::setprecision(2))
		  << (static_cast<double>(usedDataSpace) / totalUsed) * 100
		  << "%" << std::endl;

	size_t iterCount = 0;
	for (auto it = begin(); it != end(); ++it)
		iterCount++;
	EXPECT_EQ(iterCount, count);
}
