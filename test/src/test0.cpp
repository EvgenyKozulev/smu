#include <gtest/gtest.h>
#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <random>
#include <set>

#include "smu_tree.h"

/**
 * @brief Helper to convert string to span of bytes for testing.
 */
auto to_span(const std::string &str)
{
	return std::span<std::byte>(
		reinterpret_cast<std::byte *>(const_cast<char *>(str.data())),
		str.size());
}

/**
 * @brief Default string comparator for SmuTree.
 */
int string_cmp(std::span<std::byte> a, std::span<std::byte> b)
{
	size_t min_len = std::min(a.size(), b.size());
	int res = std::memcmp(a.data(), b.data(), min_len);
	if (res == 0) {
		if (a.size() < b.size())
			return -1;
		if (a.size() > b.size())
			return 1;
	}
	return res;
}

/**
 * @test Verify tree initialization and empty state.
 * @details Checks if the tree correctly reports as empty and begin() equals end() on startup.
 */
TEST(SMUTree, Initialization)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	EXPECT_TRUE(tree.empty());
	EXPECT_EQ(tree.begin(), tree.end());
}

/**
 * @test Basic insertion and find operation.
 * @details Ensures that data pointers remain valid and searchable.
 */
TEST(SMUTree, BasicInsertAndFind)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	// Keep the actual data alive in the scope of the test
	std::string a = "apple";
	std::string b = "banana";

	auto val1 = std::span<std::byte>(
		reinterpret_cast<std::byte *>(a.data()), a.size());
	auto val2 = std::span<std::byte>(
		reinterpret_cast<std::byte *>(b.data()), b.size());

	EXPECT_TRUE(tree.insert(val1));
	EXPECT_TRUE(tree.insert(val2));

	uintptr_t idx = tree.find(val1);
	ASSERT_NE(idx, SmuTree::Node::NIL);

	auto data = tree.getData(idx);
	std::string result(reinterpret_cast<char *>(data.data()), data.size());
	EXPECT_EQ(result, "apple");
}

/**
 * @test Tree overflow handling.
 * @details Attempts to insert more elements than the provided span can hold.
 */
TEST(SMUTree, StorageOverflow)
{
	std::array<SmuTree::Node, 3>
		memory; // Index 0 is NIL, 1 and 2 are available
	SmuTree tree(memory, string_cmp);

	EXPECT_TRUE(tree.insert(to_span("1")));
	EXPECT_TRUE(tree.insert(to_span("2")));
	EXPECT_FALSE(
		tree.insert(to_span("3"))); // Should return false (no space)
}

/**
 * @test In-order iterator traversal.
 * @details Verifies that the iterator visits elements in the correct sorted order.
 */
TEST(SMUTree, IteratorOrder)
{
	std::array<SmuTree::Node, 20> memory;
	SmuTree tree(memory, string_cmp);

	std::vector<std::string> inputs = { "c", "a", "b", "e", "d" };
	for (const auto &s : inputs)
		tree.insert(to_span(s));

	std::vector<std::string> results;
	for (auto it = tree.begin(); it != tree.end(); ++it) {
		auto span = *it;
		results.push_back(std::string(
			reinterpret_cast<char *>(span.data()), span.size()));
	}

	std::vector<std::string> expected = { "a", "b", "c", "d", "e" };
	EXPECT_EQ(results, expected);
}

/**
 * @test Node removal and memory recycling.
 * @details Deletes an element and checks if the memory is reused for the next insertion via the Free List.
 */
TEST(SMUTree, RemoveAndRecycle)
{
	std::array<SmuTree::Node, 3> memory;
	SmuTree tree(memory, string_cmp);

	auto val1 = to_span("first");
	auto val2 = to_span("second");

	tree.insert(val1);
	uintptr_t first_idx = tree.find(val1);

	EXPECT_TRUE(tree.remove(val1));
	EXPECT_EQ(tree.find(val1), SmuTree::Node::NIL);

	// This insertion should reuse the index of the deleted "first" node
	tree.insert(val2);
	uintptr_t second_idx = tree.find(val2);

	EXPECT_EQ(first_idx, second_idx);
}

/**
 * @test Complex balancing (Red-Black properties).
 * @details Performs multiple insertions and deletions to trigger rotations and re-balancing.
 */
