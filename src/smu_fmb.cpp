#include "smu_fmb.h"

// SmuFmb::SmuFmb(uint8_t alignment, uint16_t minBlockSize, CollisionFunc cb)
// 	: SmuTree::SmuTree(nil)
// 	, align(alignment)
// 	, minBlock(minBlockSize)
// 	, collisioncb(cb)
// {
// }

// int SmuFmb::compare(std::span<std::byte> a, std::span<std::byte> b)
// {
// 	if (a.size_bytes() < b.size_bytes())
// 		return -1;
// 	if (a.size_bytes() > b.size_bytes())
// 		return 1;
// 	return 0;
// }

// void SmuFmb::collision(const Node *node, std::span<std::byte> &key)
// {
// 	if (collisioncb)
// 		std::invoke(collisioncb, node, key);
// }

// SmuTree::Node *SmuFmb::findBestFit(size_t size)
// {
// 	Iterator it = end();
// 	Node *current = it.getRoot();
// 	Node *nil = it.getNil();
// 	Node *best = nullptr;

// 	while (current != nil) {
// 		if (current->keyData.size_bytes() >= size) {
// 			best = current;
// 			current = current->left;
// 		} else {
// 			current = current->right;
// 		}
// 	}

// 	return best;
// }

// bool SmuFmb::push(SmuTree::Node *node, std::span<std::byte> pool)
// {
// 	uintptr_t rawAddr = reinterpret_cast<uintptr_t>(pool.data());
// 	uintptr_t alignedAddr = (rawAddr + (align - 1)) &
// 				~(uintptr_t)(align - 1);
// 	size_t padding = alignedAddr - rawAddr;

// 	if (padding > 0) {
// 		if (pool.size_bytes() <= padding)
// 			return false;
// 		pool = pool.subspan(padding);
// 	}

// 	if (pool.size_bytes() < minBlock) {
// 		return false;
// 	}

// 	return insert(node, pool);
// }

// std::span<std::byte> SmuFmb::pop(size_t size)
// {
// 	size_t alignedSize = (size + (align - 1)) & ~(size_t)(align - 1);

// 	Node *best = findBestFit(alignedSize);
// 	if (!best)
// 		return {};

// 	size_t totalBytes = best->keyData.size_bytes();
// 	size_t remainderSize = totalBytes - alignedSize;

// 	std::span<std::byte> allocated =
// 		best->keyData.subspan(remainderSize, alignedSize);

// 	if (!remove(best))
// 		return {};

// 	if (remainderSize < minBlock) {
// 		return best->keyData;
// 	} else {
// 		std::span<std::byte> remainder =
// 			best->keyData.subspan(0, remainderSize);

// 		if (!insert(best, remainder)) {
// 			/** @todo link after */
// 		}
// 	}
// 	return allocated;
// }
