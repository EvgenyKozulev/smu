#pragma once

#include "smu_tree.h"

class SmuTab : public SmuTree {
    public:
	struct MetaHead {
		uintptr_t node;
		size_t nodeCount;
		size_t xorMeta;
	};

	bool push(std::span<SmuTree::Node> nodes);
	std::span<SmuTree::Node> pop(std::span<std::byte> a);

	SmuTab(Node &nil);
	~SmuTab();

    private:
	SmuTab::MetaHead *readMeta(std::span<std::byte> data) const;
	bool writeMeta(std::span<std::byte> data, uintptr_t nodeAddr,
		       size_t count);

	int compare(std::span<std::byte> a, std::span<std::byte> b) override;
	void collision(const Node *node, std::span<std::byte> &key) override;
};