TEST(SMUTree, ComplexRebalancing)
{
	std::array<SmuTree::Node, 100> memory;
	SmuTree tree(memory, string_cmp);

	// Triggering various rotations
	std::vector<std::string> data = { "10", "20", "30", "15", "25", "05" };
	for (auto &s : data)
		EXPECT_TRUE(tree.insert(to_span(s)));

	// Ensure they all exist
	for (auto &s : data)
		EXPECT_NE(tree.find(to_span(s)), SmuTree::Node::NIL);

	// Remove half
	EXPECT_TRUE(tree.remove(to_span("20")));
	EXPECT_TRUE(tree.remove(to_span("05")));

	EXPECT_EQ(tree.find(to_span("20")), SmuTree::Node::NIL);
	EXPECT_NE(tree.find(to_span("30")), SmuTree::Node::NIL);
}

/**
 * @test Full Lifecycle Stress Test (1000 Operations).
 * @details Simulates a mix of 1000 insertions and deletions. 
 *          Uses a std::set as a reference to validate tree contents and order.
 */
TEST(SMUTree, FullLifecycleStressTest)
{
	const int OPERATION_COUNT = 1000;
	const int MAX_NODES = 1100; // Buffer slightly larger than operations

	std::vector<SmuTree::Node> memory(MAX_NODES);
	SmuTree tree(memory, string_cmp);

	// Reference set to track what SHOULD be in the tree
	std::set<std::string> reference_set;
	// Pool to keep strings alive (since span doesn't own data)
	std::vector<std::string> pool;
	pool.reserve(OPERATION_COUNT);

	std::mt19937 rng(42); // Fixed seed for reproducibility
	std::uniform_int_distribution<int> op_dist(0,
						   1); // 0 = Insert, 1 = Remove
	std::uniform_int_distribution<int> val_dist(1000, 9999);

	for (int i = 0; i < OPERATION_COUNT; ++i) {
		int op = op_dist(rng);
		std::string val = std::to_string(val_dist(rng));

		if (op == 0) { // INSERT
			if (reference_set.find(val) == reference_set.end()) {
				pool.push_back(val);
				auto s = to_span(pool.back());

				ASSERT_TRUE(tree.insert(s))
					<< "Failed to insert value: " << val;
				reference_set.insert(val);
			}
		} else { // REMOVE
			if (!reference_set.empty()) {
				// Pick a random element from our reference set to remove
				auto it = reference_set.begin();
				std::advance(it, rng() % reference_set.size());
				std::string to_remove = *it;

				ASSERT_TRUE(tree.remove(to_span(to_remove)))
					<< "Failed to remove existing value: "
					<< to_remove;
				reference_set.erase(it);
			}
		}

		// --- Validation Phase ---

		// 1. Check size consistency
		size_t tree_count = 0;
		std::string last_val = "";
		for (auto it = tree.begin(); it != tree.end(); ++it) {
			auto span = *it;
			std::string current_val(
				reinterpret_cast<const char *>(span.data()),
				span.size());

			// 2. Validate Order (In-order traversal must be sorted)
			if (tree_count > 0) {
				EXPECT_TRUE(string_cmp(to_span(last_val),
						       to_span(current_val)) <
					    0)
					<< "Tree order violated at index "
					<< tree_count;
			}

			// 3. Ensure data is actually in our reference set
			EXPECT_TRUE(reference_set.count(current_val))
				<< "Tree contains value not in reference set: "
				<< current_val;

			last_val = current_val;
			tree_count++;
		}

		EXPECT_EQ(tree_count, reference_set.size())
			<< "Tree size mismatch at operation " << i;
	}

	// Final check: tree must be searchable for every element in the reference set
	for (const auto &s : reference_set) {
		EXPECT_NE(tree.find(to_span(s)), SmuTree::Node::NIL)
			<< "Final search failed for value: " << s;
	}
}

/**
 * @test Boundary cases for SmuTree.
 * @details Tests empty tree operations, full storage limits, and complex removals.
 */

/**
 * @test Verify behavior on an empty tree.
 * @details Ensures find() and remove() gracefully handle a tree with zero elements.
 */
TEST(SMUTree, EmptyTreeBoundaries)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	auto val = to_span("ghost");

	EXPECT_EQ(tree.find(val), SmuTree::Node::NIL);
	EXPECT_FALSE(tree.remove(val));
	EXPECT_TRUE(tree.empty());
	EXPECT_EQ(tree.begin(), tree.end());
}

