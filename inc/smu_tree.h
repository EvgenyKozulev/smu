#pragma once

#include <span>
#include <atomic>
#include <optional>

class SmuTree {
    public:
	enum class Color : uint8_t { Black = 0, Red = 1 };

	struct Node {
		Color color;
		Node *left = nullptr;
		Node *right = nullptr;
		Node *parent = nullptr;
		std::span<std::byte> keyData;
	};

	struct Statistics {
		std::atomic<size_t> keyDataSize;
		std::atomic<size_t> treeSize;
		std::atomic<size_t> nodeCount;
		std::atomic<size_t> redCount;
		std::atomic<size_t> blackCount;

		Statistics()
			: keyDataSize(0)
			, treeSize(0)
			, nodeCount(0)
			, redCount(0)
			, blackCount(0)
		{
		}
	} stats;

	class Iterator {
		friend class SmuTree;
		Node *node;
		Node *nil;
		Node *root;
		const SmuTree *tree;

	    public:
		Iterator(Node *n, Node *nil, Node *root, const SmuTree *t);

		Node &operator*() const;
		Node *operator->() const;
		Iterator &operator++();
		bool operator==(const Iterator &other) const;
		bool operator!=(const Iterator &other) const;
		Node *get() const;
		Node *getRoot() const;
		Node *getNil() const;
	};

	virtual int compare(std::span<std::byte> a, std::span<std::byte> b) = 0;
	virtual void collision(const Node *node, std::span<std::byte> &key) = 0;

	bool insert(Node *node, std::span<std::byte> key);
	bool remove(Node *node);
	bool remove(std::span<std::byte> key);
	Node *find(std::span<std::byte> key);

	Iterator begin();
	Iterator end();

	SmuTree(Node &nilMem);
	virtual ~SmuTree() = default;

    private:
	friend class Iterator;
	struct ParentInfo {
		Node *parent;
		int lastRes;
	};

	Node *root = nullptr;
	Node *nil = nullptr;

	void rotateLeft(Node *x);
	void rotateRight(Node *y);
	void insertFixup(Node *z);
	void removeFixup(Node *x, Node *parent);
	void transplant(Node *u, Node *v);
	void fixupStep(Node *&z, bool isLeft);
	void applyRemove(Node *z);

	Node *minimum(Node *x) const;
	Node *successor(Node *x) const;
	Node *predecessor(Node *x) const;

	std::optional<ParentInfo> getParentInfo(std::span<std::byte> key);
	void setColor(Node *node, Color newColor);
};
