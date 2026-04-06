#pragma once

#include <functional>

#include "smu_tree.h"

class SmuFmb : public SmuTree {
    public:
	std::span<SmuTree::Node> extract(size_t size);
	bool release(std::span<SmuTree::Node> nodes);
size_t getBlockCount();

	SmuFmb(uint8_t alignment, uint16_t minBlockSize,
	       std::span<std::byte> memory);
	~SmuFmb() = default;

    private:
	const uint8_t align;
	const uint16_t minBlock;
	size_t blockCount = 0;
	std::byte *dataStart = nullptr;

	size_t calculateBlockCount(size_t totalBytes) const;
	void populateTree(std::byte *dataStart, size_t blockCount);
	std::span<std::byte> findNodeSpan(SmuTree::Node *node);

	SmuTree::Node *findBestFit(size_t size);
	int compare(std::span<std::byte> a, std::span<std::byte> b) override;
	void collision(const Node *node, std::span<std::byte> &key) override;
};