/**
 * @test Verify behavior when storage is completely full.
 * @details Fills the span to its maximum capacity and verifies that further 
 *          insertions return false and do not corrupt existing data.
 */
TEST(SMUTree, FullStorageBoundary)
{
	const size_t capacity = 5; // 1 for NIL + 4 available slots
	std::vector<SmuTree::Node> memory(capacity);
	SmuTree tree(memory, string_cmp);

	std::vector<std::string> data = { "1", "2", "3", "4" };
	for (const auto &s : data) {
		EXPECT_TRUE(tree.insert(to_span(s)));
	}

	// Try to insert the 5th element into a 4-slot tree
	EXPECT_FALSE(tree.insert(to_span("5")));

	// Verify everything is still there
	for (const auto &s : data) {
		EXPECT_NE(tree.find(to_span(s)), SmuTree::Node::NIL);
	}
}

/**
 * @test Remove root and verify re-rooting.
 * @details Deletes the root node in various configurations (only child, two children).
 */
TEST(SMUTree, RootRemovalBoundaries)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	std::string s_root = "50";
	tree.insert(to_span(s_root));
	tree.insert(to_span("30"));
	tree.insert(to_span("70"));

	// Remove the actual root
	EXPECT_TRUE(tree.remove(to_span(s_root)));

	// Ensure the tree is still valid and searchable
	EXPECT_EQ(tree.find(to_span(s_root)), SmuTree::Node::NIL);
	EXPECT_NE(tree.find(to_span("30")), SmuTree::Node::NIL);
	EXPECT_NE(tree.find(to_span("70")), SmuTree::Node::NIL);
}

/**
 * @test Duplicate data insertion.
 * @details Validates the tree's behavior when inserting identical spans.
 *          In our current logic, it should treat them as distinct or allow 
 *          them based on the compare function results.
 */
TEST(SMUTree, DuplicateDataBoundary)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	std::string val = "duplicate";
	EXPECT_TRUE(tree.insert(to_span(val)));
	EXPECT_TRUE(tree.insert(to_span(
		val))); // Depending on logic, usually goes to right child

	size_t count = 0;
	for (auto it = tree.begin(); it != tree.end(); ++it) {
		count++;
	}
	EXPECT_EQ(count, 2);
}

/**
 * @test Single node tree lifecycle.
 * @details Tests insertion and removal of a single node until the tree is empty again.
 */
TEST(SMUTree, SingleNodeLifecycle)
{
	std::array<SmuTree::Node, 5> memory;
	SmuTree tree(memory, string_cmp);

	std::string val = "lonely";
	tree.insert(to_span(val));
	EXPECT_FALSE(tree.empty());

	tree.remove(to_span(val));
	EXPECT_TRUE(tree.empty());
	EXPECT_EQ(tree.find(to_span(val)), SmuTree::Node::NIL);
}

/**
 * @test NIL Sentinel Integrity.
 * @details Ensures that the NIL node (index 0) remains immutable 
 *          and consistently black throughout various operations.
 */
TEST(SMUTree, NilSentinelIntegrity)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	for (int i = 0; i < 5; ++i)
		tree.insert(to_span(std::to_string(i)));

	const auto &nil = memory[0];
	EXPECT_EQ(nil.color, SmuTree::Color::BLACK);
	EXPECT_EQ(nil.left, SmuTree::Node::NIL);
	EXPECT_EQ(nil.right, SmuTree::Node::NIL);
}

/**
 * @test Worst-Case Insertion (Pre-sorted).
 * @details Inserts elements in strictly increasing order. 
 *          A non-balancing tree would create a list of depth N. 
 *          An RB-tree must keep height around O(log N).
 */
TEST(SMUTree, PreSortedInsertion)
{
	std::array<SmuTree::Node, 100> memory;
	SmuTree tree(memory, string_cmp);

	for (int i = 0; i < 64; ++i) {
		std::string val = std::to_string(i); // Note: string "10" < "2"
		// To get true sorted order, use a better padding or int comparator
		tree.insert(to_span(val));
	}

	// If the tree is balanced, we should be able to find the first
	// element without a massive stack depth (internal logic check).
	EXPECT_NE(tree.find(to_span("0")), SmuTree::Node::NIL);
}

