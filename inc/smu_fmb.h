#pragma once

#include <functional>

#include "smu_tree.h"

class SmuFmb : private SmuTree {
    public:
	std::span<std::byte> pop(size_t size);
	bool push(std::span<std::byte> pool);
	using CollisionFunc =
		std::function<void(const Node *node, std::span<std::byte> &key)>;

	SmuFmb(uint8_t alignment, uint16_t minBlockSize, CollisionFunc cb);
	~SmuFmb() = default;

    private:
	const uint8_t align;
	const uint16_t minBlock;
	CollisionFunc collisioncb = nullptr;
	

	SmuTree::Node *findBestFit(size_t size);
	int compare(std::span<std::byte> a, std::span<std::byte> b) override;
	void collision(const Node *node, std::span<std::byte> &key) override;
};
