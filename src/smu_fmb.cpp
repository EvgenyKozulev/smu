#include "smu_fmb.h"

SmuFmb::SmuFmb(uint8_t alignment, uint16_t minBlockSize,
	       std::span<std::byte> memory)
	: SmuTree(*(reinterpret_cast<SmuTree::Node *>(memory.data())))
	, align(alignment)
	, minBlock(minBlockSize)
{
	size_t totalBytes = memory.size_bytes();
	blockCount = calculateBlockCount(totalBytes);

	if (blockCount == 0)
		return;

	size_t stackSpace = (blockCount + 1) * sizeof(SmuTree::Node);
	dataStart = memory.data() + stackSpace;

	size_t padding =
		(align - (reinterpret_cast<uintptr_t>(dataStart) % align)) %
		align;
	dataStart += padding;

	populateTree(dataStart, blockCount);
}

size_t SmuFmb::calculateBlockCount(size_t totalBytes) const
{
	size_t nodeSize = sizeof(SmuTree::Node);
	if (totalBytes <= nodeSize)
		return 0;
	return (totalBytes - nodeSize) / (minBlock + nodeSize);
}

void SmuFmb::populateTree(std::byte *dataStart, size_t blockCount)
{
	auto it = this->begin();
	Node *baseNode = it.getNil();

	for (size_t i = 0; i < blockCount; ++i) {
		Node *newNode = baseNode + (i + 1);

		std::span<std::byte> blockSpan(dataStart + (i * minBlock),
					       minBlock);

		this->insert(newNode, blockSpan);
	}
}

int SmuFmb::compare(std::span<std::byte> a, std::span<std::byte> b)
{
	if (a.data() < b.data())
		return -1;
	if (a.data() > b.data())
		return 1;
	return 0;
}

void SmuFmb::collision(const Node *node, std::span<std::byte> &key)
{
	/** @todo later */
}

SmuTree::Node *SmuFmb::findBestFit(size_t size)
{
	size_t blocksNeeded = (size + minBlock - 1) / minBlock;

	auto it = this->begin();
	auto sentinel = this->end();

	while (it != sentinel) {
		auto startIt = it;
		size_t foundBlocks = 1;

		while (foundBlocks < blocksNeeded) {
			auto current = it.get();
			++it;

			if (it == sentinel)
				break;

			auto next = it.get();

			if (current->keyData.data() +
				    current->keyData.size_bytes() ==
			    next->keyData.data()) {
				foundBlocks++;
			} else {
				break;
			}
		}

		if (foundBlocks == blocksNeeded) {
			return startIt.get();
		}

		if (foundBlocks < blocksNeeded && it != sentinel) {
		} else {
			if (it != sentinel)
				++it;
		}
	}

	return nullptr;
}

std::span<SmuTree::Node> SmuFmb::extract(size_t size)
{
	Node *firstNode = findBestFit(size);
	if (!firstNode) {
		return {};
	}

	size_t blocksNeeded = (size + minBlock - 1) / minBlock;

	Node *current = firstNode;
	for (size_t i = 0; i < blocksNeeded; ++i) {
		this->remove(current->keyData);
		current++;
	}

	return std::span<Node>(firstNode, blocksNeeded);
}

std::span<std::byte> SmuFmb::findNodeSpan(SmuTree::Node *node)
{
	auto it = this->begin();
	const Node *nilBase = it.getNil();

	ptrdiff_t index = node - nilBase;

	if (index <= 0 || static_cast<size_t>(index) > blockCount) {
		return {}; // Нода не из нашего стека
	}

	std::byte *blockAddr =
		dataStart + (static_cast<size_t>(index - 1) * minBlock);

	return std::span<std::byte>(blockAddr, minBlock);
}

bool SmuFmb::release(std::span<SmuTree::Node> nodes)
{
	if (nodes.empty()) {
		return false;
	}

	bool allOk = true;

	for (Node &currentNode : nodes) {
		std::span<std::byte> originalBlock = findNodeSpan(&currentNode);

		if (originalBlock.empty()) {
			allOk = false;
			continue;
		}

		if (!this->insert(&currentNode, originalBlock)) {
			allOk = false;
		}
	}

	return allOk;
}

size_t SmuFmb::getBlockCount()
{
	return blockCount;
}