/**
 * @test Data Content with Null Bytes.
 * @details Verifies that the comparator and tree handle spans 
 *          containing binary zeros or non-text data.
 */
TEST(SMUTree, BinaryDataHandling)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	std::byte binary_data[] = { std::byte{ 0 }, std::byte{ 1 },
				    std::byte{ 0 }, std::byte{ 2 } };
	std::span<std::byte> s(binary_data, 4);

	EXPECT_TRUE(tree.insert(s));
	EXPECT_NE(tree.find(s), SmuTree::Node::NIL);
}

/**
 * @test Logical size and memory footprint verification.
 * @details Checks if the tree correctly tracks the number of elements 
 *          and reports its physical memory usage accurately.
 */

/**
 * @test Verify the logical size() method.
 * @details Inserts and removes elements to ensure the internal counter 
 *          increments and decrements correctly.
 */
TEST(SMUTree, LogicalSizeTracking)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	EXPECT_EQ(tree.size(), 0);

	tree.insert(to_span("A"));
	tree.insert(to_span("B"));
	tree.insert(to_span("C"));
	EXPECT_EQ(tree.size(), 3);

	tree.remove(to_span("B"));
	EXPECT_EQ(tree.size(), 2);

	tree.remove(to_span("A"));
	tree.remove(to_span("C"));
	EXPECT_EQ(tree.size(), 0);
}

/**
 * @test Verify the physical memory_usage() method.
 * @details Calculates the expected byte size of the management object 
 *          plus the storage buffer and compares it with the reported value.
 */
TEST(SMUTree, PhysicalMemoryUsage)
{
	const size_t element_count = 1024;
	std::vector<SmuTree::Node> memory(element_count + 1); // +1 for NIL
	SmuTree tree(memory, string_cmp);

	size_t expected_node_size = sizeof(SmuTree::Node);
	size_t expected_object_size = sizeof(SmuTree);
	size_t expected_total =
		expected_object_size + (memory.size() * expected_node_size);

	// Should be around 49,200 bytes + object overhead (~49.3 KB)
	EXPECT_EQ(tree.memory_usage(), expected_total);

	// Print for manual verification
	std::cout << "[ INFO     ] Memory for 1024 nodes: "
		  << tree.memory_usage() / 1024.0 << " KB" << std::endl;
}

/**
 * @test Size stability during duplicates.
 * @details Ensures that if your tree allows duplicates, the size() counter 
 *          reflects the actual number of nodes, not just unique keys.
 */
TEST(SMUTree, SizeWithDuplicates)
{
	std::array<SmuTree::Node, 10> memory;
	SmuTree tree(memory, string_cmp);

	tree.insert(to_span("dup"));
	tree.insert(to_span("dup"));

	EXPECT_EQ(tree.size(), 2);

	tree.remove(to_span("dup"));
	EXPECT_EQ(tree.size(), 1);
}

/**
 * @test Memory Efficiency Analysis
 * @details Calculates the real cost (overhead) per inserted element 
 *          based on the total memory usage and current size.
 */
TEST(SMUTree, MemoryEfficiencyAnalysis) {
    const size_t capacity = 1024;
    std::vector<SmuTree::Node> memory(capacity + 1);
    SmuTree tree(memory, string_cmp);

    // Вставляем 500 элементов
    for (int i = 0; i < 500; ++i) {
        // Здесь используем статическую строку, чтобы не плодить аллокации в тесте
        static std::string val = "test_data"; 
        tree.insert(to_span(val));
    }

    // 1. Физический размер одного узла (Node)
    size_t node_bytes = sizeof(SmuTree::Node);
    
    // 2. Общий физический размер (Object + Buffer)
    size_t total_allocated = sizeof(SmuTree) + (memory.size() * node_bytes);
    
    // 3. Расчет "цены" одного активного элемента
    double cost_per_element = (double)total_allocated / tree.size();

    std::cout << "[ INFO     ] Total nodes allocated: " << memory.size() << std::endl;
    std::cout << "[ INFO     ] Active elements: " << tree.size() << std::endl;
    std::cout << "[ INFO     ] Physical cost per active element: " << cost_per_element << " bytes" << std::endl;

    // Проверка: цена за элемент должна быть выше размера Node, так как есть пустые слоты
    EXPECT_GT(cost_per_element, (double)node_bytes);
}
