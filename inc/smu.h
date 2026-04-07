#pragma once

#include "smu_fmb.h"
#include "smu_tab.h"

class Smu {
    private:
	const uint8_t align;
	SmuFmb *fmb = nullptr;
	SmuTab *tab = nullptr;

	void *alignPointerUp(std::byte *rawData) const;
	void *alignPointerDown(void *ptr) const;

    public:
	bool deallocate(void *ptr);
	void *allocate(size_t size);
	bool checkIntegrity() const;

	size_t totalBlocks() const;
	uint8_t getAlignment() const;
	size_t freeNodes() const;
	size_t freeBytes() const;
	size_t busyNodes() const;
	size_t busyBytes() const;
	size_t redNodes(bool isFreeTree) const;
	size_t blackNodes(bool isFreeTree) const;
	size_t getBusyNodesMemory() const;
	size_t getFreeNodesMemory() const;
	size_t getAdminSpace() const;

	Smu(uint8_t alignment, uint16_t minBlockSize,
	    std::span<std::byte> memory);
	~Smu();
};
