#pragma once
#include <span>
#include <cstdint>
#include <functional>

/**
 * @class SmuTree
 * @brief A Red-Black Tree implementation using a static buffer (std::span).
 * 
 * This class provides a balanced binary search tree that operates within a pre-allocated 
 * memory pool. It uses indices (uintptr_t) instead of raw pointers to ensure 
 * memory relocatability and to avoid dynamic heap allocations during runtime.
 */
class SmuTree {
    public:
	/** @brief Node color constants as per Red-Black Tree properties. */
	enum class Color : uint8_t { RED, BLACK };

#pragma pack(push, 1)
	/**
     * @struct Node
     * @brief Internal tree node structure.
     */
	struct Node {
		std::span<std::byte>
			data; ///< Pointer and size of the stored data
		Color color; ///< Node color (Red or Black)
		uintptr_t left = 0; ///< Index of the left child
		uintptr_t right = 0; ///< Index of the right child
		uintptr_t parent = 0; ///< Index of the parent node

		/** @brief Reserved index representing a null pointer (Sentinel/NIL). */
		static constexpr uintptr_t NIL = 0;

		/** @brief Default constructor initializing a Red node with NIL links. */
		Node();
	};
#pragma pack(pop)

	/** 
     * @brief Comparison function type. 
     * @return Negative if a < b, zero if a == b, positive if a > b.
     */
	using CompareFunc = std::function<int(std::span<std::byte> a,
					      std::span<std::byte> b)>;

	/**
     * @struct Iterator
     * @brief In-order traversal iterator.
     */
	struct Iterator {
		const SmuTree *tree;
		uintptr_t current;

		std::span<std::byte> operator*() const;
		bool operator!=(const Iterator &other) const;
		bool operator==(const Iterator &other) const;
		Iterator &operator++();
	};

    private:
	std::span<Node> storage; ///< The memory pool provided during construction
	uintptr_t root = Node::NIL; ///< Index of the root node
	uintptr_t next_free = 1; ///< Next available index for linear allocation
	uintptr_t free_head =
		Node::NIL; ///< Head of the free list for recycled indices
	CompareFunc compare; ///< Injected comparison logic

	Node &get(uintptr_t idx);
	const Node &get(uintptr_t idx) const;

	// Balancing and structural helpers
	void rotateLeft(uintptr_t x);
	void rotateRight(uintptr_t y);
	void fixViolation(uintptr_t z);
	void transplant(uintptr_t u, uintptr_t v);
	void fixDeletion(uintptr_t x);

	uintptr_t minimum(uintptr_t n) const;
	uintptr_t successor(uintptr_t x) const;

    public:
	/**
     * @brief Constructs the tree using an external memory buffer.
     * @param mem A span of Nodes to be used as storage. Element at index 0 is reserved for NIL.
     * @param comp A function to compare data spans.
     */
	SmuTree(std::span<Node> mem, CompareFunc comp);

	/**
     * @brief Inserts data into the tree.
     * @param data The data span to insert.
     * @return true if successful, false if the storage is full.
     */
	bool insert(std::span<std::byte> data);

	/**
     * @brief Removes the node containing the specified data.
     * @param data The data span to find and remove.
     * @return true if the node was found and removed, false otherwise.
     */
	bool remove(std::span<std::byte> data);

	/**
     * @brief Finds a node index by data.
     * @param data Data span to search for.
     * @return The node index or Node::NIL if not found.
     */
	uintptr_t find(std::span<std::byte> data) const;

	/**
     * @brief Retrieves the data span associated with a specific node index.
     * @param idx Node index.
     * @return Data span or an empty span if idx is NIL.
     */
	std::span<std::byte> getData(uintptr_t idx) const;

	/** @brief Returns an iterator to the smallest element. */
	Iterator begin() const;

	/** @brief Returns an iterator to the end of the tree. */
	Iterator end() const;

	/** @brief Checks if the tree contains no elements. */
	bool empty() const;

	/**
     * @brief Returns the number of elements currently stored in the tree.
     */
	size_t size() const;

	/**
     * @brief Returns the total memory footprint of the tree in bytes.
     * @details Includes both the management object and the storage buffer.
     */
	size_t memory_usage() const;
};
