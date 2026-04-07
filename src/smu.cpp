#include "smu.h"

Smu::Smu(uint8_t alignment, uint16_t minBlockSize, std::span<std::byte> memory)
	: align(alignment)
{
	std::byte *pool = memory.data();
	SmuTree::Node *nilTabPtr = new (memory.data()) SmuTree::Node();
	size_t adminSize =
		sizeof(SmuTree::Node) + sizeof(SmuFmb) + sizeof(SmuTab);

	tab = new (memory.data() + sizeof(SmuTree::Node)) SmuTab(*nilTabPtr);

	std::span<std::byte> fmbWorkSpan = memory.subspan(adminSize);
	fmb = new (memory.data() + sizeof(SmuTree::Node) + sizeof(SmuTab))
		SmuFmb(alignment, minBlockSize, fmbWorkSpan);
}

Smu::~Smu()
{
}

void *Smu::allocate(size_t size)
{
	size_t totalNeeded = size + sizeof(SmuTab::MetaHead) + align;
	std::byte *userPtr = nullptr;

	do {
		auto nodes = fmb->extract(totalNeeded);
		if (nodes.empty())
			break;

		if (!tab->push(nodes)) {
			fmb->release(nodes);
			break;
		}

		userPtr = static_cast<std::byte *>(
			alignPointerUp(nodes[0].keyData.data()));

		std::byte *headAddr = userPtr - sizeof(SmuTab::MetaHead);

	} while (0);

	return checkIntegrity() ? static_cast<void *>(userPtr) : nullptr;
}

void *Smu::alignPointerUp(std::byte *rawData) const
{
	uintptr_t base =
		reinterpret_cast<uintptr_t>(rawData) + sizeof(SmuTab::MetaHead);
	uintptr_t mask = static_cast<uintptr_t>(align) - 1;
	uintptr_t aligned = (base + mask) & ~mask;

	return reinterpret_cast<void *>(aligned);
}

void *Smu::alignPointerDown(void *ptr) const
{
	if (!ptr)
		return nullptr;

	uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
	uintptr_t mask = static_cast<uintptr_t>(align) - 1;
	uintptr_t headLimit = addr - sizeof(SmuTab::MetaHead);
	uintptr_t rawData = headLimit & ~mask;

	return reinterpret_cast<void *>(rawData);
}

bool Smu::deallocate(void *ptr)
{
	bool result = false;

	do {
		if (!ptr)
			break;

		std::byte *masterAddr =
			reinterpret_cast<std::byte *>(alignPointerDown(ptr));

		auto nodes = tab->pop(std::span<std::byte>(
			masterAddr, sizeof(SmuTab::MetaHead)));

		if (nodes.empty())
			break;

		result = fmb->release(nodes);
	} while (0);

	return checkIntegrity() ? result : false;
}

bool Smu::checkIntegrity() const
{
	size_t freeNodes = fmb->stats.nodeCount.load();
	size_t busyNodes = tab->stats.nodeCount.load();

	if (freeNodes + busyNodes != fmb->getBlockCount())
		return false;

	if (freeNodes !=
	    fmb->stats.redCount.load() + fmb->stats.blackCount.load())
		return false;

	if (busyNodes !=
	    tab->stats.redCount.load() + tab->stats.blackCount.load())
		return false;

	return true;
}

size_t Smu::totalBlocks() const
{
	return fmb->getBlockCount();
}

uint8_t Smu::getAlignment() const
{
	return align;
}

size_t Smu::freeNodes() const
{
	return fmb->stats.nodeCount.load(std::memory_order_relaxed);
}

size_t Smu::freeBytes() const
{
	return fmb->stats.keyDataSize.load(std::memory_order_relaxed);
}

size_t Smu::busyNodes() const
{
	return tab->stats.nodeCount.load(std::memory_order_relaxed);
}

size_t Smu::busyBytes() const
{
	return tab->stats.keyDataSize.load(std::memory_order_relaxed);
}

size_t Smu::redNodes(bool isFreeTree) const
{
	return isFreeTree ?
		       fmb->stats.redCount.load(std::memory_order_relaxed) :
		       tab->stats.redCount.load(std::memory_order_relaxed);
}

size_t Smu::blackNodes(bool isFreeTree) const
{
	return isFreeTree ?
		       fmb->stats.blackCount.load(std::memory_order_relaxed) :
		       tab->stats.blackCount.load(std::memory_order_relaxed);
}

size_t Smu::getFreeNodesMemory() const
{
	return fmb->stats.treeSize.load(std::memory_order_relaxed);
}

size_t Smu::getBusyNodesMemory() const
{
	return tab->stats.treeSize.load(std::memory_order_relaxed);
}

size_t Smu::getAdminSpace() const
{
	size_t tabMemory = getBusyNodesMemory();
	size_t fmbMemory = getFreeNodesMemory();

	return sizeof(SmuTree::Node) + sizeof(SmuFmb) + sizeof(SmuTab) +
	       tabMemory + fmbMemory;
}